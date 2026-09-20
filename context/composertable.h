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

#ifndef COMPOSER_TABLE_H
#define COMPOSER_TABLE_H

// A small built-in table of ~150 widely-recorded classical composers (see
// composertable.cpp), used by WorkInfo::deriveWork() so a composer is only
// ever guessed from a raw artist/albumartist/album tag when that text
// actually names one of them - never a performer, conductor or ensemble.
// Pure QtCore, no GUI/network dependencies - see
// tests/composertable_test.cpp.

#include <QString>

namespace ComposerTable {

// Resolves "text" (a composer tag, artist/albumartist tag, or a candidate
// name extracted from an album title) to a known composer's canonical full
// name, or an empty string when it does not name one of them. Handles the
// messy real-world spellings tag data comes in:
//  - a canonical or near-canonical full name ("Frédéric Chopin", "Ludwig
//    van Beethoven", accents optional);
//  - "Surname, Given name(s)" ("Bach, Johann Sebastian (1685-1750)" - a
//    trailing parenthesised remark, e.g. dates or an instrument, is
//    ignored);
//  - "Given name(s)/initials Surname" or "Surname initials" in either
//    order ("J.S.Bach", "JS Bach", "Bach J.S.", "Bach, A");
//  - a bare surname, all-caps or not ("TCHAIKOVSKY", "Bach", "Handel");
//  - initials glued directly to the surname with no space at all
//    ("J.S.Bach").
// When a surname is shared by more than one composer in the table (Bach,
// Mozart, Marcello, Scarlatti, Strauss, ...), the given name/initials
// (when present) disambiguate which one; a bare, unqualified surname only
// resolves when either the surname is unique in the table or it has an
// explicit default (bare "Bach" -> Johann Sebastian Bach, bare "Mozart" ->
// Wolfgang Amadeus Mozart) - otherwise it is rejected (empty result) rather
// than guessed.
QString resolve(const QString& text);

// Conservative identity lookup for biographies: full known names, explicit
// transliteration aliases and initials only. Unlike resolve(), never infer a
// person from just a matching surname (e.g. the Borodin Quartet).
QString biographyName(const QString& text);

}// namespace ComposerTable

#endif
