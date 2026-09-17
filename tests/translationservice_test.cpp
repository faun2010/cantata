#include "network/translationservice.h"
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QVariant>

class TranslationServiceTest : public QObject {
	Q_OBJECT

	class RecordingProxyFactory : public QNetworkProxyFactory {
	public:
		RecordingProxyFactory(int* calls, quint16 port) : callCount(calls), proxyPort(port) {}
		QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery&) override
		{
			++*callCount;
			return { QNetworkProxy(QNetworkProxy::HttpProxy, QLatin1String("127.0.0.1"), proxyPort) };
		}
	private:
		int* callCount;
		quint16 proxyPort;
	};

private:
	static void writeConfig(const QString& path, const QUrl& url, const QString& model = QLatin1String("qwen3.8:27b"), int cooldown = 30,
	                        int timeout = 2000, int maxConcurrent = 1, int maxQueued = 16, int searchTimeout = 30000)
	{
		QSettings settings(path, QSettings::IniFormat);
		settings.beginGroup(QLatin1String("Translation"));
		settings.setValue(QLatin1String("enabled"), true);
		settings.setValue(QLatin1String("provider"), QLatin1String("ollama"));
		settings.setValue(QLatin1String("url"), url.toString());
		settings.setValue(QLatin1String("model"), model);
		settings.setValue(QLatin1String("targetLanguage"), QLatin1String("Simplified Chinese"));
		settings.setValue(QLatin1String("promptVersion"), QLatin1String("2"));
		settings.setValue(QLatin1String("timeoutMs"), timeout);
		settings.setValue(QLatin1String("searchTimeoutMs"), searchTimeout);
		settings.setValue(QLatin1String("cooldownSeconds"), cooldown);
		settings.setValue(QLatin1String("maxConcurrentRequests"), maxConcurrent);
		settings.setValue(QLatin1String("maxQueuedRequests"), maxQueued);
		settings.endGroup();
		sync(settings);
	}

	static void sync(QSettings& settings)
	{
		settings.sync();
		QCOMPARE(settings.status(), QSettings::NoError);
	}

	static QUrl serverUrl(const QTcpServer& server)
	{
		return QUrl(QString::fromLatin1("http://127.0.0.1:%1").arg(server.serverPort()));
	}

	static void serve(QTcpServer& server, int& requestCount, const QByteArray& body, int statusCode = 200, int delayMs = 0, QByteArray* capturedBody = nullptr)
	{
		QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &requestCount, body, statusCode, delayMs, capturedBody]() {
			while (QTcpSocket* socket = server.nextPendingConnection()) {
				QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &server, &requestCount, body, statusCode, delayMs, capturedBody]() {
					QByteArray& request = *static_cast<QByteArray*>(socket->property("requestBuffer").value<void*>());
					request += socket->readAll();
					const int headerEnd = request.indexOf("\r\n\r\n");
					if (headerEnd < 0) return;
					int contentLength = 0;
					const QList<QByteArray> headers = request.left(headerEnd).split('\n');
					for (QByteArray header : headers) {
						if (header.trimmed().toLower().startsWith("content-length:")) contentLength = header.mid(header.indexOf(':') + 1).trimmed().toInt();
					}
					if (request.size() < headerEnd + 4 + contentLength || socket->property("answered").toBool()) return;
					socket->setProperty("answered", true);
					if (capturedBody) *capturedBody = request.mid(headerEnd + 4, contentLength);
					++requestCount;
					const QByteArray status = QByteArray::number(statusCode) + (statusCode == 200 ? " OK" : " Error");
					const QByteArray response = "HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
					const QPointer<QTcpSocket> guardedSocket(socket);
					QTimer::singleShot(delayMs, &server, [guardedSocket, response]() {
						if (!guardedSocket) return;
						guardedSocket->write(response);
						guardedSocket->disconnectFromHost();
					});
				});
				QByteArray* request = new QByteArray;
				socket->setProperty("requestBuffer", QVariant::fromValue(static_cast<void*>(request)));
				QObject::connect(socket, &QObject::destroyed, [request]() { delete request; });
			}
		});
	}

private Q_SLOTS:
	void initTestCase()
	{
		QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
	}

	void createsExactDefaultConfig()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSettings settings(config, QSettings::IniFormat);
		settings.beginGroup(QLatin1String("Translation"));
		QCOMPARE(settings.value(QLatin1String("provider")).toString(), QLatin1String("ollama"));
		QCOMPARE(settings.value(QLatin1String("url")).toString(), QLatin1String("http://127.0.0.1:11434"));
		QCOMPARE(settings.value(QLatin1String("model")).toString(), QLatin1String("qwen3.8:27b"));
		// Off by default: an unattended local Ollama endpoint must not receive
		// every tooltip hover, search term and artist biography without opt-in.
		QVERIFY(!settings.value(QLatin1String("enabled")).toBool());
		QCOMPARE(settings.value(QLatin1String("timeoutMs")).toInt(), 180000);
		QCOMPARE(settings.value(QLatin1String("searchTimeoutMs")).toInt(), 30000);
		QCOMPARE(settings.value(QLatin1String("maxConcurrentRequests")).toInt(), 1);
		QCOMPARE(settings.value(QLatin1String("maxQueuedRequests")).toInt(), 16);
#ifdef Q_OS_UNIX
		const QFile::Permissions permissions = QFileInfo(config).permissions();
		QVERIFY(!(permissions & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
#endif
	}

	void escapesTranslatedTextForRichTextViews()
	{
		QCOMPARE(TranslationService::plainTextToHtml(QLatin1String("<script>x</script>\r\nnext")),
		         QLatin1String("&lt;script&gt;x&lt;/script&gt;<br/>next"));
	}

	void asynchronousMergeAndPersistentCache()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		QJsonObject response;
		response.insert(QLatin1String("response"), QString::fromUtf8("中文译文"));
		serve(server, requestCount, QJsonDocument(response).toJson(QJsonDocument::Compact));
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		const QString cache = temporary.filePath(QLatin1String("cache"));
		writeConfig(config, serverUrl(server));

		{
			TranslationService service(nullptr, config, cache);
			QSignalSpy ready(&service, &TranslationService::translationReady);
			QVERIFY(service.cached(QLatin1String("English biography"), QLatin1String("artist:A")).isEmpty());
			QCoreApplication::processEvents();
			QCOMPARE(requestCount, 0);
			QCOMPARE(service.translate(QLatin1String("English biography"), QLatin1String("artist:A")), QLatin1String("English biography"));
			QCOMPARE(service.translate(QLatin1String("English biography"), QLatin1String("artist:A")), QLatin1String("English biography"));
			QTRY_COMPARE(ready.count(), 1);
			QCOMPARE(requestCount, 1);
			QCOMPARE(ready.first().at(2).toString(), QString::fromUtf8("中文译文"));
			QCOMPARE(service.cached(QLatin1String("English biography"), QLatin1String("artist:A")), QString::fromUtf8("中文译文"));
			QCOMPARE(service.translate(QLatin1String("English biography"), QLatin1String("artist:A")), QString::fromUtf8("中文译文"));
			QCOMPARE(requestCount, 1);
		}

		TranslationService restarted(nullptr, config, cache);
		QCOMPARE(restarted.translate(QLatin1String("English biography"), QLatin1String("artist:A")), QString::fromUtf8("中文译文"));
		QCOMPARE(requestCount, 1);
	}

	void cacheIsolatedByModelAndContext()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"translated\"}"));
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		const QString cache = temporary.filePath(QLatin1String("cache"));
		writeConfig(config, serverUrl(server), QLatin1String("model-one"));

		TranslationService service(nullptr, config, cache);
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("text"), QLatin1String("context-one"));
		service.translate(QLatin1String("text"), QLatin1String("context-two"));
		QTRY_COMPARE(ready.count(), 2);
		QCOMPARE(requestCount, 2);

		writeConfig(config, serverUrl(server), QLatin1String("model-two"));
		service.reloadConfiguration();
		service.translate(QLatin1String("text"), QLatin1String("context-one"));
		QTRY_COMPARE(ready.count(), 3);
		QCOMPARE(requestCount, 3);
		QSet<QString> contexts;
		contexts.insert(ready.at(0).at(1).toString());
		contexts.insert(ready.at(1).at(1).toString());
		QCOMPARE(contexts.size(), 2);
		QVERIFY(contexts.contains(QLatin1String("context-one")));
		QVERIFY(contexts.contains(QLatin1String("context-two")));

		QSettings settings(config, QSettings::IniFormat);
		settings.setValue(QLatin1String("Translation/targetLanguage"), QLatin1String("Traditional Chinese"));
		sync(settings);
		service.reloadConfiguration();
		service.translate(QLatin1String("text"), QLatin1String("context-one"));
		QTRY_COMPARE(ready.count(), 4);
		QCOMPARE(requestCount, 4);

		settings.setValue(QLatin1String("Translation/promptVersion"), QLatin1String("3"));
		sync(settings);
		service.reloadConfiguration();
		service.translate(QLatin1String("text"), QLatin1String("context-one"));
		QTRY_COMPARE(ready.count(), 5);
		QCOMPARE(requestCount, 5);
	}

	void failureFallsBackAndCoolsDown()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{}"));
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server), QLatin1String("model"), 60);

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		QCOMPARE(service.translate(QLatin1String("original"), QLatin1String("context")), QLatin1String("original"));
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(ready.first().at(2).toString(), QLatin1String("original"));
		QCOMPARE(service.translate(QLatin1String("original"), QLatin1String("context")), QLatin1String("original"));
		QTest::qWait(50);
		QCOMPARE(requestCount, 1);
	}

	void endpointCooldownAfterConnectionRefused()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		const QUrl refusedUrl = serverUrl(server);
		server.close(); // Nothing is listening on this port now: connections are refused.

		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, refusedUrl, QLatin1String("model"), 30, 2000);

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		QCOMPARE(service.translate(QLatin1String("first"), QLatin1String("context-a")), QLatin1String("first"));
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(ready.first().at(2).toString(), QLatin1String("first"));

		// The endpoint is now marked unavailable for ~60s: a completely
		// different (source, context) pair must fail fast synchronously,
		// with no network attempt and therefore no further signal at all.
		QCOMPARE(service.translate(QLatin1String("second"), QLatin1String("context-b")), QLatin1String("second"));
		QTest::qWait(150);
		QCOMPARE(ready.count(), 1);
	}

	void queuedRequestsClearedWhenEndpointBecomesUnavailable()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		const QUrl refusedUrl = serverUrl(server);
		server.close();

		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, refusedUrl, QLatin1String("model"), 30, 2000, 1, 16);

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		for (int i = 0; i < 5; ++i) {
			service.translate(QString::fromLatin1("text-%1").arg(i), QString::fromLatin1("context-%1").arg(i));
		}
		// Every dropped request must finish so search owners can stop waiting.
		QTRY_COMPARE(ready.count(), 5);
		for (const QList<QVariant>& result : ready) {
			QCOMPARE(result.at(2).toString(), result.at(0).toString());
			QVERIFY(!service.isPending(result.at(0).toString(), result.at(1).toString()));
		}
		QTest::qWait(150);
		QCOMPARE(ready.count(), 5);
	}

	void limitsConcurrencyAndQueueSize()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"translated\"}"), 200, 100);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server), QLatin1String("model"), 30, 2000, 1, 3);

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		for (int i = 0; i < 8; ++i) {
			service.translate(QString::fromLatin1("text-%1").arg(i), QString::fromLatin1("context-%1").arg(i));
		}
		QTest::qWait(30);
		QCOMPARE(requestCount, 1);
		QTRY_COMPARE(ready.count(), 8);
		QCOMPARE(requestCount, 4);
		QSet<QString> sources;
		for (const QList<QVariant>& result : ready) sources.insert(result.at(0).toString());
		QCOMPARE(sources.size(), 8);
		for (int i = 0; i < 8; ++i) QVERIFY(sources.contains(QString::fromLatin1("text-%1").arg(i)));
	}

	void prefixSupersedesQueuedAndInFlightRequests()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		QJsonObject response;
		// music-search-v1 responses must be a JSON array of alternatives, or
		// the malformed-response guard clears them back to the source text.
		response.insert(QLatin1String("response"), QString::fromUtf8("[\"Beethoven\"]"));
		serve(server, requestCount, QJsonDocument(response).toJson(QJsonDocument::Compact), 200, 100);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server), QLatin1String("model"), 30, 5000, 1, 16);

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		// Simulates an IME keystroke burst against the music-search context:
		// each later term is a strict extension of the previous one.
		service.translate(QString::fromUtf8("\xe8\xb4\x9d"), QLatin1String("music-search-v1"));
		service.translate(QString::fromUtf8("\xe8\xb4\x9d\xe5\xa4\x9a"), QLatin1String("music-search-v1"));
		service.translate(QString::fromUtf8("\xe8\xb4\x9d\xe5\xa4\x9a\xe8\x8a\xac"), QLatin1String("music-search-v1"));

		// Only the final, most specific term should ever complete: the
		// stale prefixes are superseded without emitting a signal for them.
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(ready.first().at(0).toString(), QString::fromUtf8("\xe8\xb4\x9d\xe5\xa4\x9a\xe8\x8a\xac"));
		QCOMPARE(ready.first().at(2).toString(), QLatin1String("[\"Beethoven\"]"));
		QTest::qWait(150);
		QCOMPARE(ready.count(), 1);
	}

	void prefixSupersedeKeepsUnrelatedContext()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"translated\"}"), 200, 50);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server), QLatin1String("model"), 30, 5000, 1, 16);

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		// "ab" is a prefix of "abc", but the contexts differ, so neither
		// request should supersede the other.
		service.translate(QLatin1String("ab"), QLatin1String("context-one"));
		service.translate(QLatin1String("abc"), QLatin1String("context-two"));
		QTRY_COMPARE(ready.count(), 2);
		QSet<QString> sources;
		sources.insert(ready.at(0).at(0).toString());
		sources.insert(ready.at(1).at(0).toString());
		QVERIFY(sources.contains(QLatin1String("ab")));
		QVERIFY(sources.contains(QLatin1String("abc")));
	}

	void timeoutFallsBackToSource()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"too late\"}"), 200, 500);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server), QLatin1String("model"), 30, 100);

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("original"), QLatin1String("timeout"));
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(ready.first().at(2).toString(), QLatin1String("original"));
		QCOMPARE(requestCount, 1);
	}

	void malformedAndHttpErrorsFallBack()
	{
		for (int failureType = 0; failureType < 2; ++failureType) {
			QTemporaryDir temporary;
			QTcpServer server;
			QVERIFY(server.listen(QHostAddress::LocalHost));
			int requestCount = 0;
			serve(server, requestCount, failureType == 0 ? QByteArrayLiteral("not-json") : QByteArrayLiteral("{\"response\":\"ignored\"}"),
			      failureType == 0 ? 200 : 500);
			const QString config = temporary.filePath(QLatin1String("translation.ini"));
			writeConfig(config, serverUrl(server));

			TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
			QSignalSpy ready(&service, &TranslationService::translationReady);
			service.translate(QLatin1String("original"), QString::number(failureType));
			QTRY_COMPARE(ready.count(), 1);
			QCOMPARE(ready.first().at(2).toString(), QLatin1String("original"));
			QCOMPARE(requestCount, 1);
		}
	}

	void reloadCancelsOldGenerationWithoutSignal()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"translated\"}"), 200, 200);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		const QString cache = temporary.filePath(QLatin1String("cache"));
		writeConfig(config, serverUrl(server), QLatin1String("old-model"));

		TranslationService service(nullptr, config, cache);
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("same source"), QLatin1String("same context"));
		QTRY_COMPARE(requestCount, 1);
		writeConfig(config, serverUrl(server), QLatin1String("new-model"));
		service.reloadConfiguration();
		service.translate(QLatin1String("same source"), QLatin1String("same context"));
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(ready.first().at(2).toString(), QLatin1String("translated"));
		QTest::qWait(250);
		QCOMPARE(ready.count(), 1);
	}

	void cancelRemovesRunningAndQueuedRequests()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"translated\"}"), 200, 300);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		writeConfig(config, serverUrl(server), QLatin1String("model"), 30, 2000, 1, 4);
		service.reloadConfiguration();
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("running"), QLatin1String("context"));
		QTRY_COMPARE(requestCount, 1);
		service.translate(QLatin1String("queued"), QLatin1String("context"));
		QVERIFY(service.isPending(QLatin1String("running"), QLatin1String("context")));
		QVERIFY(service.isPending(QLatin1String("queued"), QLatin1String("context")));
		service.cancel(QLatin1String("running"), QLatin1String("context"));
		service.cancel(QLatin1String("queued"), QLatin1String("context"));
		QVERIFY(!service.isPending(QLatin1String("running"), QLatin1String("context")));
		QVERIFY(!service.isPending(QLatin1String("queued"), QLatin1String("context")));
		QTest::qWait(450);
		QCOMPARE(ready.count(), 0);
		QCOMPARE(requestCount, 1);
	}

	void searchUsesTheLowerTimeout()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"[\\\"source\\\",\\\"translated\\\"]\"}"), 200, 500);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server), QLatin1String("model"), 30, 1000, 1, 4, 100);
		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		QElapsedTimer elapsed;
		elapsed.start();
		service.translate(QLatin1String("source"), QLatin1String("music-search-v1"));
		QTRY_COMPARE(ready.count(), 1);
		QVERIFY2(elapsed.elapsed() < 400, qPrintable(QString::fromLatin1("elapsed %1 ms").arg(elapsed.elapsed())));
		QCOMPARE(requestCount, 1);
	}

	void connectionRefusedCoolsEndpointButKeepsCacheReadable()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"cached translation\"}"));
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		writeConfig(config, serverUrl(server));
		service.reloadConfiguration();
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("cached source"), QLatin1String("context"));
		QTRY_COMPARE(ready.count(), 1);
		const quint16 port = server.serverPort();
		server.close();
		service.translate(QLatin1String("failed source"), QLatin1String("context"));
		QTRY_COMPARE(ready.count(), 2);
		QVERIFY(server.listen(QHostAddress::LocalHost, port));
		QCOMPARE(service.translate(QLatin1String("cached source"), QLatin1String("context")), QLatin1String("cached translation"));
		QCOMPARE(service.translate(QLatin1String("another source"), QLatin1String("context")), QLatin1String("another source"));
		QTest::qWait(100);
		QCOMPARE(requestCount, 1);
	}

	void requestsGoThroughInjectedNetworkAccessManager()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"translated\"}"));
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server));

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QNetworkAccessManager injected;
		QSignalSpy injectedFinished(&injected, &QNetworkAccessManager::finished);
		service.setNetworkAccessManager(&injected);

		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("source"), QLatin1String("context"));
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(ready.first().at(2).toString(), QLatin1String("translated"));
		// The reply came through the injected manager, not the service's own.
		QCOMPARE(injectedFinished.count(), 1);
		QCOMPARE(requestCount, 1);
	}

	void defaultNetworkManagerUsesApplicationProxyFactory()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"translated\"}"));
		int proxyQueries = 0;
		QNetworkProxyFactory::setApplicationProxyFactory(new RecordingProxyFactory(&proxyQueries, server.serverPort()));
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, QUrl(QLatin1String("http://example.invalid")));
		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("source"), QLatin1String("context"));
		QTRY_COMPARE(ready.count(), 1);
		QVERIFY(proxyQueries > 0);
		QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
	}

	void recommendedRecordingsContextUsesDedicatedSystemPrompt()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		QByteArray capturedBody;
		QJsonObject response;
		response.insert(QLatin1String("response"), QStringLiteral("[{\"soloist\":\"Rudolf Serkin\",\"conductor\":\"\",\"ensemble\":\"\",\"label\":\"\",\"year\":\"\"}]"));
		serve(server, requestCount, QJsonDocument(response).toJson(QJsonDocument::Compact), 200, 0, &capturedBody);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server));

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("Ludwig van Beethoven — Piano Concerto No.5, Op.73"), QLatin1String("recommended-recordings-v1"));
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(requestCount, 1);

		const QJsonObject payload = QJsonDocument::fromJson(capturedBody).object();
		const QString systemPrompt = payload.value(QLatin1String("system")).toString();
		// The dedicated recommended-recordings-v1 prompt, not the generic
		// translation one - and it must never ask for ratings/guide names.
		QVERIFY(systemPrompt.contains(QLatin1String("JSON array")));
		QVERIFY(systemPrompt.contains(QLatin1String("soloist, conductor, ensemble, label, and year")));
		QVERIFY(systemPrompt.contains(QLatin1String("no ratings, no guide")) || systemPrompt.contains(QLatin1String("Include no ratings")));
		QVERIFY(!systemPrompt.contains(QLatin1String("translation engine")));
	}

	void smartFilterContextUsesDedicatedSystemPrompt()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		QByteArray capturedBody;
		QJsonObject response;
		response.insert(QLatin1String("response"), QStringLiteral("[0]"));
		serve(server, requestCount, QJsonDocument(response).toJson(QJsonDocument::Compact), 200, 0, &capturedBody);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server));

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("Description:\ncalm piano\n\nCandidates:\n[{\"i\":0,\"title\":\"Allegro\"}]"), QLatin1String("smart-filter-v1"));
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(requestCount, 1);

		const QJsonObject payload = QJsonDocument::fromJson(capturedBody).object();
		const QString systemPrompt = payload.value(QLatin1String("system")).toString();
		// The dedicated smart-filter-v1 prompt, not the generic translation one.
		QVERIFY(systemPrompt.contains(QLatin1String("JSON array")));
		QVERIFY(systemPrompt.contains(QLatin1String("Candidates")));
		QVERIFY(!systemPrompt.contains(QLatin1String("translation engine")));
	}

	void workDossierContextUsesDedicatedSystemPromptWithTargetLanguage()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		QByteArray capturedBody;
		QJsonObject response;
		response.insert(QLatin1String("response"), QStringLiteral("{\"introduction\":{\"overview\":\"...\"},\"recordings\":[]}"));
		serve(server, requestCount, QJsonDocument(response).toJson(QJsonDocument::Compact), 200, 0, &capturedBody);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server));

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("Work: Ludwig van Beethoven — Piano Concerto No.2, Op.19"), QLatin1String("work-dossier-v1"));
		QTRY_COMPARE(ready.count(), 1);
		QCOMPARE(requestCount, 1);

		const QJsonObject payload = QJsonDocument::fromJson(capturedBody).object();
		const QString systemPrompt = payload.value(QLatin1String("system")).toString();
		// The dedicated work-dossier-v1 prompt, not the generic translation
		// one, and it names the configured target language (see
		// writeConfig()'s "Simplified Chinese").
		QVERIFY(systemPrompt.contains(QLatin1String("Simplified Chinese")));
		QVERIFY(systemPrompt.contains(QLatin1String("introduction")));
		QVERIFY(systemPrompt.contains(QLatin1String("recordings")));
		QVERIFY(systemPrompt.contains(QLatin1String("Do not invent")));
		QVERIFY(!systemPrompt.contains(QLatin1String("translation engine")));
	}

	void zzDisabledNetworkStillReadsCache()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("{\"response\":\"cached\"}"));
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server));

		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate(QLatin1String("source"), QLatin1String("context"));
		QTRY_COMPARE(ready.count(), 1);
		TranslationService::disableNetworkAccess();
		QCOMPARE(service.translate(QLatin1String("source"), QLatin1String("context")), QLatin1String("cached"));
		QCOMPARE(service.translate(QLatin1String("uncached"), QLatin1String("context")), QLatin1String("uncached"));
		QTest::qWait(50);
		QCOMPARE(requestCount, 1);
	}
};

QTEST_GUILESS_MAIN(TranslationServiceTest)
#include "translationservice_test.moc"
