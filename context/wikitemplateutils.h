/*
 * Cantata
 *
 * Helpers for retaining visible text from common Wikipedia templates.
 */

#ifndef WIKI_TEMPLATE_UTILS_H
#define WIKI_TEMPLATE_UTILS_H

#include <QString>
#include <QStringList>

namespace WikiTemplateUtils {

inline int templateEnd(const QString& text, int start)
{
	int depth = 0;
	for (int pos = start; pos < text.length() - 1; ++pos) {
		if (text.at(pos) == QLatin1Char('{') && text.at(pos + 1) == QLatin1Char('{')) {
			++depth;
			++pos;
		}
		else if (text.at(pos) == QLatin1Char('}') && text.at(pos + 1) == QLatin1Char('}')) {
			if (0 == --depth) {
				return pos;
			}
			++pos;
		}
	}
	return -1;
}

inline QString visibleTemplateText(QString text)
{
	int start = 0;
	while ((start = text.indexOf(QLatin1String("{{"), start)) >= 0) {
		int end = templateEnd(text, start);
		if (end < 0) {
			break;
		}
		QStringList parts = text.mid(start + 2, end - start - 2).split(QLatin1Char('|'));
		QString name = parts.isEmpty() ? QString() : parts.takeFirst().trimmed().toLower();
		QString replacement;
		if (QLatin1String("ill") == name || QLatin1String("interlanguage link") == name || QLatin1String("interlanguage-link") == name) {
			for (const QString& part : parts) {
				int equals = part.indexOf(QLatin1Char('='));
				if (equals > 0 && QLatin1String("lt") == part.left(equals).trimmed().toLower()) {
					replacement = part.mid(equals + 1).trimmed();
					break;
				}
			}
			if (replacement.isEmpty() && !parts.isEmpty()) {
				replacement = parts.first().trimmed();
			}
		}
		else if (QLatin1String("lang") == name) {
			if (parts.size() > 1) {
				replacement = parts.at(1).trimmed();
			}
		}
		else if (name.startsWith(QLatin1String("lang-")) && !parts.isEmpty()) {
			replacement = parts.first().trimmed();
		}

		if (replacement.isEmpty()) {
			start = end + 2;
		}
		else {
			text.replace(start, end - start + 2, replacement);
		}
	}
	return text;
}

}

#endif
