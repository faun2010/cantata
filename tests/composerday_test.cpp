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
		QVERIFY(composers.count() > 100);
		QCOMPARE(ComposerDay::anniversariesFor(composers, QDate(2026, 9, 20)).count(), 1);
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
