#include "context/wikitemplateutils.h"

#include <QTest>

class WikiTemplateUtilsTest : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void retainsInterlanguageTitles();
	void usesInterlanguageLtTitle();
	void retainsLanguageTemplateText();
	void leavesUnknownTemplatesForExistingCleanup();
	void detectsNamesakeListings();
	void keepsRealBiographies();
};

void WikiTemplateUtilsTest::retainsInterlanguageTitles()
{
	QString source = "the song-cycle ''{{ill|Clairières dans le ciel|fr}}'' and ''{{interlanguage-link|D'un soir triste|fr}}''";
	QCOMPARE(WikiTemplateUtils::visibleTemplateText(source), QString("the song-cycle ''Clairières dans le ciel'' and ''D'un soir triste''"));
}

void WikiTemplateUtilsTest::usesInterlanguageLtTitle()
{
	QString source = "{{ill|French title|fr|lt=Visible English title}}";
	QCOMPARE(WikiTemplateUtils::visibleTemplateText(source), QString("Visible English title"));
}

void WikiTemplateUtilsTest::retainsLanguageTemplateText()
{
	QString source = "{{lang|fr|D'un soir triste}} / {{lang-fr|D'un matin de printemps}}";
	QCOMPARE(WikiTemplateUtils::visibleTemplateText(source), QString("D'un soir triste / D'un matin de printemps"));
	QCOMPARE(WikiTemplateUtils::visibleTemplateText("{{lang|fr|Clairières dans le ciel|italic=no}}"), QString("Clairières dans le ciel"));
	QCOMPARE(WikiTemplateUtils::visibleTemplateText("{{lang-fr|D'un soir triste|label=none}}"), QString("D'un soir triste"));
}

void WikiTemplateUtilsTest::leavesUnknownTemplatesForExistingCleanup()
{
	QString source = "{{citation needed|date=September 2026}}";
	QCOMPARE(WikiTemplateUtils::visibleTemplateText(source), source);
}

void WikiTemplateUtilsTest::detectsNamesakeListings()
{
	// The page Cantata used to translate for "Julius Fu\u010d\u00edk": a list of the
	// composer and the journalist, rather than a biography of either.
	QVERIFY(WikiTemplateUtils::isDisambiguationPage("'''Julius Fu\u010d\u00edk''' may refer to:\n* [[Julius Fu\u010d\u00edk (composer)]]\n{{hndis|Fucik, Julius}}"));
	QVERIFY(WikiTemplateUtils::isDisambiguationPage("{{disambiguation}}"));
	QVERIFY(WikiTemplateUtils::isDisambiguationPage("{{ disambig }}"));
	QVERIFY(WikiTemplateUtils::isDisambiguationPage("{{\u6d88\u6b67\u4e49}}"));
	QVERIFY(WikiTemplateUtils::isDisambiguationPage("{{Begriffskl\u00e4rung}}"));
	QVERIFY(WikiTemplateUtils::isDisambiguationPage("{{Surname|Fu\u010d\u00edk}}"));
}

void WikiTemplateUtilsTest::keepsRealBiographies()
{
	QVERIFY(!WikiTemplateUtils::isDisambiguationPage("{{Infobox musical artist}}\n'''Julius Fu\u010d\u00edk''' was a Czech composer.\n{{Authority control}}"));
	QVERIFY(!WikiTemplateUtils::isDisambiguationPage("His work is often confused with that of his namesake; see the disambiguation page."));
}

QTEST_MAIN(WikiTemplateUtilsTest)
#include "wikitemplateutils_test.moc"
