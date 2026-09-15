#include "context/recommendedrecordings.h"
#include "context/workinfo.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace RecommendedRecordings;

namespace {

// Built via QJsonObject/QJsonDocument, rather than as an inline raw string
// literal, to steer clear of a moc raw-string-literal parsing limitation:
// a multi-line raw string containing a quoted "https://..." URL on its own
// line (see recording.source below) makes moc report "No relevant classes
// found" for the whole translation unit.
Dataset emperorDataset()
{
	QJsonObject recording;
	recording.insert(QLatin1String("guide"), QLatin1String("TAS Super LP List"));
	recording.insert(QLatin1String("edition"), QLatin1String("2023"));
	recording.insert(QLatin1String("rating"), QLatin1String("Super LP"));
	recording.insert(QLatin1String("soloist"), QLatin1String("Clifford Curzon"));
	recording.insert(QLatin1String("conductor"), QLatin1String("Hans Knappertsbusch"));
	recording.insert(QLatin1String("ensemble"), QLatin1String("Vienna Philharmonic"));
	recording.insert(QLatin1String("label"), QLatin1String("Decca"));
	recording.insert(QLatin1String("catalogue"), QLatin1String("SLX 2002"));
	recording.insert(QLatin1String("year"), QString());
	recording.insert(QLatin1String("source"), QLatin1String("https://www.theabsolutesound.com/articles/2023-tas-super-lp-list/"));
	recording.insert(QLatin1String("comment"), QLatin1String("A performance of aristocratic poise, closely balanced and cleanly transferred."));

	QJsonObject work;
	work.insert(QLatin1String("composer"), QLatin1String("Ludwig van Beethoven"));
	work.insert(QLatin1String("title"), QLatin1String("Piano Concerto No. 5 in E-flat major (Emperor)"));
	work.insert(QLatin1String("catalogue"), QLatin1String("Op. 73"));
	work.insert(QLatin1String("aliases"), QJsonArray{QLatin1String("Piano Concerto No.5"), QLatin1String("Emperor Concerto")});
	work.insert(QLatin1String("recordings"), QJsonArray{recording});

	QJsonObject root;
	root.insert(QLatin1String("version"), 1);
	root.insert(QLatin1String("generated"), QLatin1String("2026-09-15"));
	root.insert(QLatin1String("works"), QJsonArray{work});

	return parseDataset(QJsonDocument(root).toJson());
}

}// namespace

class RecommendedRecordingsTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void parsesDatasetSchema()
	{
		const Dataset dataset = emperorDataset();
		QCOMPARE(dataset.version, 1);
		QCOMPARE(dataset.generated, QString("2026-09-15"));
		QCOMPARE(dataset.works.size(), 1);
		const WorkEntry& work = dataset.works.first();
		QCOMPARE(work.composer, QString("Ludwig van Beethoven"));
		QCOMPARE(work.catalogue, QString("Op. 73"));
		QCOMPARE(work.aliases.size(), 2);
		QCOMPARE(work.recordings.size(), 1);
		const Recording& recording = work.recordings.first();
		QCOMPARE(recording.soloist, QString("Clifford Curzon"));
		QCOMPARE(recording.conductor, QString("Hans Knappertsbusch"));
		QCOMPARE(recording.ensemble, QString("Vienna Philharmonic"));
		QCOMPARE(recording.label, QString("Decca"));
		QCOMPARE(recording.catalogue, QString("SLX 2002"));
		QCOMPARE(recording.guide, QString("TAS Super LP List"));
		QCOMPARE(recording.rating, QString("Super LP"));
		QCOMPARE(recording.comment, QString("A performance of aristocratic poise, closely balanced and cleanly transferred."));
		QVERIFY(!recording.isAi);
	}

	void parsesDatasetWithoutAComment()
	{
		// "comment" is optional - a dataset entry that omits it entirely must
		// still parse cleanly, with an empty comment rather than a failure.
		QJsonObject recording;
		recording.insert(QLatin1String("soloist"), QLatin1String("Someone"));
		QJsonObject work;
		work.insert(QLatin1String("composer"), QLatin1String("Johannes Brahms"));
		work.insert(QLatin1String("title"), QLatin1String("Symphony No. 1"));
		work.insert(QLatin1String("catalogue"), QLatin1String("Op. 68"));
		work.insert(QLatin1String("recordings"), QJsonArray{recording});
		QJsonObject root;
		root.insert(QLatin1String("works"), QJsonArray{work});
		const Dataset dataset = parseDataset(QJsonDocument(root).toJson());
		QCOMPARE(dataset.works.size(), 1);
		QVERIFY(dataset.works.first().recordings.first().comment.isEmpty());
	}

	void parseDatasetHandlesInvalidJson()
	{
		const Dataset dataset = parseDataset("not json");
		QCOMPARE(dataset.works.size(), 0);
	}

	void normaliseCatalogueFoldsPunctuationAndSpacing()
	{
		QCOMPARE(normaliseCatalogue(QStringLiteral("Op.73")), QString("op73"));
		QCOMPARE(normaliseCatalogue(QStringLiteral("Op. 73")), QString("op73"));
		QCOMPARE(normaliseCatalogue(QStringLiteral("op 73")), QString("op73"));
		QCOMPARE(normaliseCatalogue(QStringLiteral("BWV 1007")), QString("bwv1007"));
		QCOMPARE(normaliseCatalogue(QStringLiteral("K. 466")), QString("k466"));
	}

	void normaliseTitleFoldsNumberWords()
	{
		QCOMPARE(normaliseTitle(QStringLiteral("Piano Concerto No.5")), normaliseTitle(QStringLiteral("Piano Concerto No. 5")));
		QCOMPARE(normaliseTitle(QStringLiteral("Symphony Nr. 5")), normaliseTitle(QStringLiteral("Symphony No.5")));
	}

	void findMatchingWorkMatchesByCatalogueNumberDespitePunctuation()
	{
		const Dataset dataset = emperorDataset();
		// "Op.73" (as WorkInfo::deriveWork would produce it) vs the dataset's
		// "Op. 73" - different punctuation/spacing, same catalogue number.
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QStringLiteral("Ludwig van Beethoven"),
		    QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		QCOMPARE(findMatchingWork(dataset, work.composer, work.catalogueNumber, work.title), 0);
	}

	void findMatchingWorkMatchesByAliasWithoutACatalogueNumber()
	{
		const Dataset dataset = emperorDataset();
		// No catalogue number at all this time - only the alias "Piano
		// Concerto No.5" (contained within the derived title) should match.
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QStringLiteral("Ludwig van Beethoven"),
		    QStringLiteral("Piano Concerto No.5 'Emperor' (Serkin - 1981)"));
		QVERIFY(work.catalogueNumber.isEmpty());
		QCOMPARE(findMatchingWork(dataset, work.composer, work.catalogueNumber, work.title), 0);
	}

	void findMatchingWorkDoesNotMatchADifferentWork()
	{
		const Dataset dataset = emperorDataset();
		const WorkInfo::Candidate work = WorkInfo::deriveWork(
		    QStringLiteral("Ludwig van Beethoven"),
		    QStringLiteral("Piano Concerto No.2, Op.19 (Serkin - 1984)"));
		QCOMPARE(findMatchingWork(dataset, work.composer, work.catalogueNumber, work.title), -1);
	}

	void findMatchingWorkRequiresComposerSurnameMatch()
	{
		const Dataset dataset = emperorDataset();
		// Same catalogue number, different composer entirely.
		QCOMPARE(findMatchingWork(dataset, QStringLiteral("Johannes Brahms"), QStringLiteral("Op.73"), QStringLiteral("Symphony No.2")), -1);
	}

	void parseAiRecordingsHandlesCodeFenceAndSurroundingText()
	{
		const QString response = QStringLiteral(
		    "Here are some recommended recordings:\n"
		    "```json\n"
		    "[\n"
		    "  {\"soloist\": \"Rudolf Serkin\", \"conductor\": \"Eugene Ormandy\", \"ensemble\": \"Philadelphia Orchestra\", \"label\": \"CBS\", \"year\": \"1962\"},\n"
		    "  {\"soloist\": \"\", \"conductor\": \"Wilhelm Furtwangler\", \"ensemble\": \"\", \"label\": \"\", \"year\": \"\"}\n"
		    "]\n"
		    "```\n"
		    "I hope this helps!");
		const QList<Recording> recordings = parseAiRecordings(response);
		QCOMPARE(recordings.size(), 2);
		QCOMPARE(recordings.at(0).soloist, QString("Rudolf Serkin"));
		QCOMPARE(recordings.at(0).ensemble, QString("Philadelphia Orchestra"));
		QVERIFY(recordings.at(0).isAi);
		QVERIFY(recordings.at(0).guide.isEmpty());
		QVERIFY(recordings.at(0).rating.isEmpty());
		QCOMPARE(recordings.at(1).conductor, QString("Wilhelm Furtwangler"));
	}

	void parseAiRecordingsCapsAtFiveAndDropsEmptyEntries()
	{
		QString array = QStringLiteral("[");
		for (int i = 0; i < 8; ++i) {
			if (i > 0) array += QLatin1Char(',');
			array += QString("{\"soloist\":\"Performer %1\",\"conductor\":\"\",\"ensemble\":\"\",\"label\":\"\",\"year\":\"\"}").arg(i);
		}
		array += QStringLiteral(",{\"soloist\":\"\",\"conductor\":\"\",\"ensemble\":\"\",\"label\":\"\",\"year\":\"\"}]");
		const QList<Recording> recordings = parseAiRecordings(array);
		QCOMPARE(recordings.size(), 5);
	}

	void parseAiRecordingsReturnsEmptyForUnrecoverableText()
	{
		QVERIFY(parseAiRecordings(QStringLiteral("Sorry, I cannot help with that.")).isEmpty());
		QVERIFY(parseAiRecordings(QStringLiteral("")).isEmpty());
	}

	void libraryAlbumMatchesBySoloistSurname()
	{
		// The library album for the currently-shown work vs. two candidate
		// recordings: one whose soloist is really on that album, one who
		// isn't.
		const WorkInfo::Candidate album = WorkInfo::deriveWork(
		    QStringLiteral("Ludwig van Beethoven"),
		    QStringLiteral("Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)"));
		QCOMPARE(album.performer, QString("Serkin"));
		QVERIFY(performerNameMatches(album.performer, QStringLiteral("Rudolf Serkin")));
		QVERIFY(!performerNameMatches(album.performer, QStringLiteral("Clifford Curzon")));
	}

	void libraryAlbumMatchingIsDiacriticsInsensitive()
	{
		QVERIFY(performerNameMatches(QStringLiteral("Dvorak"), QStringLiteral("Antonín Dvořák")));
		QVERIFY(performerNameMatches(QStringLiteral("Dvořák"), QStringLiteral("Antonin Dvorak")));
	}

	void performerNameMatchesRejectsEmptyInputs()
	{
		QVERIFY(!performerNameMatches(QString(), QStringLiteral("Rudolf Serkin")));
		QVERIFY(!performerNameMatches(QStringLiteral("Serkin"), QString()));
	}

	void recordingCoverKeyIsStableAcrossCosmeticDifferences()
	{
		const QString a = recordingCoverKey(QStringLiteral("Artur Rubinstein, Daniel Barenboim"), QStringLiteral("RCA"), QStringLiteral("ARL1-4711"));
		const QString b = recordingCoverKey(QStringLiteral("Artur Rubinstein, Daniel Barenboim"), QStringLiteral("rca"), QStringLiteral("ARL1 4711"));
		QCOMPARE(a, b);
		QVERIFY(!a.isEmpty());
	}

	void recordingCoverKeyDiffersForDifferentRecordings()
	{
		const QString a = recordingCoverKey(QStringLiteral("Artur Rubinstein"), QStringLiteral("RCA"), QStringLiteral("ARL1-4711"));
		const QString b = recordingCoverKey(QStringLiteral("Glenn Gould"), QStringLiteral("Columbia"), QStringLiteral("6011"));
		QVERIFY(a != b);
	}

	void separatorsAreTheRealCharactersNotMojibake()
	{
		// Regression test for QLatin1String(" · ")/QLatin1String(" — ")
		// literals containing raw UTF-8 bytes, which QLatin1String reads one
		// byte per QChar and so mangles into 2-3 wrong Latin-1 characters
		// each - see context/albumview.cpp's use of these separators.
		const QString middleDot = middleDotSeparator();
		QCOMPARE(middleDot.size(), 3);
		QCOMPARE(middleDot.at(1), QChar(0x00B7));

		const QString emDash = emDashSeparator();
		QCOMPARE(emDash.size(), 3);
		QCOMPARE(emDash.at(1), QChar(0x2014));
	}
};

QTEST_GUILESS_MAIN(RecommendedRecordingsTest)
#include "recommendedrecordings_test.moc"
