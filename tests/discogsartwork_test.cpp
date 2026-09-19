#include "gui/discogsartwork.h"
#include <QTest>

class DiscogsArtworkTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void resolvesOnlyVerifiedArtistLinks()
	{
		const QByteArray response = R"({"id":"mb-haydn","relations":[{"type":"discogs","url":{"resource":"https://www.discogs.com/artist/123-Joseph-Haydn"}}]})";
		QCOMPARE(DiscogsArtwork::artistId(response, "mb-haydn"), QString("123"));
		QVERIFY(DiscogsArtwork::artistId(response, "another-artist").isEmpty());
		QVERIFY(DiscogsArtwork::artistId(response, QString()).isEmpty());
	}
	void rejectsWrongDomainReleaseAndAmbiguousLinks()
	{
		const QByteArray invalid = R"({"id":"mb","relations":[{"type":"discogs","url":{"resource":"https://discogs.com.invalid/artist/123"}},{"type":"discogs","url":{"resource":"https://www.discogs.com/release/123"}}]})";
		const QByteArray ambiguous = R"({"id":"mb","relations":[{"type":"discogs","url":{"resource":"https://www.discogs.com/artist/123"}},{"type":"discogs","url":{"resource":"https://www.discogs.com/artist/456"}}]})";
		QVERIFY(DiscogsArtwork::artistId(invalid, "mb").isEmpty());
		QVERIFY(DiscogsArtwork::artistId(ambiguous, "mb").isEmpty());
	}
	void prioritizesPrimaryAndKeepsBackups()
	{
		const QByteArray response = R"({"id":123,"images":[{"type":"secondary","uri":"https://i.discogs.com/secondary.jpg","width":600,"height":600},{"type":"primary","uri":"https://i.discogs.com/primary.jpg","width":600,"height":600},{"type":"secondary","uri":"https://i.discogs.com/primary.jpg","width":600,"height":600}]})";
		QCOMPARE(DiscogsArtwork::imageUrls(response, "123"), QStringList({"https://i.discogs.com/primary.jpg", "https://i.discogs.com/secondary.jpg"}));
		QVERIFY(DiscogsArtwork::imageUrls(response, "456").isEmpty());
	}
	void rejectsInvalidImagesAndFallsBackToThumbnail()
	{
		const QByteArray response = R"({"id":123,"images":[{"type":"primary","uri":"https://i.discogs.com/tiny.jpg","width":16,"height":16},{"type":"primary","uri":"https://discogs.com.invalid/image.jpg","width":600,"height":600},{"type":"primary","uri":"http://i.discogs.com/image.jpg","width":600,"height":600},{"type":"secondary","uri150":"https://i.discogs.com/thumb.jpg","width":600,"height":600}]})";
		QCOMPARE(DiscogsArtwork::imageUrls(response, "123"), QStringList({"https://i.discogs.com/thumb.jpg"}));
		QVERIFY(DiscogsArtwork::imageUrls("bad-json", "123").isEmpty());
		const QByteArray noImages = R"({"id":123})";
		QVERIFY(DiscogsArtwork::imageUrls(noImages, "123").isEmpty());
	}
	void capsCandidates()
	{
		const QByteArray response = R"({"id":123,"images":[{"type":"primary","uri":"https://i.discogs.com/1.jpg","width":600,"height":600},{"type":"secondary","uri":"https://i.discogs.com/2.jpg","width":600,"height":600},{"type":"secondary","uri":"https://i.discogs.com/3.jpg","width":600,"height":600},{"type":"secondary","uri":"https://i.discogs.com/4.jpg","width":600,"height":600}]})";
		QCOMPARE(DiscogsArtwork::imageUrls(response, "123").size(), 3);
	}
};

QTEST_GUILESS_MAIN(DiscogsArtworkTest)
#include "discogsartwork_test.moc"
