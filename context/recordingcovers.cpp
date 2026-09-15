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

#include "recordingcovers.h"
#include "support/globalstatic.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

// MusicBrainz asks for a descriptive User-Agent identifying the
// application (and ideally a contact) - see
// https://musicbrainz.org/doc/MusicBrainz_API/Rate_Limiting.
static const char musicBrainzUserAgent[] = "Cantata-RecordingCovers/1.0 ( https://github.com/CDrummond/cantata )";
// MusicBrainz asks for at most one request per second; a little headroom
// keeps this comfortably under that even with clock/timer jitter.
static const qint64 musicBrainzMinIntervalMs = 1100;
// How long a negative ("no cover found") result is trusted before a fresh
// lookup is attempted again.
static const qint64 negativeResultTtlMs = qint64(30) * 24 * 3600 * 1000;

namespace {

QString luceneEscape(QString value)
{
	value.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
	value.replace(QLatin1Char('"'), QLatin1String("\\\""));
	return value;
}

QString sha1Hex(const QString& value)
{
	return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1).toHex());
}

}// namespace

GLOBAL_STATIC(RecordingCovers, recordingCoversInstance)

RecordingCovers::RecordingCovers(QObject* parent, const QString& cacheDirectory)
    : QObject(parent)
    , cacheDir(cacheDirectory.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QLatin1String("recording-covers")) : cacheDirectory)
    , network(new QNetworkAccessManager(this))
{
	// Cover Art Archive redirects release/<mbid>/front-250 to an
	// archive.org image URL.
	network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
	QDir().mkpath(cacheDir);
}

RecordingCovers::~RecordingCovers()
{
}

static bool recordingCoversNetworkAccessEnabled = true;

void RecordingCovers::disableNetworkAccess()
{
	recordingCoversNetworkAccessEnabled = false;
}

void RecordingCovers::setNetworkAccessManager(QNetworkAccessManager* manager)
{
	if (!manager || manager == network) return;
	// The manager this object created for itself (if any) stays parented to
	// `this` and is destroyed normally in the destructor; it is simply no
	// longer used to post new requests. An injected manager is owned by its
	// caller and is never deleted here.
	network = manager;
}

QString RecordingCovers::cacheFilePath(const QString& key, const QString& extension) const
{
	return QDir(cacheDir).filePath(sha1Hex(key) + QLatin1Char('.') + extension);
}

QString RecordingCovers::negativeMarkerPath(const QString& key) const
{
	return cacheFilePath(key, QLatin1String("none"));
}

bool RecordingCovers::hasFreshNegativeMarker(const QString& key) const
{
	const QFileInfo info(negativeMarkerPath(key));
	if (!info.exists()) return false;
	return info.lastModified().msecsTo(QDateTime::currentDateTime()) < negativeResultTtlMs;
}

QString RecordingCovers::findExistingCoverFile(const QString& key) const
{
	for (const char* extension : {"jpg", "png"}) {
		const QString path = cacheFilePath(key, QLatin1String(extension));
		if (QFile::exists(path)) return path;
	}
	return QString();
}

QString RecordingCovers::cachedCover(const QString& key) const
{
	if (key.isEmpty()) return QString();

	const auto found = memoryCache.constFind(key);
	if (found != memoryCache.constEnd()) return found.value();

	const QString path = findExistingCoverFile(key);
	if (!path.isEmpty()) memoryCache.insert(key, path);
	return path;
}

void RecordingCovers::request(const QString& key, const QString& performers, const QString& label, const QString& catalogue, const QString& year)
{
	if (key.isEmpty()) return;
	if (!cachedCover(key).isEmpty()) return;
	if (!recordingCoversNetworkAccessEnabled) return;
	if (queuedKeys.contains(key)) return;
	if (hasFreshNegativeMarker(key)) return;

	queuedKeys.insert(key);
	pendingQueue.enqueue({key, performers, label, catalogue, year});
	processQueue();
}

void RecordingCovers::processQueue()
{
	if (busy || pendingQueue.isEmpty()) return;

	busy = true;
	current = pendingQueue.dequeue();
	chosenRelease = ReleaseCandidate();

	const QString query = buildCatalogueQuery(current.catalogue, current.label);
	if (!query.isEmpty()) {
		stage = Stage::CatalogueSearch;
		runMusicBrainzSearch(query);
		return;
	}

	// Without a catalogue number there is nothing that identifies the exact
	// release; an artist/label search picks unrelated releases by the same
	// performer, and a wrong cover is worse than none.
	finishNegative();
}

void RecordingCovers::runMusicBrainzSearch(const QString& query)
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const qint64 elapsed = now - lastMusicBrainzRequestAtMs;
	if (lastMusicBrainzRequestAtMs > 0 && elapsed < musicBrainzMinIntervalMs) {
		QTimer::singleShot(musicBrainzMinIntervalMs - elapsed, this, [this, query]() { sendMusicBrainzSearch(query); });
	}
	else {
		sendMusicBrainzSearch(query);
	}
}

void RecordingCovers::sendMusicBrainzSearch(const QString& query)
{
	lastMusicBrainzRequestAtMs = QDateTime::currentMSecsSinceEpoch();

	QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release/"));
	QUrlQuery urlQuery;
	urlQuery.addQueryItem(QStringLiteral("query"), query);
	urlQuery.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
	urlQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
	url.setQuery(urlQuery);

	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", musicBrainzUserAgent);
	QNetworkReply* reply = network->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleMusicBrainzReply(reply); });
}

void RecordingCovers::handleMusicBrainzReply(QNetworkReply* reply)
{
	reply->deleteLater();

	const QNetworkReply::NetworkError error = reply->error();
	const QByteArray body = QNetworkReply::NoError == error ? reply->readAll() : QByteArray();
	const QList<ReleaseCandidate> candidates = parseReleaseSearchResponse(body);

	if (candidates.isEmpty()) {
		finishNegative();
		return;
	}

	chosenRelease = selectBestRelease(candidates, normaliseCatalogueNumber(current.catalogue));
	if (chosenRelease.id.isEmpty()) {
		finishNegative();
		return;
	}

	stage = Stage::ReleaseCover;
	fetchCoverArt(QUrl(QStringLiteral("https://coverartarchive.org/release/%1/front-250").arg(chosenRelease.id)));
}

void RecordingCovers::fetchCoverArt(const QUrl& url)
{
	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", musicBrainzUserAgent);
	QNetworkReply* reply = network->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleCoverArtReply(reply); });
}

void RecordingCovers::handleCoverArtReply(QNetworkReply* reply)
{
	reply->deleteLater();

	if (QNetworkReply::NoError == reply->error()) {
		const QByteArray data = reply->readAll();
		if (!data.isEmpty()) {
			const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
			saveImageAndFinish(data, contentType);
			return;
		}
	}

	if (Stage::ReleaseCover == stage && !chosenRelease.releaseGroupId.isEmpty()) {
		stage = Stage::ReleaseGroupCover;
		fetchCoverArt(QUrl(QStringLiteral("https://coverartarchive.org/release-group/%1/front-250").arg(chosenRelease.releaseGroupId)));
		return;
	}

	finishNegative();
}

void RecordingCovers::saveImageAndFinish(const QByteArray& data, const QString& contentType)
{
	const QString extension = contentType.contains(QLatin1String("png"), Qt::CaseInsensitive) ? QLatin1String("png") : QLatin1String("jpg");
	const QString path = cacheFilePath(current.key, extension);

	QDir().mkpath(cacheDir);
	QSaveFile file(path);
	if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit()) {
		memoryCache.insert(current.key, path);
		QFile::remove(negativeMarkerPath(current.key));
		emit coverReady(current.key, path);
	}

	finishCurrentRequest();
}

void RecordingCovers::finishNegative()
{
	QDir().mkpath(cacheDir);
	QSaveFile marker(negativeMarkerPath(current.key));
	if (marker.open(QIODevice::WriteOnly)) {
		marker.commit();
	}
	finishCurrentRequest();
}

void RecordingCovers::finishCurrentRequest()
{
	queuedKeys.remove(current.key);
	current = PendingRequest();
	chosenRelease = ReleaseCandidate();
	busy = false;
	QTimer::singleShot(0, this, &RecordingCovers::processQueue);
}

// ---------------------------------------------------------------------------
// Pure helpers - see context/recordingcovers.h for documentation, and
// tests/recordingcovers_test.cpp for canned-JSON coverage.

QString RecordingCovers::normaliseCatalogueNumber(const QString& catalogue)
{
	QString result;
	result.reserve(catalogue.size());
	for (const QChar& ch : catalogue) {
		if (ch.isLetterOrNumber()) result.append(ch.toUpper());
	}
	return result;
}

QString RecordingCovers::primaryLabelToken(const QString& label)
{
	const QString trimmed = label.trimmed();
	const int slash = trimmed.indexOf(QLatin1Char('/'));
	return slash < 0 ? trimmed : trimmed.left(slash).trimmed();
}

QString RecordingCovers::buildCatalogueQuery(const QString& catalogue, const QString& label)
{
	const QString catalogueTrimmed = catalogue.trimmed();
	const QString labelToken = primaryLabelToken(label);

	QString catalogueClause;
	if (!catalogueTrimmed.isEmpty()) {
		QString stripped = catalogueTrimmed;
		stripped.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")));
		if (!stripped.isEmpty() && 0 != stripped.compare(catalogueTrimmed, Qt::CaseInsensitive)) {
			catalogueClause = QStringLiteral("(catno:\"%1\" OR catno:\"%2\")").arg(luceneEscape(catalogueTrimmed), luceneEscape(stripped));
		}
		else {
			catalogueClause = QStringLiteral("catno:\"%1\"").arg(luceneEscape(catalogueTrimmed));
		}
	}

	QStringList clauses;
	if (!catalogueClause.isEmpty()) clauses << catalogueClause;
	if (!labelToken.isEmpty()) clauses << QStringLiteral("label:\"%1\"").arg(luceneEscape(labelToken));
	return clauses.join(QLatin1String(" AND "));
}

QList<RecordingCovers::ReleaseCandidate> RecordingCovers::parseReleaseSearchResponse(const QByteArray& json)
{
	QList<ReleaseCandidate> result;

	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
	if (QJsonParseError::NoError != parseError.error || !doc.isObject()) return result;

	const QJsonArray releases = doc.object().value(QLatin1String("releases")).toArray();
	result.reserve(releases.size());
	for (const QJsonValue& releaseValue : releases) {
		if (!releaseValue.isObject()) continue;
		const QJsonObject release = releaseValue.toObject();

		ReleaseCandidate candidate;
		candidate.id = release.value(QLatin1String("id")).toString();
		if (candidate.id.isEmpty()) continue;

		candidate.score = release.value(QLatin1String("score")).toInt();
		candidate.releaseGroupId = release.value(QLatin1String("release-group")).toObject().value(QLatin1String("id")).toString();

		for (const QJsonValue& labelInfoValue : release.value(QLatin1String("label-info")).toArray()) {
			const QString catalogNumber = labelInfoValue.toObject().value(QLatin1String("catalog-number")).toString();
			if (!catalogNumber.isEmpty()) candidate.catalogNumbers.append(catalogNumber);
		}

		if (release.contains(QLatin1String("cover-art-archive"))) {
			const QJsonObject coverArtArchive = release.value(QLatin1String("cover-art-archive")).toObject();
			candidate.hasCoverArtFrontField = coverArtArchive.contains(QLatin1String("front"));
			candidate.coverArtFront = coverArtArchive.value(QLatin1String("front")).toBool(false);
		}

		result.append(candidate);
	}
	return result;
}

RecordingCovers::ReleaseCandidate RecordingCovers::selectBestRelease(const QList<ReleaseCandidate>& candidates, const QString& wantedCatalogue)
{
	if (candidates.isEmpty()) return ReleaseCandidate();

	if (!wantedCatalogue.isEmpty()) {
		const ReleaseCandidate* withFront = nullptr;
		const ReleaseCandidate* first = nullptr;
		for (const ReleaseCandidate& candidate : candidates) {
			for (const QString& catalogNumber : candidate.catalogNumbers) {
				if (normaliseCatalogueNumber(catalogNumber) == wantedCatalogue) {
					if (!first) first = &candidate;
					if (!withFront && candidate.hasCoverArtFrontField && candidate.coverArtFront) withFront = &candidate;
					break;
				}
			}
		}
		if (withFront) return *withFront;
		if (first) return *first;
	}

	// Only an exact catalogue number identifies the recording; anything else
	// risks showing the cover of a different release.
	return ReleaseCandidate();
}
