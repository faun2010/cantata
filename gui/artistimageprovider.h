/*
 * Cantata
 *
 * Helpers for identity-verified artist artwork lookups.
 */

#ifndef ARTIST_IMAGE_PROVIDER_H
#define ARTIST_IMAGE_PROVIDER_H

#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <QXmlStreamReader>

namespace ArtistImageProvider {

enum RetryState {
	NoRetryFailure,
	RetryDeferred,
	RetryExpired
};

inline QString nameKey(const QString& name)
{
	return name.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
}

inline QString lastFmMusicBrainzId(const QByteArray& data, const QString& requestedArtist)
{
	QXmlStreamReader doc(data);
	doc.setNamespaceProcessing(false);
	while (!doc.atEnd()) {
		doc.readNext();
		if (!doc.isStartElement() || QLatin1String("artist") != doc.name()) {
			continue;
		}

		QString name;
		QString id;
		while (doc.readNextStartElement()) {
			if (QLatin1String("name") == doc.name()) {
				name = doc.readElementText();
			}
			else if (QLatin1String("mbid") == doc.name()) {
				id = doc.readElementText();
			}
			else {
				doc.skipCurrentElement();
			}
		}
		return nameKey(name) == nameKey(requestedArtist) ? id : QString();
	}
	return QString();
}

inline QString uniqueMusicBrainzArtistId(const QByteArray& data, const QString& requestedArtist)
{
	QJsonParseError error;
	QVariantMap response = QJsonDocument::fromJson(data, &error).toVariant().toMap();
	if (QJsonParseError::NoError != error.error) {
		return QString();
	}

	QSet<QString> ids;
	QString requested = nameKey(requestedArtist);
	for (const QVariant& value : response.value("artists").toList()) {
		QVariantMap artist = value.toMap();
		if (100 != artist.value("score").toInt()) {
			continue;
		}
		QStringList names;
		names << artist.value("name").toString();
		for (const QVariant& alias : artist.value("aliases").toList()) {
			names << alias.toMap().value("name").toString();
		}
		for (const QString& name : names) {
			if (nameKey(name) == requested) {
				QString id = artist.value("id").toString();
				if (!id.isEmpty()) {
					ids.insert(id);
				}
				break;
			}
		}
	}
	return 1 == ids.size() ? *ids.constBegin() : QString();
}

inline QString wikiDataId(const QByteArray& data, const QString& expectedMusicBrainzId)
{
	QJsonParseError error;
	QVariantMap response = QJsonDocument::fromJson(data, &error).toVariant().toMap();
	if (QJsonParseError::NoError != error.error || response.value("id").toString() != expectedMusicBrainzId) {
		return QString();
	}

	for (const QVariant& value : response.value("relations").toList()) {
		QVariantMap relation = value.toMap();
		QUrl resource(relation.value("url").toMap().value("resource").toString());
		QRegularExpressionMatch match = QRegularExpression("(?:^|/)Q([1-9][0-9]*)$").match(resource.path());
		if (QLatin1String("wikidata") == relation.value("type").toString() &&
			(resource.host() == QLatin1String("wikidata.org") || resource.host().endsWith(QLatin1String(".wikidata.org"))) && match.hasMatch()) {
			return QLatin1String("Q") + match.captured(1);
		}
	}
	return QString();
}

inline QString commonsImageFileName(const QByteArray& data, const QString& wikiDataId)
{
	QJsonParseError error;
	QVariantMap response = QJsonDocument::fromJson(data, &error).toVariant().toMap();
	if (QJsonParseError::NoError != error.error) {
		return QString();
	}
	QVariantMap entity = response.value("entities").toMap().value(wikiDataId).toMap();
	QVariantList images = entity.value("claims").toMap().value("P18").toList();
	return images.isEmpty() ? QString() : images.first().toMap().value("mainsnak").toMap().value("datavalue").toMap().value("value").toString();
}

inline RetryState retryState(qint64 failureTime, qint64 now, qint64 retryInterval)
{
	if (failureTime <= 0) {
		return NoRetryFailure;
	}
	return now - failureTime < retryInterval ? RetryDeferred : RetryExpired;
}

}

#endif
