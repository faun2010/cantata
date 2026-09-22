#include "context/artistlookup.h"
#include <QJsonDocument>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>

class ArtistLookupTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void portraitMusicIdentity()
	{
		QCOMPARE(ArtistLookup::portraitName("Chopin"), QString::fromUtf8("Frédéric Chopin"));
		QJsonObject page{{"ns", 0}, {"title", "Gavriil Popov (composer)"},
		    {"pageprops", QJsonObject{{"wikibase-shortdesc", "Soviet composer"}}},
		    {"thumbnail", QJsonObject{{"source", "https://upload.wikimedia.org/portrait.jpg"}}}};
		auto response = [&]() { return QJsonObject{{"query", QJsonObject{{"pages", QJsonArray{page}}}}}; };
		QVERIFY(!ArtistLookup::portraitImageUrl(response(), "Gavriil Popov").isEmpty());
		QVERIFY(ArtistLookup::portraitImageUrl(response(), "Chopin").isEmpty());
		page["pageprops"] = QJsonObject{{"wikibase-shortdesc", "Russian politician"}};
		QVERIFY(ArtistLookup::portraitImageUrl(response(), "Gavriil Popov").isEmpty());
		page["title"] = "Khachaturian";
		page["pageprops"] = QJsonObject{{"disambiguation", ""}};
		QVERIFY(ArtistLookup::portraitImageUrl(response(), "Khachaturian").isEmpty());
		page["title"] = "Aram Khachaturian";
		page["pageprops"] = QJsonObject{{"wikibase-shortdesc", "Armenian composer"}};
		auto redirected = response();
		auto query = redirected["query"].toObject();
		query["redirects"] = QJsonArray{QJsonObject{{"from", "Khachaturian"}, {"to", "Aram Khachaturian"}}};
		redirected["query"] = query;
		QVERIFY(!ArtistLookup::portraitImageUrl(redirected, "Khachaturian").isEmpty());
	}
	void names_data()
	{
		QTest::addColumn<QString>("input");
		QTest::addColumn<QString>("expected");
		QTest::newRow("imslp-borodin") << "Aleksandr Borodin" << "Alexander Borodin";
		QTest::newRow("surname-first") << "Borodin, Aleksandr (1833–1887)" << "Alexander Borodin";
		QTest::newRow("french") << "Alexandre Borodine" << "Alexander Borodin";
		QTest::newRow("russian") << "Александр Порфирьевич Бородин" << "Alexander Borodin";
		QTest::newRow("patronymic") << "Aleksandr Porfiryevich Borodin" << "Alexander Borodin";
		QTest::newRow("initials") << "J.S.Bach" << "Johann Sebastian Bach";
		QTest::newRow("other-bach") << "C.P.E. Bach" << "Carl Philipp Emanuel Bach";
		QTest::newRow("handel") << "Händel, Georg Friedrich" << "George Frideric Handel";
		QTest::newRow("dvorak") << "Antonin Dvorak" << "Antonín Dvořák";
		QTest::newRow("decomposed") << QString::fromUtf8("Antonín Dvořák").normalized(QString::NormalizationForm_D) << "Antonín Dvořák";
		QTest::newRow("tchaikovsky") << "Pyotr Il'yich Tchaikovsky" << "Pyotr Ilyich Tchaikovsky";
		QTest::newRow("rachmaninov") << "Sergey Rachmaninov" << "Sergei Rachmaninoff";
		QTest::newRow("prokofiev") << "Sergey Prokofiev" << "Sergei Prokofiev";
		QTest::newRow("rimsky") << "Nikolay Rimsky-Korsakov" << "Nikolai Rimsky-Korsakov";
		QTest::newRow("scriabin") << "Aleksandr Skryabin" << "Alexander Scriabin";
	}
	void names()
	{
		QFETCH(QString, input);
		QFETCH(QString, expected);
		QCOMPARE(ArtistLookup::queryName(input), expected);
	}
	void avoidsSurnameGuessing()
	{
		for (const QString& input : {QStringLiteral("Borodin Quartet"), QStringLiteral("Clara Schumann"), QStringLiteral("Michael Haydn"), QStringLiteral("Strauss"), QStringLiteral("John Borodin"), QStringLiteral("Borodin, String Quartet"), QStringLiteral("Unknown Composer")}) {
			QCOMPARE(ArtistLookup::queryName(input), input);
		}
	}
	void rejectsTagNotices()
	{
		QVERIFY(ArtistLookup::isTagCorrection("This is mistagged for Alexander Borodin; it would help Last.fm if you could correct your tags."));
		QVERIFY(ArtistLookup::isTagCorrection("An incorrect tag for Alexander Borodin"));
		QVERIFY(!ArtistLookup::isTagCorrection("Alexander Borodin was a Russian composer and chemist."));
	}
	void acceptsRedirectButNotDisambiguation()
	{
		auto parse = [](const char* json) { return ArtistLookup::resolvedTitle(QJsonDocument::fromJson(json).object()); };
		QCOMPARE(parse(R"({"query":{"redirects":[{"from":"Aleksandr Borodin","to":"Alexander Borodin"}],"pages":[{"ns":0,"title":"Alexander Borodin","pageprops":{"wikibase_item":"Q164004"}}]}})"), QString("Alexander Borodin"));
		QVERIFY(parse(R"({"query":{"pages":[{"ns":0,"title":"Borodin","pageprops":{"disambiguation":""}}]}})").isEmpty());
		QVERIFY(parse(R"({"query":{"pages":[{"ns":0,"title":"No such person","missing":true}]}})").isEmpty());
		QVERIFY(parse(R"({"query":{"pages":[{"ns":14,"title":"Category:Borodin"}]}})").isEmpty());
		QVERIFY(parse("{}").isEmpty());
	}
	void rejectsNonMusicalNamesake()
	{
		const auto journalist = QJsonDocument::fromJson(R"({"query":{"pages":[{"ns":0,"title":"Julius Fucik","pageprops":{"wikibase-shortdesc":"Czech journalist"},"extract":"A journalist with a composer namesake."}]}})").object();
		QVERIFY(ArtistLookup::biographyTitle(journalist, QStringLiteral("Julius Fucik")).isEmpty());
		const auto composer = QJsonDocument::fromJson(R"json({"query":{"pages":[{"ns":0,"title":"Dmitri Shostakovich","pageprops":{"wikibase-shortdesc":"Russian composer and pianist (1906-1975)"}}]}})json").object();
		QCOMPARE(ArtistLookup::biographyTitle(composer, QStringLiteral("Dmitry Shostakovich")), QString("Dmitri Shostakovich"));
	}
	void musicalMetadata_data()
	{
		QTest::addColumn<QString>("title");
		QTest::addColumn<QString>("description");
		QTest::addColumn<bool>("biography");
		QTest::addColumn<bool>("accepted");
		QTest::newRow("heath-photographer") << "Dave Heath" << "American documentary and street photographer" << true << false;
		QTest::newRow("heath-composer") << "Dave Heath (composer)" << "British composer and flautist" << true << true;
		QTest::newRow("flute-player") << "Dave Heath" << "British flautist" << true << true;
		QTest::newRow("visual-artist") << "Example (artist)" << "American visual artist" << true << false;
		QTest::newRow("novel") << "A Work" << "British novel" << false << false;
		QTest::newRow("film") << "A Work" << "American film" << false << false;
		QTest::newRow("album") << "A Work" << "Studio album by a British band" << false << true;
		QTest::newRow("concerto") << "A Work" << "Violin concerto by Dave Heath" << false << true;
		QTest::newRow("no-evidence") << "Dave Heath" << "" << true << false;
		QTest::newRow("chinese") << "音乐家" << "英国作曲家" << true << true;
	}
	void musicalMetadata()
	{
		QFETCH(QString, title);
		QFETCH(QString, description);
		QFETCH(bool, biography);
		QFETCH(bool, accepted);
		QJsonObject page{{"ns", 0}, {"title", title}, {"pageprops", QJsonObject{{"wikibase-shortdesc", description}}},
		                 {"extract", "A photographer who photographed a composer, musician and orchestra."}};
		auto response = [&]() { return QJsonObject{{"query", QJsonObject{{"pages", QJsonArray{page}}}}}; };
		QCOMPARE(ArtistLookup::musicalPage(response(), biography), accepted);
		if (biography) QCOMPARE(!ArtistLookup::biographyTitle(response(), title).isEmpty(), accepted);
		page.insert("pageprops", QJsonObject{{"disambiguation", ""}});
		QVERIFY(!ArtistLookup::musicalPage(response(), biography));
	}
	void musicalCategories()
	{
		QJsonObject page{{"ns", 0}, {"title", "Dave Heath"},
		                 {"categories", QJsonArray{QJsonObject{{"title", "Category:British flautists"}}}}};
		const QJsonObject response{{"query", QJsonObject{{"pages", QJsonArray{page}}}}};
		QVERIFY(ArtistLookup::musicalPage(response, true));
		QVERIFY(!ArtistLookup::musicalPage(response, false));
	}
	void portraitCacheTracksIdentityChanges()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString previous = ComposerIdentities::configurationPath();
		ComposerIdentities::setConfigurationFile(dir.filePath("identity.json"));
		const QString before = ArtistLookup::imageCacheToken("Test Musician");
		QFile file(dir.filePath("identity.json"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(R"({"version":1,"people":[{"canonical":"Test Musician","imslp":"Category:Musician,_Test","aliases":[],"musicbrainz":"e252e2e9-5cca-4bb6-a787-f9236d3a91e0"}]})");
		file.close();
		QVERIFY(ArtistLookup::imageCacheToken("Test Musician") != before);
		QCOMPARE(ArtistLookup::musicBrainzId("Test Musician"), QString("e252e2e9-5cca-4bb6-a787-f9236d3a91e0"));
		ComposerIdentities::setConfigurationFile(previous);
	}
	void portraitIdentityAndCache()
	{
		QCOMPARE(ArtistLookup::musicBrainzId("Dave Heath"), QString("e252e2e9-5cca-4bb6-a787-f9236d3a91e0"));
		QCOMPARE(ArtistLookup::musicBrainzId("Heath, Dave"), ArtistLookup::musicBrainzId("Dave Heath"));
		QVERIFY(ArtistLookup::musicBrainzId("David Martin Heath").isEmpty());
		QVERIFY(ArtistLookup::musicBrainzId("Heath").isEmpty());
		QVERIFY(ArtistLookup::imageCacheToken("Dave Heath").startsWith("artist-identity-v3-"));
		QVERIFY(ArtistLookup::imageCacheToken("Dave Heath") != ArtistLookup::imageCacheToken("David Martin Heath"));
		const QJsonObject page{{"ns", 0}, {"title", "Alexander Borodin"},
		    {"pageprops", QJsonObject{{"wikibase-shortdesc", "Photographer"}}},
		    {"thumbnail", QJsonObject{{"source", "https://upload.wikimedia.org/portrait.jpg"}}}};
		QVERIFY(ArtistLookup::composerImageUrl(QJsonObject{{"query", QJsonObject{{"pages", QJsonArray{page}}}}}, "Alexander Borodin").isEmpty());
	}
	void composerPortraitThumbnailHost()
	{
		const QStringList allowed = {
		    "https://upload.wikimedia.org/wikipedia/commons/portrait.jpg",
		    "https://thumb.wikimedia.org/wikipedia/commons/thumb/portrait.jpg/960px-portrait.jpg?utm_source=en.wikipedia.org"
		};
		const QStringList rejected = {
		    "http://thumb.wikimedia.org/portrait.jpg",
		    "https://thumb.wikimedia.org.example.com/portrait.jpg",
		    "https://thumb.wikimedia.org@evil.example/portrait.jpg",
		    "https://other.wikimedia.org/portrait.jpg"
		};
		for (const QString& source : allowed + rejected) {
			QJsonObject page{{"ns", 0}, {"title", "Sergei Prokofiev"},
		                 {"pageprops", QJsonObject{{"wikibase-shortdesc", "Russian composer"}}},
			                 {"thumbnail", QJsonObject{{"source", source}}}};
			const QJsonObject response{{"query", QJsonObject{{"pages", QJsonArray{page}}}}};
			const QUrl expected = allowed.contains(source) ? QUrl(source) : QUrl();
			QCOMPARE(ArtistLookup::composerImageUrl(response, "Sergey Prokofiev"), expected);
			QVERIFY(ArtistLookup::composerImageUrl(response, "Sergei Rachmaninoff").isEmpty());
		}
	}
	void composerPortraitRequiresMatchingIdentity()
	{
		auto response = [](QJsonObject page) {
			return QJsonObject{{"query", QJsonObject{{"pages", QJsonArray{page}}}}};
		};
		QJsonObject page{{"ns", 0}, {"title", "Alexander Borodin"},
		                 {"pageprops", QJsonObject{{"wikibase-shortdesc", "Russian composer"}}},
		                 {"thumbnail", QJsonObject{{"source", "https://upload.wikimedia.org/wikipedia/commons/7/70/Borodin.jpg"}}}};
		const QUrl expected("https://upload.wikimedia.org/wikipedia/commons/7/70/Borodin.jpg");
		QCOMPARE(ArtistLookup::composerImageUrl(response(page), "Aleksandr Borodin"), expected);
		QCOMPARE(ArtistLookup::composerImageUrl(response(page), "Alexandre Borodine"), expected);
		QVERIFY(ArtistLookup::composerImageUrl(response(page), "Borodin Quartet").isEmpty());
		QVERIFY(ArtistLookup::composerImageUrl(response(page), "Alexander Scriabin").isEmpty());
		page.insert("pageprops", QJsonObject{{"disambiguation", ""}});
		QVERIFY(ArtistLookup::composerImageUrl(response(page), "Aleksandr Borodin").isEmpty());
		page.remove("pageprops");
		page.insert("missing", true);
		QVERIFY(ArtistLookup::composerImageUrl(response(page), "Aleksandr Borodin").isEmpty());
		page.remove("missing");
		page.remove("thumbnail");
		QVERIFY(ArtistLookup::composerImageUrl(response(page), "Aleksandr Borodin").isEmpty());
		page.insert("thumbnail", QJsonObject{{"source", "https://example.org/Borodin.jpg"}});
		QVERIFY(ArtistLookup::composerImageUrl(response(page), "Aleksandr Borodin").isEmpty());
		QVERIFY(ArtistLookup::composerImageUrl({}, "Aleksandr Borodin").isEmpty());
	}
};

QTEST_GUILESS_MAIN(ArtistLookupTest)
#include "artistlookup_test.moc"
