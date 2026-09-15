#include "context/workinfo.h"
#include <QTest>

class WorkInfoTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void notAClassicalWorkWithoutComposer()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(QString(), QStringLiteral("Some Album (Someone - 2000)"));
		QVERIFY(!work.valid);
		QVERIFY(work.title.isEmpty());
		QVERIFY(work.searchQuery.isEmpty());
	}

	void beethovenEmperorConcerto()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QStringLiteral("Ludwig van Beethoven"),
		    QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"),
		    QStringLiteral("Allegro"),
		    QStringLiteral("Concerto"));
		QVERIFY(work.valid);
		QCOMPARE(work.surname, QString("Beethoven"));
		QCOMPARE(work.title, QString("Piano Concerto No.5, Op.73 'Emperor'"));
		QCOMPARE(work.catalogueNumber, QString("Op.73"));
		QCOMPARE(work.performer, QString("Serkin"));
		QCOMPARE(work.year, QString("1981"));
		QCOMPARE(work.genreKeyword, QString("Concerto"));
		QCOMPARE(work.searchQuery, QString("Beethoven Piano Concerto No.5 Op.73"));
	}

	void beethovenSecondConcerto()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QStringLiteral("Ludwig van Beethoven"),
		    QStringLiteral("Piano Concerto No.2, Op.19 (Serkin - 1984)"),
		    QStringLiteral("Allegro con brio"));
		QVERIFY(work.valid);
		QCOMPARE(work.title, QString("Piano Concerto No.2, Op.19"));
		QCOMPARE(work.catalogueNumber, QString("Op.19"));
		QCOMPARE(work.performer, QString("Serkin"));
		QCOMPARE(work.year, QString("1984"));
		QCOMPARE(work.searchQuery, QString("Beethoven Piano Concerto No.2 Op.19"));
	}

	void albumWithoutTrailingParentheses()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QStringLiteral("Johann Sebastian Bach"),
		    QStringLiteral("Cello Suite No.1 in G major, BWV 1007"));
		QVERIFY(work.valid);
		QCOMPARE(work.title, QString("Cello Suite No.1 in G major, BWV 1007"));
		QVERIFY(work.performer.isEmpty());
		QVERIFY(work.year.isEmpty());
		QCOMPARE(work.catalogueNumber, QString("BWV 1007"));
		QCOMPARE(work.surname, QString("Bach"));
	}

	void catalogueNumberVariants()
	{
		QCOMPARE(WorkInfo::deriveWork(QStringLiteral("Wolfgang Amadeus Mozart"), QStringLiteral("Piano Concerto No.20 in D minor, K. 466")).catalogueNumber, QString("K. 466"));
		QCOMPARE(WorkInfo::deriveWork(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Sonata No.14, Op. 27 No. 2 'Moonlight'")).catalogueNumber, QString("Op. 27 No. 2"));
		QCOMPARE(WorkInfo::deriveWork(QStringLiteral("Joseph Haydn"), QStringLiteral("Piano Sonata, Hob. XVI:52")).catalogueNumber, QString("Hob. XVI:52"));
		QCOMPARE(WorkInfo::deriveWork(QStringLiteral("Antonio Vivaldi"), QStringLiteral("The Four Seasons, RV 269 'Spring'")).catalogueNumber, QString("RV 269"));
		QCOMPARE(WorkInfo::deriveWork(QStringLiteral("Franz Schubert"), QStringLiteral("Symphony No.8, D. 759 'Unfinished'")).catalogueNumber, QString("D. 759"));
		QCOMPARE(WorkInfo::deriveWork(QStringLiteral("Richard Strauss"), QStringLiteral("Also sprach Zarathustra, Op. 30")).catalogueNumber, QString("Op. 30"));
		QCOMPARE(WorkInfo::deriveWork(QStringLiteral("Percy Grainger"), QStringLiteral("Country Gardens, WoO 12")).catalogueNumber, QString("WoO 12"));
		QVERIFY(WorkInfo::deriveWork(QStringLiteral("Gustav Mahler"), QStringLiteral("Symphony No.2 'Resurrection'")).catalogueNumber.isEmpty());
	}

	void composerParticleHandling()
	{
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Ludwig van Beethoven")), QString("Beethoven"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Anton von Webern")), QString("Webern"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Johann Sebastian Bach")), QString("Bach"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Camille Saint-Sa\u00ebns")), QStringLiteral("Saint-Sa\u00ebns"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Prokofiev")), QString("Prokofiev"));
		QCOMPARE(WorkInfo::composerSurname(QString()), QString());
		// Defensive: a name that (unusually) ends in a bare particle keeps it
		// attached to the preceding word rather than returning it alone.
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Foo Bar van")), QString("Bar van"));
	}

	void selectSearchResultPrefersGenreAndSurnameMatch()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		const QByteArray response = "{\"query\":{\"search\":["
		                             "{\"title\":\"Piano Concerto No. 2 (Rachmaninoff)\"},"
		                             "{\"title\":\"Piano Concerto No. 5 (Beethoven)\"},"
		                             "{\"title\":\"Piano Sonata No. 23 (Beethoven)\"}"
		                             "]}}";
		QCOMPARE(WorkInfo::selectSearchResult(response, work), QString("Piano Concerto No. 5 (Beethoven)"));
	}

	void selectSearchResultFallsBackToCatalogueNumber()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		const QByteArray response = "{\"query\":{\"search\":[{\"title\":\"Emperor Concerto, Op. 73\"}]}}";
		QCOMPARE(WorkInfo::selectSearchResult(response, work), QString("Emperor Concerto, Op. 73"));
	}

	void selectSearchResultReturnsEmptyWithoutAMatch()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		const QByteArray response = "{\"query\":{\"search\":[{\"title\":\"Symphony No. 9 (Beethoven)\"}]}}";
		// Genre keyword "Concerto" is absent from the result title, so it is
		// rejected even though the composer surname matches.
		QVERIFY(WorkInfo::selectSearchResult(response, work).isEmpty());
		QVERIFY(WorkInfo::selectSearchResult("not json", work).isEmpty());
		QVERIFY(WorkInfo::selectSearchResult(QByteArray("{\"query\":{\"search\":[]}}"), work).isEmpty());
	}

	void parseSiteLinksFindsZhArticle()
	{
		const QByteArray response = "{\"query\":{\"pages\":{\"12345\":{\"title\":\"Piano Concerto No. 5 (Beethoven)\","
		                             "\"pageprops\":{\"wikibase_item\":\"Q182609\"},"
		                             "\"langlinks\":[{\"lang\":\"de\",\"*\":\"Klavierkonzert Nr. 5\"},"
		                             "{\"lang\":\"zh\",\"*\":\"\\u8d1d\\u591a\\u82ac\\u7b2c5\\u53f7\\u94a2\\u7434\\u534f\\u594f\\u66f2\"}]}}}}";
		const WorkInfo::SiteLinks links = WorkInfo::parseSiteLinks(response);
		QCOMPARE(links.wikidataId, QString("Q182609"));
		QCOMPARE(links.zhTitle, QStringLiteral("贝多芬第5号钢琴协奏曲"));
	}

	void parseSiteLinksHandlesMissingZhArticle()
	{
		const QByteArray response = "{\"query\":{\"pages\":{\"12345\":{\"title\":\"Some Work\",\"pageprops\":{\"wikibase_item\":\"Q1\"}}}}}";
		const WorkInfo::SiteLinks links = WorkInfo::parseSiteLinks(response);
		QCOMPARE(links.wikidataId, QString("Q1"));
		QVERIFY(links.zhTitle.isEmpty());
		QVERIFY(WorkInfo::parseSiteLinks("not json").zhTitle.isEmpty());
	}

	void parseSummaryExtractsTextAndUrl()
	{
		const QByteArray response = "{\"type\":\"standard\",\"title\":\"Piano Concerto No. 5 (Beethoven)\","
		                             "\"extract\":\"The Piano Concerto No. 5 in E-flat major, Op. 73, is a work by Ludwig van Beethoven.\","
		                             "\"content_urls\":{\"desktop\":{\"page\":\"https://en.wikipedia.org/wiki/Piano_Concerto_No._5_(Beethoven)\"},"
		                             "\"mobile\":{\"page\":\"https://en.m.wikipedia.org/wiki/Piano_Concerto_No._5_(Beethoven)\"}}}";
		const WorkInfo::Summary summary = WorkInfo::parseSummary(response);
		QCOMPARE(summary.extract, QString("The Piano Concerto No. 5 in E-flat major, Op. 73, is a work by Ludwig van Beethoven."));
		QCOMPARE(summary.url, QString("https://en.wikipedia.org/wiki/Piano_Concerto_No._5_(Beethoven)"));
	}

	void parseSummaryFallsBackToMobileUrlAndHandlesBadJson()
	{
		const QByteArray response = "{\"extract\":\"Text\",\"content_urls\":{\"mobile\":{\"page\":\"https://en.m.wikipedia.org/wiki/X\"}}}";
		const WorkInfo::Summary summary = WorkInfo::parseSummary(response);
		QCOMPARE(summary.url, QString("https://en.m.wikipedia.org/wiki/X"));
		const WorkInfo::Summary broken = WorkInfo::parseSummary("not json");
		QVERIFY(broken.extract.isEmpty());
		QVERIFY(broken.url.isEmpty());
	}
};

QTEST_GUILESS_MAIN(WorkInfoTest)
#include "workinfo_test.moc"
