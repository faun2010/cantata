#include "gui/artistimageprovider.h"

#include <QTest>

class ArtistImageProviderTest : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void acceptsOnlyExactLastFmArtist();
	void acceptsAliasOnlyWhenMusicBrainzMatchIsUnique();
	void refusesAmbiguousMusicBrainzName();
	void verifiesMusicBrainzIdBeforeWikidataRelation();
	void extractsCommonsP18AndDistinguishesRetryStates();
	void normalizesUnicodeNames();
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
}

QTEST_MAIN(ArtistImageProviderTest)
#include "artistimageprovider_test.moc"
