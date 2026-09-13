/* Cantata - render translated metadata without exposing model formatting. */
#ifndef MUSIC_TOOLTIP_H
#define MUSIC_TOOLTIP_H

#include "support/translationtext.h"
#include <QPersistentModelIndex>
#include <QPointer>
#include <QTextDocument>

class QAbstractItemView;
class QHelpEvent;

namespace MusicToolTip {

inline QString sourceText(const QString& originalHtml)
{
	const int end = originalHtml.indexOf(QStringLiteral("</table>"), 0, Qt::CaseInsensitive);
	if (end < 0) {
		return QString();
	}
	QTextDocument document;
	document.setHtml(originalHtml.left(end + 8));
	return document.toPlainText().trimmed();
}

inline QString legacySourceText(const QString& originalHtml)
{
	QTextDocument document;
	document.setHtml(originalHtml);
	return document.toPlainText().trimmed();
}

inline QString translatedHtml(const QString& originalHtml, const QString& translation)
{
	const int tableEnd = originalHtml.indexOf(QStringLiteral("</table>"), 0, Qt::CaseInsensitive);
	if (tableEnd < 0 || translation.isEmpty()) {
		return originalHtml;
	}
	const QString table = originalHtml.left(tableEnd + 8);
	// Keep the original column alignment, bold labels, spacing, and footer
	// markup. Only the contents of the two metadata cells are replaced.
	static const QRegularExpression row(
	    QStringLiteral("(<tr\\b[^>]*>\\s*<td\\b[^>]*>)(.*?)(</td>\\s*<td\\b[^>]*>)(.*?)(</td>\\s*</tr>)"),
	    QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
	QList<QRegularExpressionMatch> originalRows;
	auto matches = row.globalMatch(table);
	while (matches.hasNext()) {
		originalRows.append(matches.next());
	}
	if (originalRows.isEmpty()) {
		return originalHtml;
	}

	static const QRegularExpression field(QStringLiteral("^([\\p{L}][^:：]{0,79}[:：])\\s*(.*)$"));
	static const QRegularExpression pathOrUrl(QStringLiteral("^(?:[A-Za-z][A-Za-z0-9+.-]*://|[A-Za-z]:[\\\\/])"));
	QList<QPair<QString, QString>> translatedRows;
	const QString text = TranslationText::compactKeyValueLines(translation);
	for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
		const QRegularExpressionMatch match = field.match(line);
		if (match.hasMatch() && !pathOrUrl.match(line).hasMatch()) {
			translatedRows.append(qMakePair(match.captured(1).trimmed(), match.captured(2)));
			if (translatedRows.size() == originalRows.size()) {
				// Anything after the metadata in legacy caches is a translated
				// footer. The actual directory and its italic markup come from
				// originalHtml below, never from the model.
				break;
			}
		}
		else if (!translatedRows.isEmpty()) {
			translatedRows.last().second += QLatin1Char(' ') + line;
		}
	}
	if (translatedRows.size() != originalRows.size()) {
		return originalHtml;
	}
	QString html = table;
	for (int index = originalRows.size() - 1; index >= 0; --index) {
		const auto& match = originalRows.at(index);
		const auto& translated = translatedRows.at(index);
		const QString replacement = match.captured(1) + QStringLiteral("<b>") + translated.first.toHtmlEscaped()
		    + QStringLiteral("&nbsp;&nbsp;</b>") + match.captured(3) + translated.second.toHtmlEscaped() + match.captured(5);
		html.replace(match.capturedStart(), match.capturedLength(), replacement);
	}
	return QStringLiteral("<qt>") + html + originalHtml.mid(tableEnd + 8) + QStringLiteral("</qt>");
}

// Tracks the tooltip translation currently in flight for one delegate
// instance, so a later TranslationService::translationReady signal can be
// matched back to the row (and cursor position) it was requested for.
struct PendingState {
	QString source;
	QString html;
	QString context;
	QPointer<QAbstractItemView> view;
	QPersistentModelIndex index;

	// Defined out-of-line (musictooltip.cpp) since clearing `view` needs a
	// complete QAbstractItemView; keeping it there avoids forcing every
	// includer of this header (e.g. tests that never touch PendingState) to
	// also pull in <QAbstractItemView>.
	void clear();
};

// Handles a QEvent::ToolTip help event for `index`. If its Qt::ToolTipRole
// data is a music-details "<table>...", shows a cached/translated tooltip (or
// the original text while a translation request is in flight), records
// `pending` so a later translationReady signal can update it, and returns
// true. Otherwise clears `pending` and returns false so the caller falls back
// to default tooltip handling.
bool showTranslatedTableTooltip(QHelpEvent* e, QAbstractItemView* view, const QModelIndex& index, PendingState& pending);

// Call from a TranslationService::translationReady handler. Shows the
// translated tooltip if `pending` is still current: same source/context, the
// view is visible and active, the cursor is still over the same index, and
// the index's tooltip data has not changed since the request was made.
void handleTranslationReady(const QString& source, const QString& context, const QString& translation, PendingState& pending);

}
#endif
