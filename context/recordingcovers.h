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

#ifndef RECORDING_COVERS_H
#define RECORDING_COVERS_H

// Fetches small album-cover thumbnails for the "Recommended Recordings"
// section of the classical work introduction (see
// context/recommendedrecordings.h) - one per curated/AI-suggested
// recording, keyed by an opaque, caller-supplied string. Searches MusicBrainz
// by catalogue number, then by work/composer/performers when necessary.
// Only matching releases are used for Cover Art Archive thumbnails. Cached
// images have a sidecar identifying the specific edition; misses expire.
//
// Like network/translationservice.h, this deliberately talks to its own
// private QNetworkAccessManager rather than NetworkAccessManager::self()
// (see network/contextwidget.cpp's getMusicbrainzId() for that heavier
// pattern): NetworkAccessManager pulls in gui/settings.h, and from there
// the whole MPD client, which would drag support/utils.cpp's Utils::cacheDir()
// and network/networkaccessmanager.cpp into the lightweight
// tests/CMakeLists.txt project used to unit test the pure helpers below
// (see tests/recordingcovers_test.cpp). setNetworkAccessManager() lets the
// application inject its shared, proxy-aware manager instead, exactly as
// TranslationService::setNetworkAccessManager() does.
//
// The release-selection logic (MusicBrainz JSON in -> chosen release/
// release-group id) and catalogue/label/artist normalisation are pure
// static functions, so they can be unit tested with canned MusicBrainz
// responses without any network access.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

class RecordingCovers : public QObject {
	Q_OBJECT

public:
	static RecordingCovers* self();

	// "cacheDirectory" is intended for isolated tests; the default is the
	// user's cache location, "recording-covers" sub-folder.
	explicit RecordingCovers(QObject* parent = nullptr, const QString& cacheDirectory = QString());
	~RecordingCovers() override;

	// Lets the application route lookups through its shared, proxy-aware
	// QNetworkAccessManager instead of the private one this object creates
	// for itself. The previously used manager, if owned by this object, is
	// left alone and destroyed as usual when this object is destroyed; an
	// injected manager is never deleted here.
	// Stops new lookups, e.g. for --no-network; cached covers still load.
	static void disableNetworkAccess();
	static bool networkAccessEnabled();
	void setNetworkAccessManager(QNetworkAccessManager* manager);

	// Local file path of a previously downloaded cover for "key", or an
	// empty string when nothing has been fetched (successfully) yet -
	// including while a negative result is cached, i.e. a previous lookup
	// genuinely found no cover.
	QString cachedCover(const QString& key) const;
	QString cachedSourceUrl(const QString& key) const;

	// Queues an asynchronous lookup. Work context enables a conservative
	// fallback when the exact catalogue is absent or has no cover. The year
	// prefers dated matches; release dates may refer to later reissues.
	// Already cached, queued or temporarily suppressed keys are a no-op.

	void request(const QString& key, const QString& performers, const QString& label, const QString& catalogue, const QString& year,
	             const QString& composer = QString(), const QString& work = QString());

Q_SIGNALS:
	void coverReady(const QString& key, const QString& localPath);
	void requestFinished(const QString& key, const QString& status);

public:
	// ---- Pure, unit-testable helpers (see tests/recordingcovers_test.cpp) ----

	// Upper-cased, letters/digits-only form of a catalogue number, e.g.
	// "ARL1-4711" and "arl1 4711" both become "ARL14711". Used both to
	// compare a candidate release's own reported catalogue numbers, and to
	// build the "spaces/hyphens removed" query variant.
	static QString normaliseCatalogueNumber(const QString& catalogue);

	// A dataset label such as "RCA/Analogue Productions" or
	// "Columbia/Impex" names two real-world labels joined for display;
	// MusicBrainz's own label:"..." search field only ever matches one
	// actual label name, so queries use just the first component ("RCA",
	// "Columbia").
	static QString primaryLabelToken(const QString& label);


	// Lucene query for the MusicBrainz "release" search, combining a
	// catalogue number - as given, and with spaces/hyphens removed, when
	// that differs - with a label (see primaryLabelToken()). Either
	// "catalogue" or "label" may be empty; returns an empty string when
	// both are.
	static QString buildCatalogueQuery(const QString& catalogue, const QString& label);
	static QString buildWorkQuery(const QString& composer, const QString& work, const QString& performers);


	// A single candidate release from a MusicBrainz release search
	// response.
	struct ReleaseCandidate {
		QString id;
		QString releaseGroupId;
		QStringList catalogNumbers;
		QString title;
		QStringList artists;
		QStringList labels;
		QString date;
		QString disambiguation;
		QString status;
		int score = 0;
		bool coverArtFront = false;
		bool hasCoverArtFrontField = false;
	};

	// Parses a MusicBrainz "release/?fmt=json" search response, preserving
	// the server's score-descending order. Returns an empty list for
	// malformed JSON, a non-object document, or a response with no/empty
	// "releases" array; entries without an "id" are skipped.
	static QList<ReleaseCandidate> parseReleaseSearchResponse(const QByteArray& json);

	// Selects only an exact normalised catalogue match, preferring a known
	// front cover. No exact match returns an empty candidate.

	static ReleaseCandidate selectBestRelease(const QList<ReleaseCandidate>& candidates, const QString& wantedCatalogue);
	static QList<ReleaseCandidate> matchingWorkReleases(const QList<ReleaseCandidate>& candidates, const QString& composer,
	                                                  const QString& work, const QString& performers, const QString& label, const QString& year);

private:
	struct PendingRequest {
		QString key;
		QString performers;
		QString label;
		QString catalogue;
		QString year;
		QString composer;
		QString work;
	};

	enum class Stage { CatalogueSearch, WorkSearch, ReleaseCover };

	QString cacheFilePath(const QString& key, const QString& extension) const;
	QString negativeMarkerPath(const QString& key) const;
	bool hasFreshNegativeMarker(const QString& key) const;
	QString findExistingCoverFile(const QString& key) const;

	void processQueue();
	void searchByWork();
	void fetchNextRelease();
	void runMusicBrainzSearch(const QString& query);
	void sendMusicBrainzSearch(const QString& query);
	void handleMusicBrainzReply(QNetworkReply* reply);
	void fetchCoverArt(const QUrl& url);
	void handleCoverArtReply(QNetworkReply* reply);
	bool saveImageAndFinish(const QByteArray& data);
	void finishNegative(bool transient = false);
	void finishCurrentRequest();

	QString cacheDir;
	QNetworkAccessManager* network;

	QQueue<PendingRequest> pendingQueue;
	QSet<QString> queuedKeys;
	mutable QHash<QString, QString> memoryCache;

	bool busy = false;
	PendingRequest current;
	Stage stage = Stage::CatalogueSearch;
	ReleaseCandidate chosenRelease;
	QList<ReleaseCandidate> remainingReleases;
	bool workSearchTried = false;
	bool transientFailure = false;
	QString resultStatus;

	qint64 lastMusicBrainzRequestAtMs = 0;
};

#endif
