#include "network/translationservice.h"
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
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

private:
	static void writeConfig(const QString& path, const QUrl& url, const QString& model = QLatin1String("qwen3.8:27b"), int cooldown = 30,
	                        int timeout = 2000, int maxConcurrent = 1, int maxQueued = 16)
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

	static void serve(QTcpServer& server, int& requestCount, const QByteArray& body, int statusCode = 200, int delayMs = 0)
	{
		QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &requestCount, body, statusCode, delayMs]() {
			while (QTcpSocket* socket = server.nextPendingConnection()) {
				QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &server, &requestCount, body, statusCode, delayMs]() {
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
		QVERIFY(settings.value(QLatin1String("enabled")).toBool());
		QCOMPARE(settings.value(QLatin1String("timeoutMs")).toInt(), 180000);
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
		QTRY_COMPARE(ready.count(), 4);
		QCOMPARE(requestCount, 4);
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
