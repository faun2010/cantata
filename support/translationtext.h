/* Cantata - plain-text formatting shared by music translation and tooltips. */
#ifndef TRANSLATION_TEXT_H
#define TRANSLATION_TEXT_H

#include <QRegularExpression>
#include <QStringList>

namespace TranslationText {

// QTextDocument puts each table cell on a separate line. Join a field label
// ending in a colon to its value, including results already stored in cache.
inline QString compactKeyValueLines(QString text)
{
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
	text.replace(QChar::LineSeparator, QLatin1Char('\n'));
	static const QRegularExpression splitValue(QStringLiteral("([:：])[^\\S\\n]*\\n\\s*"));
	text.replace(splitValue, QStringLiteral("\\1 "));
	QStringList lines;
	for (const QString& line : text.split(QLatin1Char('\n'))) {
		const QString compact = line.simplified();
		if (!compact.isEmpty()) {
			lines.append(compact);
		}
	}
	return lines.join(QLatin1Char('\n'));
}

}
#endif
