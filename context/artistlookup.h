#ifndef ARTIST_LOOKUP_H
#define ARTIST_LOOKUP_H

#include "composeridentities.h"
#include "composertable.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QUrl>

namespace ArtistLookup {
inline QString queryName(const QString& raw)
{
	const QString canonical = ComposerTable::biographyName(raw);
	return canonical.isEmpty() ? raw.trimmed() : canonical;
}

inline QString portraitName(const QString& raw)
{
	const QString name = queryName(raw);
	if (name == raw.trimmed() && !name.contains(QLatin1Char(' '))) {
		const QString composer = ComposerTable::resolve(name);
		if (!composer.isEmpty()) return composer;
	}
	return name;
}

inline QString wikipediaName(const QString& raw)
{
	const auto person = ComposerIdentities::lookup(raw);
	const QString title = person.value("wikipedia").toObject().value("en").toString();
	return title.isEmpty() ? portraitName(raw) : title;
}

// Prefer curated authority IDs over any name-only service result.
inline QString musicBrainzId(const QString& raw)
{
	bool conflict = false;
	const auto person = ComposerIdentities::lookup(raw, &conflict);
	if (conflict) return {};
	const QString id = person.value("musicbrainz").toString();
	static const QRegularExpression uuid(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
	if (uuid.match(id).hasMatch()) return id.toLower();
	if (!person.isEmpty()) return {};
	// British composer/flautist, not either of the same-name bass players.
	// https://musicbrainz.org/artist/e252e2e9-5cca-4bb6-a787-f9236d3a91e0
	// https://www.discogs.com/artist/202611 (David C. Heath, born 1956)
	const QString key = ComposerIdentities::nameKey(raw);
	if (key == QLatin1String("daveheath") || key == QLatin1String("davidcheath"))
		return QStringLiteral("e252e2e9-5cca-4bb6-a787-f9236d3a91e0");
	return {};
}

// Historical disk keys, also used to detect identity changes during a download.
// Portraits now persist under the artist name; keep these for cache migration.
inline QString legacyImageCacheToken(const QString& raw)
{
	bool conflict = false;
	const auto person = ComposerIdentities::lookup(raw, &conflict);
	const QByteArray identity = raw.toUtf8() + '\n' + queryName(raw).toUtf8() + '\n'
	    + musicBrainzId(raw).toUtf8() + '\n' + QJsonDocument(person).toJson(QJsonDocument::Compact)
	    + (conflict ? "conflict" : "");
	return QStringLiteral("artist-identity-v4-") + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

inline QString imageCacheToken(const QString& raw)
{
	bool conflict = false;
	const auto person = ComposerIdentities::lookup(raw, &conflict);
	// Translation, source annotations, and alias ordering do not change the
	// resolved person. Include only fields that identify or locate their image.
	const QJsonObject identity{{"raw", raw}, {"query", queryName(raw)},
	    {"musicbrainz", musicBrainzId(raw)}, {"canonical", person.value("canonical")},
	    {"imslp", person.value("imslp")}, {"wikipedia", person.value("wikipedia")},
	    {"conflict", conflict}};
	return QStringLiteral("artist-identity-v4-") + QString::fromLatin1(QCryptographicHash::hash(
	    QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

inline bool isTagCorrection(const QString& html)
{
	return html.contains(QStringLiteral("This is mistagged"), Qt::CaseInsensitive)
	    || html.contains(QStringLiteral("incorrect tag for"), Qt::CaseInsensitive)
	    || html.contains(QStringLiteral("Please correct your tags"), Qt::CaseInsensitive)
	    || html.contains(QStringLiteral("it would help Last.fm if you could correct"), Qt::CaseInsensitive);
}

// Only direct MediaWiki title/redirect resolution is accepted here. A search
// result sharing a surname is not proof of identity.
inline QString resolvedTitle(const QJsonObject& response)
{
	const QJsonArray pages = response.value(QStringLiteral("query")).toObject().value(QStringLiteral("pages")).toArray();
	if (pages.size() != 1) return QString();
	const QJsonObject page = pages.first().toObject();
	if (page.contains(QStringLiteral("missing")) || page.contains(QStringLiteral("invalid"))
	    || page.value(QStringLiteral("ns")).toInt(-1) != 0
	    || page.value(QStringLiteral("pageprops")).toObject().contains(QStringLiteral("disambiguation"))) return QString();
	return page.value(QStringLiteral("title")).toString();
}

// Validate structured page metadata, never incidental mentions in the article
// body (a photographer may have photographed a musician).
inline bool musicalPage(const QJsonObject& response, bool biography)
{
	if (resolvedTitle(response).isEmpty()) return false;
	const auto page = response.value(QStringLiteral("query")).toObject().value(QStringLiteral("pages")).toArray().first().toObject();
	QString metadata = page.value(QStringLiteral("pageprops")).toObject().value(QStringLiteral("wikibase-shortdesc")).toString();
	for (const auto& category : page.value(QStringLiteral("categories")).toArray()) {
		metadata += QLatin1Char('\n') + category.toObject().value(QStringLiteral("title")).toString();
	}
	static const QRegularExpression roles(QStringLiteral(
	    "\\b(composers?|musicians?|pianists?|violinists?|cellists?|conductors?|singers?|vocalists?|songwriters?|rappers?|guitarists?|bassists?|drummers?|flautists?|flutists?|saxophonists?|organists?|orchestras?|choirs?|musical groups?|rock bands?|pop bands?|jazz bands?|musical duos?|record producers?|Komponist|Flötist)\\b|作曲家|音乐家|音樂家|歌手|乐团|樂團|指挥家|指揮家"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression works(QStringLiteral(
	    "\\b(albums?|songs?|symphon(y|ies)|concertos?|sonatas?|cantatas?|operas?|ballets?|overtures?|musical compositions?|compositions by|soundtracks?)\\b|\\bsuites? \\(music\\)|专辑|專輯|歌曲|交响曲|交響曲|协奏曲|協奏曲|奏鸣曲|奏鳴曲|歌剧|歌劇|乐曲|樂曲"), QRegularExpression::CaseInsensitiveOption);
	return (biography ? roles : works).match(metadata).hasMatch();
}

inline QString biographyTitle(const QJsonObject& response, const QString& requestedName)
{
	const QString title = resolvedTitle(response);
	if (title.isEmpty()) return QString();
	const QString expected = ComposerTable::biographyName(requestedName);
	const QString actual = ComposerTable::biographyName(title);
	if (!expected.isEmpty() && expected == actual) return title;
	// An exact title/redirect alone can lead to a namesake journalist or
	// politician. Unknown names need musical identity evidence; otherwise
	// let the existing role-qualified search and work hints disambiguate.
	return musicalPage(response, true) ? title : QString();
}

// A biography thumbnail may depict a score, building or other work. Resolve
// the verified person first; only their Wikidata image statements are candidates.
inline QString portraitEntityId(const QJsonObject& response, const QString& requestedName)
{
	QString title = resolvedTitle(response);
	if (title.isEmpty() || !musicalPage(response, true)) return {};
	title.remove(QRegularExpression(QStringLiteral("\\s*\\((?:composer|musician|pianist|conductor|singer)\\)$"), QRegularExpression::CaseInsensitiveOption));
	const QString requested = portraitName(requestedName);
	bool redirected = false;
	for (const auto& value : response.value("query").toObject().value("redirects").toArray()) {
		const auto redirect = value.toObject();
		if (ComposerIdentities::nameKey(redirect.value("from").toString()) == ComposerIdentities::nameKey(requested)
		    && redirect.value("to").toString() == resolvedTitle(response)) redirected = true;
	}
	if (!redirected && ComposerIdentities::nameKey(title) != ComposerIdentities::nameKey(requested)
	    && (ComposerTable::biographyName(title).isEmpty() || ComposerTable::biographyName(title) != ComposerTable::biographyName(requested))) return {};
	const auto page = response.value("query").toObject().value("pages").toArray().first().toObject();
	const QString id = page.value("pageprops").toObject().value("wikibase_item").toString();
	static const QRegularExpression entityId(QStringLiteral("\\AQ[1-9][0-9]*\\z"));
	return entityId.match(id).hasMatch() ? id : QString();
}
}
#endif
