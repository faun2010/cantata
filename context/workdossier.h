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

#ifndef WORK_DOSSIER_H
#define WORK_DOSSIER_H

// Pure QtCore helpers for the "Work Introduction"/"Recommended Recordings"
// source dossier handed to the work-dossier-v1 LLM prompt (see
// TranslationService::startRequest()), and for parsing its structured JSON
// answer. No GUI or network dependencies live here, so all of this can be
// unit tested with canned data - see tests/workdossier_test.cpp. The
// network requests themselves, caching, and rendering are handled by
// AlbumView.

#include "recommendedrecordings.h"
#include <QList>
#include <QString>

namespace WorkDossier {

// Extracts the plain "extract" text of the (single) page returned by a
// MediaWiki "action=query&prop=extracts&explaintext=1&exsectionformat=wiki"
// response. Returns an empty string when the response cannot be parsed or
// carries no extract.
QString extractPlainText(const QByteArray& extractsJson);

// Filters a plain-text Wikipedia extract (as produced by extractPlainText(),
// with "exsectionformat=wiki" section headers such as "== Background ==" /
// "=== Movements ===") down to the lead paragraph(s) plus sections whose
// heading suggests it is relevant to a work introduction or its recording
// history (Background, History, Composition, Structure, Movements, Music,
// Premiere, Reception, Recordings, Discography, ...). Sections whose heading
// suggests boilerplate (References, External links, Notes, Further reading,
// See also, ...) are dropped, along with their subsections. The result is
// then capped to at most "capChars" characters (trimmed at a paragraph
// boundary where possible).
QString filterAndCapSections(const QString& plainText, int capChars = 12000);

// Assembles the plain-text dossier handed to the work-dossier-v1 prompt:
// the work's identity, the filtered English Wikipedia text, an optional
// Chinese Wikipedia summary hint, and the dataset's recording facts (never
// including the dataset's own rating text as a "why", per the prompt rules
// enforced in the caller). "datasetRecordings" is empty when the work has no
// dataset entry.
QString buildDossierText(const QString& composer, const QString& workTitle, const QString& catalogueNumber, const QString& englishText, const QString& zhSummaryHint, const QList<RecommendedRecordings::Recording>& datasetRecordings);

// One movement/description pair for Introduction::structure.
struct MovementInfo {
	QString movement;
	QString description;
};

// The "introduction" object of a work-dossier-v1 response.
struct Introduction {
	QString overview;
	QString background;
	QList<MovementInfo> structure;
	QString highlights;
	QString premiere;

	bool isEmpty() const { return overview.isEmpty() && background.isEmpty() && structure.isEmpty() && highlights.isEmpty() && premiere.isEmpty(); }
};

// One entry of the "recordings" array of a work-dossier-v1 response. "id"
// is the dataset index (as a string) it corresponds to, or empty for a
// recording named only in the source text (an "extra").
struct RecordingAnnotation {
	QString id;
	QString performers;
	QString label;
	QString catalogue;
	QString year;
	QString why;
};

struct Result {
	bool valid = false;
	Introduction introduction;
	QList<RecordingAnnotation> recordings;
};

// Parses a work-dossier-v1 response into a Result. Robust to a ```json ...
// ``` fence and to extraneous text surrounding the JSON object. Result.valid
// is false, and every other field left default-constructed, when no JSON
// object can be recovered or it carries neither an introduction nor
// recordings.
Result parseResponse(const QString& response);

}// namespace WorkDossier

#endif
