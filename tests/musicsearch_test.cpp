#include "network/musicsearch.h"
#include "network/translationservice.h"
#include "support/searchterms.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>
#include <QVariant>

class MusicSearchTest : public QObject {
	Q_OBJECT

	static void writeConfig(const QString& path, const QUrl& url)
	{
		QSettings settings(path, QSettings::IniFormat);
		settings.beginGroup(QLatin1String("Translation"));
		settings.setValue(QLatin1String("enabled"), true);
		settings.setValue(QLatin1String("provider"), QLatin1String("ollama"));
		settings.setValue(QLatin1String("url"), url.toString());
		settings.setValue(QLatin1String("model"), QLatin1String("test-model"));
		settings.setValue(QLatin1String("targetLanguage"), QLatin1String("Simplified Chinese"));
		settings.setValue(QLatin1String("promptVersion"), QLatin1String("search-test-1"));
		settings.setValue(QLatin1String("timeoutMs"), 2000);
		settings.setValue(QLatin1String("cooldownSeconds"), 0);
		settings.setValue(QLatin1String("maxConcurrentRequests"), 1);
		settings.setValue(QLatin1String("maxQueuedRequests"), 16);
		settings.endGroup();
		settings.sync();
		QCOMPARE(settings.status(), QSettings::NoError);
	}

	static QUrl serverUrl(const QTcpServer& server)
	{
		return QUrl(QString::fromLatin1("http://127.0.0.1:%1").arg(server.serverPort()));
	}

	static void serve(QTcpServer& server, int& requestCount, const QByteArray& translatedResponse)
	{
		QJsonObject envelope;
		envelope.insert(QLatin1String("response"), QString::fromUtf8(translatedResponse));
		const QByteArray body = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
		QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &requestCount, body]() {
			while (QTcpSocket* socket = server.nextPendingConnection()) {
				QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				QByteArray* request = new QByteArray;
				socket->setProperty("requestBuffer", QVariant::fromValue(static_cast<void*>(request)));
				QObject::connect(socket, &QObject::destroyed, [request]() { delete request; });
				QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &requestCount, body]() {
					auto* request = static_cast<QByteArray*>(socket->property("requestBuffer").value<void*>());
					*request += socket->readAll();
					const int headerEnd = request->indexOf("\r\n\r\n");
					if (headerEnd < 0) return;
					int contentLength = 0;
					for (QByteArray header : request->left(headerEnd).split('\n')) {
						if (header.trimmed().toLower().startsWith("content-length:"))
							contentLength = header.mid(header.indexOf(':') + 1).trimmed().toInt();
					}
					if (request->size() < headerEnd + 4 + contentLength || socket->property("answered").toBool()) return;
					socket->setProperty("answered", true);
					++requestCount;
					const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
					    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
					socket->write(response);
					socket->disconnectFromHost();
				});
			}
		});
	}

	static QStringList ftsMatches(const QString& expression, const QStringList& rows, bool* succeeded)
	{
		const QString connection = QLatin1String("music-search-") + QUuid::createUuid().toString();
		QStringList matches;
		bool ok = true;
		{
			QSqlDatabase database = QSqlDatabase::addDatabase(QLatin1String("QSQLITE"), connection);
			database.setDatabaseName(QLatin1String(":memory:"));
			ok = database.open();
			QSqlQuery query(database);
			if (ok) ok = query.exec(QLatin1String("CREATE VIRTUAL TABLE docs USING fts4(title, tokenize=unicode61)"));
			if (ok) {
				query.prepare(QLatin1String("INSERT INTO docs(title) VALUES(?)"));
				for (const QString& row : rows) {
					query.bindValue(0, row);
					if (!query.exec()) {
						ok = false;
						break;
					}
				}
			}
			if (ok) {
				query.prepare(QLatin1String("SELECT title FROM docs WHERE docs MATCH ? ORDER BY rowid"));
				query.addBindValue(expression);
				ok = query.exec();
				if (ok) while (query.next()) matches.append(query.value(0).toString());
			}
		}
		QSqlDatabase::removeDatabase(connection);
		if (succeeded) *succeeded = ok;
		return matches;
	}

private Q_SLOTS:
	void initTestCase()
	{
		QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
	}

	void expandsChineseAndPersistsCache()
	{
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(temporary.isValid());
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, QByteArrayLiteral("[\"animals\",\"animaux\",\"动物\"]"));
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		const QString cache = temporary.filePath(QLatin1String("cache"));
		writeConfig(config, serverUrl(server));

		{
			TranslationService service(nullptr, config, cache);
			MusicSearch search(nullptr, &service);
			QSignalSpy ready(&search, &MusicSearch::alternativesReady);
			QCOMPARE(search.alternatives(QString::fromUtf8("动物")), QStringList({QString::fromUtf8("动物")}));
			QCOMPARE(search.alternatives(QString::fromUtf8("动物")), QStringList({QString::fromUtf8("动物")}));
			QTRY_COMPARE(ready.count(), 1);
			QCOMPARE(requestCount, 1);
			const QStringList alternatives = search.alternatives(QString::fromUtf8("动物"));
			QCOMPARE(alternatives, QStringList({QString::fromUtf8("动物"), QLatin1String("animals"), QLatin1String("animaux")}));
			QVERIFY(SearchTerms::matches({alternatives}, {QLatin1String("Le carnaval des animaux")}));
			QVERIFY(SearchTerms::matches({alternatives}, {QString::fromUtf8("动物狂欢节")}));
			QCOMPARE(requestCount, 1);
		}

		TranslationService restarted(nullptr, config, cache);
		MusicSearch restartedSearch(nullptr, &restarted);
		QCOMPARE(restartedSearch.alternatives(QString::fromUtf8("动物")),
		         QStringList({QString::fromUtf8("动物"), QLatin1String("animals"), QLatin1String("animaux")}));
		QCOMPARE(requestCount, 1);
	}

	void safeInputsAndInvalidJsonFallBack_data()
	{
		QTest::addColumn<QByteArray>("response");
		QTest::newRow("malformed") << QByteArrayLiteral("not JSON");
		QTest::newRow("empty") << QByteArrayLiteral("[]");
		QTest::newRow("no-valid-alias") << QStringLiteral("[\"动物\",\"目录/动物.flac\"]").toUtf8();
	}

	void safeInputsAndInvalidJsonFallBack()
	{
		QFETCH(QByteArray, response);
		QTemporaryDir temporary;
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));
		int requestCount = 0;
		serve(server, requestCount, response);
		const QString config = temporary.filePath(QLatin1String("translation.ini"));
		writeConfig(config, serverUrl(server));
		TranslationService service(nullptr, config, temporary.filePath(QLatin1String("cache")));
		MusicSearch search(nullptr, &service);
		QSignalSpy translated(&service, &TranslationService::translationReady);
		QSignalSpy ready(&search, &MusicSearch::alternativesReady);

		QCOMPARE(search.alternatives(QLatin1String("animals")), QStringList({QLatin1String("animals")}));
		QCOMPARE(search.alternatives(QString::fromUtf8("目录/动物.flac")), QStringList({QString::fromUtf8("目录/动物.flac")}));
		QCOMPARE(search.alternatives(QString::fromUtf8("目录\\动物.flac")), QStringList({QString::fromUtf8("目录\\动物.flac")}));
		QTest::qWait(20);
		QCOMPARE(requestCount, 0);

		QCOMPARE(search.alternatives(QString::fromUtf8("动物")), QStringList({QString::fromUtf8("动物")}));
		QTRY_COMPARE(translated.count(), 1);
		QVERIFY(service.cached(QStringLiteral("动物"), QStringLiteral("music-search-v1")).isEmpty());
		// Configuration clamps the cooldown to at least one second.
		QTest::qWait(1100);
		QCOMPARE(search.alternatives(QString::fromUtf8("动物")), QStringList({QString::fromUtf8("动物")}));
		QCOMPARE(ready.count(), 0);
		QTRY_COMPARE(requestCount, 2);
		QTRY_COMPARE(translated.count(), 2);
	}

	void localMatchingUsesAndAcrossFieldsAndIgnoresAccents()
	{
		const QList<QStringList> groups = {
		    {QString::fromUtf8("动物"), QLatin1String("animaux")},
		    {QLatin1String("saint saens"), QString::fromUtf8("圣桑")}};
		QVERIFY(SearchTerms::matches(groups, {QLatin1String("Le carnaval des animaux"), QString::fromUtf8("Camille Saint-Saëns")}));
		QVERIFY(!SearchTerms::matches(groups, {QLatin1String("Le carnaval des animaux"), QLatin1String("Debussy")}));
		QVERIFY(SearchTerms::matches({{QLatin1String("saens")}}, {QString::fromUtf8("Saint-Saëns")}));
	}

	void ftsGroupsAreAndedAndModelTextRemainsLiteral()
	{
		const QStringList rows = {
		    QLatin1String("Le carnaval des animaux Saint Saens"),
		    QLatin1String("Le carnaval des animaux Debussy"),
		    QLatin1String("Nocturne Saint Saens"),
		    QLatin1String("secret")};
		const QString animal = SearchTerms::ftsAlternatives({QString::fromUtf8("动物"), QLatin1String("animaux"), QLatin1String("animals")});
		const QString composer = SearchTerms::ftsAlternatives({QLatin1String("Saint Saens"), QString::fromUtf8("圣桑")});
		bool succeeded = false;
		QCOMPARE(ftsMatches(animal + QLatin1Char(' ') + composer, rows, &succeeded),
		         QStringList({QLatin1String("Le carnaval des animaux Saint Saens")}));
		QVERIFY(succeeded);

		const QString hostile = SearchTerms::ftsAlternatives({QLatin1String("animals\" OR secret")});
		QVERIFY(!hostile.isEmpty());
		QCOMPARE(ftsMatches(hostile, rows, &succeeded), QStringList());
		QVERIFY(succeeded);
	}
};

QTEST_GUILESS_MAIN(MusicSearchTest)
#include "musicsearch_test.moc"
