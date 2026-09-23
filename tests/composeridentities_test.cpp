#include "context/artistlookup.h"
#include "context/composeridentities.h"
#include "context/composertable.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

class ComposerIdentitiesTest : public QObject {
	Q_OBJECT
	QTemporaryDir directory;
	QByteArray fixture;
	void write(const QByteArray& bytes)
	{
		QDir().mkpath(QFileInfo(ComposerIdentities::configurationPath()).absolutePath());
		QFile file(ComposerIdentities::configurationPath());
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(bytes), bytes.size());
	}
private slots:
	void initTestCase()
	{
		ComposerIdentities::setConfigurationFile(directory.path() + "/identities/taneyev.json");
		QFile file(CANTATA_IDENTITY_FIXTURE);
		QVERIFY(file.open(QIODevice::ReadOnly));
		fixture = file.readAll();
	}
	void cleanupTestCase() { ComposerIdentities::setConfigurationFile(QString()); }
	void init() { write(fixture); }
	void resolvesConfiguredIdentityAndSiteTitle()
	{
		QCOMPARE(ComposerTable::biographyName("Sergei Taneyev"), QString("Sergey Taneyev"));
		QCOMPARE(ComposerTable::resolve("Taneyev, Sergey"), QString("Sergey Taneyev"));
		QCOMPARE(ArtistLookup::wikipediaName("Sergey Taneyev"), QString("Sergei Taneyev"));
		QVERIFY(ComposerTable::biographyName("Taneyev").isEmpty());
		QVERIFY(ComposerIdentities::hints("Sergey Taneyev biography").contains("谢尔盖"));
		QJsonObject page{{"ns", 0}, {"title", "Sergei Taneyev"},
		    {"pageprops", QJsonObject{{"wikibase-shortdesc", "Russian composer and pianist"}, {"wikibase_item", "Q123"}}}};
		QCOMPARE(ArtistLookup::portraitEntityId(QJsonObject{{"query", QJsonObject{{"pages", QJsonArray{page}}}}}, "Sergey Taneyev"), QString("Q123"));
	}
	void hotReloadAndInvalidFileProtection()
	{
		const QString before = ComposerIdentities::revision();
		auto doc = QJsonDocument::fromJson(fixture).object();
		auto list = doc["people"].toArray();
		auto person = list[0].toObject();
		auto aliases = person["aliases"].toArray();
		aliases.append("Serge Tanéeff");
		person["aliases"] = aliases;
		list[0] = person;
		doc["people"] = list;
		write(QJsonDocument(doc).toJson());
		QCOMPARE(ComposerTable::biographyName("Serge Tanéeff"), QString("Sergey Taneyev"));
		QVERIFY(before != ComposerIdentities::revision());
		write("not json");
		QCOMPARE(ComposerTable::biographyName("Serge Tanéeff"), QString("Sergey Taneyev"));
		QString error;
		QVERIFY(!ComposerIdentities::mergeVerified(person, &error));
		QVERIFY(!error.isEmpty());
		QFile file(ComposerIdentities::configurationPath());
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArray("not json"));
	}
	void conflictingAliasesAreNotGuessedOrOverwritten()
	{
		auto first = QJsonDocument::fromJson(fixture).object()["people"].toArray()[0].toObject();
		auto other = first;
		other["canonical"] = "Another Person";
		other["imslp"] = "Category:Person,_Another";
		QString error;
		QVERIFY(!ComposerIdentities::mergeVerified(other, &error));
		write(QJsonDocument(QJsonObject{{"version", 1}, {"people", QJsonArray{first, other}}}).toJson());
		bool conflict = false;
		QVERIFY(ComposerIdentities::lookup("Sergei Taneyev", &conflict).isEmpty());
		QVERIFY(conflict);
		QVERIFY(ComposerTable::biographyName("Sergei Taneyev").isEmpty());
		QVERIFY(ComposerIdentities::hints("Sergei Taneyev").isEmpty());
	}
	void onlyImslpHeaderSuppliesAliases()
	{
		QByteArray html = "<h1 id=\"firstHeading\">Category:Taneyev, Sergey</h1><div class=\"cp_firsth\"><h2>Sergey Taneyev</h2></div>\nAlternative Names/Transliterations: Sergei Taneyev, Taneyev<br><span>Name in Other Languages: <span title=\"ru\">Танеев, Сергей Иванович</span>, <span title=\"zh-hans\">谢尔盖·塔涅耶夫</span></span><br>Aliases: Sergey Ivanovich Taneyev<br>Authorities - <a href=\"https://en.wikipedia.org/wiki/Sergei_Taneyev\">Wikipedia</a>\nCompositions by: Taneyev, Sergey <p>Unrelated Composer</p>";
		auto p = ComposerIdentities::fromImslp(html, "Category:Taneyev,_Sergey", "Sergei Taneyev");
		QCOMPARE(p["canonical"].toString(), QString("Sergey Taneyev"));
		QCOMPARE(p["zh"].toString(), QString("谢尔盖·塔涅耶夫"));
		QVERIFY(!p["aliases"].toArray().contains("Taneyev"));
		QVERIFY(p["aliases"].toArray().contains("Танеев, Сергей Иванович"));
		QVERIFY(!p["aliases"].toArray().contains("Сергей Иванович"));
		QVERIFY(ComposerIdentities::fromImslp(html, "Category:Taneyev,_Sergey", "Unrelated Composer").isEmpty());
		QVERIFY(ComposerIdentities::fromImslp(html, "Category:Wrong,_Person", "Sergei Taneyev").isEmpty());
		html.replace("Compositions by:", "Performances by:");
		QVERIFY(ComposerIdentities::fromImslp(html, "Category:Taneyev,_Sergey", "Sergei Taneyev").isEmpty());
	}
	void indexTracksReloadConflictAndRemovedAliases()
	{
		QJsonObject first{{"canonical", "First Person"}, {"imslp", "Category:Person,_First"},
		    {"aliases", QJsonArray{"Shared Náme", "Name, Shared", "Old Alias"}}};
		const QJsonObject second{{"canonical", "Second Person"}, {"imslp", "Category:Person,_Second"},
		    {"aliases", QJsonArray{"Shared Name"}}};
		write(QJsonDocument(QJsonObject{{"version", 1}, {"people", QJsonArray{first}}}).toJson());
		bool conflict = true;
		QCOMPARE(ComposerIdentities::lookup("Shared Name", &conflict).value("canonical").toString(), QString("First Person"));
		QVERIFY(!conflict);// Repeated normalized aliases on the same person are safe.
		const QString before = ComposerIdentities::revision();
		write(QJsonDocument(QJsonObject{{"version", 1}, {"people", QJsonArray{first, second}}}).toJson());
		QVERIFY(ComposerIdentities::lookup("Name, Shared", &conflict).isEmpty());
		QVERIFY(conflict);
		QVERIFY(ComposerIdentities::hints("Shared Name").isEmpty());
		QVERIFY(!ComposerIdentities::hints("First Person").isEmpty());
		const QString conflicted = ComposerIdentities::revision();
		QVERIFY(before != conflicted);
		write("broken json");
		QVERIFY(ComposerIdentities::lookup("Shared Name", &conflict).isEmpty());
		QVERIFY(conflict);
		QCOMPARE(ComposerIdentities::revision(), conflicted);
		first["aliases"] = QJsonArray{"Shared Name", "Replacement Alias"};
		write(QJsonDocument(QJsonObject{{"version", 1}, {"people", QJsonArray{first}}}).toJson());
		QCOMPARE(ComposerIdentities::lookup("Shared Name", &conflict).value("canonical").toString(), QString("First Person"));
		QVERIFY(!conflict);
		QVERIFY(ComposerIdentities::lookup("Old Alias").isEmpty());
		QVERIFY(ComposerIdentities::lookup("Second Person").isEmpty());
		QVERIFY(!ComposerIdentities::lookup("Replacement Alias").isEmpty());
		QVERIFY(!ComposerIdentities::hints("Shared Name").isEmpty());
	}
	void indexTracksMergeAndConfigurationSwitch()
	{
		const QJsonObject person{{"canonical", "Merged Person"}, {"imslp", "Category:Person,_Merged"},
		    {"aliases", QJsonArray{"Fresh Alias"}}};
		QVERIFY(ComposerIdentities::lookup("Fresh Alias").isEmpty());
		const QString before = ComposerIdentities::revision();
		QString error;
		QVERIFY2(ComposerIdentities::mergeVerified(person, &error), qPrintable(error));
		QCOMPARE(ComposerIdentities::lookup("Fresh Alias").value("canonical").toString(), QString("Merged Person"));
		QVERIFY(before != ComposerIdentities::revision());
		const QString previousPath = ComposerIdentities::configurationPath();
		struct RestoreConfiguration {
			QString path;
			~RestoreConfiguration() { ComposerIdentities::setConfigurationFile(path); }
		} restore{previousPath};
		ComposerIdentities::setConfigurationFile(directory.filePath("malformed.json"));
		write("invalid initial file");
		QVERIFY(ComposerIdentities::lookup("Fresh Alias").isEmpty());
		QVERIFY(ComposerIdentities::hints("Fresh Alias").isEmpty());
		ComposerIdentities::setConfigurationFile(previousPath);
		QCOMPARE(ComposerIdentities::lookup("Fresh Alias").value("canonical").toString(), QString("Merged Person"));
	}
};
QTEST_GUILESS_MAIN(ComposerIdentitiesTest)
#include "composeridentities_test.moc"
