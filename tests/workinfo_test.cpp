#include "context/workinfo.h"
#include <QTest>

namespace {
// Most tests only care about the composer/album (and optionally
// songTitle/genre) - this forwards to the real signature with an empty
// artist/albumartist, exactly as a song with a normal composer tag would.
WorkInfo::Candidate deriveW(const QString& composer, const QString& album, const QString& songTitle = QString(), const QString& genre = QString())
{
	return WorkInfo::deriveWork(composer, QString(), QString(), album, songTitle, genre);
}
}// namespace

class WorkInfoTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void notAClassicalWorkWithoutComposer()
	{
		const WorkInfo::Candidate work = deriveW(QString(), QStringLiteral("Some Album (Someone - 2000)"));
		QVERIFY(!work.valid);
		QVERIFY(work.title.isEmpty());
		QVERIFY(work.searchQuery.isEmpty());
	}

	void beethovenEmperorConcerto()
	{
		const WorkInfo::Candidate work = deriveW(
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
		const WorkInfo::Candidate work = deriveW(
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
		const WorkInfo::Candidate work = deriveW(
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
		QCOMPARE(deriveW(QStringLiteral("Wolfgang Amadeus Mozart"), QStringLiteral("Piano Concerto No.20 in D minor, K. 466")).catalogueNumber, QString("K. 466"));
		QCOMPARE(deriveW(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Sonata No.14, Op. 27 No. 2 'Moonlight'")).catalogueNumber, QString("Op. 27 No. 2"));
		QCOMPARE(deriveW(QStringLiteral("Joseph Haydn"), QStringLiteral("Piano Sonata, Hob. XVI:52")).catalogueNumber, QString("Hob. XVI:52"));
		QCOMPARE(deriveW(QStringLiteral("Antonio Vivaldi"), QStringLiteral("The Four Seasons, RV 269 'Spring'")).catalogueNumber, QString("RV 269"));
		QCOMPARE(deriveW(QStringLiteral("Franz Schubert"), QStringLiteral("Symphony No.8, D. 759 'Unfinished'")).catalogueNumber, QString("D. 759"));
		QCOMPARE(deriveW(QStringLiteral("Richard Strauss"), QStringLiteral("Also sprach Zarathustra, Op. 30")).catalogueNumber, QString("Op. 30"));
		QCOMPARE(deriveW(QStringLiteral("Percy Grainger"), QStringLiteral("Country Gardens, WoO 12")).catalogueNumber, QString("WoO 12"));
		QVERIFY(deriveW(QStringLiteral("Gustav Mahler"), QStringLiteral("Symphony No.2 'Resurrection'")).catalogueNumber.isEmpty());
	}

	void catalogueNumberRangesAndLists()
	{
		// A range like "BWV 225-229" is one collection work, not five.
		const WorkInfo::Candidate motets = deriveW(QStringLiteral("Johann Sebastian Bach"), QStringLiteral("Motets BWV 225-229"));
		QVERIFY(motets.valid);
		QCOMPARE(motets.title, QString("Motets BWV 225-229"));
		QCOMPARE(motets.catalogueNumber, QString("BWV 225-229"));

		QCOMPARE(deriveW(QStringLiteral("Johann Sebastian Bach"), QStringLiteral("Flutensonaten und obligates Chembalo BWV 1020, 1030-1032")).catalogueNumber,
		         QString("BWV 1020, 1030-1032"));
		QCOMPARE(deriveW(QStringLiteral("Johann Sebastian Bach"), QStringLiteral("Suites BWV 1066,1069")).catalogueNumber, QString("BWV 1066,1069"));
	}

	void composerParticleHandling()
	{
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Ludwig van Beethoven")), QString("Beethoven"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Anton von Webern")), QString("Webern"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Johann Sebastian Bach")), QString("Bach"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Camille Saint-Saëns")), QStringLiteral("Saint-Saëns"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Prokofiev")), QString("Prokofiev"));
		QCOMPARE(WorkInfo::composerSurname(QString()), QString());
		// Defensive: a name that (unusually) ends in a bare particle keeps it
		// attached to the preceding word rather than returning it alone.
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("Foo Bar van")), QString("Bar van"));
	}

	void composerSurnameNormalisesAllCapsTags()
	{
		// "TCHAIKOVSKY 1812 Overture" style tagging: the composer tag itself
		// is shouty-cased, so the surname (used for the search query) is
		// title-cased. Initials are never touched.
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("TCHAIKOVSKY")), QString("Tchaikovsky"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("JS Bach")), QString("Bach"));
		QCOMPARE(WorkInfo::composerSurname(QStringLiteral("J.S.Bach")), QStringLiteral("J.S.Bach"));// dotted - left alone
	}

	// --- Fallback composer from artist/albumartist tags (no composer tag) ---

	void fallbackComposerFromArtistWhenCatalogueConfirmsIt()
	{
		// The real failing case: no composer tag, but the artist tag names
		// the composer and the album carries a BWV catalogue number.
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Johann Sebastian Bach"), QString(),
		    QStringLiteral("Kaffee-Kantate BWV 211 - Bauern-Kantate BWV 212"),
		    QStringLiteral("Kaffeekantate - Rezitativ - Schweigt stille, plaudert nicht"),
		    QStringLiteral("Classical"));
		QVERIFY(work.valid);
		QCOMPARE(work.composer, QString("Johann Sebastian Bach"));
		QCOMPARE(work.surname, QString("Bach"));
		QCOMPARE(work.title, QString("Kaffee-Kantate BWV 211"));
		QCOMPARE(work.catalogueNumber, QString("BWV 211"));
		QCOMPARE(work.genreKeyword, QString("Cantata"));
		QCOMPARE(work.searchQuery, QString("Bach Kaffee-Kantate BWV 211"));
	}

	void fallbackComposerPicksSecondSegmentByTrackTitle()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Johann Sebastian Bach"), QString(),
		    QStringLiteral("Kaffee-Kantate BWV 211 - Bauern-Kantate BWV 212"),
		    QStringLiteral("Bauern-Kantate - Aria - Ach, es schmeckt doch gar zu gut"),
		    QStringLiteral("Classical"));
		QVERIFY(work.valid);
		QCOMPARE(work.title, QString("Bauern-Kantate BWV 212"));
		QCOMPARE(work.catalogueNumber, QString("BWV 212"));
	}

	void fallbackComposerUsesAlbumArtistOverArtist()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Karl Richter"), QStringLiteral("Johann Sebastian Bach"),
		    QStringLiteral("Brandenburg Concertos, BWV 1046"), QString(), QStringLiteral("Classical"));
		QVERIFY(work.valid);
		QCOMPARE(work.composer, QString("Johann Sebastian Bach"));
	}

	void fallbackComposerRejectedWhenArtistDoesNotMatchImpliedComposer()
	{
		// BWV implies Bach, but the artist tag names a performer instead -
		// never treat a performer as the composer.
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Karl Richter"), QString(),
		    QStringLiteral("Brandenburg Concertos, BWV 1046"), QString(), QStringLiteral("Classical"));
		QVERIFY(!work.valid);
	}

	void fallbackComposerRejectsPopSongWithoutCatalogueOrClassicalGenre()
	{
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Taylor Swift"), QString(),
		    QStringLiteral("Lover"), QString(), QStringLiteral("Pop"));
		QVERIFY(!work.valid);
	}

	void fallbackComposerRejectsEnsemblePerformerWithoutCatalogue()
	{
		// Classical genre alone is not enough when there is no catalogue
		// number to confirm it and the "composer" candidate is obviously an
		// ensemble.
		const WorkInfo::Candidate ensemble = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Berliner Philharmoniker"), QString(),
		    QStringLiteral("Live in Berlin"), QString(), QStringLiteral("Classical"));
		QVERIFY(!ensemble.valid);

		// A plain (non-ensemble) name in the same situation is now also
		// rejected: without a catalogue number to back it up, an
		// artist/albumartist-sourced composer must resolve against the
		// known-composer table (see composertable.h) - a conductor's name
		// never does, so it is never mistaken for the composer either.
		const WorkInfo::Candidate plain = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Herbert von Karajan"), QString(),
		    QStringLiteral("Live in Berlin"), QString(), QStringLiteral("Classical"));
		QVERIFY(!plain.valid);
	}

	void fallbackComposerRejectsPerformerCreditWithColonRole()
	{
		// "Preston, Stephen : Flute -" (an "Artist : Role" credit) must never
		// be treated as the composer, even though the genre is Classical -
		// but the album title itself has a genuine composer prefix once the
		// leading track-count number ("63 ") is skipped, so the work is
		// still recognised via that album-prefix mechanism (see
		// extractLeadingComposerPrefix() in workinfo.cpp).
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Preston, Stephen : Flute -"), QString(),
		    QStringLiteral("63 C.P.E. Bach - 2 Conciertos para flauta - Stephen Preston"),
		    QString(), QStringLiteral("Classical"));
		QVERIFY(work.valid);
		QCOMPARE(work.composer, QString("Carl Philipp Emanuel Bach"));
		QCOMPARE(work.surname, QString("Bach"));
		QCOMPARE(work.title, QString("2 Conciertos para flauta"));
		QCOMPARE(work.performer, QString("Stephen Preston"));
	}

	void fallbackComposerHandlesMessyArtistSpellings()
	{
		// All of these just need "Bach" to appear somewhere in the artist
		// tag - see resolveFallbackComposer().
		QCOMPARE(WorkInfo::deriveWork(QString(), QStringLiteral("Bach J.S."), QString(), QStringLiteral("Suites BWV 1066,1069")).composer, QString("Johann Sebastian Bach"));
		QCOMPARE(WorkInfo::deriveWork(QString(), QStringLiteral("J.S.Bach"), QString(), QStringLiteral("Suites BWV 1066,1069")).composer, QString("Johann Sebastian Bach"));
		QCOMPARE(WorkInfo::deriveWork(QString(), QStringLiteral("Bach, Johann Sebastian (1685-1750)"), QString(), QStringLiteral("Suites BWV 1066,1069")).composer,
		         QString("Johann Sebastian Bach"));
		QCOMPARE(WorkInfo::deriveWork(QString(), QStringLiteral("Bach"), QString(), QStringLiteral("Suites BWV 1066,1069")).composer, QString("Johann Sebastian Bach"));
		// Slash-joined multi-composer credit: the catalogue system found
		// picks which composer the tag confirms.
		QCOMPARE(WorkInfo::deriveWork(QString(), QStringLiteral("Bach/Vivaldi"), QString(), QStringLiteral("Motets BWV 230")).composer, QString("Johann Sebastian Bach"));
		QCOMPARE(WorkInfo::deriveWork(QString(), QStringLiteral("Bach/Vivaldi"), QString(), QStringLiteral("Gloria RV 589")).composer, QString("Antonio Vivaldi"));
	}

	// --- Album decoration cleanup ---

	void albumDecorationsStripDiscAndFormatSuffixes()
	{
		QCOMPARE(deriveW(QStringLiteral("Bach"), QStringLiteral("Bach - French Suites (Disc 2)")).title, QString("French Suites"));
		QCOMPARE(deriveW(QStringLiteral("Bach"), QStringLiteral("Bach - English Suites (CD1)")).title, QString("English Suites"));
		const WorkInfo::Candidate withFormatTag = deriveW(QStringLiteral("Johann Sebastian Bach"), QStringLiteral("Flutensonaten und obligates Chembalo BWV 1020, 1030-1032 (Aurele Nicolet...)"));
		QVERIFY(withFormatTag.valid);
		QCOMPARE(withFormatTag.title, QString("Flutensonaten und obligates Chembalo BWV 1020, 1030-1032"));
		QCOMPARE(withFormatTag.performer, QString("Aurele Nicolet..."));
		// The composer's own name, repeated as a leading prefix of the album
		// title (a very common real-world tagging pattern), is stripped once
		// the composer/surname is known - see
		// albumDecorationsStripComposerAndPerformerPrefixes() below.
		QCOMPARE(deriveW(QStringLiteral("Tchaikovsky"), QStringLiteral("TCHAIKOVSKY 1812 Overture (2CH-DST)")).title, QString("1812 Overture"));
	}

	void albumDecorationsStripComposerAndPerformerPrefixes()
	{
		QCOMPARE(deriveW(QStringLiteral("Handel"), QStringLiteral("Handel: Messiah")).title, QString("Messiah"));
		QCOMPARE(deriveW(QStringLiteral("Bach"), QStringLiteral("Bach: Organ Concertos Nos. 1-6")).title, QString("Organ Concertos Nos. 1-6"));

		const WorkInfo::Candidate performerPrefix = deriveW(QStringLiteral("Johann Sebastian Bach"), QStringLiteral("Karl Richter - Brandenburg Concertos Nos.1 - 6 (CD 1)"));
		QVERIFY(performerPrefix.valid);
		QCOMPARE(performerPrefix.title, QString("Brandenburg Concertos Nos.1 - 6"));
		QCOMPARE(performerPrefix.performer, QString("Karl Richter"));
	}

	void chopinBrownIndexCatalogueNumber()
	{
		const WorkInfo::Candidate work = deriveW(QStringLiteral("Frederic Chopin"), QStringLiteral("Mazurka in F minor, B.108"));
		QVERIFY(work.valid);
		QCOMPARE(work.catalogueNumber, QString("B.108"));
		// Op. still takes priority when present.
		QCOMPARE(deriveW(QStringLiteral("Chopin"), QStringLiteral("Nocturne, Op.72 No.1")).catalogueNumber, QString("Op.72 No.1"));
	}

	// --- German/French/Italian work-type keywords ---

	void workTypeKeywordsMatchGermanCompoundsAsSubstrings()
	{
		QCOMPARE(deriveW(QStringLiteral("Bach"), QStringLiteral("Kaffeekantate BWV 211")).genreKeyword, QString("Cantata"));
		QCOMPARE(deriveW(QStringLiteral("Mozart"), QStringLiteral("Klavierkonzert Nr. 20")).genreKeyword, QString("Concerto"));
		QCOMPARE(deriveW(QStringLiteral("Beethoven"), QStringLiteral("Streichquartett Op. 18 Nr. 1")).genreKeyword, QString("String Quartet"));
		QCOMPARE(deriveW(QStringLiteral("Schubert"), QStringLiteral("Sinfonie Nr. 8")).genreKeyword, QString("Symphony"));
		QCOMPARE(deriveW(QStringLiteral("Bach"), QStringLiteral("Matthaus-Passion")).genreKeyword, QString("Passion"));
	}

	void workTypeKeywordsMatchFrenchAndItalianWords()
	{
		// The title has no recognisable keyword of its own - falls back to
		// the genre tag, exercising the French "Messe" -> "Mass" mapping.
		QCOMPARE(deriveW(QStringLiteral("Faure"), QStringLiteral("Pavane"), QString(), QStringLiteral("Messe")).genreKeyword, QString("Mass"));
		QCOMPARE(deriveW(QStringLiteral("Vivaldi"), QStringLiteral("Sinfonia in G major")).genreKeyword, QString("Symphony"));
		QCOMPARE(deriveW(QStringLiteral("Rossini"), QStringLiteral("Petite Messe Solennelle")).genreKeyword, QString("Mass"));
		QCOMPARE(deriveW(QStringLiteral("Saint-Saens"), QStringLiteral("Cantate")).genreKeyword, QString("Cantata"));
	}

	// --- Wikipedia search result selection ---

	void selectSearchResultPrefersGenreAndSurnameMatch()
	{
		const WorkInfo::Candidate work = deriveW(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		const QByteArray response = "{\"query\":{\"search\":["
		                             "{\"title\":\"Piano Concerto No. 2 (Rachmaninoff)\"},"
		                             "{\"title\":\"Piano Concerto No. 5 (Beethoven)\"},"
		                             "{\"title\":\"Piano Sonata No. 23 (Beethoven)\"}"
		                             "]}}";
		QCOMPARE(WorkInfo::selectSearchResult(response, work), QString("Piano Concerto No. 5 (Beethoven)"));
	}

	void selectSearchResultFallsBackToCatalogueNumber()
	{
		const WorkInfo::Candidate work = deriveW(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		const QByteArray response = "{\"query\":{\"search\":[{\"title\":\"Emperor Concerto, Op. 73\"}]}}";
		QCOMPARE(WorkInfo::selectSearchResult(response, work), QString("Emperor Concerto, Op. 73"));
	}

	void selectSearchResultReturnsEmptyWithoutAMatch()
	{
		const WorkInfo::Candidate work = deriveW(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		const QByteArray response = "{\"query\":{\"search\":[{\"title\":\"Symphony No. 9 (Beethoven)\"}]}}";
		// Genre keyword "Concerto" is absent from the result title, so it is
		// rejected even though the composer surname matches.
		QVERIFY(WorkInfo::selectSearchResult(response, work).isEmpty());
		QVERIFY(WorkInfo::selectSearchResult("not json", work).isEmpty());
		QVERIFY(WorkInfo::selectSearchResult(QByteArray("{\"query\":{\"search\":[]}}"), work).isEmpty());
	}

	void selectSearchResultUsesSnippetForCoffeeCantata()
	{
		// Canned from the real response to
		// https://en.wikipedia.org/w/api.php?action=query&list=search&srsearch=Bach%20Kaffee-Kantate%20BWV%20211&srlimit=5&format=json
		// (2026-09-15). The real English article title is "Schweigt stille,
		// plaudert nicht, BWV 211" ("Coffee Cantata" is only a redirect to
		// it - confirmed live via the REST summary API and
		// action=query&redirects=1). That title contains neither "Cantata"
		// nor "Bach", but it does contain the catalogue number "BWV 211"
		// itself - the strongest possible signal - so it must still win over
		// a result that only matches via the genre keyword/snippet, such as
		// the "List of secular cantatas..." index page below.
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QString(), QStringLiteral("Johann Sebastian Bach"), QString(),
		    QStringLiteral("Kaffee-Kantate BWV 211 - Bauern-Kantate BWV 212"),
		    QStringLiteral("Kaffeekantate - Rezitativ - Schweigt stille, plaudert nicht"),
		    QStringLiteral("Classical"));
		QCOMPARE(work.genreKeyword, QString("Cantata"));
		const QByteArray response =
		    "{\"query\":{\"search\":["
		    "{\"title\":\"Schweigt stille, plaudert nicht, BWV 211\","
		    "\"snippet\":\"nicht (Be still, stop chattering), <span class=\\\"searchmatch\\\">BWV</span> <span class=\\\"searchmatch\\\">211</span>, "
		    "also known as the Coffee Cantata, is a secular cantata by Johann Sebastian <span class=\\\"searchmatch\\\">Bach</span>.\"},"
		    "{\"title\":\"Mer hahn en neue Oberkeet, BWV 212\",\"snippet\":\"Bauern-Kantate; Kaffee-Kantate.\"},"
		    "{\"title\":\"List of secular cantatas by Johann Sebastian Bach\","
		    "\"snippet\":\"Coffee Cantata (Kaffee-Kantate) BWV 211\"}"
		    "]}}";
		QCOMPARE(WorkInfo::selectSearchResult(response, work), QString("Schweigt stille, plaudert nicht, BWV 211"));
	}

	void selectSearchResultPrefersSnippetCatalogueOverTitleOnlyMatch()
	{
		const WorkInfo::Candidate work = deriveW(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		const QByteArray response = "{\"query\":{\"search\":["
		                             "{\"title\":\"Piano Concerto No. 5 (Beethoven)\"},"// title/surname match only
		                             "{\"title\":\"Emperor Concerto\",\"snippet\":\"...Op. 73...\"}"// snippet catalogue match
		                             "]}}";
		QCOMPARE(WorkInfo::selectSearchResult(response, work), QString("Emperor Concerto"));
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
