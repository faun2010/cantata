#include "gui/artistimageprovider.h"

#include <QTest>

class ArtistImageProviderTest : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void acceptsOnlyExactLastFmArtist();
	void acceptsAliasOnlyWhenMusicBrainzMatchIsUnique();
	void refusesAmbiguousMusicBrainzName();
	void acceptsMotorheadDiacriticVariant();
	void prefersExactMusicBrainzMatchOverDiacriticVariant();
	void refusesAmbiguousDiacriticMusicBrainzName();
	void verifiesMusicBrainzIdBeforeWikidataRelation();
	void extractsCommonsP18AndDistinguishesRetryStates();
	void normalizesUnicodeNames();
	void aggressiveNameKeyFolding();
	void luceneQuotedEscaping();
};

void ArtistImageProviderTest::acceptsOnlyExactLastFmArtist()
{
	QByteArray response = R"(<lfm><artist><name>Bach</name><mbid>right</mbid><similar><artist><name>Wrong Bach</name><mbid>wrong</mbid></artist></similar></artist></lfm>)";
	QCOMPARE(ArtistImageProvider::lastFmMusicBrainzId(response, "Bach"), QString("right"));
	QVERIFY(ArtistImageProvider::lastFmMusicBrainzId(response, "Wrong Bach").isEmpty());
}

void ArtistImageProviderTest::acceptsAliasOnlyWhenMusicBrainzMatchIsUnique()
{
	QByteArray response = R"({"artists":[{"id":"id-1","score":100,"name":"Ryuichi Sakamoto","aliases":[{"name":"坂本龍一"}]}]})";
	QCOMPARE(ArtistImageProvider::uniqueMusicBrainzArtistId(response, "坂本龍一"), QString("id-1"));
}

void ArtistImageProviderTest::refusesAmbiguousMusicBrainzName()
{
	QByteArray response = R"({"artists":[{"id":"id-1","score":100,"name":"John Williams"},{"id":"id-2","score":100,"name":"John Williams"}]})";
	QVERIFY(ArtistImageProvider::uniqueMusicBrainzArtistId(response, "John Williams").isEmpty());
}

void ArtistImageProviderTest::acceptsMotorheadDiacriticVariant()
{
	QByteArray response = R"({"artists":[{"id":"motor-id","score":100,"name":"Motörhead"}]})";
	QCOMPARE(ArtistImageProvider::uniqueMusicBrainzArtistId(response, "Motorhead"), QString("motor-id"));
	QCOMPARE(ArtistImageProvider::uniqueMusicBrainzArtistId(response, QString::fromUtf8("Moto\xCC\x88rhead")), QString("motor-id"));

	QByteArray lastFm = R"(<lfm><artist><name>Motörhead</name><mbid>motor-id</mbid></artist></lfm>)";
	QCOMPARE(ArtistImageProvider::lastFmMusicBrainzId(lastFm, "Motorhead"), QString("motor-id"));
}

void ArtistImageProviderTest::prefersExactMusicBrainzMatchOverDiacriticVariant()
{
	QByteArray response = R"({"artists":[{"id":"accented-id","score":100,"name":"Motörhead"},{"id":"exact-id","score":100,"name":"Motorhead"}]})";
	QCOMPARE(ArtistImageProvider::uniqueMusicBrainzArtistId(response, "Motorhead"), QString("exact-id"));
}

void ArtistImageProviderTest::refusesAmbiguousDiacriticMusicBrainzName()
{
	QByteArray response = R"({"artists":[{"id":"motor-id-1","score":100,"name":"Motörhead"},{"id":"motor-id-2","score":100,"name":"Motōrhead"}]})";
	QVERIFY(ArtistImageProvider::uniqueMusicBrainzArtistId(response, "MOTORHEAD").isEmpty());
}

void ArtistImageProviderTest::verifiesMusicBrainzIdBeforeWikidataRelation()
{
	QByteArray response = R"({"id":"right-id","relations":[{"type":"wikidata","url":{"resource":"https://www.wikidata.org/wiki/Q255"}}]})";
	QCOMPARE(ArtistImageProvider::wikiDataId(response, "right-id"), QString("Q255"));
	QVERIFY(ArtistImageProvider::wikiDataId(response, "wrong-id").isEmpty());
}

void ArtistImageProviderTest::extractsCommonsP18AndDistinguishesRetryStates()
{
	QByteArray response = R"({"entities":{"Q255":{"claims":{"P18":[{"mainsnak":{"datavalue":{"value":"Beethoven.jpg"}}}]}}}})";
	QCOMPARE(ArtistImageProvider::commonsImageFileName(response, "Q255"), QString("Beethoven.jpg"));
	QCOMPARE(ArtistImageProvider::retryState(0, 1001, 1000), ArtistImageProvider::NoRetryFailure);
	QCOMPARE(ArtistImageProvider::retryState(1000, 1001, 1000), ArtistImageProvider::RetryDeferred);
	QCOMPARE(ArtistImageProvider::retryState(1000, 2000, 1000), ArtistImageProvider::RetryExpired);
}

void ArtistImageProviderTest::normalizesUnicodeNames()
{
	QCOMPARE(ArtistImageProvider::nameKey(QString::fromUtf8("Björk")), ArtistImageProvider::nameKey(QString::fromUtf8("Bjo\xCC\x88rk")));
	QCOMPARE(ArtistImageProvider::diacriticKey(QString::fromUtf8("Motörhead")), ArtistImageProvider::diacriticKey("Motorhead"));
	QVERIFY(ArtistImageProvider::diacriticKey(QString::fromUtf8("Søren")) != ArtistImageProvider::diacriticKey("Soren"));
}

void ArtistImageProviderTest::aggressiveNameKeyFolding()
{
	// Accents: Motorhead (plain) should match Motörhead (accented)
	QCOMPARE(ArtistImageProvider::nameKey("Motorhead"), ArtistImageProvider::nameKey("Motörhead"));

	// Numbers with spaces vs hyphens: Blink 182 should match blink-182
	QCOMPARE(ArtistImageProvider::nameKey("Blink 182"), ArtistImageProvider::nameKey("blink-182"));

	// Dollar sign: Ke$ha loses the dollar, giving "keha" != "kesha"
	// This is documented as acceptable behavior
	QString keha = ArtistImageProvider::nameKey("Ke$ha");
	QString kesha = ArtistImageProvider::nameKey("Kesha");
	QVERIFY(keha != kesha);  // They don't match, which is OK per the spec

	// Symbols only should fall back to caseFolded simplified
	QString symbolsOnly = ArtistImageProvider::nameKey("!!!");
	QVERIFY(!symbolsOnly.isEmpty());  // Should not be empty; falls back to simplified().caseFolded()
	QCOMPARE(symbolsOnly, QString("!!!").simplified().toCaseFolded());
}

void ArtistImageProviderTest::luceneQuotedEscaping()
{
	// Double quote should be escaped
	QCOMPARE(ArtistImageProvider::luceneQuoted("\"Weird Al\" Yankovic"),
	         QString("\\\"Weird Al\\\" Yankovic"));

	// Backslash should be escaped
	QCOMPARE(ArtistImageProvider::luceneQuoted("back\\slash"),
	         QString("back\\\\slash"));

	// Both quote and backslash
	QCOMPARE(ArtistImageProvider::luceneQuoted("a\\\"b"),
	         QString("a\\\\\\\"b"));

	// Normal text unchanged
	QCOMPARE(ArtistImageProvider::luceneQuoted("Normal Artist"),
	         QString("Normal Artist"));
}

QTEST_MAIN(ArtistImageProviderTest)
#include "artistimageprovider_test.moc"
