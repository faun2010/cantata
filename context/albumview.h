/*
 * Cantata
 *
 * Copyright (c) 2011-2022 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef ALBUM_VIEW_H
#define ALBUM_VIEW_H

#include "recommendedrecordings.h"
#include "view.h"
#include "workdossier.h"
#include "workinfo.h"
#include <QList>

class QImage;
class NetworkJob;
class QByteArray;
class QUrl;
class QJsonObject;
class ContextEngine;
class RecordingCoverFetcher;
class Action;
class QAction;

class AlbumView : public View {
	Q_OBJECT
public:
	static const QLatin1String constCacheDir;
	static const QLatin1String constInfoExt;
	static const QLatin1String constWorkCacheDir;

	AlbumView(QWidget* p);
	~AlbumView() override;

	void update(const Song& song, bool force = false) override;

Q_SIGNALS:
	void playSong(const QString& file);
	// Emitted when a "Recommended Recordings" library link is clicked - see
	// ArtistView::findAlbum() for the sibling signal this mirrors.
	void findAlbum(const QString& artist, const QString& albumId);

public Q_SLOTS:
	void coverRetrieved(const Song& s, const QImage& img, const QString& file);
	void coverUpdated(const Song& s, const QImage& img, const QString& file);
	void playSong(const QUrl& u);

private Q_SLOTS:
	void showContextMenu(const QPoint& pos);
	void refresh();
	void clearCache();
	void searchResponse(const QString& resp, const QString& lang);
	void detailsTranslationReady(const QString& source, const QString& context, const QString& translation);
	void workIntroTranslationReady(const QString& source, const QString& context, const QString& translation);
	void workSearchFinished();
	void workPagePropsFinished();
	void workSummaryFinished();
	void showOriginalToggled();
	void recommendedRecordingsTranslationReady(const QString& source, const QString& context, const QString& translation);
	void workDossierTranslationReady(const QString& source, const QString& context, const QString& translation);
	void workExtractsFinished();
	void recordingCoverReady(const QString& key, const QString& localPath);

private:
	void clearDetails();
	void getTrackListing();
	void getDetails();
	void updateDetails(bool preservePos = false);
	QString buildWorkIntroductionSection(bool showOriginal) const;
	QString renderStructuredIntroduction(const WorkDossier::Introduction& intro) const;
	bool workDossierPending() const;
	void abort() override;

	// Priority-1/2 "work introduction" lookup - see context/workinfo.h for
	// the pure parsing/derivation logic.
	QString workCacheFileName(bool createDir) const;
	void updateWorkIntroductionSource();
	void loadWorkFromCacheOrNetwork();
	void startWorkSearch();
	void applyWorkSummary(const WorkInfo::Summary& summary, bool isZh);
	void abortWorkLookup();
	void mergeWorkCacheFile(const QJsonObject& updates) const;

	// "work-dossier-v1" single structured LLM call - see context/workdossier.h
	// for the pure dossier-assembly/response-parsing logic. Supersedes the
	// plain translated introduction and (when the dataset has no entry for
	// the work) the recommended-recordings-v1 AI fallback, once it returns.
	void maybeStartWorkDossier();
	void startWorkDossier();

	// "Recommended Recordings" (see context/recommendedrecordings.h/.cpp for
	// the pure dataset/AI-response/library matching logic).
	void updateRecommendedRecordings();
	void rebuildRecommendedRecordingsHtml();

private:
	QString currentArtist;
	Action* refreshAction;
#ifndef Q_OS_WIN
	Action* fullWidthCoverAction;
#endif
	QAction* originalTextAction;
	ContextEngine* engine;
	int detailsReceived;
	QString pic;
	QString details;
	QString originalDetails;
	QString detailsSource;
	QString detailsTranslationContext;
	QString detailsLink;
	QString trackList;
	QString bioArtist;
	QString bio;
	QList<Song> songs;

	// Classical work introduction (作品介绍) - see workinfo.h/.cpp.
	WorkInfo::Candidate currentWork;
	QString workIntroHtml;
	QString workIntroOriginalHtml;
	QString workIntroSource;
	QString workIntroTranslationContext;
	QString workIntroLink;
	// "Recommended Recordings" section, appended after the work
	// introduction when non-empty - see updateRecommendedRecordings().
	QString recommendedRecordings;
	RecordingCoverFetcher* coverFetcher;
	QList<RecommendedRecordings::Recording> recRecordings;// Bundled/override dataset match, if any.
	QList<RecommendedRecordings::Recording> recAiRecordings;// AI fallback, used only when the dataset has no match.
	QString recAiSource;
	QString recAiContext;

	NetworkJob* workJob;
	NetworkJob* workExtractsJob;
	QString workSelectedTitle;
	bool workSummaryIsZh;

	// "work-dossier-v1" source dossier (see context/workdossier.h) and its
	// state - see startWorkDossier()/maybeStartWorkDossier().
	QString workEnglishFullText;// Filtered/capped English Wikipedia article text.
	QString workZhHintText;// zh Wikipedia summary, extra hint only.
	QString workDossierSource;
	QString workDossierContext;
	bool workDossierStarted;
	bool workDossierResponded;// A response (parseable or not) has been received.
	WorkDossier::Result workDossierResult;
};

#endif
