#include "context/biographytranslation.h"

#include <QTest>
#include <QTextDocument>

class BiographyTranslationTest : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void translatesLinkTextAndUsesOriginalHref();
	void missingMarkersKeepLinksAvailable();
	void rejectsUnsafeHrefsAndDoesNotTrustModelMarkup();
	void oldPlainTextResponseCannotCreateLinks();
};

void BiographyTranslationTest::translatesLinkTextAndUsesOriginalHref()
{
	const BiographyTranslation::Prepared prepared = BiographyTranslation::prepare(
	    "Known for <a href=\"cantata:///?artist=The%20Artist&amp;albumId=42\">The Album</a> and <a href='https://example.test/a?x=1&amp;y=2'>a review</a>.");
	QCOMPARE(prepared.links.size(), 2);
	QVERIFY(prepared.source.contains(BiographyTranslation::marker(0, "BEGIN")));
	const QString translation = QString::fromUtf8("以[[CANTATA_LINK_0_BEGIN]]这张专辑[[CANTATA_LINK_0_END]]和[[CANTATA_LINK_1_BEGIN]]一篇评论[[CANTATA_LINK_1_END]]闻名。");
	const QString html = BiographyTranslation::restore(translation, prepared);
	QVERIFY(html.contains("href=\"cantata:///?artist=The%20Artist&amp;albumId=42\""));
	QVERIFY(html.contains("href=\"https://example.test/a?x=1&amp;y=2\""));
	QVERIFY(html.contains(QString::fromUtf8(">这张专辑</a>")));
}

void BiographyTranslationTest::missingMarkersKeepLinksAvailable()
{
	const BiographyTranslation::Prepared prepared = BiographyTranslation::prepare("<a href=\"cantata:///album/7\">Original album</a> by <a href=\"https://example.test\">The artist</a>");
	const QString translation = QString::fromUtf8("[[CANTATA_LINK_0_BEGIN]]原专辑[[CANTATA_LINK_0_END]]，由该艺人创作");
	const QString html = BiographyTranslation::restore(translation, prepared);
	QVERIFY(html.contains(QString::fromUtf8(">原专辑</a>")));
	QVERIFY(html.contains("href=\"https://example.test\">The artist</a>"));
}

void BiographyTranslationTest::rejectsUnsafeHrefsAndDoesNotTrustModelMarkup()
{
	const BiographyTranslation::Prepared prepared = BiographyTranslation::prepare("<a href=\"javascript:alert(1)\">bad</a> <a href=\"https://safe.test\">good</a>");
	QCOMPARE(prepared.links.size(), 1);
	const QString html = BiographyTranslation::restore("[[CANTATA_LINK_0_BEGIN]]<img src=x onerror=alert(1)>[[CANTATA_LINK_0_END]]", prepared);
	QVERIFY(html.contains("href=\"https://safe.test\""));
	QVERIFY(html.contains("&lt;img src=x onerror=alert(1)&gt;"));
	QVERIFY(!html.contains("javascript:"));
}

void BiographyTranslationTest::oldPlainTextResponseCannotCreateLinks()
{
	const BiographyTranslation::Prepared prepared = BiographyTranslation::prepare("See <a href=\"https://safe.test\">the source</a>");
	QVERIFY(prepared.source != QLatin1String("See the source"));
	const QString html = BiographyTranslation::restore(QString::fromUtf8("查看译文 [CANTATA_LINK_0_BEGIN]伪造标记"), prepared);
	QVERIFY(html.contains("href=\"https://safe.test\">the source</a>"));
	QVERIFY(!html.contains(QString::fromUtf8(">伪造标记</a>")));
}

QTEST_MAIN(BiographyTranslationTest)
#include "biographytranslation_test.moc"
