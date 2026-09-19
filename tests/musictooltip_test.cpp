#include "widgets/musictooltip.h"
#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QTextBlock>
#include <QTextTable>
#include <QTest>

class MusicToolTipTest : public QObject {
	Q_OBJECT

	static QString originalHtml()
	{
		return QStringLiteral("<table>"
		    "<tr><td align=\"right\"><b>Title:&nbsp;&nbsp;</b></td><td>Allegro con brio</td></tr>"
		    "<tr><td align=\"right\"><b>Artist:&nbsp;&nbsp;</b></td><td>Ludwig van Beethoven</td></tr>"
		    "<tr><td align=\"right\"><b>Length:&nbsp;&nbsp;</b></td><td>7:28</td></tr>"
		    "</table><br/><br/><small><i>0.imslp/Beethoven/Symphony No.5/01 - Allegro.flac</i></small>");
	}

	static QTextTable* table(QTextDocument& document)
	{
		for (QTextFrame* frame : document.rootFrame()->childFrames()) {
			if (auto* result = qobject_cast<QTextTable*>(frame)) return result;
		}
		return nullptr;
	}

	static QString cellText(const QTextTableCell& cell)
	{
		QTextCursor cursor = cell.firstCursorPosition();
		cursor.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
		return cursor.selectedText().simplified();
	}

private Q_SLOTS:
	void metadataRequestExcludesDirectory()
	{
		const QString source = MusicToolTip::sourceText(originalHtml());
		QVERIFY(source.contains(QStringLiteral("Ludwig van Beethoven")));
		QVERIFY(!source.contains(QStringLiteral("0.imslp")));
		QVERIFY(!source.contains(QStringLiteral(".flac")));
		QVERIFY(MusicToolTip::legacySourceText(originalHtml()).contains(QStringLiteral("0.imslp")));
		QCOMPARE(TranslationText::compactKeyValueLines(source),
		         QStringLiteral("Title: Allegro con brio\nArtist: Ludwig van Beethoven\nLength: 7:28"));
	}

	void legacyCachedBreaksBecomeAlignedBoldRows()
	{
		const QString cached = QStringLiteral("标题：  \n快板\n\n艺术家：\u00a0\u00a0\n路德维希·凡·贝多芬\n时长：\n7:28\n\n0.imslp/错误的目录译文.flac");
		const QString html = MusicToolTip::translatedHtml(originalHtml(), cached);
		QTextDocument document;
		document.setHtml(html);
		document.setTextWidth(560);
		document.documentLayout()->documentSize();
		QTextTable* details = table(document);
		QVERIFY(details);
		QCOMPARE(details->rows(), 3);
		QCOMPARE(details->columns(), 2);
		QCOMPARE(cellText(details->cellAt(0, 0)), QStringLiteral("标题："));
		QCOMPARE(cellText(details->cellAt(0, 1)), QStringLiteral("快板"));
		QCOMPARE(cellText(details->cellAt(2, 1)), QStringLiteral("7:28"));
		for (int row = 0; row < details->rows(); ++row) {
			QTextCursor key = details->cellAt(row, 0).firstCursorPosition();
			const QTextBlock value = details->cellAt(row, 1).firstCursorPosition().block();
			QVERIFY(key.blockFormat().alignment().testFlag(Qt::AlignRight));
			QVERIFY(value.blockFormat().alignment().testFlag(Qt::AlignLeft));
			key.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
			QCOMPARE(key.charFormat().fontWeight(), int(QFont::Bold));
			const qreal keyY = document.documentLayout()->blockBoundingRect(key.block()).top();
			const qreal valueY = document.documentLayout()->blockBoundingRect(value).top();
			QVERIFY(qAbs(keyY - valueY) < 1.0);
		}
		QVERIFY(!document.toPlainText().contains(QStringLiteral("错误的目录译文")));
		const QTextCursor path = document.find(QStringLiteral("0.imslp/Beethoven/Symphony No.5/01 - Allegro.flac"));
		QVERIFY(!path.isNull());
		QVERIFY(path.charFormat().fontItalic());
	}

	void inlineValuesAndColonsRemainIntact()
	{
		const QString translated = QStringLiteral("标题: 快板：有活力\r\n艺术家: 贝多芬\r\n时长: 7:28");
		QTextDocument document;
		document.setHtml(MusicToolTip::translatedHtml(originalHtml(), translated));
		QTextTable* details = table(document);
		QVERIFY(details);
		QCOMPARE(cellText(details->cellAt(0, 1)), QStringLiteral("快板：有活力"));
		QCOMPARE(cellText(details->cellAt(2, 1)), QStringLiteral("7:28"));
	}

	void translatedMarkupIsTextAndIncompleteResultsFallBack()
	{
		QTextDocument document;
		document.setHtml(MusicToolTip::translatedHtml(originalHtml(),
		    QStringLiteral("标题： <img src=x> & 音乐\n艺术家： 贝多芬\n时长： 7:28")));
		QVERIFY(document.toPlainText().contains(QStringLiteral("<img src=x> & 音乐")));
		QCOMPARE(MusicToolTip::translatedHtml(originalHtml(), QStringLiteral("标题： 快板")), originalHtml());
	}
};

QTEST_MAIN(MusicToolTipTest)
#include "musictooltip_test.moc"
