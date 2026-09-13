/* Cantata - literal, multilingual music search terms. */
#ifndef SEARCH_TERMS_H
#define SEARCH_TERMS_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace SearchTerms {

inline bool containsChinese(const QString& text)
{
	static const QRegularExpression han(QStringLiteral("\\p{Han}"));
	return han.match(text).hasMatch();
}

inline QStringList tokens(const QString& text)
{
	static const QRegularExpression whitespace(QStringLiteral("\\s+"));
	return text.split(whitespace, Qt::SkipEmptyParts);
}

inline bool isMark(const QChar& ch)
{
	return ch.category() == QChar::Mark_NonSpacing || ch.category() == QChar::Mark_SpacingCombining
	    || ch.category() == QChar::Mark_Enclosing;
}

inline bool hasMarks(const QString& text)
{
	for (const QChar ch : text) if (isMark(ch)) return true;
	return false;
}

inline QString stripMarks(const QString& text)
{
	QString result;
	for (const QChar ch : text) if (!isMark(ch)) result += ch;
	return result.simplified();
}

// Compatibility decomposition (NFKD) so fullwidth/ligature forms fold onto their
// plain equivalents (e.g. "ＡＢＣ" -> "abc"). Marks are kept here: whether they
// should be stripped depends on the needle being compared (see matches()), not
// on this text in isolation.
inline QString normalized(const QString& text)
{
	QString result;
	for (const QChar ch : text.normalized(QString::NormalizationForm_KD).toCaseFolded()) {
		if (ch.category() == QChar::Punctuation_Dash) result += QLatin1Char(' ');
		else result += ch;
	}
	return result.simplified();
}

inline QStringList alternatives(const QString& source, QString response)
{
	QStringList result { source };
	response = response.trimmed();
	if (response.startsWith(QLatin1String("```"))) {
		response = response.mid(response.indexOf(QLatin1Char('\n')) + 1);
		if (response.endsWith(QLatin1String("```"))) response.chop(3);
	}
	const QJsonDocument document = QJsonDocument::fromJson(response.toUtf8());
	if (!document.isArray()) return result;
	QStringList seen { normalized(source) };
	for (const QJsonValue& value : document.array()) {
		if (!value.isString()) continue;
		const QString term = value.toString().simplified();
		// Model output is data, never query syntax. Keep the expansion bounded.
		if (term.size() < 2 || term.size() > 160 || term.contains(QLatin1Char('/'))
		    || term.contains(QLatin1Char('\\')) || term.contains(QLatin1Char(':'))) continue;
		const QString key = normalized(term);
		if (seen.contains(key)) continue;
		seen.append(key);
		result.append(term);
		if (result.size() >= 17) break;
	}
	return result;
}

// A term prepared once per filter change instead of once per candidate row. `normalized`
// is already mark-stripped unless `hasMarks` is set (see matches() below for why), and is
// left empty for a needle that normalizes away to nothing (e.g. a bare "-" or "--"); such a
// needle only ever matches via the raw fast path.
struct Needle {
	QString raw;
	QString normalized;
	bool hasMarks;
};
using NeedleGroup = QList<Needle>;

inline QList<NeedleGroup> prepare(const QList<QStringList>& groups)
{
	QList<NeedleGroup> prepared;
	prepared.reserve(groups.size());
	for (const QStringList& group : groups) {
		NeedleGroup needles;
		needles.reserve(group.size());
		for (const QString& term : group) {
			Needle needle { term, QString(), false };
			const QString needleFull = normalized(term);
			if (!needleFull.isEmpty()) {
				// Only strip marks from a mark-free needle. A needle that itself carries
				// marks (e.g. "か" + dakuten) must keep them, so it does not over-match
				// the bare base character.
				needle.hasMarks = hasMarks(needleFull);
				needle.normalized = needle.hasMarks ? needleFull : stripMarks(needleFull);
			}
			needles.append(needle);
		}
		prepared.append(needles);
	}
	return prepared;
}

inline bool matches(const QList<NeedleGroup>& groups, const QStringList& values)
{
	QStringList candidates;
	candidates.reserve(values.size());
	for (const QString& value : values) candidates.append(normalized(value));

	// Mark-stripped candidates are only needed for mark-free needles, and are the same
	// for every such needle in this call, so compute each at most once, lazily.
	QStringList strippedCandidates(candidates.size());
	QVector<bool> haveStripped(candidates.size(), false);
	auto strippedCandidate = [&](int i) -> const QString& {
		if (!haveStripped[i]) {
			strippedCandidates[i] = stripMarks(candidates.at(i));
			haveStripped[i] = true;
		}
		return strippedCandidates.at(i);
	};

	for (const NeedleGroup& group : groups) {
		bool found = false;
		for (const Needle& needle : group) {
			// Fast path, and also the fallback for a needle that normalizes away to
			// nothing (e.g. a bare "-" or "--"): a plain case-insensitive contains()
			// against the original text. This must never be skipped, since an empty
			// normalized needle would otherwise match every row.
			for (const QString& value : values) {
				if (value.contains(needle.raw, Qt::CaseInsensitive)) {
					found = true;
					break;
				}
			}
			if (found) break;

			if (needle.normalized.isEmpty()) continue;

			for (int i = 0; i < candidates.size(); ++i) {
				const QString& candidate = needle.hasMarks ? candidates.at(i) : strippedCandidate(i);
				if (candidate.contains(needle.normalized)) {
					found = true;
					break;
				}
			}
			if (found) break;
		}
		if (!found) return false;
	}
	return true;
}

inline bool matches(const QList<QStringList>& groups, const QStringList& values)
{
	return matches(prepare(groups), values);
}

inline QString ftsAlternatives(const QStringList& alternatives)
{
	static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}]+"));
	QStringList phrases;
	for (const QString& alternative : alternatives) {
		QStringList words;
		auto matches = word.globalMatch(alternative);
		while (matches.hasNext()) words.append(matches.next().captured());
		if (!words.isEmpty()) phrases.append(QLatin1Char('"') + words.join(QLatin1Char(' ')) + QStringLiteral("*\""));
	}
	return phrases.isEmpty() ? QString() : QLatin1Char('(') + phrases.join(QStringLiteral(" OR ")) + QLatin1Char(')');
}

}
#endif
