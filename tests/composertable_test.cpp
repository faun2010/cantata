#include "context/composertable.h"
#include <QTest>

class ComposerTableTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void resolvesCanonicalNamesAsIs()
	{
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Ludwig van Beethoven")), QString("Ludwig van Beethoven"));
		QCOMPARE(ComposerTable::resolve(QString::fromUtf8("Frédéric Chopin")), QString::fromUtf8("Frédéric Chopin"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Antonio Vivaldi")), QString("Antonio Vivaldi"));
	}

	void resolvesAccentlessSpellings()
	{
		// No diacritics at all - still the same composer.
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Frederic Chopin")), QString::fromUtf8("Frédéric Chopin"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Antonin Dvorak")), QString::fromUtf8("Antonín Dvořák"));
	}

	void resolvesAbbreviationsAndReorderedNames()
	{
		QCOMPARE(ComposerTable::resolve(QStringLiteral("J.S.Bach")), QString("Johann Sebastian Bach"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("JS Bach")), QString("Johann Sebastian Bach"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Bach J.S.")), QString("Johann Sebastian Bach"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Bach, Johann Sebastian (1685 - 1750)")), QString("Johann Sebastian Bach"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("TCHAIKOVSKY")), QString("Pyotr Ilyich Tchaikovsky"));
	}

	void disambiguatesSharedSurnames()
	{
		// Bare "Bach" defaults to J.S.; C.P.E./J.C. only resolve when named.
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Bach")), QString("Johann Sebastian Bach"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("C.P.E. Bach")), QString("Carl Philipp Emanuel Bach"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("J.C. Bach")), QString("Johann Christian Bach"));
		// Bare "Mozart" defaults to Wolfgang Amadeus; "Leopold" picks his father.
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Mozart")), QString("Wolfgang Amadeus Mozart"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Leopold Mozart")), QString("Leopold Mozart"));
		// Marcello/Scarlatti have no default - an initial is required.
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Marcello, A")), QString("Alessandro Marcello"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Marcello, B")), QString("Benedetto Marcello"));
		QVERIFY(ComposerTable::resolve(QStringLiteral("Marcello")).isEmpty());
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Alessandro Scarlatti")), QString("Alessandro Scarlatti"));
		QCOMPARE(ComposerTable::resolve(QStringLiteral("Domenico Scarlatti")), QString("Domenico Scarlatti"));
	}

	void rejectsPerformersConductorsAndEnsembles()
	{
		QVERIFY(ComposerTable::resolve(QStringLiteral("Sir Charles Mackerras")).isEmpty());
		QVERIFY(ComposerTable::resolve(QStringLiteral("Karl Richter")).isEmpty());
		QVERIFY(ComposerTable::resolve(QStringLiteral("Helmut Walcha (organ)")).isEmpty());
		QVERIFY(ComposerTable::resolve(QStringLiteral("Eduard Melkus")).isEmpty());
		QVERIFY(ComposerTable::resolve(QStringLiteral("Camerata Bern")).isEmpty());
		QVERIFY(ComposerTable::resolve(QStringLiteral("Preston, Stephen : Flute -")).isEmpty());
		QVERIFY(ComposerTable::resolve(QStringLiteral("Gregorianischer Choral")).isEmpty());
	}

	void rejectsEmptyAndPunctuationOnlyInput()
	{
		QVERIFY(ComposerTable::resolve(QString()).isEmpty());
		QVERIFY(ComposerTable::resolve(QStringLiteral("   ")).isEmpty());
		QVERIFY(ComposerTable::resolve(QStringLiteral("- )")).isEmpty());
	}
};

QTEST_GUILESS_MAIN(ComposerTableTest)
#include "composertable_test.moc"
