#ifndef ARTIST_LOOKUP_H
#define ARTIST_LOOKUP_H

#include "composertable.h"
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

inline QUrl composerImageUrl(const QJsonObject& response, const QString& requestedName)
{
	const QString expected = ComposerTable::biographyName(requestedName);
	const QString title = resolvedTitle(response);
	if (expected.isEmpty() || title.isEmpty() || ComposerTable::biographyName(title) != expected) return QUrl();
	const QJsonObject page = response.value(QStringLiteral("query")).toObject().value(QStringLiteral("pages")).toArray().first().toObject();
	const QUrl url(page.value(QStringLiteral("thumbnail")).toObject().value(QStringLiteral("source")).toString());
	return url.isValid() && url.scheme() == QLatin1String("https") && url.host() == QLatin1String("upload.wikimedia.org") ? url : QUrl();
}
}
#endif
