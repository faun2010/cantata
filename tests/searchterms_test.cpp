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
	void emptyNeedleAlongsideRealTermInGroup();
};

static QList<QStringList> singleTermGroups(const QStringList& terms)
{
	QList<QStringList> groups;
	for (const QString& term : terms) groups.append(QStringList { term });
	return groups;
}

// Checks both the QStringList-groups convenience overload and the prepare()+NeedleGroup
// overload it wraps, asserting they agree with each other and with `expected`. prepare()
// must preserve the exact matching semantics of the original single-call matches().
static void expectMatches(const QList<QStringList>& groups, const QStringList& values, bool expected)
{
	QCOMPARE(SearchTerms::matches(groups, values), expected);
	QCOMPARE(SearchTerms::matches(SearchTerms::prepare(groups), values), expected);
}

void SearchTermsTest::dashOnlyNeedleDoesNotMatchEverything()
{
	QStringList unrelated { QStringLiteral("Clair de lune") };
	QStringList withDash { QStringLiteral("Track One - B-Side") };

	// A bare dash (or run of dashes) must never normalize away to an empty
	// needle that matches every row.
	for (const QString& term : { QStringLiteral("-"), QStringLiteral("--"), QStringLiteral("—") /* em dash */ }) {
		const QList<QStringList> groups = singleTermGroups({ term });
		expectMatches(groups, unrelated, false);
	}

	// But the literal dash character itself should still be found where present.
	expectMatches(singleTermGroups({ QStringLiteral("-") }), withDash, true);
}

void SearchTermsTest::baseKanaMatchesVoicedKana()
{
	// "か" (no marks) must still find "が" (か + combining dakuten), as before.
	QStringList candidate { QStringLiteral("かが") }; // "かが"
	expectMatches(singleTermGroups({ QStringLiteral("か") }), candidate, true);

	QStringList voicedOnly { QStringLiteral("が") }; // "が"
	expectMatches(singleTermGroups({ QStringLiteral("か") }), voicedOnly, true);
}

void SearchTermsTest::voicedKanaDoesNotMatchBaseKana()
{
	// "が" must not over-match a candidate containing only the bare "か".
	QStringList baseOnly { QStringLiteral("か") }; // "か"
	expectMatches(singleTermGroups({ QStringLiteral("が") }), baseOnly, false);
}

void SearchTermsTest::plainLatinMatchesFullwidthLatin()
{
	QStringList fullwidth { QStringLiteral("ＡＢＣ") }; // "ABC"
	expectMatches(singleTermGroups({ QStringLiteral("abc") }), fullwidth, true);
}

void SearchTermsTest::plainAsciiMatchesAccentedCandidate()
{
	QStringList candidate { QStringLiteral("Café") }; // "Café"
	expectMatches(singleTermGroups({ QStringLiteral("cafe") }), candidate, true);
}

void SearchTermsTest::accentedNeedleMatchesAccentedCandidate()
{
	QStringList candidate { QStringLiteral("Café") }; // "Café"
	expectMatches(singleTermGroups({ QStringLiteral("café") }), candidate, true);
}

void SearchTermsTest::chineseTermMatchesAlternativesGroup()
{
	// A group of alternatives (as produced by SearchTerms::alternatives()) matches
	// if any term in the group is found; all groups must match (AND across groups).
	QStringList row { QStringLiteral("Le carnaval des animaux") };

	QList<QStringList> groups;
	groups.append(QStringList { QStringLiteral("动物"), QStringLiteral("animaux") }); // "动物" + alternative
	expectMatches(groups, row, true);

	// A second, unmet group should make the whole match fail.
	groups.append(QStringList { QStringLiteral("交响乐") }); // unrelated Chinese term, not in row
	expectMatches(groups, row, false);
}

void SearchTermsTest::emptyNeedleAlongsideRealTermInGroup()
{
	// A group can mix a needle that normalizes away to nothing (e.g. a bare "-") with a
	// real term; prepare() must keep the real term usable even though the empty needle's
	// normalized form is dropped.
	QStringList candidate { QStringLiteral("Café society") };

	QList<QStringList> groups;
	groups.append(QStringList { QStringLiteral("--"), QStringLiteral("cafe") });
	expectMatches(groups, candidate, true);

	// And when neither the empty needle's raw text nor the real term is present, the
	// group must still fail to match.
	QStringList unrelated { QStringLiteral("Clair de lune") };
	expectMatches(groups, unrelated, false);
}

QTEST_MAIN(SearchTermsTest)
#include "searchterms_test.moc"
