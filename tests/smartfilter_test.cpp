#include "playlists/smartfilter.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace {
SmartFilter::Candidate makeCandidate(const QString& title, const QString& artist = QString(), const QString& composer = QString(), const QString& genre = QString())
{
	SmartFilter::Candidate c;
	c.title = title;
	c.artist = artist;
	c.composer = composer;
	c.genre = genre;
	return c;
}

QJsonArray candidatesFromSource(const QString& source)
{
	const int marker = source.indexOf(QLatin1String("\n\nCandidates:\n"));
	if (marker < 0) return QJsonArray();
	return QJsonDocument::fromJson(source.mid(marker + 14).toUtf8()).array();
}
}// namespace

class SmartFilterTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void buildSourceContainsDescriptionAndCandidates()
	{
		const QList<SmartFilter::Candidate> candidates = {
		    makeCandidate(QStringLiteral("Allegro"), QStringLiteral("Serkin"), QStringLiteral("Beethoven"), QStringLiteral("Classical")),
		    makeCandidate(QStringLiteral("Adagio")),
		};
		const QString source = SmartFilter::buildSource(QStringLiteral("rainy evening\nwant calm piano"), candidates);
		QVERIFY(source.startsWith(QLatin1String("Description:\nrainy evening\nwant calm piano")));
		const QJsonArray array = candidatesFromSource(source);
		QCOMPARE(array.count(), 2);
		QCOMPARE(array.at(0).toObject().value(QLatin1String("i")).toInt(), 0);
		QCOMPARE(array.at(0).toObject().value(QLatin1String("title")).toString(), QLatin1String("Allegro"));
		QCOMPARE(array.at(0).toObject().value(QLatin1String("composer")).toString(), QLatin1String("Beethoven"));
		QCOMPARE(array.at(1).toObject().value(QLatin1String("i")).toInt(), 1);
		// Empty optional fields are omitted entirely.
		QVERIFY(!array.at(1).toObject().contains(QLatin1String("composer")));
		QVERIFY(!array.at(1).toObject().contains(QLatin1String("genre")));
		QVERIFY(!array.at(1).toObject().contains(QLatin1String("year")));
	}

	void buildSourceTruncatesAtMaxCandidates()
	{
		QList<SmartFilter::Candidate> candidates;
		for (int i = 0; i < 10; ++i) candidates.append(makeCandidate(QString::number(i)));
		const QJsonArray array = candidatesFromSource(SmartFilter::buildSource(QStringLiteral("x"), candidates, 3));
		QCOMPARE(array.count(), 3);
		QCOMPARE(array.at(2).toObject().value(QLatin1String("title")).toString(), QLatin1String("2"));
	}

	void parsePlainArray()
	{
		bool ok = false;
		const QSet<int> selected = SmartFilter::parseSelection(QStringLiteral("[0, 2, 5]"), 10, &ok);
		QVERIFY(ok);
		QCOMPARE(selected, QSet<int>({0, 2, 5}));
	}

	void parseToleratesCodeFenceAndProse()
	{
		bool ok = false;
		const QSet<int> selected = SmartFilter::parseSelection(
		    QStringLiteral("Here are the picks:\n```json\n[3, 1]\n```\nHope that helps!"), 10, &ok);
		QVERIFY(ok);
		QCOMPARE(selected, QSet<int>({1, 3}));
	}

	void parseDropsOutOfRangeAndNonNumeric()
	{
		bool ok = false;
		const QSet<int> selected = SmartFilter::parseSelection(QStringLiteral("[0, 9, -1, 10, \"4\", \"abc\", null]"), 10, &ok);
		QVERIFY(ok);
		QCOMPARE(selected, QSet<int>({0, 4, 9}));
	}

	void parseRejectsEmptyArray()
	{
		bool ok = true;
		SmartFilter::parseSelection(QStringLiteral("[]"), 10, &ok);
		QVERIFY(!ok);
	}

	void parseRejectsOnlyInvalidIndices()
	{
		bool ok = true;
		SmartFilter::parseSelection(QStringLiteral("[42, -3]"), 10, &ok);
		QVERIFY(!ok);
	}

	void parseRejectsNonJson()
	{
		bool ok = true;
		SmartFilter::parseSelection(QStringLiteral("no idea, sorry"), 10, &ok);
		QVERIFY(!ok);
	}
};

QTEST_GUILESS_MAIN(SmartFilterTest)
#include "smartfilter_test.moc"
