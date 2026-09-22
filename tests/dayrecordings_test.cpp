#include "playlists/dayrecordings.h"
#include <QTest>

namespace {
ComposerDay::Work work()
{
	ComposerDay::Work w;
	w.title = QStringLiteral("Violin Concerto in D minor");
	w.catalogue = QStringLiteral("Op.47");
	return w;
}
ComposerDay::Track track(const QString& path, const QString& artist, int year = 1954)
{
	ComposerDay::Track t;
	t.file = path;
	t.album = work().displayTitle();
	t.title = QStringLiteral("I. Allegro moderato");
	t.composer = QStringLiteral("Jean Sibelius");
	t.albumArtist = t.artist = artist;
	t.year = year;
	t.track = 1;
	t.genre = QStringLiteral("Classical");
	return t;
}
RecommendedRecordings::Dataset dataset()
{
	RecommendedRecordings::Dataset d;
	RecommendedRecordings::WorkEntry w;
	w.composer = QStringLiteral("Jean Sibelius");
	w.title = work().title;
	w.catalogue = work().catalogue;
	RecommendedRecordings::Recording r;
	r.soloist = QStringLiteral("David Oistrakh");
	r.year = QStringLiteral("1954");
	w.recordings.append(r);
	d.works.append(w);
	return d;
}
DayRecordings::Selection select(const QList<ComposerDay::Track>& tracks, const RecommendedRecordings::Dataset& d = dataset())
{
	return DayRecordings::select(QStringLiteral("Jean Sibelius"), tracks, work(), d);
}
}

class DayRecordingsTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void prefersKnownRecording()
	{
		const auto result = select({track(QStringLiteral("other/a.flac"), QStringLiteral("Jascha Heifetz")),
				track(QStringLiteral("known/a.flac"), QStringLiteral("David Oistrakh"))});
		QVERIFY(result.recommended);
		QCOMPARE(result.indices, QList<int>({1}));
	}
	void doesNotConflateSameSurnameOrPartialName()
	{
		for (const QString& name : {QStringLiteral("Igor Oistrakh"), QStringLiteral("Oistrakh"), QStringLiteral("David Oistrakhson")}) {
			const auto result = select({track(QStringLiteral("a.flac"), name)});
			QVERIFY(!result.recommended);
			QCOMPARE(result.indices, QList<int>({0}));
		}
	}
	void checksKnownYear()
	{
		QVERIFY(!select({track(QStringLiteral("a.flac"), QStringLiteral("David Oistrakh"), 1964)}).recommended);
		QVERIFY(!select({track(QStringLiteral("a.flac"), QStringLiteral("David Oistrakh"), 0)}).recommended);
	}
	void keepsSeparateDirectoriesAndTrackOrder()
	{
		auto second = track(QStringLiteral("one/2.flac"), QStringLiteral("David Oistrakh"));
		second.track = 2;
		const auto result = select({second,
				track(QStringLiteral("two/1.flac"), QStringLiteral("David Oistrakh")),
				track(QStringLiteral("one/1.flac"), QStringLiteral("David Oistrakh"))});
		QVERIFY(result.recommended);
		QCOMPARE(result.indices, QList<int>({2, 0}));
	}
	void mustMatchWorkAndComposer()
	{
		auto d = dataset();
		d.works[0].composer = QStringLiteral("Other Sibelius");
		QVERIFY(!select({track(QStringLiteral("a.flac"), QStringLiteral("David Oistrakh"))}, d).recommended);
		auto absent = track(QStringLiteral("a.flac"), QStringLiteral("David Oistrakh"));
		absent.album = QStringLiteral("Finlandia, Op.26");
		QVERIFY(select({absent}).indices.isEmpty());
	}
	void atMostTwoRecordingsWithRecommendedFirst()
	{
		const QList<ComposerDay::Track> tracks = {
			track(QStringLiteral("one/a.flac"), QStringLiteral("Jascha Heifetz")),
			track(QStringLiteral("two/a.flac"), QStringLiteral("Igor Oistrakh")),
			track(QStringLiteral("three/a.flac"), QStringLiteral("David Oistrakh")),
			track(QStringLiteral("four/a.flac"), QStringLiteral("Other Violinist"))};
		const auto results = DayRecordings::selectRecordings(QStringLiteral("Jean Sibelius"), tracks, work(), dataset());
		QCOMPARE(results.size(), 2);
		QCOMPARE(results[0].indices, QList<int>({2}));
		QVERIFY(results[0].recommended);
		QCOMPARE(results[1].indices, QList<int>({0}));
		QVERIFY(!results[1].recommended);
	}
	void fallbackPreservesBestWorkMatchScore()
	{
		auto weak = track(QStringLiteral("weak/a.flac"), QStringLiteral("Other Violinist"));
		weak.album = work().title;
		const auto exact = track(QStringLiteral("exact/a.flac"), QStringLiteral("Jascha Heifetz"));
		const auto results = DayRecordings::selectRecordings(QStringLiteral("Jean Sibelius"), {weak, exact}, work(), {});
		QCOMPARE(results.size(), 2);
		QCOMPARE(results[0].indices, QList<int>({1}));
		QCOMPARE(results[1].indices, QList<int>({0}));
		QVERIFY(!results[0].recommended);
		QCOMPARE(select({weak, exact}, {}).indices, QList<int>({1}));
	}

	void bothRecordingsKeepAllMovementsInOrder()
	{
		auto first = track(QStringLiteral("one/1.flac"), QStringLiteral("David Oistrakh"));
		auto second = first;
		second.file = QStringLiteral("one/2.flac");
		second.track = 2;
		auto third = track(QStringLiteral("two/1.flac"), QStringLiteral("Jascha Heifetz"));
		auto fourth = third;
		fourth.file = QStringLiteral("two/2.flac");
		fourth.disc = 2;
		const auto results = DayRecordings::selectRecordings(QStringLiteral("Jean Sibelius"), {second, fourth, first, third}, work(), dataset());
		QCOMPARE(results.size(), 2);
		QCOMPARE(results[0].indices, QList<int>({2, 0}));
		QCOMPARE(results[1].indices, QList<int>({3, 1}));
	}
	void duplicateRecommendationsAndFilesAreNotRepeated()
	{
		auto d = dataset();
		d.works[0].recordings.append(d.works[0].recordings.first());
		d.works.append(d.works.first());
		const auto first = track(QStringLiteral("one/a.flac"), QStringLiteral("David Oistrakh"));
		auto duplicateGroup = first;
		duplicateGroup.albumArtist = QStringLiteral("Other Artist");
		const auto results = DayRecordings::selectRecordings(QStringLiteral("Jean Sibelius"),
				{first, first, duplicateGroup, track(QStringLiteral("two/a.flac"), QStringLiteral("Jascha Heifetz"))}, work(), d);
		QCOMPARE(results.size(), 2);
		QCOMPARE(results[0].indices, QList<int>({0}));
		QCOMPARE(results[1].indices, QList<int>({3}));
	}
	void noMatchingWorkReturnsNoRecordings()
	{
		auto absent = track(QStringLiteral("a.flac"), QStringLiteral("David Oistrakh"));
		absent.album = QStringLiteral("Finlandia, Op.26");
		QVERIFY(DayRecordings::selectRecordings(QStringLiteral("Jean Sibelius"), {absent}, work(), dataset()).isEmpty());
		QVERIFY(DayRecordings::selectRecordings(QStringLiteral("Jean Sibelius"), {}, work(), dataset()).isEmpty());
	}

	void allNamedPerformersMustBePresent()
	{
		auto d = dataset();
		d.works[0].recordings[0].conductor = QStringLiteral("Eugene Ormandy");
		QVERIFY(!select({track(QStringLiteral("a.flac"), QStringLiteral("David Oistrakh"))}, d).recommended);
		QVERIFY(select({track(QStringLiteral("a.flac"), QStringLiteral("David Oistrakh; Eugene Ormandy"))}, d).recommended);
	}
};

QTEST_GUILESS_MAIN(DayRecordingsTest)
#include "dayrecordings_test.moc"
