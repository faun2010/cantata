#include "context/workdossier.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace WorkDossier;

namespace {

QByteArray extractsResponse(const QString& extract)
{
	QJsonObject page;
	page.insert(QLatin1String("pageid"), 12345);
	page.insert(QLatin1String("title"), QLatin1String("Piano Concerto No. 2 (Beethoven)"));
	page.insert(QLatin1String("extract"), extract);
	QJsonObject pages;
	pages.insert(QLatin1String("12345"), page);
	QJsonObject query;
	query.insert(QLatin1String("pages"), pages);
	QJsonObject root;
	root.insert(QLatin1String("query"), query);
	return QJsonDocument(root).toJson();
}

}// namespace

class WorkDossierTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void extractPlainTextParsesTheSinglePageExtract()
	{
		const QString text = extractPlainText(extractsResponse(QStringLiteral("Lead paragraph.\n\n== Background ==\nSome background.")));
		QCOMPARE(text, QString("Lead paragraph.\n\n== Background ==\nSome background."));
	}

	void extractPlainTextHandlesInvalidJson()
	{
		QVERIFY(extractPlainText("not json").isEmpty());
		QVERIFY(extractPlainText(QByteArray()).isEmpty());
	}

	void filterAndCapKeepsLeadAndAllowedSectionsOnly()
	{
		const QString text = QStringLiteral(
		    "The lead paragraph introduces the work.\n\n"
		    "== Background ==\n"
		    "How the work came to be written.\n\n"
		    "== Structure ==\n"
		    "It is in three movements.\n\n"
		    "=== I. Allegro ===\n"
		    "The first movement is a sonata-form allegro.\n\n"
		    "== Reception ==\n"
		    "Critics praised the work.\n\n"
		    "== See also ==\n"
		    "* Some other article\n\n"
		    "== References ==\n"
		    "1. A footnote.\n\n"
		    "== External links ==\n"
		    "* Some link\n");
		const QString filtered = filterAndCapSections(text);
		QVERIFY(filtered.contains(QLatin1String("lead paragraph")));
		QVERIFY(filtered.contains(QLatin1String("Background")));
		QVERIFY(filtered.contains(QLatin1String("How the work came to be written")));
		QVERIFY(filtered.contains(QLatin1String("Structure")));
		QVERIFY(filtered.contains(QLatin1String("first movement is a sonata-form")));
		QVERIFY(filtered.contains(QLatin1String("Reception")));
		QVERIFY(filtered.contains(QLatin1String("Critics praised")));
		QVERIFY(!filtered.contains(QLatin1String("See also")));
		QVERIFY(!filtered.contains(QLatin1String("Some other article")));
		QVERIFY(!filtered.contains(QLatin1String("References")));
		QVERIFY(!filtered.contains(QLatin1String("A footnote")));
		QVERIFY(!filtered.contains(QLatin1String("External links")));
		QVERIFY(!filtered.contains(QLatin1String("Some link")));
	}

	void filterAndCapDropsSubsectionsOfADeniedSection()
	{
		const QString text = QStringLiteral(
		    "Lead.\n\n"
		    "== Notes ==\n"
		    "General notes.\n\n"
		    "=== Citations ===\n"
		    "Should also be dropped, even though it is a subsection heading of\n"
		    "a dropped section rather than itself matching a denied keyword.\n\n"
		    "== Recordings ==\n"
		    "A discussion of notable recordings.\n");
		const QString filtered = filterAndCapSections(text);
		QVERIFY(!filtered.contains(QLatin1String("General notes")));
		QVERIFY(!filtered.contains(QLatin1String("Should also be dropped")));
		QVERIFY(filtered.contains(QLatin1String("Recordings")));
		QVERIFY(filtered.contains(QLatin1String("A discussion of notable recordings")));
	}

	void filterAndCapCapsLengthAtAParagraphBoundary()
	{
		QString text = QStringLiteral("Lead.\n\n== Background ==\n");
		for (int i = 0; i < 200; ++i) {
			text += QStringLiteral("Paragraph number %1 with some filler words to pad it out.\n\n").arg(i);
		}
		const QString filtered = filterAndCapSections(text, 500);
		QVERIFY(filtered.size() <= 500);
		QVERIFY(!filtered.isEmpty());
		// The cut lands on a paragraph boundary, so the result never ends
		// mid-sentence with a dangling partial paragraph.
		QVERIFY(filtered.endsWith(QLatin1Char('.')));
	}

	void filterAndCapHandlesEmptyInput()
	{
		QVERIFY(filterAndCapSections(QString()).isEmpty());
	}

	void buildDossierTextAssemblesAllSections()
	{
		RecommendedRecordings::Recording r;
		r.soloist = QStringLiteral("Artur Rubinstein");
		r.conductor = QStringLiteral("Daniel Barenboim");
		r.label = QStringLiteral("RCA");
		r.catalogue = QStringLiteral("ARL1-4711");
		r.year = QStringLiteral("1975");
		r.comment = QStringLiteral("Warmly recommended for its poise.");

		const QString dossier = buildDossierText(QStringLiteral("Ludwig van Beethoven"), QStringLiteral("Piano Concerto No. 2"), QStringLiteral("Op. 19"),
		                                          QStringLiteral("English article text."), QStringLiteral("Chinese hint text."), {r});

		QVERIFY(dossier.contains(QLatin1String("Ludwig van Beethoven")));
		QVERIFY(dossier.contains(QLatin1String("Piano Concerto No. 2")));
		QVERIFY(dossier.contains(QLatin1String("Op. 19")));
		QVERIFY(dossier.contains(QLatin1String("English article text.")));
		QVERIFY(dossier.contains(QLatin1String("Chinese hint text.")));
		QVERIFY(dossier.contains(QLatin1String("Artur Rubinstein, Daniel Barenboim")));
		QVERIFY(dossier.contains(QLatin1String("RCA")));
		QVERIFY(dossier.contains(QLatin1String("ARL1-4711")));
		QVERIFY(dossier.contains(QLatin1String("Warmly recommended for its poise.")));
		// Recording ids are assigned by position, for the LLM to echo back.
		QVERIFY(dossier.contains(QLatin1String("0 | Artur Rubinstein")));
	}

	void buildDossierTextOmitsEmptySections()
	{
		const QString dossier = buildDossierText(QStringLiteral("Composer"), QStringLiteral("Title"), QString(), QString(), QString(), {});
		QVERIFY(!dossier.contains(QLatin1String("Chinese Wikipedia")));
		QVERIFY(!dossier.contains(QLatin1String("dataset")));
	}

	void parseResponseHandlesFencedJsonWithFullSchema()
	{
		const QString response = QStringLiteral(
		    "Here is the introduction:\n"
		    "```json\n"
		    "{\"introduction\":{\"overview\":\"An overview.\",\"background\":\"Some background.\","
		    "\"structure\":[{\"movement\":\"I. Allegro\",\"description\":\"Sonata form.\"},{\"movement\":\"II. Adagio\",\"description\":\"Slow movement.\"}],"
		    "\"highlights\":\"Notable highlights.\",\"premiere\":\"Premiered in 1795.\"},"
		    "\"recordings\":[{\"id\":\"0\",\"performers\":\"Artur Rubinstein\",\"label\":\"RCA\",\"catalogue\":\"ARL1-4711\",\"year\":\"1975\",\"why\":\"Poised and elegant.\"},"
		    "{\"id\":\"\",\"performers\":\"Extra Performer\",\"label\":\"\",\"catalogue\":\"\",\"year\":\"\",\"why\":\"\"}]}\n"
		    "```\n");
		const Result result = parseResponse(response);
		QVERIFY(result.valid);
		QCOMPARE(result.introduction.overview, QString("An overview."));
		QCOMPARE(result.introduction.background, QString("Some background."));
		QCOMPARE(result.introduction.structure.size(), 2);
		QCOMPARE(result.introduction.structure.at(0).movement, QString("I. Allegro"));
		QCOMPARE(result.introduction.structure.at(0).description, QString("Sonata form."));
		QCOMPARE(result.introduction.highlights, QString("Notable highlights."));
		QCOMPARE(result.introduction.premiere, QString("Premiered in 1795."));
		QCOMPARE(result.recordings.size(), 2);
		QCOMPARE(result.recordings.at(0).id, QString("0"));
		QCOMPARE(result.recordings.at(0).why, QString("Poised and elegant."));
		QVERIFY(result.recordings.at(1).id.isEmpty());
		QCOMPARE(result.recordings.at(1).performers, QString("Extra Performer"));
	}

	void parseResponseHandlesMissingIntroductionParts()
	{
		const QString response = QStringLiteral("{\"introduction\":{\"overview\":\"Just an overview.\"},\"recordings\":[]}");
		const Result result = parseResponse(response);
		QVERIFY(result.valid);
		QCOMPARE(result.introduction.overview, QString("Just an overview."));
		QVERIFY(result.introduction.background.isEmpty());
		QVERIFY(result.introduction.structure.isEmpty());
		QVERIFY(result.introduction.highlights.isEmpty());
		QVERIFY(result.introduction.premiere.isEmpty());
		QVERIFY(result.recordings.isEmpty());
	}

	void parseResponseHandlesSurroundingTextWithoutFence()
	{
		const QString response = QStringLiteral("Sure, here you go: {\"introduction\":{\"overview\":\"Overview text.\"},\"recordings\":[]} Hope that helps!");
		const Result result = parseResponse(response);
		QVERIFY(result.valid);
		QCOMPARE(result.introduction.overview, QString("Overview text."));
	}

	void parseResponseReturnsInvalidForUnrecoverableText()
	{
		QVERIFY(!parseResponse(QStringLiteral("Sorry, I cannot help with that.")).valid);
		QVERIFY(!parseResponse(QString()).valid);
	}

	void parseResponseReturnsInvalidForEmptyObject()
	{
		QVERIFY(!parseResponse(QStringLiteral("{}")).valid);
	}
};

QTEST_GUILESS_MAIN(WorkDossierTest)
#include "workdossier_test.moc"
