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

#ifndef SMART_FILTER_H
#define SMART_FILTER_H

#include <QList>
#include <QString>

// Network/GUI-free helpers for the smart-playlist LLM filter, so they can be
// unit tested like RecommendedRecordings.
namespace SmartFilter {

static const int constMaxCandidates = 200;

// Plain-data view of a candidate track; SmartPlaylistsPage fills these from
// Song so this helper does not depend on song.cpp.
struct Candidate {
	QString title;
	QString artist;
	QString album;
	QString composer;
	QString genre;
	int year = 0;
	int secs = 0;
};

// Builds the source text sent to the LLM: the user's free-text description
// followed by a JSON array of candidates, each tagged with its index "i".
// At most maxCandidates entries are included; callers pre-sort and pre-trim.
QString buildSource(const QString& description, const QList<Candidate>& candidates, int maxCandidates = constMaxCandidates);

// Extracts the selected candidate indices from the LLM response, preserving
// the order in which they were listed - that order is the preferred listening
// order we asked for. Tolerates markdown code fences and surrounding prose;
// out-of-range, duplicate and non-numeric entries are dropped. *ok is set
// false when no usable JSON array with at least one valid index is found.
QList<int> parseSelection(const QString& response, int candidateCount, bool* ok);

}// namespace SmartFilter

#endif
