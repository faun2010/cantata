/* Cantata - literal, multilingual music search terms. */
#ifndef SEARCH_TERMS_H
#define SEARCH_TERMS_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

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

inline QString normalized(const QString& text)
{
	QString result;
	for (const QChar ch : text.normalized(QString::NormalizationForm_D).toCaseFolded()) {
		if (ch.category() == QChar::Punctuation_Dash) result += QLatin1Char(' ');
		else if (ch.category() != QChar::Mark_NonSpacing && ch.category() != QChar::Mark_SpacingCombining
		    && ch.category() != QChar::Mark_Enclosing) result += ch;
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

inline bool matches(const QList<QStringList>& groups, const QStringList& values)
{
	QStringList candidates;
	for (const QString& value : values) candidates.append(normalized(value));
	for (const QStringList& group : groups) {
		bool found = false;
		for (const QString& term : group) {
			const QString needle = normalized(term);
			for (const QString& candidate : candidates) {
				if (candidate.contains(needle)) {
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
