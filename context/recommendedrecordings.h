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

#ifndef RECOMMENDED_RECORDINGS_H
#define RECOMMENDED_RECORDINGS_H

// Pure QtCore helpers for the "Recommended Recordings" section of the
// classical work introduction - see context/workinfo.h for the sibling
// "work introduction" helpers this builds on. No GUI or network
// dependencies live here, so all of this can be unit tested with canned
// data - see tests/recommendedrecordings_test.cpp. Reading the bundled/
// override dataset file, calling out to TranslationService for the AI
// fallback, and querying the library for matching albums are all handled by
// AlbumView.

#include <QList>
#include <QString>
#include <QStringList>

namespace RecommendedRecordings {

// A single recommended recording, either from the curated dataset or from
// the AI fallback (isAi true). "guide"/"edition"/"rating"/"source" are only
// ever populated for a dataset entry - the AI fallback must never display a
// rating or guide name.
struct Recording {
	QString soloist;
	QString conductor;
	QString ensemble;
	QString label;
	QString catalogue;
	QString year;
	// Dataset-only fields, always empty for an AI suggestion.
	QString guide;
	QString edition;
	QString rating;
	QString source;
	// Optional short guide review excerpt (English), dataset-only. May be
	// empty even for a dataset entry.
	QString comment;
	bool isAi = false;
};

// A single work entry in the curated dataset.
struct WorkEntry {
	QString composer;
	QString title;
	QString catalogue;
	QStringList aliases;
	QList<Recording> recordings;
};

// The curated "Recommended Recordings" dataset - either the bundled
// context/recommendedrecordings.json resource, or a user override file. See
// context/recommendedrecordings.json for the schema.
struct Dataset {
	int version = 0;
	QString generated;
	QList<WorkEntry> works;
};

// Parses a dataset JSON document. Returns an empty Dataset (no works) when
// the document is not valid JSON, or is not an object.
Dataset parseDataset(const QByteArray& json);

// Normalises a catalogue number for comparison: lower-cased, with everything
// but letters and digits removed, e.g. "Op. 73", "Op.73" and "op 73" all
// become "op73". Works the same way for BWV/K/KV/Hob/D/RV/S/WoO numbers,
// since those only ever differ from each other by punctuation/spacing too.
QString normaliseCatalogue(const QString& catalogue);

// Normalises a work title for comparison: lower-cased, diacritics stripped,
// "No. 5"/"No.5"/"Nr. 5"/"Nr 5" folded to "no5", and everything but letters
// and digits removed - so two titles that differ only in punctuation,
// spacing or a nickname's quoting still compare equal/contained.
QString normaliseTitle(const QString& title);

// Normalises a person's name (composer, soloist, conductor, ...) for
// case/diacritics-insensitive comparison: lower-cased, diacritics stripped,
// everything but letters removed.
QString normaliseName(const QString& name);

// True when two works (each described by its own catalogue number and
// title, with optional aliases for the second) are plausibly the same
// piece: either their normalised catalogue numbers are equal (and
// non-empty), or one normalised title/alias is contained in the other.
bool worksMatch(const QString& catalogueA, const QString& titleA, const QStringList& aliasesA, const QString& catalogueB, const QString& titleB, const QStringList& aliasesB);

// True when "performerOrArtist" (an album's performer, parsed from its
// trailing "(Performer - Year)", or its album artist) plausibly names the
// same person as "name" (a recording's soloist or conductor): compares
// name's surname, case/diacritics-insensitive, against performerOrArtist.
bool performerNameMatches(const QString& performerOrArtist, const QString& name);

// Finds the dataset work matching a song's derived composer/catalogue
// number/title (see WorkInfo::deriveWork()). The composer's surname must
// match (case/diacritics-insensitive); returns the index into
// dataset.works, or -1 when nothing matches.
int findMatchingWork(const Dataset& dataset, const QString& composer, const QString& catalogueNumber, const QString& title);

// Parses the AI fallback's response into at most 5 recordings, each marked
// isAi = true and with the dataset-only fields left empty. Robust to a
// ```json ... ``` fence and to extraneous text surrounding the JSON array;
// returns an empty list when no JSON array can be recovered. Entries with
// every field empty are dropped.
QList<Recording> parseAiRecordings(const QString& response);

// A stable key identifying a recording for cover art lookup/caching (see
// context/recordingcovers.h), built from its label, catalogue number and
// performers - the fields least likely to vary between mentions of the same
// physical release. Case/diacritics-insensitive and whitespace-normalised,
// so cosmetic differences do not change the key.
QString recordingCoverKey(const QString& performers, const QString& label, const QString& catalogue);

// " \xc2\xb7 " (U+00B7 MIDDLE DOT) and " \xe2\x80\x94 " (U+2014 EM DASH)
// padded with a single space either side - built from QChar code points
// rather than a QLatin1String literal containing raw UTF-8 bytes, which
// mangles multi-byte characters (each byte is read as one Latin-1
// character). Exposed here, rather than kept private to AlbumView, so their
// exact characters can be asserted on in a unit test.
QString middleDotSeparator();
QString emDashSeparator();

}// namespace RecommendedRecordings

#endif
