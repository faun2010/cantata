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

// Escape special characters in a Lucene query phrase:
// backslash and double quote must be escaped with a preceding backslash.
inline QString luceneQuoted(const QString& s)
{
	QString escaped;
	for (const QChar& c : s) {
		if (c == '\\' || c == '"') {
			escaped.append('\\');
		}
		escaped.append(c);
	}
	return escaped;
}

// Aggressively normalize a name for matching: decompose, drop combining marks,
// keep only letters and digits, then case-fold. Handles accents, punctuation,
// and symbols: "Motorhead" == "Motörhead", "Blink 182" == "blink-182",
// "Ke$ha" gives "keha" (not "kesha", which is acceptable).
inline QString nameKey(const QString& name)
{
	// Decompose to separate base characters from combining marks
	QString normalized = name.normalized(QString::NormalizationForm_KD);
	QString filtered;
	for (const QChar& c : normalized) {
		// Keep letters and digits, skip marks and punctuation/symbols/whitespace
		if (c.isLetter() || c.isDigit()) {
			filtered.append(c);
		}
	}
	// If the name contained only non-letter/digit characters, fall back to
	// case-folded simplified form to preserve comparison sensibility
	if (filtered.isEmpty()) {
		return name.simplified().toCaseFolded();
	}
	return filtered.toCaseFolded();
}

// Keep this deliberately narrower than compatibility matching: canonical
// decompositions make Motörhead and Motorhead equivalent, while characters
// without a canonical decomposition retain their distinct spelling.
inline QString diacriticKey(const QString& name)
{
	QString result;
	for (const QChar character : name.normalized(QString::NormalizationForm_D).simplified().toCaseFolded()) {
		QChar::Category category = character.category();
		if (category != QChar::Mark_NonSpacing && category != QChar::Mark_SpacingCombining && category != QChar::Mark_Enclosing) {
			result.append(character);
		}
	}
	return result;
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
		QString requested = nameKey(requestedArtist);
		return nameKey(name) == requested || diacriticKey(name) == diacriticKey(requestedArtist) ? id : QString();
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

	QSet<QString> exactIds;
	QSet<QString> compatibleIds;
	const QString requested = requestedArtist.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
	for (const QVariant& value : response.value("artists").toList()) {
		const QVariantMap artist = value.toMap();
		if (100 != artist.value("score").toInt()) continue;
		const QString id = artist.value("id").toString();
		if (id.isEmpty()) continue;
		QStringList names { artist.value("name").toString() };
		for (const QVariant& alias : artist.value("aliases").toList()) {
			names << alias.toMap().value("name").toString();
		}
		for (const QString& name : names) {
			// Prefer the actual spelling before applying compatibility folding.
			if (name.normalized(QString::NormalizationForm_C).simplified().toCaseFolded() == requested) {
				exactIds.insert(id);
			}
			if (nameKey(name) == nameKey(requestedArtist) || diacriticKey(name) == diacriticKey(requestedArtist)) {
				compatibleIds.insert(id);
			}
		}
	}
	const QSet<QString>& ids = exactIds.isEmpty() ? compatibleIds : exactIds;
	return ids.size() == 1 ? *ids.constBegin() : QString();
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
