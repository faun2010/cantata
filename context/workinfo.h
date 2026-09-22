/*
 * Cantata
 *
 * Copyright (c) 2026 Cantata Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef WORK_INFO_H
#define WORK_INFO_H

// Pure QtCore helpers used to derive a classical musical "work" from a
// song's composer/album/title tags, and to parse the Wikipedia/Wikidata
// JSON responses used to look up an introduction for that work. No GUI or
// network dependencies live here, so all of this can be unit tested with
// canned JSON - see tests/workinfo_test.cpp. The network requests
// themselves, and the resulting caching/translation, are handled by
// AlbumView.

#include <QByteArray>
#include <QString>

namespace WorkInfo {

// Returns an album title suitable for external metadata lookups. Real-world
// tags commonly append the recording performer and year, for example
// "Butterfly Lovers' Violin Concerto (Nishizaki - 1992)". The display/cache
// value remains unchanged; only the lookup query uses this cleaned title.
QString albumTitleForLookup(const QString& album);

// A classical work candidate derived from a song's composer/album/title
// tags.
struct Candidate {
	// True only when the song has a non-empty composer tag - i.e. it is
	// plausibly a classical work, rather than just a normal album. Every
	// other field is only meaningful when this is true.
	bool valid = false;
	QString composer;
	// The composer's surname, used both to build the search query and to
	// sanity check Wikipedia search results.
	QString surname;
	// The album tag with a trailing "(Performer - Year)" removed, e.g.
	// "Piano Concerto No.5, Op.73 'Emperor'" from
	// "Piano Concerto No.5, Op.73 'Emperor' (Serkin - 1981)".
	QString title;
	// Catalogue number found in "title" (Op./BWV/K./KV/Hob./D./RV/S./WoO),
	// e.g. "Op.73". Empty when none is found.
	QString catalogueNumber;
	// Performer/year parsed from the album's trailing "(Name - YYYY)" -
	// empty when the album has no such suffix.
	QString performer;
	QString year;
	// A work-type keyword (Concerto, Symphony, Sonata, ...) found in the
	// work title (falling back to the song's genre/title tags), used to
	// sanity check Wikipedia search results. May be empty.
	QString genreKeyword;
	// A search query suitable for the Wikipedia search API, e.g.
	// "Beethoven Piano Concerto No.5 Op.73".
	QString searchQuery;
};

// Derives a work candidate from a song's composer/artist/albumartist/album
// tags. When "composer" is empty, "albumArtist" (falling back to "artist")
// is used instead, but only when the song looks classical - see the .cpp for
// the exact heuristic. "songTitle" and "genre" are also consulted as a
// fallback source for genreKeyword when the album title itself contains no
// recognisable work-type word, and "songTitle" additionally picks the right
// work out of a multi-work album (e.g. a "Kaffee-Kantate BWV 211 -
// Bauern-Kantate BWV 212" collection).
Candidate deriveWork(const QString& composer, const QString& artist, const QString& albumArtist, const QString& album, const QString& songTitle = QString(), const QString& genre = QString());

// The composer's surname: the last whitespace-separated word, with basic
// handling for a trailing lowercase particle (van, von, de, ...) so that
// is never returned on its own.
QString composerSurname(const QString& composer);

// Picks the best result from a MediaWiki "action=query&list=search" response
// that plausibly matches "work". Lists and explicitly non-musical subjects
// are rejected. Without a detected work type, a weaker surname/snippet match
// also needs a musical-work definition or musical disambiguation suffix.
// A result's title must contain the detected
// work-type keyword (when one was found) - this alone rejects composer
// biography pages etc. Among the remaining candidates, a result whose title
// or (HTML) snippet contains the catalogue number is preferred over one that
// only matches via the composer surname, since Wikipedia often omits the
// catalogue number from the title itself (e.g. "Coffee Cantata" for Bach's
// BWV 211) but still mentions it in the snippet. Returns an empty string
// when nothing matches or the response cannot be parsed.
QString selectSearchResult(const QByteArray& searchResponseJson, const Candidate& work);

// Result of parsing a MediaWiki "action=query&prop=pageprops|langlinks"
// response for a single page.
struct SiteLinks {
	QString wikidataId;
	// Non-empty when a Chinese Wikipedia article is linked from the page.
	QString zhTitle;
};
SiteLinks parseSiteLinks(const QByteArray& pagePropsJson);

// Result of parsing a Wikipedia REST "page/summary/<title>" response.
struct Summary {
	QString extract;
	QString url;
};
Summary parseSummary(const QByteArray& summaryJson);

}// namespace WorkInfo

#endif
