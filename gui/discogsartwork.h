/* Cantata: resolve Discogs artwork through a verified MusicBrainz artist link. */
#ifndef DISCOGS_ARTWORK_H
#define DISCOGS_ARTWORK_H

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace DiscogsArtwork {

inline bool discogsHost(const QUrl& url)
{
	return url.host() == QLatin1String("discogs.com") || url.host().endsWith(QLatin1String(".discogs.com"));
}

inline QString artistId(const QByteArray& musicBrainzResponse, const QString& expectedMusicBrainzId)
{
	const QVariantMap artist = QJsonDocument::fromJson(musicBrainzResponse).toVariant().toMap();
	if (expectedMusicBrainzId.isEmpty() || artist.value("id").toString() != expectedMusicBrainzId) {
		return QString();
	}
	QSet<QString> ids;
	for (const QVariant& value : artist.value("relations").toList()) {
		const QVariantMap relation = value.toMap();
		const QUrl url(relation.value("url").toMap().value("resource").toString());
		if (relation.value("type").toString() != QLatin1String("discogs") || !discogsHost(url)) {
			continue;
		}
		const QRegularExpressionMatch match = QRegularExpression("^/artist/([1-9][0-9]*)(?:-[^/]*)?/?$").match(url.path());
		if (match.hasMatch()) ids.insert(match.captured(1));
	}
	return ids.size() == 1 ? *ids.constBegin() : QString();
}

inline QStringList imageUrls(const QByteArray& response, const QString& expectedArtistId)
{
	const QVariantMap artist = QJsonDocument::fromJson(response).toVariant().toMap();
	if (expectedArtistId.isEmpty() || artist.value("id").toString() != expectedArtistId) {
		return QStringList();
	}
	QStringList urls;
	for (const QString& type : {QStringLiteral("primary"), QStringLiteral("secondary")}) {
		for (const QVariant& value : artist.value("images").toList()) {
			const QVariantMap image = value.toMap();
			if (image.value("type").toString() != type || image.value("width").toInt() < 32 || image.value("height").toInt() < 32) continue;
			for (const QString& field : {QStringLiteral("uri"), QStringLiteral("resource_url"), QStringLiteral("uri150")}) {
				const QUrl url(image.value(field).toString());
				if (url.isValid() && url.scheme() == QLatin1String("https") && discogsHost(url) && url.userInfo().isEmpty()) {
					const QString address = url.toString();
					if (!urls.contains(address)) urls.append(address);
					break;
				}
			}
			if (urls.size() >= 3) return urls;
		}
	}
	return urls;
}

}
#endif
