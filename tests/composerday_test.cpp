#include "playlists/composerday.h"
#include <QFile>
#include <QTest>

namespace {
ComposerDay::Track makeTrack(const QString& file, const QString& album, const QString& title, int track, const QString& composer = QStringLiteral("Jean Sibelius"), const QString& albumArtist = QStringLiteral("Herbert von Karajan"))
{
	ComposerDay::Track t;
	t.file = file;
	t.album = album;
	t.title = title;
	t.track = track;
	t.composer = composer;
	t.albumArtist = albumArtist;
	t.artist = albumArtist;
	t.genre = QStringLiteral("Classical");
	return t;
}

QList<ComposerDay::Composer> calendar()
{
	return ComposerDay::parseCalendar(QByteArray(
			"{\"composers\":["
			"{\"name\":\"Jean Sibelius\",\"zh\":\"西贝柳斯\",\"born\":\"1865-12-08\",\"died\":\"1957-09-20\"},"
			"{\"name\":\"Gioachino Rossini\",\"born\":\"1792-02-29\",\"died\":\"1868-11-13\"},"
			"{\"name\":\"Arvo Pärt\",\"born\":\"1935-09-11\"},"
			"{\"name\":\"Nameless\",\"died\":\"1900-01-01\"},"
			"{\"name\":\"\",\"died\":\"1900-05-05\"},"
			"{\"name\":\"No dates\"}"
			"]}"));
}
}// namespace

class ComposerDayTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void onThisDayDeathsIncludeHighlightedPeople()
	{
		const QByteArray html = "<link rel=\"canonical\" href=\"https://www.onthisday.com/music/deaths/september/22\">\n"
"<li class=\"person\"><a href=\"/music/deaths/date/1981\">1981</a> Harry Warren [Salvatore Guaragna], American composer and lyricist (&quot;You&#039;ll Never Know&quot;), dies at 87</li>\n"
"<header><h2 class=\"poi__heading\"><a href=\"/people/irving-berlin\"><img alt=\"\"><span class=\"poi__heading-txt\">Irving Berlin <span class=\"poi__date\">(1888-1989)</span></span></a></h2></header><p>Russian-American <a href=\"/people/composers\">composer</a> and lyricist, dies at 101</p>\n"
"<li class=\"person no-border\"><b>2001</b> Isaac Stern, American-Ukrainian concert violinist, dies at 81</li>\n"
"<li class=\"person\"><b>2001</b> Isaac Stern, duplicate</li>\n"
"<li class=\"person\"><b>2030</b> Future Person, composer</li>\n"
"<li class=\"person\"><b>2018</b> Charles &quot;Chas&quot; Hodges, English musician, dies at 74</li>\n"
"<li>1999 Somebody Else, composer</li><p>1999 Sidebar Person, composer</p>\n";
		const auto found = ComposerDay::parseOnThisDay(html, QDate(2026, 9, 22), false);
		QCOMPARE(found.count(), 4);
		QCOMPARE(found.at(0).name, QStringLiteral("Harry Warren"));
		QCOMPARE(found.at(0).aliases, QStringList{QStringLiteral("Salvatore Guaragna")});
		QVERIFY(found.at(0).description.contains(QStringLiteral("You'll Never Know")));
		QCOMPARE(found.at(1).name, QStringLiteral("Irving Berlin"));
		QCOMPARE(found.at(1).date, QStringLiteral("1989-09-22"));
		QCOMPARE(found.at(1).years, 37);
		QVERIFY(found.at(1).composer);
		QVERIFY(!found.at(2).composer);
		QVERIFY(!found.at(2).birth);
		QCOMPARE(found.at(3).name, QStringLiteral("Charles Hodges"));
		QCOMPARE(found.at(3).aliases.first(), QStringLiteral("Chas Hodges"));
		QVERIFY(ComposerDay::parseOnThisDay(html, QDate(2026, 9, 23), false).isEmpty());
		QVERIFY(ComposerDay::parseOnThisDay(html, QDate(2026, 9, 22), true).isEmpty());
		QVERIFY(ComposerDay::parseOnThisDay(QByteArray("<html>Access denied</html>"), QDate(2026, 9, 22), false).isEmpty());
	}

	void onThisDayBirthdaysParseNamesAndRealYears()
	{
		const QByteArray html = "<link rel=\"canonical\" href=\"https://www.onthisday.com/music/birthdays/september/22\">\n"
"<li class=\"person\"><b>1733</b> Anton Filtz [Fils], German composer, born in Eichstätt (d. 1760)</li>\n"
"<li class=\"person\"><b>1918</b> (Archibald James) &quot;A.J.&quot; Potter, Irish composer (Finnegan&#039;s Wake), born in Belfast (d. 1980)</li>\n"
"<header><h2 class=\"poi__heading\"><a href=\"/people/andrea-bocelli\"><span>Andrea Bocelli <span class=\"poi__date\">(68 years old)</span></span></a></h2></header><p><a class=\"birthDate\">1958</a> Italian <a>tenor</a>, born in Lajatico</p>\n"
"<header><h2 class=\"poi__heading\">Unknown Singer (68 years old)</h2></header><p>Singer without a historical year</p>\n"
"<li class=\"person\"><b>1942</b> Marlena Shaw [Marlina Burgess], American R&amp;B singer,\n"
"born in New York (d. 2024)</li>\n";
		const auto found = ComposerDay::parseOnThisDay(html, QDate(2026, 9, 22), true);
		QCOMPARE(found.count(), 4);
		QCOMPARE(found.at(0).aliases.first(), QStringLiteral("Anton Fils"));
		QCOMPARE(found.at(1).name, QStringLiteral("A.J. Potter"));
		QCOMPARE(found.at(1).aliases.first(), QStringLiteral("Archibald James Potter"));
		QCOMPARE(found.at(2).name, QStringLiteral("Andrea Bocelli"));
		QCOMPARE(found.at(2).date, QStringLiteral("1958-09-22"));
		QVERIFY(found.at(2).birth);
		QVERIFY(!found.at(2).composer);
		QVERIFY(found.at(3).description.contains(QStringLiteral("R&B")));
	}

	void musicianIdentityRequiresFullName()
	{
		QVERIFY(ComposerDay::musicianMatches(QStringLiteral("Stern, Isaac"), QStringLiteral("Isaac Stern")));
		QVERIFY(ComposerDay::musicianMatches(QStringLiteral("Isaac Stern; Leonard Bernstein"), QStringLiteral("Isaac Stern")));
		QVERIFY(ComposerDay::musicianMatches(QStringLiteral("Anton Fils"), QStringLiteral("Anton Filtz"), {QStringLiteral("Anton Fils")}));
		QVERIFY(!ComposerDay::musicianMatches(QStringLiteral("Stern"), QStringLiteral("Isaac Stern")));
		QVERIFY(!ComposerDay::musicianMatches(QStringLiteral("Mike Stern"), QStringLiteral("Isaac Stern")));
		QVERIFY(!ComposerDay::musicianMatches(QStringLiteral("Isaac Stern Tribute Ensemble"), QStringLiteral("Isaac Stern")));
		QVERIFY(!ComposerDay::musicianMatches(QStringLiteral(""), QStringLiteral("")));
	}

	void parseCalendarDropsUnusableEntries()
	{
		const QList<ComposerDay::Composer> composers = calendar();
		QCOMPARE(composers.count(), 4);
		QCOMPARE(composers.at(0).name, QLatin1String("Jean Sibelius"));
		QCOMPARE(composers.at(0).zh, QString::fromUtf8("西贝柳斯"));
		QCOMPARE(composers.at(2).died, QString());
		QVERIFY(ComposerDay::parseCalendar(QByteArray("[]")).isEmpty());
		QVERIFY(ComposerDay::parseCalendar(QByteArray("not json")).isEmpty());
	}

	void anniversariesAreThoseOfTheGivenDay()
	{
		const QList<ComposerDay::Anniversary> found = ComposerDay::anniversariesFor(calendar(), QDate(2026, 9, 20));
		QCOMPARE(found.count(), 1);
		QCOMPARE(found.at(0).name, QLatin1String("Jean Sibelius"));
		QVERIFY(!found.at(0).birth);
		QCOMPARE(found.at(0).years, 69);
		QCOMPARE(found.at(0).date, QLatin1String("1957-09-20"));
		QVERIFY(ComposerDay::anniversariesFor(calendar(), QDate(2026, 9, 19)).isEmpty());
	}

	void deathAnniversariesComeBeforeBirthdays()
	{
		QList<ComposerDay::Composer> composers = calendar();
		ComposerDay::Composer sameDay;
		sameDay.name = QStringLiteral("Born Today");
		sameDay.born = QStringLiteral("1800-09-20");
		composers.append(sameDay);
		const QList<ComposerDay::Anniversary> found = ComposerDay::anniversariesFor(composers, QDate(2026, 9, 20));
		QCOMPARE(found.count(), 2);
		QVERIFY(!found.at(0).birth);
		QVERIFY(found.at(1).birth);
	}

	void calendarOrderIsKeptWithinEachGroup()
	{
		QList<ComposerDay::Composer> composers;
		ComposerDay::Composer famous;
		famous.name = QStringLiteral("Famous");
		famous.died = QStringLiteral("1950-09-20");
		ComposerDay::Composer obscure;
		obscure.name = QStringLiteral("Obscure");
		obscure.died = QStringLiteral("1700-09-20");
		composers << famous << obscure;
		const QList<ComposerDay::Anniversary> found = ComposerDay::anniversariesFor(composers, QDate(2026, 9, 20));
		QCOMPARE(found.count(), 2);
		QCOMPARE(found.at(0).name, QLatin1String("Famous"));
	}

	void februaryTwentyNinthFallsBackToMarchFirst()
	{
		QVERIFY(!ComposerDay::anniversariesFor(calendar(), QDate(2026, 3, 1)).isEmpty());
		QCOMPARE(ComposerDay::anniversariesFor(calendar(), QDate(2026, 3, 1)).at(0).name, QLatin1String("Gioachino Rossini"));
		// A leap year has the real date, so March 1 must not double it up.
		QVERIFY(ComposerDay::anniversariesFor(calendar(), QDate(2024, 3, 1)).isEmpty());
		QCOMPARE(ComposerDay::anniversariesFor(calendar(), QDate(2024, 2, 29)).count(), 1);
	}

	void anniversariesBeforeTheComposerExistedAreNotReported()
	{
		QVERIFY(ComposerDay::anniversariesFor(calendar(), QDate(1900, 9, 11)).isEmpty());
	}

	void bundledCalendarParses()
	{
		QFile file(QLatin1String(CANTATA_CALENDAR));
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QList<ComposerDay::Composer> composers = ComposerDay::parseCalendar(file.readAll());
		QVERIFY(composers.count() > 500);
		// The point of the generated calendar: nearly every day has somebody.
		int covered = 0;
		for (QDate day(2024, 1, 1); day.year() == 2024; day = day.addDays(1)) {
			if (!ComposerDay::anniversariesFor(composers, day).isEmpty()) ++covered;
		}
		QVERIFY2(covered > 330, qPrintable(QString::number(covered)));
		// Calendar order is fame order: Sibelius heads September 20, ahead of
		// Sarasate who died the same day.
		const QList<ComposerDay::Anniversary> today = ComposerDay::anniversariesFor(composers, QDate(2026, 9, 20));
		QVERIFY(today.count() >= 2);
		QCOMPARE(today.first().name, QLatin1String("Jean Sibelius"));
	}

	void parseWorksReadsObjectsStringsAndFences()
	{
		const QList<ComposerDay::Work> works = ComposerDay::parseWorks(QStringLiteral(
				"Sure!\n```json\n[{\"title\":\"Violin Concerto in D minor\",\"catalogue\":\"Op.47\",\"zh\":\"小提琴协奏曲\"},"
				"\"Finlandia\","
				"{\"title\":\"Violin Concerto\",\"catalogue\":\"Op. 47\"},"
				"{\"title\":\"\"}]\n```"));
		QCOMPARE(works.count(), 2);
		QCOMPARE(works.at(0).catalogue, QLatin1String("Op.47"));
		QCOMPARE(works.at(0).zh, QString::fromUtf8("小提琴协奏曲"));
		QCOMPARE(works.at(0).displayTitle(), QLatin1String("Violin Concerto in D minor, Op.47"));
		QCOMPARE(works.at(1).title, QLatin1String("Finlandia"));
		QVERIFY(ComposerDay::parseWorks(QStringLiteral("no json here")).isEmpty());
	}

	void parseWorksHonoursTheLimit()
	{
		QCOMPARE(ComposerDay::parseWorks(QStringLiteral("[\"A\",\"B\",\"C\"]"), 2).count(), 2);
	}

	void displayTitleDoesNotRepeatTheCatalogueNumber()
	{
		ComposerDay::Work work;
		work.title = QStringLiteral("Symphony No.5, Op.82");
		work.catalogue = QStringLiteral("Op. 82");
		QCOMPARE(work.displayTitle(), QLatin1String("Symphony No.5, Op.82"));
	}

	void selectWorkTracksPrefersTheAlbumDevotedToTheWork()
	{
		const QList<ComposerDay::Track> tracks = {
				makeTrack(QStringLiteral("a1.flac"), QStringLiteral("Sibelius Favourites"), QStringLiteral("Finlandia, Op.26"), 1),
				makeTrack(QStringLiteral("a2.flac"), QStringLiteral("Sibelius Favourites"), QStringLiteral("Violin Concerto in D minor, Op.47 - I. Allegro moderato"), 2),
				makeTrack(QStringLiteral("b2.flac"), QStringLiteral("Violin Concerto in D minor, Op.47 (Oistrakh - 1954)"), QStringLiteral("II. Adagio di molto"), 2),
				makeTrack(QStringLiteral("b1.flac"), QStringLiteral("Violin Concerto in D minor, Op.47 (Oistrakh - 1954)"), QStringLiteral("I. Allegro moderato"), 1),
		};
		ComposerDay::Work work;
		work.title = QStringLiteral("Violin Concerto in D minor");
		work.catalogue = QStringLiteral("Op.47");

		const QList<int> selected = ComposerDay::selectWorkTracks(tracks, work);
		QCOMPARE(selected.count(), 2);
		// Track order within the album, not the order the tracks arrived in.
		QCOMPARE(tracks.at(selected.at(0)).file, QLatin1String("b1.flac"));
		QCOMPARE(tracks.at(selected.at(1)).file, QLatin1String("b2.flac"));
	}

	void selectWorkTracksFallsBackToTheMatchingTracksOfACompilation()
	{
		const QList<ComposerDay::Track> tracks = {
				makeTrack(QStringLiteral("a1.flac"), QStringLiteral("Sibelius Favourites"), QStringLiteral("Finlandia, Op.26"), 1),
				makeTrack(QStringLiteral("a2.flac"), QStringLiteral("Sibelius Favourites"), QStringLiteral("Valse triste, Op.44"), 2),
		};
		ComposerDay::Work work;
		work.title = QStringLiteral("Finlandia");
		work.catalogue = QStringLiteral("Op.26");
		const QList<int> selected = ComposerDay::selectWorkTracks(tracks, work);
		QCOMPARE(selected.count(), 1);
		QCOMPARE(tracks.at(selected.at(0)).file, QLatin1String("a1.flac"));
	}

	void selectWorkTracksReturnsNothingWhenTheWorkIsAbsent()
	{
		const QList<ComposerDay::Track> tracks = {
				makeTrack(QStringLiteral("a1.flac"), QStringLiteral("Sibelius Favourites"), QStringLiteral("Finlandia, Op.26"), 1),
		};
		ComposerDay::Work work;
		work.title = QStringLiteral("Tapiola");
		work.catalogue = QStringLiteral("Op.112");
		QVERIFY(ComposerDay::selectWorkTracks(tracks, work).isEmpty());
	}

	void libraryCompilationUsesDistinctTrackWorksAndCapsAtTen()
	{
		QList<ComposerDay::Track> tracks;
		for (int n = 1; n <= 12; ++n) {
			tracks.append(makeTrack(QStringLiteral("%1a.flac").arg(n), QStringLiteral("Complete Works"), QStringLiteral("Symphony No.%1, Op.%2: I. Allegro").arg(n).arg(n + 100), n * 2));
			tracks.append(makeTrack(QStringLiteral("%1b.flac").arg(n), QStringLiteral("Complete Works"), QStringLiteral("Symphony No.%1, Op.%2: II. Andante").arg(n).arg(n + 100), n * 2 + 1));
		}
		const auto works = ComposerDay::worksFromLibrary(tracks, 50);
		QCOMPARE(works.count(), 10);
		QCOMPARE(works.first().title, QStringLiteral("Symphony No.1, Op.101"));
		QCOMPARE(ComposerDay::selectWorkTracks(tracks, works.first()).count(), 2);
		QCOMPARE(ComposerDay::worksFromLibrary(tracks, 2).count(), 2);
		QVERIFY(ComposerDay::worksFromLibrary(tracks, 0).isEmpty());
		QVERIFY(ComposerDay::worksFromLibrary(tracks, -1).isEmpty());
		QStringList titles;
		for (int n = 0; n < 12; ++n) titles.append(QStringLiteral("\"Work %1\"").arg(n));
		const QString response = QLatin1Char('[') + titles.join(QLatin1Char(',')) + QLatin1Char(']');
		QCOMPARE(ComposerDay::parseWorks(response, 50).count(), 10);
		QVERIFY(ComposerDay::parseWorks(response, 0).isEmpty());
	}

	void libraryPopSongsNeedNoComposerOrClassicalGenre()
	{
		auto first = makeTrack(QStringLiteral("song1"), QStringLiteral("Greatest Hits"), QStringLiteral("Hallelujah"), 1, QString(), QStringLiteral("Leonard Cohen"));
		first.genre = QStringLiteral("Pop");
		auto second = first;
		second.file = QStringLiteral("song2");
		second.title = QStringLiteral("Suzanne");
		second.track = 2;
		const QList<ComposerDay::Track> tracks = {first, second};
		const auto works = ComposerDay::worksFromLibrary(tracks);
		QCOMPARE(works.count(), 2);
		QCOMPARE(works.first().title, QStringLiteral("Hallelujah"));
		QCOMPARE(ComposerDay::selectWorkTracks(tracks, works.first()), QList<int>{0});
	}

	void libraryCompilationDoesNotCountMovementsAsRecordings()
	{
		const QList<ComposerDay::Track> tracks = {
			makeTrack(QStringLiteral("a1"), QStringLiteral("Collection"), QStringLiteral("Symphony No.2, Op.43: I. Allegro"), 1),
			makeTrack(QStringLiteral("a2"), QStringLiteral("Collection"), QStringLiteral("Symphony No.2, Op.43: II. Andante"), 2),
			makeTrack(QStringLiteral("a3"), QStringLiteral("Collection"), QStringLiteral("Symphony No.2, Op.43: III. Presto"), 3),
			makeTrack(QStringLiteral("a4"), QStringLiteral("Collection"), QStringLiteral("Finlandia, Op.26"), 4),
			makeTrack(QStringLiteral("b1"), QStringLiteral("Another Collection"), QStringLiteral("Finlandia, Op.26"), 1),
			makeTrack(QStringLiteral("c1"), QStringLiteral("Complete Works"), QStringLiteral("I. Allegro"), 1),
		};
		const auto works = ComposerDay::worksFromLibrary(tracks);
		QCOMPARE(works.count(), 2);
		QCOMPARE(works.first().title, QStringLiteral("Finlandia, Op.26"));
	}

	void worksFromLibraryRanksTheMostRecordedFirst()
	{
		const QList<ComposerDay::Track> tracks = {
				makeTrack(QStringLiteral("a1.flac"), QStringLiteral("Symphony No.2 in D major, Op.43 (Bernstein - 1987)"), QStringLiteral("I. Allegretto"), 1),
				makeTrack(QStringLiteral("b1.flac"), QStringLiteral("Violin Concerto in D minor, Op.47 (Oistrakh - 1954)"), QStringLiteral("I. Allegro moderato"), 1),
				makeTrack(QStringLiteral("b2.flac"), QStringLiteral("Violin Concerto in D minor, Op.47 (Oistrakh - 1954)"), QStringLiteral("II. Adagio di molto"), 2),
				makeTrack(QStringLiteral("c1.flac"), QStringLiteral("Violin Concerto in D minor, Op.47 (Heifetz - 1959)"), QStringLiteral("I. Allegro moderato"), 1, QStringLiteral("Jean Sibelius"), QStringLiteral("Jascha Heifetz")),
		};
		const QList<ComposerDay::Work> works = ComposerDay::worksFromLibrary(tracks);
		QCOMPARE(works.count(), 2);
		QVERIFY(works.at(0).title.contains(QLatin1String("Violin Concerto")));
		QVERIFY(works.at(1).title.contains(QLatin1String("Symphony No.2")));
		QCOMPARE(ComposerDay::worksFromLibrary(tracks, 1).count(), 1);
	}
};

QTEST_MAIN(ComposerDayTest)
#include "composerday_test.moc"
