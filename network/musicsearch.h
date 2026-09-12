/* Cantata - cached asynchronous Chinese search expansion. */
#ifndef MUSIC_SEARCH_H
#define MUSIC_SEARCH_H

#include <QObject>
#include <QStringList>

class TranslationService;

class MusicSearch : public QObject {
	Q_OBJECT
public:
	static MusicSearch* self();
	explicit MusicSearch(QObject* parent = nullptr, TranslationService* service = nullptr);
	static bool containsChinese(const QString& text);
	// Always includes the original term. Cache misses return immediately and
	// notify subscribers when multilingual alternatives become available.
	QStringList alternatives(const QString& term);

Q_SIGNALS:
	void alternativesReady(const QString& term);

private:
	TranslationService* translator;
};

#endif
