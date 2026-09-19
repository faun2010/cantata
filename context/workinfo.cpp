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

#include "workinfo.h"
#include "composertable.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace WorkInfo {

namespace {

// Longer/more specific phrases are listed first so they are preferred over
// a shorter word they happen to contain (e.g. "Symphonic Poem" over
// "Symphony"). These are matched with a \b word-boundary regex - see
// findGenreKeyword() - which is fine for English text but does not reach
// into a German compound word (see workTypeVariants() for those).
const QStringList& workTypeKeywords()
{
	static const QStringList keywords = {
	    QStringLiteral("Concerto Grosso"), QStringLiteral("Symphonic Poem"),
	    QStringLiteral("Piano Trio"), QStringLiteral("String Quartet"), QStringLiteral("String Quintet"),
	    QStringLiteral("Concerto"), QStringLiteral("Symphony"), QStringLiteral("Sonata"),
	    QStringLiteral("Quartet"), QStringLiteral("Quintet"), QStringLiteral("Trio"),
	    QStringLiteral("Overture"), QStringLiteral("Suite"), QStringLiteral("Nocturne"),
	    QStringLiteral("Prelude"), QStringLiteral("Etudes"), QStringLiteral("Etude"),
	    QStringLiteral("Mass"), QStringLiteral("Requiem"), QStringLiteral("Opera"),
	    QStringLiteral("Cantata"), QStringLiteral("Fugue"), QStringLiteral("Variations"),
	    QStringLiteral("Rhapsody"), QStringLiteral("Ballade"), QStringLiteral("Serenade"),
	    QStringLiteral("Partita"), QStringLiteral("Toccata"), QStringLiteral("Impromptu"),
	    QStringLiteral("Waltz"), QStringLiteral("Mazurka"), QStringLiteral("Polonaise"),
	    QStringLiteral("Oratorio"), QStringLiteral("Ballet"), QStringLiteral("Fantasia"),
	    QStringLiteral("Passion"), QStringLiteral("Song"), QStringLiteral("Motet")};
	return keywords;
}

// German/French/Italian work-type words, mapped to the English keyword used
// to sanity check Wikipedia results (which are always in English). Unlike
// workTypeKeywords() these are matched as plain substrings (see
// findGenreKeyword()), since German in particular builds compounds with no
// word boundary ("Kaffeekantate", "Klavierkonzert"). More specific compounds
// are listed before the generic word they contain (e.g. "Streichquartett"
// before "Quartett") so the more specific keyword wins.
const QList<QPair<QString, QString>>& workTypeVariants()
{
	static const QList<QPair<QString, QString>> variants = {
	    {QStringLiteral("Streichquartett"), QStringLiteral("String Quartet")},
	    {QStringLiteral("Klavierkonzert"), QStringLiteral("Concerto")},
	    {QStringLiteral("Violinkonzert"), QStringLiteral("Concerto")},
	    {QStringLiteral("Konzert"), QStringLiteral("Concerto")},
	    {QStringLiteral("Sinfonie"), QStringLiteral("Symphony")},// German
	    {QStringLiteral("Symphonie"), QStringLiteral("Symphony")},// German/French
	    {QStringLiteral("Sinfonia"), QStringLiteral("Symphony")},// Italian
	    {QStringLiteral("Sonate"), QStringLiteral("Sonata")},// German/French
	    {QStringLiteral("Sonata"), QStringLiteral("Sonata")},// Italian (also matched by workTypeKeywords())
	    {QStringLiteral("Messe"), QStringLiteral("Mass")},// German/French
	    {QStringLiteral("Messa"), QStringLiteral("Mass")},// Italian
	    {QStringLiteral("Oratorium"), QStringLiteral("Oratorio")},// German
	    {QStringLiteral("Ouverture"), QStringLiteral("Overture")},// German (without umlaut)
	    {QString::fromUtf8("Ouvertüre"), QStringLiteral("Overture")},// German "Ouvertüre"
	    {QStringLiteral("Quintett"), QStringLiteral("Quintet")},// German
	    {QStringLiteral("Quartett"), QStringLiteral("Quartet")},// German - after Streichquartett
	    {QStringLiteral("Lieder"), QStringLiteral("Song")},// German
	    {QStringLiteral("Lied"), QStringLiteral("Song")},// German
	    {QStringLiteral("Motette"), QStringLiteral("Motet")},// German
	    {QStringLiteral("Cantate"), QStringLiteral("Cantata")},// French
	    {QStringLiteral("Kantate"), QStringLiteral("Cantata")}// German
	};
	return variants;
}

QString findGenreKeyword(const QString& text)
{
	if (text.isEmpty()) {
		return QString();
	}
	for (const QString& keyword : workTypeKeywords()) {
		const QRegularExpression rx(QLatin1String("\\b") + QRegularExpression::escape(keyword) + QLatin1String("\\b"), QRegularExpression::CaseInsensitiveOption);
		if (text.contains(rx)) {
			return keyword;
		}
	}
	for (const auto& variant : workTypeVariants()) {
		if (text.contains(variant.first, Qt::CaseInsensitive)) {
			return variant.second;
		}
	}
	return QString();
}

// An optional single trailing letter after a catalogue number's digits, e.g.
// the "A" of a hypothetical "BWV 1046A" sub-movement - but never when that
// letter is itself just the start of a longer word glued directly onto the
// catalogue number with no separating space at all, as happens in real tag
// data (e.g. "BWV 225-229Regensburger Domspatzen..." - see
// stripGluedTrailingPerformer()). The negative lookahead is what tells the
// two apart: a genuine suffix letter is not itself followed by another
// letter.
#define WORKINFO_OPTLETTER "(?:[A-Za-z](?![A-Za-z]))?"

// Common numeric range/list suffix shared by most catalogue systems below,
// e.g. the "-229" of "BWV 225-229", the ", 1030-1032" of "BWV 1020,
// 1030-1032" or the ",1069" of "BWV 1066,1069". Deliberately only matches a
// following comma/hyphen immediately followed by a digit, so it never eats
// into a " - Next Work" work-segment delimiter (which always has a space
// before the following letter).
#define WORKINFO_NUMLIST "(?:\\s?[,-]\\s?\\d+" WORKINFO_OPTLETTER ")*"

const QRegularExpression& catalogueNumberRx()
{
	static const QRegularExpression rx(
	    QStringLiteral("\\b(?:Op\\.?\\s?\\d+" WORKINFO_OPTLETTER "(?:,?\\s?No\\.?\\s?\\d+)?"
	                   "|BWV\\s?\\d+" WORKINFO_OPTLETTER WORKINFO_NUMLIST
	                   "|HWV\\s?\\d+" WORKINFO_OPTLETTER WORKINFO_NUMLIST
	                   "|BuxWV\\s?\\d+" WORKINFO_OPTLETTER WORKINFO_NUMLIST
	                   "|TWV\\s?\\d+" WORKINFO_OPTLETTER WORKINFO_NUMLIST
	                   "|SWV\\s?\\d+" WORKINFO_OPTLETTER WORKINFO_NUMLIST
	                   "|KV\\.?\\s?\\d+" WORKINFO_NUMLIST
	                   "|K\\.\\s?\\d+" WORKINFO_NUMLIST
	                   "|Hob\\.?\\s?[IVXLCDM]+\\s?:\\s?\\d+" WORKINFO_OPTLETTER
	                   "|D\\.\\s?\\d+" WORKINFO_NUMLIST
	                   "|RV\\s?\\d+" WORKINFO_NUMLIST
	                   "|S\\.\\s?\\d+" WORKINFO_NUMLIST
	                   "|WoO\\s?\\d+" WORKINFO_OPTLETTER WORKINFO_NUMLIST ")\\b"));
	return rx;
}

#undef WORKINFO_NUMLIST
#undef WORKINFO_OPTLETTER

// Chopin's works are also often catalogued with the Kobylańska ("KK") or
// Brown ("B.") index rather than (or in addition to) an opus number, e.g.
// "B.108". Only tried for Chopin - "B." alone is far too generic a pattern
// to risk elsewhere.
const QRegularExpression& chopinBrownIndexRx()
{
	static const QRegularExpression rx(QStringLiteral("\\bB\\.?\\s?\\d+[A-Za-z]?\\b"));
	return rx;
}

// The composer implied by a catalogue system, keyed by the system's leading
// letters (e.g. "BWV" -> "Bach"). Deliberately does not include the very
// common "Op." (used by almost every composer) or "Hob."/"S." systems that
// were not asked for.
const QString& impliedComposerForSystem(const QString& system)
{
	static const QMap<QString, QString> composers = {
	    {QStringLiteral("BWV"), QStringLiteral("Johann Sebastian Bach")},
	    {QStringLiteral("HWV"), QString::fromUtf8("George Frideric Handel")},
	    {QStringLiteral("K"), QStringLiteral("Wolfgang Amadeus Mozart")},
	    {QStringLiteral("KV"), QStringLiteral("Wolfgang Amadeus Mozart")},
	    {QStringLiteral("Hob"), QStringLiteral("Joseph Haydn")},
	    {QStringLiteral("D"), QStringLiteral("Franz Schubert")},
	    {QStringLiteral("RV"), QStringLiteral("Antonio Vivaldi")},
	    {QStringLiteral("TWV"), QString::fromUtf8("Georg Philipp Telemann")},
	    {QStringLiteral("BuxWV"), QStringLiteral("Dieterich Buxtehude")},
	    {QStringLiteral("SWV"), QString::fromUtf8("Heinrich Schütz")},
	    {QStringLiteral("WoO"), QStringLiteral("Ludwig van Beethoven")}};
	static const QString empty;
	auto it = composers.constFind(system);
	return it == composers.constEnd() ? empty : it.value();
}

// The leading letters of a catalogue number, e.g. "BWV" from "BWV 211" or
// "Hob" from "Hob. XVI:52".
QString catalogueSystem(const QString& catalogueNumber)
{
	static const QRegularExpression rx(QStringLiteral("^([A-Za-z]+)"));
	const QRegularExpressionMatch m = rx.match(catalogueNumber);
	return m.hasMatch() ? m.captured(1) : QString();
}

// Matches an album's trailing "(Performer - Year)" decoration, e.g.
// " (Serkin - 1981)".
const QRegularExpression& trailingPerformerYearRx()
{
	static const QRegularExpression rx(QStringLiteral("^(.*?)\\s*\\(([^()]+?)\\s*-\\s*(\\d{4})\\)\\s*$"));
	return rx;
}

// A trailing parenthesised remark with no year inside, e.g.
// " (Aurele Nicolet...)" - a performer credit once the catalogue-number
// check in stripTrailingDecorations() rules out it being part of the work
// title itself (e.g. "(BWV 1007)").
const QRegularExpression& trailingParenRx()
{
	static const QRegularExpression rx(QStringLiteral("^(.*?)\\s*\\(([^()]+)\\)\\s*$"));
	return rx;
}

// A trailing bare "- Name" performer credit with no parentheses, e.g.
// " - Trevor Pinnock" or " - Karl Richter". The name is 1-4
// capitalised words, so this never fires on a work-segment delimiter like
// " - Bauern-Kantate BWV 212" (which fails to match because "BWV"/"212" are
// not both capitalised-word-shaped, or is separately guarded against by the
// catalogue-number check in the caller).
const QRegularExpression& trailingDashNameRx()
{
	static const QRegularExpression rx(QStringLiteral("^(.*\\S)\\s-\\s([\\p{Lu}][\\p{L}.'-]*(?:\\s[\\p{Lu}][\\p{L}.'-]*){0,3})$"));
	return rx;
}

// A leading "Name - " or "Name: " decoration, e.g. "Karl Richter -
// Brandenburg Concertos..." (a performer prefix) or "Handel: Messiah" /
// "Bach - English Suites" (a composer prefix. Which one it is is decided by
// the caller, by comparing "name" against the already-resolved composer's
// surname.
const QRegularExpression& leadingNameRx()
{
	static const QRegularExpression rx(QStringLiteral("^([\\p{Lu}][\\p{L}.'-]*(?:\\s[\\p{Lu}][\\p{L}.'-]*){0,3})\\s*(?:-\\s|:\\s*)(.+)$"));
	return rx;
}

// A leading "Name[ Name...]" candidate followed by a colon, a " - " dash, or
// just a bare space - e.g. "Couperin: Nouveaux Concerts", "Alessandro
// Scarlatti - Il Giardino d'Amore", "C.P.E. Bach - 2 Conciertos...",
// "TCHAIKOVSKY 1812 Overture", "Bach 'Dorienne' Toccata...". Deliberately
// permissive (a bare space is accepted as a delimiter too) - see
// extractLeadingComposerPrefix(), which is what actually gates acceptance
// on the candidate resolving to a known composer.
const QRegularExpression& leadingComposerNameRx()
{
	static const QRegularExpression rx(QStringLiteral("^([\\p{Lu}][\\p{L}.'-]*(?:\\s[\\p{Lu}][\\p{L}.'-]*){0,3})(?:\\s*:\\s*|\\s+-\\s+|\\s+)(.+)$"));
	return rx;
}

// A leading "Surname, Initial(s):"/"Surname, Initial(s) -" candidate, e.g.
// "Marcello, A: La Cetra Concertos Nos. 1-6".
const QRegularExpression& leadingComposerSurnameInitialsRx()
{
	static const QRegularExpression rx(QStringLiteral("^([\\p{Lu}][\\p{L}'-]*)\\s*,\\s*([\\p{Lu}](?:\\.?[\\p{Lu}])*)\\.?\\s*(?::\\s*|-\\s+)(.+)$"));
	return rx;
}

// A leading track-count number glued onto the front of an album tag, e.g.
// the "63 " of "63 C.P.E. Bach - 2 Conciertos para flauta - Stephen
// Preston".
const QRegularExpression& leadingTrackNumberRx()
{
	static const QRegularExpression rx(QStringLiteral("^\\d+\\s+"));
	return rx;
}

// Trailing "(CD1)"/"(Disc 2)"/", Disc 1"/" CD1" disc-number decorations.
const QRegularExpression& discSuffixRx()
{
	static const QRegularExpression rx(
	    QStringLiteral("(?:\\s*\\(\\s*(?:CD|Disc)\\.?\\s*\\d+\\s*\\)|\\s*,\\s*Disc\\.?\\s*\\d+|\\s+CD\\.?\\s?\\d+)\\s*$"),
	    QRegularExpression::CaseInsensitiveOption);
	return rx;
}

// Trailing audio-format decorations, e.g. "(2CH-DST)"/"(2CH-DSD)".
const QRegularExpression& formatTagRx()
{
	static const QRegularExpression rx(QStringLiteral("\\s*\\(\\s*\\d*CH-(?:DST|DSD)\\s*\\)\\s*$"), QRegularExpression::CaseInsensitiveOption);
	return rx;
}

// A trailing "..."/"…" left over from a truncated tag, e.g. "TCHAIKOVSKY
// 1812 Overture ... (2CH-DST)" once the format tag itself has been
// stripped. Never touches an ellipsis inside a still-unstripped trailing
// parenthesised performer credit, e.g. "(Aurele Nicolet...)" - that is
// captured whole by trailingParenRx()/trailingPerformerYearRx() first.
const QRegularExpression& trailingEllipsisRx()
{
	static const QRegularExpression rx(QString::fromUtf8("\\s*(?:\\.{3}|…)\\s*$"));
	return rx;
}

// A quoted nickname, e.g. 'Emperor' or the curly-quote equivalents.
const QRegularExpression& quotedNicknameRx()
{
	static const QRegularExpression rx(QStringLiteral("['‘’][^'‘’]*['‘’]"));
	return rx;
}

QString buildSearchQuery(const QString& surname, const QString& title)
{
	QString cleaned = title;
	cleaned.remove(quotedNicknameRx());
	cleaned.remove(QLatin1Char(','));
	cleaned = cleaned.simplified();
	if (surname.isEmpty()) {
		return cleaned;
	}
	return cleaned.isEmpty() ? surname : (surname + QLatin1Char(' ') + cleaned);
}

// Title-cases an ALL-CAPS (or all-lowercase) word, e.g. "TCHAIKOVSKY" ->
// "Tchaikovsky". Short (<3 char) and dotted words are left alone so
// initials like "JS"/"J.S." are never mangled.
QString normalizeWordCasing(const QString& word)
{
	if (word.size() < 3 || word.contains(QLatin1Char('.'))) {
		return word;
	}
	if (word == word.toUpper() && word != word.toLower()) {
		return word.left(1) + word.mid(1).toLower();
	}
	return word;
}

// Strips the trailing decorations that clutter up real-world album tags:
// audio-format tags, disc numbers, and a trailing parenthesised performer
// credit (with or without a year). Populates "performer"/"year" when a
// performer credit was found (both stay untouched if a performer was
// already known, since only one trailing credit is expected). A trailing
// performer credit with no parentheses ("... - Trevor Pinnock") is handled
// separately, by stripTrailingDashPerformer() - only once a leading
// "Composer - "/"Composer: " decoration has already had a chance to claim
// the *leading* half of a "Name - Rest" string (see deriveWork()), since
// both look identical to a plain regex.
QString stripTrailingDecorations(QString title, QString& performer, QString& year)
{
	bool changed = true;
	while (changed) {
		changed = false;
		if (title.contains(formatTagRx())) {
			title = title.remove(formatTagRx()).trimmed();
			changed = true;
			continue;
		}
		if (title.contains(discSuffixRx())) {
			title = title.remove(discSuffixRx()).trimmed();
			changed = true;
			continue;
		}
		if (title.contains(trailingEllipsisRx())) {
			title = title.remove(trailingEllipsisRx()).trimmed();
			changed = true;
			continue;
		}
		if (performer.isEmpty() && year.isEmpty()) {
			const QRegularExpressionMatch m = trailingPerformerYearRx().match(title);
			if (m.hasMatch()) {
				title = m.captured(1).trimmed();
				performer = m.captured(2).trimmed();
				year = m.captured(3);
				changed = true;
				continue;
			}
		}
		if (performer.isEmpty()) {
			const QRegularExpressionMatch pm = trailingParenRx().match(title);
			if (pm.hasMatch() && !catalogueNumberRx().match(pm.captured(2)).hasMatch()) {
				title = pm.captured(1).trimmed();
				performer = pm.captured(2).trimmed();
				changed = true;
				continue;
			}
		}
	}
	return title;
}

// A digit directly followed by an upper-case letter that itself starts a
// real word (i.e. followed by a lower-case letter), with no space at all in
// between - e.g. the "9R" of "225-229Regensburger". Marks a candidate point
// where a trailing performer credit was glued directly onto a catalogue
// number in the raw tag, with the separating space simply missing.
const QRegularExpression& gluedWordBoundaryRx()
{
	static const QRegularExpression rx(QStringLiteral("\\d(\\p{Lu}\\p{Ll})"));
	return rx;
}

// Strips a trailing performer credit glued directly onto a catalogue-number
// range with no separating space at all, e.g. "Motets BWV
// 225-229Regensburger Domspatzen, Ltg. Hanns-Martin Schneidt" (real, if
// malformed, tag data) -> title "Motets BWV 225-229", performer
// "Regensburger Domspatzen, Ltg. Hanns-Martin Schneidt". Tries each
// candidate glue point (see gluedWordBoundaryRx()) in turn and only ever
// splits at one where the text up to (and not including) it is itself
// exactly a catalogue number match end-to-end - so this never fires on
// ordinary glued text elsewhere that just happens to look similar.
QString stripGluedTrailingPerformer(QString title, QString& performer)
{
	if (!performer.isEmpty()) {
		return title;
	}
	QRegularExpressionMatchIterator it = gluedWordBoundaryRx().globalMatch(title);
	while (it.hasNext()) {
		const QRegularExpressionMatch glue = it.next();
		const int splitPos = glue.capturedStart(1);
		const QString before = title.left(splitPos);
		const QRegularExpressionMatch catalogueMatch = catalogueNumberRx().match(before);
		if (catalogueMatch.hasMatch() && catalogueMatch.capturedEnd(0) == before.size()) {
			performer = title.mid(splitPos).trimmed();
			return before.trimmed();
		}
	}
	return title;
}

// Strips a leading "Name - "/"Name: " decoration - either a composer prefix
// ("Handel: Messiah", "Bach - English Suites") which is dropped silently, or
// a performer prefix ("Karl Richter - Brandenburg Concertos...") which is
// dropped and recorded as the performer (when none is known yet).
QString stripLeadingDecoration(QString title, const QString& surname, QString& performer)
{
	const QRegularExpressionMatch m = leadingNameRx().match(title);
	if (!m.hasMatch()) {
		return title;
	}
	const QString name = m.captured(1);
	const QString remainder = m.captured(2).trimmed();
	if (remainder.isEmpty()) {
		return title;
	}
	if (!surname.isEmpty() && name.contains(surname, Qt::CaseInsensitive)) {
		// Composer prefix - just drop it.
		return remainder;
	}
	if (performer.isEmpty()) {
		performer = name;
	}
	return remainder;
}

// Strips a trailing "- Name" performer credit with no parentheses, e.g.
// "... BWV 1044 - Trevor Pinnock". Only tried once the leading decoration
// above has already had first refusal on a "Name - Rest" string, so a
// composer prefix like "Bach - French Suites" is never misread as ending in
// a "French Suites" performer credit. catalogue-bearing trailing text (e.g.
// the " - Bauern-Kantate BWV 212" half of a multi-work album) never matches
// here in the first place, since a catalogue number always ends in a digit
// and the name pattern below excludes digits.
QString stripTrailingDashPerformer(QString title, QString& performer)
{
	if (!performer.isEmpty()) {
		return title;
	}
	const QRegularExpressionMatch m = trailingDashNameRx().match(title);
	if (m.hasMatch() && !catalogueNumberRx().match(m.captured(2)).hasMatch()) {
		performer = m.captured(2).trimmed();
		return m.captured(1).trimmed();
	}
	return title;
}

struct WorkSegment {
	QString title;// Full segment text, including its catalogue number.
	QString catalogueNumber;
};

// Normalises text for fuzzy work-title matching: lower-cased, diacritics
// stripped (so "ü" and "ue" fold together), and only letters/digits kept.
QString normalizeForMatch(const QString& text)
{
	QString folded = text.toLower();
	folded.replace(QStringLiteral("ä"), QStringLiteral("a"));
	folded.replace(QStringLiteral("ö"), QStringLiteral("o"));
	folded.replace(QStringLiteral("ü"), QStringLiteral("u"));
	folded.replace(QStringLiteral("ae"), QStringLiteral("a"));
	folded.replace(QStringLiteral("oe"), QStringLiteral("o"));
	folded.replace(QStringLiteral("ue"), QStringLiteral("u"));
	const QString decomposed = folded.normalized(QString::NormalizationForm_D);
	QString result;
	result.reserve(decomposed.size());
	for (const QChar& ch : decomposed) {
		if (ch.category() == QChar::Mark_NonSpacing) {
			continue;
		}
		if (ch.isLetterOrNumber()) {
			result.append(ch);
		}
	}
	return result;
}

// Splits "title" into per-work segments when it contains two or more
// catalogue numbers of the same system, separated by " - ", " / ", "; ",
// " & " or " + ". Only actually splits when *every* resulting segment
// carries exactly one catalogue number of that system - e.g. "BWV 1066,1069,
// Triple Concerto BWV 1044 - Trevor Pinnock" is left alone because its
// trailing-dash side has none (that "- Trevor Pinnock" should already have
// been stripped as a performer credit before this is called). Returns an
// empty list when the title is not a multi-work album.
QList<WorkSegment> splitMultiWorkAlbum(const QString& title)
{
	QList<QRegularExpressionMatch> matches;
	QRegularExpressionMatchIterator it = catalogueNumberRx().globalMatch(title);
	while (it.hasNext()) {
		matches.append(it.next());
	}
	if (matches.size() < 2) {
		return {};
	}
	const QString system = catalogueSystem(matches.first().captured(0));
	for (const QRegularExpressionMatch& m : matches) {
		if (catalogueSystem(m.captured(0)) != system) {
			return {};
		}
	}

	static const QRegularExpression delimiterRx(QStringLiteral(" - | / |; | & | \\+ "));
	const QStringList parts = title.split(delimiterRx, Qt::SkipEmptyParts);
	if (parts.size() != matches.size()) {
		return {};
	}

	QList<WorkSegment> segments;
	segments.reserve(parts.size());
	for (const QString& part : parts) {
		const QRegularExpressionMatch m = catalogueNumberRx().match(part);
		if (!m.hasMatch() || catalogueSystem(m.captured(0)) != system) {
			return {};
		}
		WorkSegment segment;
		segment.title = part.trimmed();
		segment.catalogueNumber = m.captured(0).trimmed();
		segments.append(segment);
	}
	return segments;
}

// Picks the segment whose name (with its catalogue number removed) best
// matches "songTitle": a prefix/substring match wins outright, otherwise the
// segment sharing the longest common prefix (at least 5 characters) is
// used, and the first segment is the last-resort fallback.
int pickMatchingSegmentIndex(const QList<WorkSegment>& segments, const QString& songTitle)
{
	const QString normalizedSongTitle = normalizeForMatch(songTitle);
	if (!normalizedSongTitle.isEmpty()) {
		for (int i = 0; i < segments.size(); ++i) {
			QString name = segments.at(i).title;
			name.remove(catalogueNumberRx());
			const QString normalizedName = normalizeForMatch(name);
			if (!normalizedName.isEmpty() && (normalizedSongTitle.startsWith(normalizedName) || normalizedSongTitle.contains(normalizedName))) {
				return i;
			}
		}
		int bestIndex = -1;
		int bestLen = 4;// Require at least 5 matching characters.
		for (int i = 0; i < segments.size(); ++i) {
			QString name = segments.at(i).title;
			name.remove(catalogueNumberRx());
			const QString normalizedName = normalizeForMatch(name);
			int len = 0;
			const int maxLen = qMin(normalizedName.size(), normalizedSongTitle.size());
			while (len < maxLen && normalizedName.at(len) == normalizedSongTitle.at(len)) {
				++len;
			}
			if (len > bestLen) {
				bestLen = len;
				bestIndex = i;
			}
		}
		if (bestIndex >= 0) {
			return bestIndex;
		}
	}
	return 0;
}

// Resolves a fallback composer from the raw artist/albumartist tags when the
// song has no composer tag, subject to the song plausibly being classical -
// see workinfo.h/deriveWork() for the full rationale.
QString resolveFallbackComposer(const QString& artist, const QString& albumArtist, const QString& catalogueNumber, const QString& genre)
{
	const QString fallbackName = !albumArtist.trimmed().isEmpty() ? albumArtist.trimmed() : artist.trimmed();
	if (fallbackName.isEmpty()) {
		return QString();
	}

	const QString system = catalogueSystem(catalogueNumber);
	const QString implied = impliedComposerForSystem(system);
	if (!implied.isEmpty()) {
		// The catalogue system points at a specific composer - only trust it
		// when the artist/albumartist tag actually names that composer (in
		// whatever messy format - "Bach J.S.", "J.S.Bach", "Bach, Johann
		// Sebastian (1685-1750)", "Bach/Vivaldi", ...), never a performer.
		const QString impliedSurname = composerSurname(implied);
		if (artist.contains(impliedSurname, Qt::CaseInsensitive) || albumArtist.contains(impliedSurname, Qt::CaseInsensitive)) {
			return implied;
		}
		return QString();
	}

	const bool genreClassical = genre.contains(QStringLiteral("classical"), Qt::CaseInsensitive)
	                          || genre.contains(QStringLiteral("klassik"), Qt::CaseInsensitive)
	                          || genre.contains(QString::fromUtf8("古典"));// "古典"
	if (!genreClassical && catalogueNumber.isEmpty()) {
		return QString();
	}

	// No catalogue system pins down a specific composer (either there is no
	// catalogue number at all, or it is an ambiguous one like "Op."). Only
	// trust the raw artist/albumartist tag when it actually resolves to a
	// known composer (see composertable.h) - never a bare performer credit,
	// "Name : Role" tagging (e.g. "Preston, Stephen : Flute -") or an
	// ensemble/choir/quartet name, none of which resolve.
	return ComposerTable::resolve(fallbackName);
}

// Tries to find a known composer named as a leading prefix of "text" -
// "Composer: Rest", "Composer - Rest", a bare "Composer Rest" (e.g.
// "TCHAIKOVSKY 1812 Overture", "Bach 'Dorienne' Toccata...") or "Surname,
// Initials: Rest" (e.g. "Marcello, A: La Cetra..."), optionally after a
// leading "<N> " track-count number ("63 C.P.E. Bach - ..."). The
// leadingComposerNameRx()/leadingComposerSurnameInitialsRx() patterns above
// are deliberately permissive - the only real gate against a false positive
// is that the candidate must actually resolve via ComposerTable::resolve().
// A resulting remainder starting with "&"/"and"/"und"/"et" is rejected too
// (e.g. "Bach & Vivaldi" - a two-composer compilation, not a "Composer:
// Work" prefix naming a single one). Returns "text" unchanged and an empty
// "canonical" when nothing matches.
QString extractLeadingComposerPrefix(const QString& text, QString& canonical)
{
	canonical.clear();
	static const QStringList conjunctions = {QStringLiteral("and "), QStringLiteral("und "), QStringLiteral("et ")};
	for (int attempt = 0; attempt < 2; ++attempt) {
		QString working = text;
		if (attempt == 1) {
			const QRegularExpressionMatch numberMatch = leadingTrackNumberRx().match(text);
			if (!numberMatch.hasMatch()) {
				continue;
			}
			working = text.mid(numberMatch.capturedLength(0));
		}

		const QRegularExpressionMatch surnameInitials = leadingComposerSurnameInitialsRx().match(working);
		if (surnameInitials.hasMatch()) {
			const QString resolved = ComposerTable::resolve(surnameInitials.captured(1) + QStringLiteral(", ") + surnameInitials.captured(2));
			if (!resolved.isEmpty()) {
				canonical = resolved;
				return surnameInitials.captured(3).trimmed();
			}
		}

		const QRegularExpressionMatch nameMatch = leadingComposerNameRx().match(working);
		if (nameMatch.hasMatch()) {
			const QString rest = nameMatch.captured(2).trimmed();
			const QString restLower = rest.toLower();
			bool startsWithConjunction = rest.startsWith(QLatin1Char('&'));
			for (const QString& conjunction : conjunctions) {
				startsWithConjunction = startsWithConjunction || restLower.startsWith(conjunction);
			}
			if (!rest.isEmpty() && !startsWithConjunction) {
				const QString resolved = ComposerTable::resolve(nameMatch.captured(1));
				if (!resolved.isEmpty()) {
					canonical = resolved;
					return rest;
				}
			}
		}
	}
	return text;
}

}// namespace

QString composerSurname(const QString& composer)
{
	static const QSet<QString> particles = {
	    QStringLiteral("van"), QStringLiteral("von"), QStringLiteral("de"), QStringLiteral("der"),
	    QStringLiteral("den"), QStringLiteral("di"), QStringLiteral("du"), QStringLiteral("la"),
	    QStringLiteral("le"), QStringLiteral("y")};
	const QStringList words = composer.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (words.isEmpty()) {
		return QString();
	}
	// Normally the surname is simply the last word ("Ludwig van Beethoven"
	// -> "Beethoven"), which already places any particle correctly since it
	// always precedes the surname. Guard against a name that happens to end
	// in a bare particle so that is never returned on its own.
	if (words.size() > 1 && particles.contains(words.last().toLower())) {
		return normalizeWordCasing(words.at(words.size() - 2)) + QLatin1Char(' ') + normalizeWordCasing(words.last());
	}
	return normalizeWordCasing(words.last());
}

Candidate deriveWork(const QString& composer, const QString& artist, const QString& albumArtist, const QString& album, const QString& songTitle, const QString& genre)
{
	Candidate work;

	// 1. Strip the decorations real-world tags pile onto the album field
	// (audio-format/disc markers, a trailing/leading performer credit) so
	// everything below works from a clean work title.
	QString title = stripTrailingDecorations(album.trimmed(), work.performer, work.year);
	title = stripGluedTrailingPerformer(title, work.performer);

	// 2. Resolve the composer. A composer tag is normalised via the
	// known-composer table when it matches one (but always kept, even when
	// it does not - see composertable.h); otherwise resolveFallbackComposer()
	// tries the artist/albumartist tags, and failing that, a leading
	// "Composer: "/"Composer - "/"Composer " prefix of the album title
	// itself (e.g. "Couperin: Nouveaux Concerts", "Alessandro Scarlatti - Il
	// Giardino d'Amore") - either way, only ever accepted when it actually
	// resolves to a known composer, never a bare performer/ensemble credit.
	QString resolvedComposer = composer.trimmed();
	if (!resolvedComposer.isEmpty()) {
		const QString normalized = ComposerTable::resolve(resolvedComposer);
		if (!normalized.isEmpty()) {
			resolvedComposer = normalized;
		}
	}
	if (resolvedComposer.isEmpty()) {
		const QRegularExpressionMatch preliminaryCatalogue = catalogueNumberRx().match(title);
		resolvedComposer = resolveFallbackComposer(artist, albumArtist, preliminaryCatalogue.hasMatch() ? preliminaryCatalogue.captured(0).trimmed() : QString(), genre);
	}
	bool composerFromAlbumPrefix = false;
	if (resolvedComposer.isEmpty()) {
		QString canonical;
		const QString strippedTitle = extractLeadingComposerPrefix(title, canonical);
		if (!canonical.isEmpty()) {
			resolvedComposer = canonical;
			title = strippedTitle;
			composerFromAlbumPrefix = true;
		}
	}
	work.composer = resolvedComposer;
	work.valid = !work.composer.isEmpty();
	if (!work.valid) {
		return Candidate();
	}

	work.surname = composerSurname(work.composer);

	// 3. Now that the composer/surname is known, strip a leading "Name - "/
	// "Name: "/"Name " decoration - either a redundant composer prefix
	// ("Handel: Messiah", "Marcello, A: La Cetra...", "TCHAIKOVSKY 1812
	// Overture") dropped silently (skipped when the album prefix already
	// supplied the composer just above, since that already consumed it), or
	// a performer prefix ("Karl Richter - Brandenburg Concertos...")
	// recorded as the performer.
	if (!composerFromAlbumPrefix) {
		QString canonical;
		const QString strippedTitle = extractLeadingComposerPrefix(title, canonical);
		if (!canonical.isEmpty() && 0 == composerSurname(canonical).compare(work.surname, Qt::CaseInsensitive)) {
			title = strippedTitle;
		}
	}
	title = stripLeadingDecoration(title, work.surname, work.performer);

	// 3b. Now that a leading decoration has had first refusal, a remaining
	// bare trailing "- Name" is a performer credit (see
	// stripTrailingDashPerformer()).
	title = stripTrailingDashPerformer(title, work.performer);

	// 4. Split a multi-work album (e.g. "Kaffee-Kantate BWV 211 -
	// Bauern-Kantate BWV 212") into its work segments and pick the one
	// matching the track title.
	const QList<WorkSegment> segments = splitMultiWorkAlbum(title);
	if (!segments.isEmpty()) {
		const WorkSegment& chosen = segments.at(pickMatchingSegmentIndex(segments, songTitle));
		work.title = chosen.title;
		work.catalogueNumber = chosen.catalogueNumber;
	}
	else {
		work.title = title;
		const QRegularExpressionMatch catalogue = catalogueNumberRx().match(work.title);
		if (catalogue.hasMatch()) {
			work.catalogueNumber = catalogue.captured(0).trimmed();
		}
		else if (0 == work.surname.compare(QStringLiteral("Chopin"), Qt::CaseInsensitive)) {
			const QRegularExpressionMatch chopin = chopinBrownIndexRx().match(work.title);
			if (chopin.hasMatch()) {
				work.catalogueNumber = chopin.captured(0).trimmed();
			}
		}
	}

	work.genreKeyword = findGenreKeyword(work.title);
	if (work.genreKeyword.isEmpty()) {
		work.genreKeyword = findGenreKeyword(genre);
	}
	if (work.genreKeyword.isEmpty()) {
		work.genreKeyword = findGenreKeyword(songTitle);
	}

	work.searchQuery = buildSearchQuery(work.surname, work.title);
	return work;
}

namespace {

QString stripHtmlTags(const QString& html)
{
	QString text = html;
	static const QRegularExpression tagRx(QStringLiteral("<[^>]*>"));
	text.remove(tagRx);
	text.replace(QLatin1String("&quot;"), QLatin1String("\""));
	text.replace(QLatin1String("&amp;"), QLatin1String("&"));
	text.replace(QLatin1String("&#39;"), QLatin1String("'"));
	text.replace(QLatin1String("&lt;"), QLatin1String("<"));
	text.replace(QLatin1String("&gt;"), QLatin1String(">"));
	return text;
}

}// namespace

QString selectSearchResult(const QByteArray& searchResponseJson, const Candidate& work)
{
	const QJsonDocument document = QJsonDocument::fromJson(searchResponseJson);
	if (!document.isObject()) {
		return QString();
	}
	const QJsonArray results = document.object().value(QLatin1String("query")).toObject().value(QLatin1String("search")).toArray();

	QString normalizedCatalogue = work.catalogueNumber;
	normalizedCatalogue.remove(QLatin1Char('.')).remove(QLatin1Char(' '));

	// Three buckets, most confident first. A real Wikipedia article's own
	// title often *is* the catalogue-numbered name (e.g. Bach's Coffee
	// Cantata is actually titled "Schweigt stille, plaudert nicht, BWV
	// 211") and may not contain the generic work-type keyword at all - that
	// is still the single strongest signal there is, so it wins outright
	// and skips the keyword check entirely. Every weaker bucket requires
	// the keyword in the title, so composer biography/list pages are still
	// rejected.
	QString titleCatalogueResult;
	QString snippetCatalogueResult;
	QString keywordSurnameResult;

	for (const QJsonValue& value : results) {
		const QJsonObject result = value.toObject();
		const QString title = result.value(QLatin1String("title")).toString();
		if (title.isEmpty()) {
			continue;
		}
		const QString snippet = stripHtmlTags(result.value(QLatin1String("snippet")).toString());

		bool titleHasCatalogue = false;
		bool snippetHasCatalogue = false;
		if (!normalizedCatalogue.isEmpty()) {
			QString normalizedTitle = title;
			normalizedTitle.remove(QLatin1Char('.')).remove(QLatin1Char(' '));
			QString normalizedSnippet = snippet;
			normalizedSnippet.remove(QLatin1Char('.')).remove(QLatin1Char(' '));
			titleHasCatalogue = normalizedTitle.contains(normalizedCatalogue, Qt::CaseInsensitive);
			snippetHasCatalogue = normalizedSnippet.contains(normalizedCatalogue, Qt::CaseInsensitive);
		}

		if (titleHasCatalogue) {
			if (titleCatalogueResult.isEmpty()) {
				titleCatalogueResult = title;
			}
			continue;
		}

		// Every remaining route requires the work-type keyword in the title
		// (when one is known) - this rejects composer biography/list pages
		// even when they otherwise mention the surname or catalogue number.
		if (!work.genreKeyword.isEmpty() && !title.contains(work.genreKeyword, Qt::CaseInsensitive)) {
			continue;
		}
		const bool surnameMatch = !work.surname.isEmpty() && (title.contains(work.surname, Qt::CaseInsensitive) || snippet.contains(work.surname, Qt::CaseInsensitive));

		if (snippetHasCatalogue && snippetCatalogueResult.isEmpty()) {
			snippetCatalogueResult = title;
		}
		if ((snippetHasCatalogue || surnameMatch) && keywordSurnameResult.isEmpty()) {
			keywordSurnameResult = title;
		}
	}

	if (!titleCatalogueResult.isEmpty()) {
		return titleCatalogueResult;
	}
	return snippetCatalogueResult.isEmpty() ? keywordSurnameResult : snippetCatalogueResult;
}

SiteLinks parseSiteLinks(const QByteArray& pagePropsJson)
{
	SiteLinks links;
	const QJsonDocument document = QJsonDocument::fromJson(pagePropsJson);
	if (!document.isObject()) {
		return links;
	}
	const QJsonObject pages = document.object().value(QLatin1String("query")).toObject().value(QLatin1String("pages")).toObject();
	// Only a single title is ever queried, so at most one page is present.
	for (auto it = pages.constBegin(); it != pages.constEnd(); ++it) {
		const QJsonObject page = it.value().toObject();
		links.wikidataId = page.value(QLatin1String("pageprops")).toObject().value(QLatin1String("wikibase_item")).toString();
		const QJsonArray langlinks = page.value(QLatin1String("langlinks")).toArray();
		for (const QJsonValue& value : langlinks) {
			const QJsonObject link = value.toObject();
			if (link.value(QLatin1String("lang")).toString() == QLatin1String("zh")) {
				links.zhTitle = link.contains(QLatin1String("*")) ? link.value(QLatin1String("*")).toString() : link.value(QLatin1String("title")).toString();
				break;
			}
		}
		break;
	}
	return links;
}

Summary parseSummary(const QByteArray& summaryJson)
{
	Summary summary;
	const QJsonDocument document = QJsonDocument::fromJson(summaryJson);
	if (!document.isObject()) {
		return summary;
	}
	const QJsonObject root = document.object();
	summary.extract = root.value(QLatin1String("extract")).toString();
	const QJsonObject contentUrls = root.value(QLatin1String("content_urls")).toObject();
	QString url = contentUrls.value(QLatin1String("desktop")).toObject().value(QLatin1String("page")).toString();
	if (url.isEmpty()) {
		url = contentUrls.value(QLatin1String("mobile")).toObject().value(QLatin1String("page")).toString();
	}
	summary.url = url;
	return summary;
}

}// namespace WorkInfo
