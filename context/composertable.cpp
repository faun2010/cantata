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

#include "composertable.h"
#include <QMap>
#include <QRegularExpression>
#include <QStringList>

namespace ComposerTable {

namespace {

QString stripDiacritics(const QString& text)
{
	const QString decomposed = text.normalized(QString::NormalizationForm_D);
	QString result;
	result.reserve(decomposed.size());
	for (const QChar& c : decomposed) {
		if (c.category() != QChar::Mark_NonSpacing) {
			result += c;
		}
	}
	return result;
}

// Folds a surname for lookup: lower-cased, diacritics stripped, everything
// but letters and hyphens removed (a hyphenated surname like
// "Saint-Saëns"/"Rimsky-Korsakov" keeps its hyphen, since dropping it risks
// colliding with an unrelated shorter surname).
QString foldSurname(const QString& text)
{
	const QString lowered = stripDiacritics(text).toLower();
	QString result;
	result.reserve(lowered.size());
	for (const QChar& c : lowered) {
		if (c.isLetter() || c == QLatin1Char('-')) {
			result += c;
		}
	}
	return result;
}

// Folds a given-name token/initial for lookup: upper-cased, diacritics and
// periods stripped ("C.P.E." -> "CPE", "Sebastian" -> "SEBASTIAN").
QString foldGivenToken(const QString& text)
{
	const QString stripped = stripDiacritics(text).toUpper();
	QString result;
	result.reserve(stripped.size());
	for (const QChar& c : stripped) {
		if (c.isLetter()) {
			result += c;
		}
	}
	return result;
}

// One composer sharing a (possibly ambiguous) surname with others in the
// table - e.g. the three "Bach" entries. "givenNameKeys" are the
// foldGivenToken()-folded given names/initials that identify this one among
// the others ("JS"/"SEBASTIAN" for Johann Sebastian Bach) - left empty for a
// composer whose surname is unique in the table. "isDefault" marks the
// entry used when the surname is given bare, with no given name/initials to
// disambiguate with (or none of them match any entry) - at most one per
// surname.
struct Entry {
	QString canonical;
	QStringList givenNameKeys;
	bool isDefault = false;
};

struct SurnameGroup {
	// One or more folded surname spellings that all mean the same group of
	// composer(s) - alternate transliterations, e.g. "haendel" alongside
	// "handel", that are not just a diacritics difference (those already
	// fold together via foldSurname()).
	QStringList surnameKeys;
	QList<Entry> entries;
};

// The ~150-composer table backing WorkInfo::deriveWork()'s known-composer
// checks - see composertable.h. Grouped by era/nationality purely for
// readability; lookup is always by (folded) surname.
const QList<SurnameGroup>& composerGroups()
{
	static const QList<SurnameGroup> groups = {
	    // --- Medieval/Renaissance --- (Gregorian chant has no composer)
	    {{QStringLiteral("dufay")}, {{QStringLiteral("Guillaume Du Fay"), {}, false}}},
	    {{QStringLiteral("palestrina")}, {{QStringLiteral("Giovanni Pierluigi da Palestrina"), {}, false}}},
	    {{QStringLiteral("byrd")}, {{QStringLiteral("William Byrd"), {}, false}}},
	    {{QStringLiteral("tallis")}, {{QStringLiteral("Thomas Tallis"), {}, false}}},
	    {{QStringLiteral("gesualdo")}, {{QStringLiteral("Carlo Gesualdo"), {}, false}}},

	    // --- Baroque ---
	    {{QStringLiteral("monteverdi")}, {{QStringLiteral("Claudio Monteverdi"), {}, false}}},
	    {{QStringLiteral("schutz")}, {{QString::fromUtf8("Heinrich Schütz"), {}, false}}},
	    {{QStringLiteral("lully")}, {{QStringLiteral("Jean-Baptiste Lully"), {}, false}}},
	    {{QStringLiteral("purcell")}, {{QStringLiteral("Henry Purcell"), {}, false}}},
	    {{QStringLiteral("corelli")}, {{QStringLiteral("Arcangelo Corelli"), {}, false}}},
	    {{QStringLiteral("pachelbel")}, {{QStringLiteral("Johann Pachelbel"), {}, false}}},
	    {{QStringLiteral("buxtehude")}, {{QStringLiteral("Dieterich Buxtehude"), {}, false}}},
	    {{QStringLiteral("lubeck")}, {{QString::fromUtf8("Vincent Lübeck"), {}, false}}},
	    {{QStringLiteral("bruhns")}, {{QStringLiteral("Nicolaus Bruhns"), {}, false}}},
	    {{QStringLiteral("scheidt")}, {{QStringLiteral("Samuel Scheidt"), {}, false}}},
	    {{QStringLiteral("couperin")}, {{QString::fromUtf8("François Couperin"), {}, false}}},
	    {{QStringLiteral("rameau")}, {{QStringLiteral("Jean-Philippe Rameau"), {}, false}}},
	    {{QStringLiteral("vivaldi")}, {{QStringLiteral("Antonio Vivaldi"), {}, false}}},
	    {{QStringLiteral("albinoni")}, {{QStringLiteral("Tomaso Albinoni"), {}, false}}},
	    {{QStringLiteral("marcello")},
	     {{QStringLiteral("Alessandro Marcello"), {QStringLiteral("ALESSANDRO"), QStringLiteral("A")}, false},
	      {QStringLiteral("Benedetto Marcello"), {QStringLiteral("BENEDETTO"), QStringLiteral("B")}, false}}},
	    {{QStringLiteral("scarlatti")},
	     {{QStringLiteral("Alessandro Scarlatti"), {QStringLiteral("ALESSANDRO"), QStringLiteral("A")}, false},
	      {QStringLiteral("Domenico Scarlatti"), {QStringLiteral("DOMENICO"), QStringLiteral("D")}, false}}},
	    {{QStringLiteral("geminiani")}, {{QStringLiteral("Francesco Geminiani"), {}, false}}},
	    {{QStringLiteral("tartini")}, {{QStringLiteral("Giuseppe Tartini"), {}, false}}},
	    {{QStringLiteral("avison")}, {{QStringLiteral("Charles Avison"), {}, false}}},
	    {{QStringLiteral("wassenaer")}, {{QStringLiteral("Unico Wilhelm van Wassenaer"), {}, false}}},
	    {{QStringLiteral("soler")}, {{QStringLiteral("Antonio Soler"), {}, false}}},
	    {{QStringLiteral("telemann")}, {{QString::fromUtf8("Georg Philipp Telemann"), {}, false}}},
	    {{QStringLiteral("handel"), QStringLiteral("haendel")}, {{QString::fromUtf8("George Frideric Handel"), {}, false}}},
	    {{QStringLiteral("bach")},
	     {{QStringLiteral("Johann Sebastian Bach"), {QStringLiteral("JS"), QStringLiteral("SEBASTIAN")}, true},
	      {QStringLiteral("Carl Philipp Emanuel Bach"), {QStringLiteral("CPE"), QStringLiteral("CARL"), QStringLiteral("PHILIPP"), QStringLiteral("EMANUEL")}, false},
	      {QStringLiteral("Johann Christian Bach"), {QStringLiteral("JC"), QStringLiteral("CHRISTIAN")}, false}}},
	    {{QStringLiteral("pergolesi")}, {{QStringLiteral("Giovanni Battista Pergolesi"), {}, false}}},
	    {{QStringLiteral("gluck")}, {{QStringLiteral("Christoph Willibald Gluck"), {}, false}}},

	    // --- Classical ---
	    {{QStringLiteral("haydn")}, {{QStringLiteral("Joseph Haydn"), {}, false}}},
	    {{QStringLiteral("mozart")},
	     {{QStringLiteral("Wolfgang Amadeus Mozart"), {QStringLiteral("WOLFGANG"), QStringLiteral("AMADEUS"), QStringLiteral("WA")}, true},
	      {QStringLiteral("Leopold Mozart"), {QStringLiteral("LEOPOLD")}, false}}},
	    {{QStringLiteral("beethoven")}, {{QStringLiteral("Ludwig van Beethoven"), {}, false}}},
	    {{QStringLiteral("schubert")}, {{QStringLiteral("Franz Schubert"), {}, false}}},

	    // --- Romantic ---
	    {{QStringLiteral("schumann")}, {{QStringLiteral("Robert Schumann"), {}, false}}},
	    {{QStringLiteral("mendelssohn")}, {{QStringLiteral("Felix Mendelssohn"), {}, false}}},
	    {{QStringLiteral("chopin")}, {{QString::fromUtf8("Frédéric Chopin"), {}, false}}},
	    {{QStringLiteral("liszt")}, {{QStringLiteral("Franz Liszt"), {}, false}}},
	    {{QStringLiteral("brahms")}, {{QStringLiteral("Johannes Brahms"), {}, false}}},
	    {{QStringLiteral("bruckner")}, {{QStringLiteral("Anton Bruckner"), {}, false}}},
	    {{QStringLiteral("mahler")}, {{QStringLiteral("Gustav Mahler"), {}, false}}},
	    {{QStringLiteral("dvorak")}, {{QString::fromUtf8("Antonín Dvořák"), {}, false}}},
	    {{QStringLiteral("smetana")}, {{QString::fromUtf8("Bedřich Smetana"), {}, false}}},
	    {{QStringLiteral("tchaikovsky"), QStringLiteral("tschaikowsky"), QStringLiteral("tschaikovsky"), QStringLiteral("chaikovsky"), QStringLiteral("cajkovskij")},
	     {{QStringLiteral("Pyotr Ilyich Tchaikovsky"), {}, false}}},
	    {{QStringLiteral("mussorgsky"), QStringLiteral("moussorgsky")}, {{QStringLiteral("Modest Mussorgsky"), {}, false}}},
	    {{QStringLiteral("rimsky-korsakov")}, {{QStringLiteral("Nikolai Rimsky-Korsakov"), {}, false}}},
	    {{QStringLiteral("rachmaninoff"), QStringLiteral("rachmaninov")}, {{QStringLiteral("Sergei Rachmaninoff"), {}, false}}},
	    {{QStringLiteral("prokofiev")}, {{QStringLiteral("Sergei Prokofiev"), {}, false}}},
	    {{QStringLiteral("shostakovich")}, {{QStringLiteral("Dmitri Shostakovich"), {}, false}}},
	    {{QStringLiteral("stravinsky")}, {{QStringLiteral("Igor Stravinsky"), {}, false}}},
	    {{QStringLiteral("debussy")}, {{QStringLiteral("Claude Debussy"), {}, false}}},
	    {{QStringLiteral("ravel")}, {{QStringLiteral("Maurice Ravel"), {}, false}}},
	    {{QStringLiteral("faure")}, {{QString::fromUtf8("Gabriel Fauré"), {}, false}}},
	    {{QStringLiteral("saint-saens")}, {{QString::fromUtf8("Camille Saint-Saëns"), {}, false}}},
	    {{QStringLiteral("bizet")}, {{QStringLiteral("Georges Bizet"), {}, false}}},
	    {{QStringLiteral("berlioz")}, {{QStringLiteral("Hector Berlioz"), {}, false}}},
	    {{QStringLiteral("franck")}, {{QStringLiteral("Cesar Franck"), {}, false}}},
	    {{QStringLiteral("poulenc")}, {{QStringLiteral("Francis Poulenc"), {}, false}}},
	    {{QStringLiteral("satie")}, {{QStringLiteral("Erik Satie"), {}, false}}},
	    {{QStringLiteral("elgar")}, {{QStringLiteral("Edward Elgar"), {}, false}}},
	    {{QStringLiteral("holst")}, {{QStringLiteral("Gustav Holst"), {}, false}}},
	    {{QStringLiteral("vaughanwilliams")}, {{QStringLiteral("Ralph Vaughan Williams"), {}, false}}},
	    {{QStringLiteral("britten")}, {{QStringLiteral("Benjamin Britten"), {}, false}}},
	    {{QStringLiteral("grieg")}, {{QStringLiteral("Edvard Grieg"), {}, false}}},
	    {{QStringLiteral("sibelius")}, {{QStringLiteral("Jean Sibelius"), {}, false}}},
	    {{QStringLiteral("nielsen")}, {{QStringLiteral("Carl Nielsen"), {}, false}}},
	    {{QStringLiteral("verdi")}, {{QStringLiteral("Giuseppe Verdi"), {}, false}}},
	    {{QStringLiteral("puccini")}, {{QStringLiteral("Giacomo Puccini"), {}, false}}},
	    {{QStringLiteral("rossini")}, {{QStringLiteral("Gioachino Rossini"), {}, false}}},
	    {{QStringLiteral("donizetti")}, {{QStringLiteral("Gaetano Donizetti"), {}, false}}},
	    {{QStringLiteral("bellini")}, {{QStringLiteral("Vincenzo Bellini"), {}, false}}},
	    {{QStringLiteral("wagner")}, {{QStringLiteral("Richard Wagner"), {}, false}}},
	    {{QStringLiteral("strauss")},
	     {{QStringLiteral("Richard Strauss"), {QStringLiteral("RICHARD"), QStringLiteral("R")}, false},
	      {QStringLiteral("Johann Strauss II"), {QStringLiteral("JOHANN"), QStringLiteral("J")}, false}}},
	    {{QStringLiteral("bartok")}, {{QString::fromUtf8("Béla Bartók"), {}, false}}},
	    {{QStringLiteral("kodaly")}, {{QString::fromUtf8("Zoltán Kodály"), {}, false}}},
	    {{QStringLiteral("janacek")}, {{QString::fromUtf8("Leoš Janáček"), {}, false}}},
	};
	return groups;
}

const QMap<QString, QList<Entry>>& composersBySurname()
{
	static const QMap<QString, QList<Entry>> map = [] {
		QMap<QString, QList<Entry>> m;
		for (const SurnameGroup& group : composerGroups()) {
			for (const QString& key : group.surnameKeys) {
				m.insert(key, group.entries);
			}
		}
		return m;
	}();
	return map;
}

// Picks the best entry for an (already surname-matched) group of one or
// more composers, given the folded given-name/initials tokens found
// alongside the surname - see composertable.h. Returns an empty string when
// the surname is ambiguous and neither a matching token nor a default
// entry settles it.
QString resolveGroup(const QList<Entry>& entries, const QStringList& givenTokens)
{
	if (entries.size() == 1) {
		return entries.first().canonical;
	}
	QString best;
	int bestScore = 0;
	QString fallbackDefault;
	for (const Entry& entry : entries) {
		if (entry.isDefault) {
			fallbackDefault = entry.canonical;
		}
		int score = 0;
		for (const QString& token : givenTokens) {
			if (entry.givenNameKeys.contains(token)) {
				++score;
			}
		}
		if (score > bestScore) {
			bestScore = score;
			best = entry.canonical;
		}
	}
	return bestScore > 0 ? best : fallbackDefault;
}

// Tries "surnameCandidate" (with the remaining "givenTokensRaw" as
// disambiguators) against the table. Returns an empty string when
// "surnameCandidate" does not fold to any known surname.
QString resolveSurnameAndGiven(const QString& surnameCandidate, const QStringList& givenTokensRaw)
{
	const QString key = foldSurname(surnameCandidate);
	if (key.isEmpty()) {
		return QString();
	}
	const auto it = composersBySurname().constFind(key);
	if (it == composersBySurname().constEnd()) {
		return QString();
	}
	QStringList givenTokens;
	for (const QString& token : givenTokensRaw) {
		const QString folded = foldGivenToken(token);
		if (!folded.isEmpty()) {
			givenTokens << folded;
		}
	}
	return resolveGroup(it.value(), givenTokens);
}

// Last resort for a single glued token with no separating space/comma at
// all, e.g. "J.S.Bach" or "TCHAIKOVSKY" (the latter is already handled by
// resolveSurnameAndGiven() trying the whole token as a bare surname first -
// this only fires once that has failed). Peels a known surname off either
// end of the token and treats what is left as initials; the leftover part
// is capped at 5 characters so an unrelated word that merely happens to end
// or start with a short surname (e.g. "Bach/Vivaldi" ending in "...vivaldi"
// with a long, clearly-not-initials remainder) is not misread as one.
QString resolveGluedToken(const QString& token)
{
	const QString folded = foldSurname(token);
	if (folded.size() < 4) {
		return QString();
	}
	for (auto it = composersBySurname().constBegin(); it != composersBySurname().constEnd(); ++it) {
		const QString& surnameKey = it.key();
		if (folded.size() <= surnameKey.size()) {
			continue;
		}
		if (folded.endsWith(surnameKey)) {
			const QString initials = folded.left(folded.size() - surnameKey.size());
			if (initials.size() <= 5) {
				const QString resolved = resolveGroup(it.value(), {initials.toUpper()});
				if (!resolved.isEmpty()) {
					return resolved;
				}
			}
		}
		if (folded.startsWith(surnameKey)) {
			const QString initials = folded.mid(surnameKey.size());
			if (initials.size() <= 5) {
				const QString resolved = resolveGroup(it.value(), {initials.toUpper()});
				if (!resolved.isEmpty()) {
					return resolved;
				}
			}
		}
	}
	return QString();
}

}// namespace

QString resolve(const QString& rawText)
{
	QString text = rawText.trimmed();
	if (text.isEmpty()) {
		return QString();
	}

	// Drop a trailing/embedded parenthesised remark - dates ("(1685-1750)")
	// or an instrument/role note ("(organ)").
	static const QRegularExpression parenRx(QStringLiteral("\\([^()]*\\)"));
	text.remove(parenRx);
	text = text.simplified();
	while (!text.isEmpty() && (text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char(',')))) {
		text.chop(1);
	}
	text = text.trimmed();
	if (text.isEmpty()) {
		return QString();
	}

	// "Surname, Given name(s)/initials" - e.g. "Bach, Johann Sebastian",
	// "Marcello, A".
	const int commaPos = text.indexOf(QLatin1Char(','));
	if (commaPos > 0) {
		const QString resolved = resolveSurnameAndGiven(text.left(commaPos), text.mid(commaPos + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts));
		if (!resolved.isEmpty()) {
			return resolved;
		}
	}

	static const QRegularExpression spaceRx(QStringLiteral("\\s+"));
	const QStringList tokens = text.split(spaceRx, Qt::SkipEmptyParts);
	if (tokens.isEmpty()) {
		return QString();
	}
	if (tokens.size() == 1) {
		const QString bare = resolveSurnameAndGiven(tokens.first(), {});
		return bare.isEmpty() ? resolveGluedToken(tokens.first()) : bare;
	}

	// Try "... Given name(s) Surname" (surname last - by far the most
	// common order) before "Surname Given name(s)/initials" (e.g.
	// "Bach J.S.", "Marcello A").
	const QString surnameLast = resolveSurnameAndGiven(tokens.last(), tokens.mid(0, tokens.size() - 1));
	if (!surnameLast.isEmpty()) {
		return surnameLast;
	}
	return resolveSurnameAndGiven(tokens.first(), tokens.mid(1));
}

}// namespace ComposerTable
