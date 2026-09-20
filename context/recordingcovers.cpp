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
#include <QImage>
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
static const qint64 negativeResultTtlMs = qint64(24) * 3600 * 1000;

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

QStringList words(QString value)
{
	value = value.normalized(QString::NormalizationForm_D).toCaseFolded();
	value.remove(QRegularExpression(QStringLiteral("[\\p{M}]")));
	return value.split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts);
}

QStringList workWords(QString value)
{
	// Recording metadata may omit the key, opus number, and number marker.
	// Preserve the work's own number: Symphony 5 must never match Symphony 7.
	value.remove(QRegularExpression(QStringLiteral("\\b(?:op|bwv|kv?|hob|rv|woo)\\.?\\s*[0-9]+(?:[.:][0-9]+)*"), QRegularExpression::CaseInsensitiveOption));
	const QSet<QString> ignored = {"the", "a", "an", "der", "die", "das", "in", "no", "nos", "nr", "major", "minor", "flat", "sharp", "b", "c", "d", "e", "f", "g"};
	QStringList result;
	for (QString word : words(value)) {
		if (ignored.contains(word)) continue;
		if (word == "symphonies") word = "symphony";
		else if (word == "concertos") word = "concerto";
		else if (word == "sonatas") word = "sonata";
		else if (word == "quartets") word = "quartet";
		result << word;
	}
	return result;
}

bool includesWords(const QStringList& actual, const QStringList& wanted)
{
	if (wanted.isEmpty()) return false;
	for (const QString& word : wanted) if (!actual.contains(word)) return false;
	return true;
}

bool credited(const QStringList& artists, const QString& wanted)
{
	for (const QString& artist : artists) if (includesWords(words(artist), words(wanted))) return true;
	return false;
}

QStringList performerNames(const QString& performers)
{
	return performers.split(QRegularExpression(QStringLiteral("\\s*[,;/·]\\s*")), Qt::SkipEmptyParts);
}

}// namespace

GLOBAL_STATIC(RecordingCovers, recordingCoversInstance)

RecordingCovers::RecordingCovers(QObject* parent, const QString& cacheDirectory)
    : QObject(parent)
    , cacheDir(cacheDirectory.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QLatin1String("recording-covers-v2")) : cacheDirectory)
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

bool RecordingCovers::networkAccessEnabled()
{
	return recordingCoversNetworkAccessEnabled;
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
	QFile marker(info.filePath());
	const bool transient = marker.open(QIODevice::ReadOnly) && marker.readAll() == "retry";
	return info.lastModified().msecsTo(QDateTime::currentDateTime()) < (transient ? 5 * 60 * 1000 : negativeResultTtlMs);
}

QString RecordingCovers::findExistingCoverFile(const QString& key) const
{
	for (const char* extension : {"jpg", "png"}) {
		const QString path = cacheFilePath(key, QLatin1String(extension));
		if (QFile::exists(path)) return path;
	}
	return QString();
}

QString RecordingCovers::cachedSourceUrl(const QString& key) const
{
	QFile file(cacheFilePath(key, QLatin1String("json")));
	if (!file.open(QIODevice::ReadOnly)) return QString();
	return QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("source")).toString();
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

void RecordingCovers::request(const QString& key, const QString& performers, const QString& label, const QString& catalogue, const QString& year, const QString& composer, const QString& work)
{
	if (key.isEmpty()) return;
	if (!cachedCover(key).isEmpty()) { emit requestFinished(key, QStringLiteral("cached")); return; }
	if (!recordingCoversNetworkAccessEnabled) { emit requestFinished(key, QStringLiteral("disabled")); return; }
	if (queuedKeys.contains(key)) return;
	if (hasFreshNegativeMarker(key)) { emit requestFinished(key, QStringLiteral("deferred")); return; }

	queuedKeys.insert(key);
	pendingQueue.enqueue({key, performers, label, catalogue, year, composer, work});
	processQueue();
}

void RecordingCovers::processQueue()
{
	if (busy || pendingQueue.isEmpty()) return;

	busy = true;
	current = pendingQueue.dequeue();
	chosenRelease = ReleaseCandidate();
	remainingReleases.clear();
	workSearchTried = false;
	transientFailure = false;
	resultStatus.clear();

	const QString query = buildCatalogueQuery(current.catalogue, current.label);
	if (!current.catalogue.trimmed().isEmpty() && !query.isEmpty()) {
		stage = Stage::CatalogueSearch;
		runMusicBrainzSearch(query);
		return;
	}

	searchByWork();
}

void RecordingCovers::searchByWork()
{
	if (workSearchTried) {
		finishNegative(transientFailure);
		return;
	}
	workSearchTried = true;
	const QString query = buildWorkQuery(current.composer, current.work, current.performers);
	if (query.isEmpty()) {
		finishNegative(transientFailure);
		return;
	}
	stage = Stage::WorkSearch;
	runMusicBrainzSearch(query);
}

void RecordingCovers::fetchNextRelease()
{
	if (remainingReleases.isEmpty()) {
		searchByWork();
		return;
	}
	chosenRelease = remainingReleases.takeFirst();
	stage = Stage::ReleaseCover;
	fetchCoverArt(QUrl(QStringLiteral("https://coverartarchive.org/release/%1/front-250").arg(chosenRelease.id)));
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
	urlQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("30"));
	url.setQuery(urlQuery);

	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", musicBrainzUserAgent);
	request.setTransferTimeout(15000);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply* reply = network->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleMusicBrainzReply(reply); });
}

void RecordingCovers::handleMusicBrainzReply(QNetworkReply* reply)
{
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError) {
		finishNegative(true);
		return;
	}
	QJsonParseError error;
	const QByteArray body = reply->readAll();
	const auto document = QJsonDocument::fromJson(body, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value("releases").isArray()) {
		finishNegative(true);
		return;
	}
	const QList<ReleaseCandidate> candidates = parseReleaseSearchResponse(body);
	if (stage == Stage::CatalogueSearch) {
		const ReleaseCandidate exact = selectBestRelease(candidates, normaliseCatalogueNumber(current.catalogue));
		if (!exact.id.isEmpty()) remainingReleases << exact;
	}
	else {
		remainingReleases = matchingWorkReleases(candidates, current.composer, current.work, current.performers, current.label, current.year);
		// Bound the amount of cover probing for one recommendation.
		while (remainingReleases.size() > 3) remainingReleases.removeLast();
	}
	fetchNextRelease();
}

void RecordingCovers::fetchCoverArt(const QUrl& url)
{
	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", musicBrainzUserAgent);
	request.setTransferTimeout(15000);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply* reply = network->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleCoverArtReply(reply); });
}

void RecordingCovers::handleCoverArtReply(QNetworkReply* reply)
{
	reply->deleteLater();

	if (QNetworkReply::NoError == reply->error()) {
		if (saveImageAndFinish(reply->readAll())) return;
		transientFailure = true;
	}
	else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 404) {
		transientFailure = true;
	}

	// Use only verified release candidates. A release-group cover can be a
	// different edition from the source link shown beside the thumbnail.
	fetchNextRelease();
}

bool RecordingCovers::saveImageAndFinish(const QByteArray& data)
{
	QImage image = QImage::fromData(data);
	if (image.isNull() || image.width() < 32 || image.height() < 32) return false;
	image = image.scaled(250, 250, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	const QString path = cacheFilePath(current.key, QLatin1String("jpg"));
	QDir().mkpath(cacheDir);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "JPEG") || !file.commit()) return false;
	QSaveFile metadata(cacheFilePath(current.key, QLatin1String("json")));
	if (metadata.open(QIODevice::WriteOnly)) {
		metadata.write(QJsonDocument(QJsonObject{{"source", QStringLiteral("https://musicbrainz.org/release/") + chosenRelease.id},
		                                         {"title", chosenRelease.title}, {"date", chosenRelease.date}}).toJson());
		metadata.commit();
	}
	memoryCache.insert(current.key, path);
	QFile::remove(negativeMarkerPath(current.key));
	emit coverReady(current.key, path);
	resultStatus = QStringLiteral("downloaded");
	finishCurrentRequest();
	return true;
}

void RecordingCovers::finishNegative(bool transient)
{
	resultStatus = transient ? QStringLiteral("retry") : QStringLiteral("missing");
	// Network outages are retried after five minutes, genuine misses after
	// one day. Persist both, so rebuilding the view cannot loop requests.
	QDir().mkpath(cacheDir);
	QSaveFile marker(negativeMarkerPath(current.key));
	if (marker.open(QIODevice::WriteOnly)) {
		marker.write(transient ? "retry" : "missing");
		marker.commit();
	}
	finishCurrentRequest();
}

void RecordingCovers::finishCurrentRequest()
{
	const QString key = current.key;
	const QString status = resultStatus;
	queuedKeys.remove(current.key);
	current = PendingRequest();
	chosenRelease = ReleaseCandidate();
	busy = false;
	emit requestFinished(key, status);
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

		candidate.title = release.value("title").toString();
		candidate.date = release.value("date").toString();
		candidate.status = release.value("status").toString();
		candidate.disambiguation = release.value("disambiguation").toString() + QLatin1Char(' ') + release.value("release-group").toObject().value("disambiguation").toString();
		for (const QJsonValue& credit : release.value("artist-credit").toArray()) {
			candidate.artists << credit.toObject().value("name").toString()
			                  << credit.toObject().value("artist").toObject().value("name").toString();
		}
		candidate.score = release.value(QLatin1String("score")).toInt();
		candidate.releaseGroupId = release.value(QLatin1String("release-group")).toObject().value(QLatin1String("id")).toString();

		for (const QJsonValue& labelInfoValue : release.value(QLatin1String("label-info")).toArray()) {
			candidate.labels << labelInfoValue.toObject().value("label").toObject().value("name").toString();
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

QString RecordingCovers::buildWorkQuery(const QString& composer, const QString& work, const QString& performers)
{
	const QStringList names = performerNames(performers);
	const QStringList title = workWords(work);
	if (composer.trimmed().isEmpty() || names.isEmpty() || title.isEmpty()) return QString();
	QString term;
	for (const QString& word : work.split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts)) {
		if (word.size() > term.size() && word.at(0).isLetter()) term = word;
	}
	if (term.isEmpty()) return QString();
	QString releaseTerm = QStringLiteral("release:\"%1\"").arg(luceneEscape(term));
	if (term.startsWith("symphon", Qt::CaseInsensitive)) releaseTerm = QStringLiteral("release:symphon*");
	return QStringLiteral("artist:\"%1\" AND artist:\"%2\" AND %3")
	    .arg(luceneEscape(composer.simplified().section(QLatin1Char(' '), -1)),
	         luceneEscape(names.first().simplified().section(QLatin1Char(' '), -1)), releaseTerm);
}

QList<RecordingCovers::ReleaseCandidate> RecordingCovers::matchingWorkReleases(const QList<ReleaseCandidate>& candidates,
    const QString& composer, const QString& work, const QString& performers, const QString& label, const QString& year)
{
	QList<ReleaseCandidate> matches;
	const QStringList names = performerNames(performers);
	const QStringList title = workWords(work);
	if (composer.isEmpty() || names.isEmpty() || title.isEmpty()) return matches;
	for (const ReleaseCandidate& candidate : candidates) {
		if (!candidate.status.isEmpty() && candidate.status != QLatin1String("Official")) continue;
		if (!credited(candidate.artists, composer) || !includesWords(workWords(candidate.title), title)) continue;
		bool allPerformers = true;
		for (const QString& name : names) allPerformers = allPerformers && credited(candidate.artists, name);
		if (!allPerformers) continue;
		if (!label.trimmed().isEmpty() && !credited(candidate.labels, primaryLabelToken(label))) continue;
		if (QRegularExpression(QStringLiteral("^[0-9]{4}$")).match(year).hasMatch()
		    && QRegularExpression(QStringLiteral("\\b(?:18|19|20)[0-9]{2}\\b")).match(candidate.disambiguation).hasMatch()
		    && !QRegularExpression(QStringLiteral("\\b%1\\b").arg(year)).match(candidate.disambiguation).hasMatch()) continue;
		matches << candidate;
	}
	// Prefer dated evidence when available; release dates may be reissue dates.
	QList<ReleaseCandidate> dated;
	if (QRegularExpression(QStringLiteral("^[0-9]{4}$")).match(year).hasMatch()) {
		const QRegularExpression date(QStringLiteral("\\b%1\\b").arg(year));
		for (const ReleaseCandidate& candidate : matches) {
			if (date.match(candidate.disambiguation).hasMatch() || candidate.date.left(4) == year) dated << candidate;
		}
	}
	if (!dated.isEmpty()) matches = dated;
	QSet<QString> groups;
	for (const ReleaseCandidate& candidate : matches) groups.insert(candidate.releaseGroupId.isEmpty() ? candidate.id : candidate.releaseGroupId);
	// Distinct albums with the same credited performers can be different
	// sessions. Do not choose between those without enough identifying data.
	return groups.size() == 1 ? matches : QList<ReleaseCandidate>();
}
