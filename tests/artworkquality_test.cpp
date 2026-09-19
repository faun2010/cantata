#include "gui/artworkquality.h"
#include <QBuffer>
#include <QTest>

class ArtworkQualityTest : public QObject {
	Q_OBJECT
private Q_SLOTS:
	void rejectsDownloadedStarAndResizedCaches()
	{
		const QImage original(QFINDTESTDATA("fixtures/lastfm-star.png"));
		QVERIFY(!original.isNull());
		for (int size : {32, 64, 128, 256, 300, 600}) {
			const QImage scaled = original.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			QVERIFY2(ArtworkQuality::isStarPlaceholder(scaled), qPrintable(QString::number(size)));
			QByteArray data;
			QBuffer buffer(&data);
			QVERIFY(buffer.open(QIODevice::WriteOnly));
			QVERIFY(scaled.save(&buffer, "JPG", 85));
			QVERIFY(ArtworkQuality::isStarPlaceholder(QImage::fromData(data)));
		}
	}
	void doesNotRejectPlainOrColoredArtwork()
	{
		QVERIFY(!ArtworkQuality::isStarPlaceholder(QImage()));
		for (QRgb color : {qRgb(235, 235, 235), qRgb(255, 255, 255), qRgb(0, 0, 0), qRgb(235, 180, 100)}) {
			QImage image(300, 300, QImage::Format_RGB32);
			image.fill(color);
			QVERIFY(!ArtworkQuality::isStarPlaceholder(image));
		}
	}
};

QTEST_GUILESS_MAIN(ArtworkQualityTest)
#include "artworkquality_test.moc"
