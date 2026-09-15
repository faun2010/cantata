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
// recording, keyed by an opaque, caller-supplied string. Looks releases up
// on MusicBrainz (by catalogue number/label, falling back to
// artist/label), then fetches the front cover from the Cover Art Archive,
// and caches the result (positive or negative) on disk so the same
// recording is never looked up twice.
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
	void setNetworkAccessManager(QNetworkAccessManager* manager);

	// Local file path of a previously downloaded cover for "key", or an
	// empty string when nothing has been fetched (successfully) yet -
	// including while a negative result is cached, i.e. a previous lookup
	// genuinely found no cover.
	QString cachedCover(const QString& key) const;

	// Queues an asynchronous MusicBrainz + Cover Art Archive lookup for a
	// recommended recording ("performers" is a free-form string such as
	// "Artur Rubinstein, Daniel Barenboim"; "year" is currently unused by
	// the lookup itself but accepted for interface stability/future use).
	// A no-op when "key" is already cached (positive, or an unexpired
	// negative result) or already queued/in flight.
	void request(const QString& key, const QString& performers, const QString& label, const QString& catalogue, const QString& year);

Q_SIGNALS:
	void coverReady(const QString& key, const QString& localPath);

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


	// A single candidate release from a MusicBrainz release search
	// response.
	struct ReleaseCandidate {
		QString id;
		QString releaseGroupId;
		QStringList catalogNumbers;
		int score = 0;
		bool coverArtFront = false;
		bool hasCoverArtFrontField = false;
	};

	// Parses a MusicBrainz "release/?fmt=json" search response, preserving
	// the server's score-descending order. Returns an empty list for
	// malformed JSON, a non-object document, or a response with no/empty
	// "releases" array; entries without an "id" are skipped.
	static QList<ReleaseCandidate> parseReleaseSearchResponse(const QByteArray& json);

	// Picks the best release from "candidates": one whose own reported
	// catalogue number normalises (see normaliseCatalogueNumber()) to
	// exactly "wantedCatalogue" wins outright, whatever its position in
	// the list; otherwise the first candidate (MusicBrainz's own score
	// order) whose cover-art-archive.front field is true; failing that,
	// simply the first candidate. "wantedCatalogue" may be empty, in which
	// case only the cover-art-archive/first-candidate rules apply. Returns
	// a default-constructed (empty id) candidate when "candidates" is
	// empty.
	static ReleaseCandidate selectBestRelease(const QList<ReleaseCandidate>& candidates, const QString& wantedCatalogue);

private:
	struct PendingRequest {
		QString key;
		QString performers;
		QString label;
		QString catalogue;
		QString year;
	};

	enum class Stage { CatalogueSearch, ReleaseCover, ReleaseGroupCover };

	QString cacheFilePath(const QString& key, const QString& extension) const;
	QString negativeMarkerPath(const QString& key) const;
	bool hasFreshNegativeMarker(const QString& key) const;
	QString findExistingCoverFile(const QString& key) const;

	void processQueue();
	void runMusicBrainzSearch(const QString& query);
	void sendMusicBrainzSearch(const QString& query);
	void handleMusicBrainzReply(QNetworkReply* reply);
	void fetchCoverArt(const QUrl& url);
	void handleCoverArtReply(QNetworkReply* reply);
	void saveImageAndFinish(const QByteArray& data, const QString& contentType);
	void finishNegative();
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

	qint64 lastMusicBrainzRequestAtMs = 0;
};

#endif
