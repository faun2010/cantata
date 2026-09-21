/*
 * Cantata
 *
 * Copyright (c) 2011-2026 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef COMPOSER_DAY_H
#define COMPOSER_DAY_H

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>

// Network/GUI-free helpers behind the "Composer of the Day" list: the
// anniversary calendar, the famous-works LLM call, and the matching of a
// work against the local library. Kept free of Song/Qt-GUI types so they can
// be unit tested like SmartFilter and RecommendedRecordings.
namespace ComposerDay {

// Default number of works listed per composer.
static const int constWorksPerComposer = 10;

// One calendar entry - see playlists/composercalendar.json for the schema.
// "born"/"died" are ISO "YYYY-MM-DD"; "died" is empty for a living composer.
struct Composer {
	QString name;
	QString zh;
	QString born;
	QString died;
};

// A composer whose birth or death anniversary falls on the shown date.
struct Anniversary {
	QString name;
	QString zh;
	// True for a birthday, false for a death anniversary.
	bool birth = true;
	// The full ISO date of the birth/death, and how many years ago it was.
	QString date;
	int years = 0;
};

// Parses the anniversary calendar. Returns an empty list when the document
// is not a JSON object, or carries no "composers" array; entries without a
// name, or with neither a valid "born" nor "died" date, are dropped.
QList<Composer> parseCalendar(const QByteArray& json);

// The anniversaries falling on "date"'s month/day - February 29 birthdays
// (Rossini) are also reported on March 1 of a non-leap year, so they are not
// skipped three years out of four. Death anniversaries come first (the more
// commonly commemorated of the two); within each group the calendar's own
// order is kept, which puts the most famous composers first (see
// scripts/gen-composer-calendar.py). An anniversary in the future relative
// to "date" (a composer born on this day but after "date"'s year) is never
// reported.
QList<Anniversary> anniversariesFor(const QList<Composer>& composers, const QDate& date);

// A work named by the LLM (or derived from the library).
struct Work {
	QString title;
	// Catalogue number ("Op.47", "BWV 1046", ...), empty when unknown.
	QString catalogue;
	// The established Chinese title, when the LLM supplied one.
	QString zh;

	bool isEmpty() const { return title.isEmpty(); }
	// "Violin Concerto in D minor, Op.47" - title with the catalogue number
	// appended when the title does not already carry it.
	QString displayTitle() const;
};

// Builds the source text for the "composer-works-v1" prompt: the composer,
// how many works are wanted, and (when known) the anniversary being marked.
QString buildWorksSource(const QString& composer, int count = constWorksPerComposer);

// Extracts at most "max" works from a "composer-works-v1" response.
// Tolerates markdown code fences and surrounding prose; entries without a
// title, and duplicates (compared by normalised title/catalogue), are
// dropped. Returns an empty list when no usable JSON array is found.
QList<Work> parseWorks(const QString& response, int max = constWorksPerComposer);

// Plain-data view of a library track; the page fills these from Song so this
// helper does not depend on song.cpp.
struct Track {
	QString file;
	QString title;
	QString album;
	QString artist;
	QString albumArtist;
	QString composer;
	QString genre;
	int disc = 0;
	int track = 0;
	int year = 0;
	int secs = 0;
};

// Picks the single best recording of "work" among "tracks" (all of one
// composer, as returned by an MPD composer search) and returns its track
// indices in disc/track order. Album groups are scored by how well the
// album's derived work (see WorkInfo::deriveWork()) matches the work -
// a catalogue-number match beats a title match - and, within the chosen
// album, only the tracks belonging to the work are kept when the album
// covers several works. Returns an empty list when nothing matches.
QList<int> selectWorkTracks(const QList<Track>& tracks, const Work& work);

// The works the library itself has for this composer, most-recorded first -
// the fallback used when no LLM answer is available. Works are derived from
// the album groups' tags, deduplicated by normalised title.
QList<Work> worksFromLibrary(const QList<Track>& tracks, int max = constWorksPerComposer);

}// namespace ComposerDay

#endif
