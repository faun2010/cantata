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

#ifndef BIOGRAPHY_TRANSLATION_H
#define BIOGRAPHY_TRANSLATION_H

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QTextDocument>
#include <QUrl>

// Translation providers receive plain text, so preserve links as delimiters
// around their visible text. The href is deliberately kept out of that text:
// a model must never be able to choose the URL displayed by Cantata.
namespace BiographyTranslation {

struct Link {
	QString href;
	QString text;
};

struct Prepared {
	QString source;
	QList<Link> links;
};

inline QString marker(int index, const char* boundary)
{
	return QString::fromLatin1("[[CANTATA_LINK_%1_%2]]").arg(index).arg(QLatin1String(boundary));
}

inline QString plainTextToHtml(QString text)
{
	text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
	text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	return text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>"));
}

inline QString decodedAttribute(const QString& value)
{
	QTextDocument document;
	document.setHtml(QLatin1String("<span>") + value + QLatin1String("</span>"));
	return document.toPlainText();
}

inline bool safeHref(const QString& href)
{
	const QUrl url(href);
	if (!url.isValid() || href.contains(QRegularExpression(QLatin1String("[\\x00-\\x1f]")))) {
		return false;
	}
	const QString scheme = url.scheme().toLower();
	return scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("cantata");
}

inline Prepared prepare(const QString& html)
{
	Prepared prepared;
	QString markedHtml;
	int offset = 0;
	const QRegularExpression anchors(QLatin1String("<a\\b([^>]*)>(.*?)</a\\s*>"),
	                                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
	const QRegularExpression hrefAttribute(QLatin1String("\\bhref\\s*=\\s*(?:\\\"([^\\\"]*)\\\"|'([^']*)'|([^\\s>]+))"),
	                                      QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatchIterator iterator = anchors.globalMatch(html);
	while (iterator.hasNext()) {
		const QRegularExpressionMatch match = iterator.next();
		markedHtml += html.mid(offset, match.capturedStart() - offset);
		const QRegularExpressionMatch hrefMatch = hrefAttribute.match(match.captured(1));
		QString href;
		if (hrefMatch.hasMatch()) {
			href = decodedAttribute(hrefMatch.captured(1).isNull() ? (hrefMatch.captured(2).isNull() ? hrefMatch.captured(3) : hrefMatch.captured(2)) : hrefMatch.captured(1));
		}
		QTextDocument labelDocument;
		labelDocument.setHtml(match.captured(2));
		const QString label = labelDocument.toPlainText();
		if (safeHref(href) && !label.isEmpty()) {
			const int index = prepared.links.size();
			prepared.links.append({href, label});
			markedHtml += marker(index, "BEGIN") + label.toHtmlEscaped() + marker(index, "END");
		}
		else {
			markedHtml += match.captured();
		}
		offset = match.capturedEnd();
	}
	markedHtml += html.mid(offset);
	QTextDocument document;
	document.setHtml(markedHtml);
	prepared.source = document.toPlainText().trimmed();
	return prepared;
}

inline QString restore(const QString& translation, const Prepared& prepared)
{
	QString html;
	QList<int> missing;
	int position = 0;
	for (int index = 0; index < prepared.links.size(); ++index) {
		const QString begin = marker(index, "BEGIN");
		const QString end = marker(index, "END");
		const int beginPosition = translation.indexOf(begin, position);
		const int endPosition = beginPosition < 0 ? -1 : translation.indexOf(end, beginPosition + begin.size());
		if (beginPosition < 0 || endPosition < 0) {
			missing.append(index);
			continue;
		}
		const QString label = translation.mid(beginPosition + begin.size(), endPosition - beginPosition - begin.size()).trimmed();
		if (label.isEmpty()) {
			missing.append(index);
			continue;
		}
		html += plainTextToHtml(translation.mid(position, beginPosition - position));
		html += QLatin1String("<a href=\"") + prepared.links.at(index).href.toHtmlEscaped() + QLatin1String("\">") + label.toHtmlEscaped() + QLatin1String("</a>");
		position = endPosition + end.size();
	}
	html += plainTextToHtml(translation.mid(position));
	if (!missing.isEmpty()) {
		html += QLatin1String("<br/><br/>");
		for (int index : missing) {
			if (index != missing.first()) {
				html += QLatin1String(" · ");
			}
			const Link& link = prepared.links.at(index);
			html += QLatin1String("<a href=\"") + link.href.toHtmlEscaped() + QLatin1String("\">") + link.text.toHtmlEscaped() + QLatin1String("</a>");
		}
	}
	return html;
}

}

#endif
