#include "musicsearch.h"
#include "translationservice.h"
#include "support/globalstatic.h"
#include "support/searchterms.h"

GLOBAL_STATIC(MusicSearch, musicSearchInstance)

static const QString searchContext = QStringLiteral("music-search-v1");

MusicSearch::MusicSearch(QObject* parent, TranslationService* service)
	: QObject(parent), translator(service ? service : TranslationService::self())
{
	connect(translator, &TranslationService::translationReady, this,
	        [this](const QString& source, const QString& context, const QString& response) {
		if (context == searchContext) {
			if (SearchTerms::alternatives(source, response).size() > 1) emit alternativesReady(source);
			emit alternativesFinished(source);
		}
	});
}

bool MusicSearch::containsChinese(const QString& text)
{
	return SearchTerms::containsChinese(text);
}

QStringList MusicSearch::alternatives(const QString& term)
{
	if (!containsChinese(term) || term.size() > 160 || term.contains(QLatin1Char('/')) || term.contains(QLatin1Char('\\'))) {
		return { term };
	}
	return SearchTerms::alternatives(term, translator->translate(term, searchContext));
}

bool MusicSearch::isPending(const QString& term) const
{
	return translator->isPending(term, searchContext);
}

void MusicSearch::setQuery(QObject* owner, const QStringList& terms)
{
	if (!queryTerms.contains(owner)) {
		connect(owner, &QObject::destroyed, this, [this, owner]() {
			setQuery(owner, {});
			queryTerms.remove(owner);
		});
	}
	const QSet<QString> previous = queryTerms.value(owner);
	QSet<QString> current;
	for (const QString& term : terms) if (containsChinese(term)) current.insert(term);
	queryTerms.insert(owner, current);
	for (const QString& old : previous - current) {
		bool needed = false;
		for (auto it = queryTerms.constBegin(); it != queryTerms.constEnd(); ++it) {
			if (it.value().contains(old)) { needed = true; break; }
		}
		if (!needed) translator->cancel(old, searchContext);
	}
}

#include "moc_musicsearch.cpp"
