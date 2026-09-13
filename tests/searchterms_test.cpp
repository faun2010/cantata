#include "support/searchterms.h"

#include <QTest>

class SearchTermsTest : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void dashOnlyNeedleDoesNotMatchEverything();
	void baseKanaMatchesVoicedKana();
	void voicedKanaDoesNotMatchBaseKana();
	void plainLatinMatchesFullwidthLatin();
	void plainAsciiMatchesAccentedCandidate();
	void accentedNeedleMatchesAccentedCandidate();
	void chineseTermMatchesAlternativesGroup();
};

static QList<QStringList> singleTermGroups(const QStringList& terms)
{
	QList<QStringList> groups;
	for (const QString& term : terms) groups.append(QStringList { term });
	return groups;
}

void SearchTermsTest::dashOnlyNeedleDoesNotMatchEverything()
{
	QStringList unrelated { QStringLiteral("Clair de lune") };
	QStringList withDash { QStringLiteral("Track One - B-Side") };

	// A bare dash (or run of dashes) must never normalize away to an empty
	// needle that matches every row.
	for (const QString& term : { QStringLiteral("-"), QStringLiteral("--"), QStringLiteral("—") /* em dash */ }) {
		const QList<QStringList> groups = singleTermGroups({ term });
		QVERIFY2(!SearchTerms::matches(groups, unrelated), qPrintable(term));
	}

	// But the literal dash character itself should still be found where present.
	QVERIFY(SearchTerms::matches(singleTermGroups({ QStringLiteral("-") }), withDash));
}

void SearchTermsTest::baseKanaMatchesVoicedKana()
{
	// "か" (no marks) must still find "が" (か + combining dakuten), as before.
	QStringList candidate { QStringLiteral("かが") }; // "かが"
	QVERIFY(SearchTerms::matches(singleTermGroups({ QStringLiteral("か") }), candidate));

	QStringList voicedOnly { QStringLiteral("が") }; // "が"
	QVERIFY(SearchTerms::matches(singleTermGroups({ QStringLiteral("か") }), voicedOnly));
}

void SearchTermsTest::voicedKanaDoesNotMatchBaseKana()
{
	// "が" must not over-match a candidate containing only the bare "か".
	QStringList baseOnly { QStringLiteral("か") }; // "か"
	QVERIFY(!SearchTerms::matches(singleTermGroups({ QStringLiteral("が") }), baseOnly));
}

void SearchTermsTest::plainLatinMatchesFullwidthLatin()
{
	QStringList fullwidth { QStringLiteral("ＡＢＣ") }; // "ABC"
	QVERIFY(SearchTerms::matches(singleTermGroups({ QStringLiteral("abc") }), fullwidth));
}

void SearchTermsTest::plainAsciiMatchesAccentedCandidate()
{
	QStringList candidate { QStringLiteral("Café") }; // "Café"
	QVERIFY(SearchTerms::matches(singleTermGroups({ QStringLiteral("cafe") }), candidate));
}

void SearchTermsTest::accentedNeedleMatchesAccentedCandidate()
{
	QStringList candidate { QStringLiteral("Café") }; // "Café"
	QVERIFY(SearchTerms::matches(singleTermGroups({ QStringLiteral("café") }), candidate));
}

void SearchTermsTest::chineseTermMatchesAlternativesGroup()
{
	// A group of alternatives (as produced by SearchTerms::alternatives()) matches
	// if any term in the group is found; all groups must match (AND across groups).
	QStringList row { QStringLiteral("Le carnaval des animaux") };

	QList<QStringList> groups;
	groups.append(QStringList { QStringLiteral("动物"), QStringLiteral("animaux") }); // "动物" + alternative
	QVERIFY(SearchTerms::matches(groups, row));

	// A second, unmet group should make the whole match fail.
	groups.append(QStringList { QStringLiteral("交响乐") }); // unrelated Chinese term, not in row
	QVERIFY(!SearchTerms::matches(groups, row));
}

QTEST_MAIN(SearchTermsTest)
#include "searchterms_test.moc"
