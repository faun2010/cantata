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

#ifndef TRANSLATION_SERVICE_H
#define TRANSLATION_SERVICE_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class QNetworkReply;
class QNetworkAccessManager;
class QTimer;

class TranslationService : public QObject {
	Q_OBJECT

public:
	static TranslationService* self();
	static void disableNetworkAccess();

	// The optional paths are intended for isolated tests and portable builds.
	explicit TranslationService(QObject* parent = nullptr, const QString& configurationFile = QString(), const QString& cacheDirectory = QString());
	~TranslationService() override;

	// Source and emitted translations are plain text. Returns a cached
	// translation, or source immediately while an asynchronous
	// request is queued. Callers interested in the result connect to
	// translationReady and compare both source and context with current state.
	QString translate(const QString& source, const QString& context = QString());
	// Read an existing result without scheduling a request. Used to reuse old
	// tooltip caches while omitting file paths from all new translation requests.
	QString cached(const QString& source, const QString& context = QString()) const;
	static QString plainTextToHtml(QString text);
	void reloadConfiguration();

	QString configurationFilePath() const { return configFile; }
	QString translationCacheDirectory() const { return cacheDir; }
	bool isEnabled() const { return enabled; }

Q_SIGNALS:
	void translationReady(const QString& source, const QString& context, const QString& translation);

private Q_SLOTS:
	void requestFinished();

private:
	struct Request {
		QString key;
		QString pendingToken;
		QString source;
		QString context;
		QString provider;
		quint64 generation = 0;
		QTimer* timer = nullptr;
		// Set when a newer, more specific request (same context) has taken
		// over for this one; finishRequest() then discards it silently.
		bool superseded = false;
	};

	QString cacheKey(const QString& source, const QString& context) const;
	QString cachedTranslation(const QString& key) const;
	void storeTranslation(const QString& key, const QString& translation);
	void startRequest(Request request);
	void startQueuedRequests();
	void finishRequest(QNetworkReply* reply, bool timedOut = false);
	void createDefaultConfiguration() const;
	QString endpoint() const;
	// Drops/aborts still-pending requests in the same context whose source is
	// a strict prefix of, or is prefixed by, the incoming source. Keeps a
	// burst of IME keystrokes (e.g. 贝 -> 贝多 -> 贝多芬) from queuing up
	// stale intermediate search terms behind the final one.
	void supersedeRelatedRequests(const QString& source, const QString& context);

	QString configFile;
	QString cacheDir;
	QString provider;
	QString url;
	QString model;
	QString targetLanguage;
	QString promptVersion;
	QString apiKey;
	QNetworkAccessManager* network;
	bool enabled = true;
	int timeoutMs = 180000;
	int cooldownSeconds = 30;
	int maxMemoryEntries = 512;
	int maxConcurrentRequests = 1;
	int maxQueuedRequests = 16;
	quint64 configurationGeneration = 0;
	mutable QHash<QString, QString> memoryCache;
	mutable QList<QString> memoryOrder;
	QHash<QString, qint64> failures;
	// Set for ~60s after a connection-level failure (refused, host not
	// found, timed out, ...) so every context fails fast instead of each
	// (source, context) pair separately retrying a dead endpoint.
	qint64 endpointUnavailableUntil = 0;
	QHash<QNetworkReply*, Request> requests;
	QList<Request> queuedRequests;
	QSet<QString> pendingTokens;
};

#endif
