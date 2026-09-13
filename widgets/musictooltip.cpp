/* Cantata - render translated metadata without exposing model formatting. */
#include "musictooltip.h"
#include "network/translationservice.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QHelpEvent>
#include <QToolTip>

namespace MusicToolTip {

void PendingState::clear()
{
	source.clear();
	html.clear();
	context.clear();
	view = nullptr;
	index = QPersistentModelIndex();
}

bool showTranslatedTableTooltip(QHelpEvent* e, QAbstractItemView* view, const QModelIndex& index, PendingState& pending)
{
	const QString sourceHtml = index.data(Qt::ToolTipRole).toString();
	pending.clear();
	// Music models provide structured detail tables; action and navigation
	// help remains in the application's normal language.
	if (!sourceHtml.startsWith(QLatin1String("<table>")) || !sourceHtml.contains(QLatin1String("<b>"))) {
		return false;
	}

	const QString source = sourceText(sourceHtml);
	const QString context = QLatin1String("music-details");
	pending.source = source;
	pending.html = sourceHtml;
	pending.context = context;
	pending.view = view;
	pending.index = index;

	TranslationService* service = TranslationService::self();
	QString translated = service->cached(source, context);
	if (translated.isEmpty()) {
		translated = service->cached(legacySourceText(sourceHtml), context);
	}
	if (translated.isEmpty()) {
		translated = service->translate(source, context);
	}
	QToolTip::showText(e->globalPos(), translated == source ? sourceHtml : translatedHtml(sourceHtml, translated), view);
	return true;
}

void handleTranslationReady(const QString& source, const QString& context, const QString& translation, PendingState& pending)
{
	if (source != pending.source || context != pending.context || !pending.view || !pending.view->isVisible()
	    || QApplication::activeWindow() != pending.view->window()
	    || pending.view->indexAt(pending.view->viewport()->mapFromGlobal(QCursor::pos())) != pending.index) {
		return;
	}
	if (pending.index.data(Qt::ToolTipRole).toString() != pending.html) {
		return;
	}
	const QString html = translation.isEmpty() || translation == source ? pending.html : translatedHtml(pending.html, translation);
	QToolTip::showText(QCursor::pos(), html, pending.view);
}

}
