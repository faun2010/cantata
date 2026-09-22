/* Cantata: reject the legacy Last.fm star placeholder, including resized copies. */
#ifndef ARTWORK_QUALITY_H
#define ARTWORK_QUALITY_H

#include <QImage>

namespace ArtworkQuality {

inline bool isStarPlaceholder(const QImage& image)
{
	if (image.isNull() || image.width() != image.height()) {
		return false;
	}
	// 16x16 luminance fingerprint of Last.fm's 2a96cbd8b46e442fc41c2b86b821562f.png.
	// Compare pixels instead of PNG bytes so resized/re-encoded cache entries match.
	static const unsigned char reference[] = {
		235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
		235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
		235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
		235,235,235,235,235,235,235,243,243,235,235,235,235,235,235,235,
		235,235,235,235,235,235,235,250,250,235,235,235,235,235,235,235,
		235,235,235,235,235,235,237,255,255,237,235,235,235,235,235,235,
		235,235,235,247,248,248,250,255,255,250,248,248,247,235,235,235,
		235,235,235,241,254,255,255,255,255,255,255,254,241,235,235,235,
		235,235,235,235,237,250,255,255,255,255,250,237,235,235,235,235,
		235,235,235,235,235,243,255,255,255,255,242,235,235,235,235,235,
		235,235,235,235,235,249,255,253,253,255,249,235,235,235,235,235,
		235,235,235,235,236,254,249,237,237,249,254,236,235,235,235,235,
		235,235,235,235,237,242,235,235,235,235,243,237,235,235,235,235,
		235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
		235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
		235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235
	};
	const QImage sample = image.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
	int difference = 0;
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			const QRgb pixel = sample.pixel(x, y);
			const int expected = reference[y * 16 + x];
			difference += qAbs(qRed(pixel) - expected) + qAbs(qGreen(pixel) - expected) + qAbs(qBlue(pixel) - expected);
		}
	}
	return difference <= 16 * 16 * 3 * 1.5;
}

// Only score identity-verified images. Treat portraits of at least 320px as sufficient for artist tiles;
// source quality breaks ties. Historical monochrome portraits remain valid.
inline int portraitScore(const QImage& image, int sourcePriority)
{
	if (image.isNull() || qMin(image.width(), image.height()) < 64 || isStarPlaceholder(image)) return -1;
	const int shortSide = qMin(image.width(), image.height());
	if (qMax(image.width(), image.height()) > shortSide * 3) return -1;
	const QImage sample = image.scaled(24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	int low = 255, high = 0;
	for (int y = 0; y < sample.height(); ++y) for (int x = 0; x < sample.width(); ++x) {
		const int value = qGray(sample.pixel(x, y));
		low = qMin(low, value); high = qMax(high, value);
	}
	if (high - low < 8) return -1;
	return (qMin(shortSide, 320) / 32) * 1000 + sourcePriority;
}

}
#endif
