#include "context/composeridentities.h"
#include "network/translationservice.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstring>

class Reply : public QNetworkReply {
	QByteArray body;
	qint64 pos = 0;

public:
	Reply(const QNetworkRequest& request, const QByteArray& bytes, QObject* parent) : QNetworkReply(parent), body(bytes)
	{
		setRequest(request);
		setUrl(request.url());
		setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
		open(QIODevice::ReadOnly);
		QTimer::singleShot(0, this, [this]() {if(isFinished())return;setFinished(true);emit readyRead();emit finished(); });
	}
	void abort() override
	{
		if (isFinished()) return;
		setError(OperationCanceledError, "aborted");
		setFinished(true);
		emit finished();
	}
	qint64 bytesAvailable() const override { return body.size() - pos + QNetworkReply::bytesAvailable(); }
	qint64 readData(char* data, qint64 maximum) override
	{
		const qint64 size = qMin(maximum, qint64(body.size()) - pos);
		if (size <= 0) return -1;
		memcpy(data, body.constData() + pos, size);
		pos += size;
		return size;
	}
};
class Manager : public QNetworkAccessManager {
public:
	QByteArray page;
	int posts = 0, gets = 0;
	QByteArray payload;
	QString translated;
	QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice* outgoing) override
	{
		if (op == PostOperation) {
			++posts;
			payload = outgoing->readAll();
			if (!translated.isEmpty() && !QString::fromUtf8(payload).contains("composer-identity-v1"))
				return new Reply(request, QJsonDocument(QJsonObject{{"response", translated}}).toJson(), this);
			return new Reply(request, "{\"response\":\"{\\\"imslp\\\":\\\"Category:Test,_Composer\\\"}\"}", this);
		}
		++gets;
		if (request.url().host() != "imslp.org" || request.url().scheme() != "https") return new Reply(request, {}, this);
		return new Reply(request, page, this);
	}
};
class ComposerIdentityServiceTest : public QObject {
	Q_OBJECT
	QTemporaryDir directory;
	QString config;
	QByteArray page() const
	{
		return "<h1 id=\"firstHeading\">Category:Test, Composer</h1><div class=\"cp_firsth\"><h2>Composer Test</h2></div>"
			   "Alternative Names/Transliterations: Komponist Test<br>Name in Other Languages: <span title=\"zh-hans\">测试·作曲家</span><br>"
			   "Aliases: Composer Example, 测试·作者<br>Authorities - <a href=\"https://en.wikipedia.org/wiki/Composer_Test\">Wikipedia</a>Compositions by: Test, Composer";
	}
	void settings(bool enabled = true)
	{
		QSettings s(config, QSettings::IniFormat);
		s.beginGroup("Translation");
		s.setValue("enabled", enabled);
		s.setValue("provider", "ollama");
		s.setValue("url", "http://localhost:1");
		s.sync();
	}
private slots:
	void initTestCase()
	{
		ComposerIdentities::setConfigurationFile(directory.path() + "/identities/taneyev.json");
		config = directory.path() + "/translation.ini";
	}
	void cleanupTestCase() { ComposerIdentities::setConfigurationFile(QString()); }
	void init()
	{
		QFile::remove(ComposerIdentities::configurationPath());
		settings();
	}
	void verifiesThenWritesAndUpdatesTranslationHints()
	{
		Manager manager;
		manager.page = page();
		TranslationService service(nullptr, config, directory.path() + "/cache");
		service.setNetworkAccessManager(&manager);
		QSignalSpy updated(&service, &TranslationService::composerIdentityUpdated);
		service.ensureComposerIdentity("Komponist Test");
		QTRY_COMPARE(updated.count(), 1);
		QCOMPARE(manager.posts, 1);
		QCOMPARE(manager.gets, 1);
		QVERIFY(QFile::exists(ComposerIdentities::configurationPath()));
		QCOMPARE(ComposerIdentities::lookup("Komponist Test")["canonical"].toString(), QString("Composer Test"));
		const QString discovery = QJsonDocument::fromJson(manager.payload).object()["system"].toString();
		QVERIFY(discovery.contains("taneyev.json"));
		QVERIFY(discovery.contains("IMSLP"));
		service.translate("Composer Test", "Artist name");
		QTRY_COMPARE(manager.posts, 2);
		QVERIFY(QString::fromUtf8(manager.payload).contains("测试·作曲家"));
		service.ensureComposerIdentity("Komponist Test");
		QCOMPARE(manager.gets, 1);
	}
	void wrongPersonIsNeverWritten()
	{
		Manager manager;
		manager.page = page();
		TranslationService service(nullptr, config, directory.path() + "/wrong");
		service.setNetworkAccessManager(&manager);
		QSignalSpy rejected(&service, &TranslationService::composerIdentityRejected);
		service.ensureComposerIdentity("Different Person");
		QTRY_COMPARE(rejected.count(), 1);
		QVERIFY(!QFile::exists(ComposerIdentities::configurationPath()));
	}
	void normalizesVerifiedTranslationAlias()
	{
		Manager manager;
		manager.page = page();
		TranslationService service(nullptr, config, directory.path() + "/conflict-translation");
		service.setNetworkAccessManager(&manager);
		QSignalSpy updated(&service, &TranslationService::composerIdentityUpdated);
		service.ensureComposerIdentity("Komponist Test");
		QTRY_COMPARE(updated.count(), 1);
		manager.translated = "测试·作者的作品。";
		QSignalSpy ready(&service, &TranslationService::translationReady);
		service.translate("Composer Test wrote music.", "Artist biography for Composer Test");
		QTRY_VERIFY(!ready.isEmpty());
		QCOMPARE(ready.first().at(2).toString(), QString("测试·作曲家的作品。"));
		QTRY_COMPARE(manager.gets, 2);
	}
	void refusesBuiltinIdentityCollision()
	{
		Manager manager;
		manager.page = page();
		manager.page.replace("Composer Example", "Ludwig van Beethoven");
		TranslationService service(nullptr, config, directory.path() + "/collision");
		service.setNetworkAccessManager(&manager);
		QSignalSpy rejected(&service, &TranslationService::composerIdentityRejected);
		service.ensureComposerIdentity("Komponist Test");
		QTRY_COMPARE(rejected.count(), 1);
		QVERIFY(!QFile::exists(ComposerIdentities::configurationPath()));
	}

	void disabledLlmDoesNotWriteOrFetch()
	{
		settings(false);
		Manager manager;
		manager.page = page();
		TranslationService service(nullptr, config, directory.path() + "/disabled");
		service.setNetworkAccessManager(&manager);
		service.ensureComposerIdentity("Komponist Test");
		QCOMPARE(manager.posts, 0);
		QCOMPARE(manager.gets, 0);
		QVERIFY(!QFile::exists(ComposerIdentities::configurationPath()));
	}
};
QTEST_GUILESS_MAIN(ComposerIdentityServiceTest)
#include "composeridentityservice_test.moc"
