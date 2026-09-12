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
		if (context == searchContext && SearchTerms::alternatives(source, response).size() > 1) {
			emit alternativesReady(source);
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

#include "moc_musicsearch.cpp"
