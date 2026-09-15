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

#include "albumview.h"
#include "artistview.h"
#include "contextengine.h"
#include "gui/covers.h"
#include "models/mpdlibrarymodel.h"
#include "models/playqueuemodel.h"
#include "mpd-interface/cuefile.h"
#include "network/networkaccessmanager.h"
#include "network/translationservice.h"
#include "recordingcovers.h"
#include "support/action.h"
#include "support/actioncollection.h"
#include "support/configuration.h"
#include "support/utils.h"
#include "widgets/icons.h"
#include "widgets/textbrowser.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#ifdef BUNDLED_KARCHIVE
#include <kcompressiondevice.h>
#else
#include <KCompressionDevice>
#endif

const QLatin1String AlbumView::constCacheDir("albums/");
const QLatin1String AlbumView::constInfoExt(".html.gz");
const QLatin1String AlbumView::constWorkCacheDir("works/");

static const QLatin1String constScheme("cantata");
static const QByteArray constWorkUserAgent("Cantata classical-work lookup (https://github.com/CDrummond/cantata)");

static QString cacheFileName(const QString& artist, const QString& album, const QString& lang, bool createDir)
{
	return Utils::cacheDir(AlbumView::constCacheDir, createDir) + Covers::encodeName(artist) + QLatin1String(" - ") + Covers::encodeName(album) + "." + lang + AlbumView::constInfoExt;
}

enum Parts {
	Cover = 0x01,
	Details = 0x02,
	All = Cover + Details
};

// Wikipedia and Last.fm both append a single "read more"/"open in browser"
// anchor (preceded by one or more <br> tags) to the very end of the HTML
// they return - see WikipediaEngine::wikiToHtml() and
// LastFmEngine::parseResponse(), and ArtistView::extractTrailingLink() for
// the same treatment of artist biographies. Pull that trailing anchor out
// so it can be kept aside from the plain text sent for translation, then
// re-attach it afterwards. "textEnd" is set to the offset in "html" where
// the trailing decoration (leading <br> tags included) begins.
static QString extractTrailingLink(const QString& html, int* textEnd = nullptr)
{
	static const QRegularExpression trailingLinkRx(QStringLiteral("(?:<br\\s*/?>\\s*)*(<a\\s+href=(['\"])[^'\"]*\\2[^>]*>[^<]*</a>)\\s*$"), QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatch match = trailingLinkRx.match(html);
	if (textEnd) {
		*textEnd = match.hasMatch() ? match.capturedStart(0) : html.length();
	}
	return match.hasMatch() ? match.captured(1) : QString();
}

static QString appendLink(const QString& html, const QString& link)
{
	if (link.isEmpty()) {
		return html;
	}
	QString result = html;
	if (!result.isEmpty()) {
		result += QLatin1String("<br/><br/>");
	}
	return result + link;
}

// Loads the "Recommended Recordings" dataset: a user override file (same
// config directory as translation.ini - see TranslationService) when
// present and valid, otherwise the bundled resource. Re-read on every call
// rather than cached, since it is only ever parsed once per song change and
// doing so lets an edited override file be picked up without a restart.
static RecommendedRecordings::Dataset loadRecommendedRecordingsDataset()
{
	const QString overridePath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QLatin1String("recommendedrecordings.json"));
	QFile overrideFile(overridePath);
	if (overrideFile.open(QIODevice::ReadOnly)) {
		const RecommendedRecordings::Dataset overrideDataset = RecommendedRecordings::parseDataset(overrideFile.readAll());
		if (!overrideDataset.works.isEmpty()) {
			return overrideDataset;
		}
	}
	QFile bundled(QLatin1String(":/recommendedrecordings.json"));
	if (bundled.open(QIODevice::ReadOnly)) {
		return RecommendedRecordings::parseDataset(bundled.readAll());
	}
	return RecommendedRecordings::Dataset();
}

static QString recommendedRecordingAlbumUrl(const QString& artist, const QString& albumId)
{
	QUrl url(QLatin1String("cantata:///"));
	QUrlQuery query;
	query.addQueryItem(QLatin1String("artist"), artist);
	query.addQueryItem(QLatin1String("albumId"), albumId);
	url.setQuery(query);
	return url.toString();
}

// A library album (from MpdLibraryModel::getArtistOrComposerAlbums()) that
// has been confirmed, via RecommendedRecordings::worksMatch(), to represent
// the same work as the song currently shown.
struct RecommendedRecordingLibraryMatch {
	LibraryDb::Album album;
	QString performer;// Album's trailing "(Performer - Year)", or its artist.
	QString year;
	bool nowPlaying = false;
};

// Renders the cover thumbnail slot for a recording - see
// RecordingCovers::cachedCover(). Empty when no cover is cached (yet).
static QString renderCoverThumbnail(const QString& localCoverPath)
{
	if (localCoverPath.isEmpty()) {
		return QString();
	}
	return QLatin1String("<img src=\"") + QUrl::fromLocalFile(localCoverPath).toString().toHtmlEscaped() + QLatin1String("\" width=\"96\"/><br/>");
}

// One full "Recommended Recordings" block: cover thumbnail, bold performers
// line, "label catalogue (year)", guide/rating with its source link
// (dataset entries only - never for an AI/extra suggestion), the
// work-dossier-v1 "why" text when one was supplied, and the library link.
static QString renderRecordingBlock(const RecommendedRecordings::Recording& r, const QString& why, const RecommendedRecordingLibraryMatch* libraryMatch)
{
	QStringList rawNames;
	QStringList escapedNames;
	if (!r.soloist.isEmpty()) {
		rawNames << r.soloist;
		escapedNames << r.soloist.toHtmlEscaped();
	}
	if (!r.conductor.isEmpty()) {
		rawNames << r.conductor;
		escapedNames << r.conductor.toHtmlEscaped();
	}
	if (!r.ensemble.isEmpty()) {
		rawNames << r.ensemble;
		escapedNames << r.ensemble.toHtmlEscaped();
	}

	QString html;
	const QString coverKey = RecommendedRecordings::recordingCoverKey(rawNames.join(QLatin1String(", ")), r.label, r.catalogue);
	html += renderCoverThumbnail(RecordingCovers::self()->cachedCover(coverKey));
	if (!escapedNames.isEmpty()) {
		html += QLatin1String("<b>") + escapedNames.join(RecommendedRecordings::middleDotSeparator()) + QLatin1String("</b><br/>");
	}

	QStringList labelParts;
	if (!r.label.isEmpty()) {
		labelParts << r.label.toHtmlEscaped();
	}
	if (!r.catalogue.isEmpty()) {
		labelParts << r.catalogue.toHtmlEscaped();
	}
	QString labelLine = labelParts.join(QLatin1Char(' '));
	if (!r.year.isEmpty()) {
		labelLine += (labelLine.isEmpty() ? QString() : QLatin1String(" ")) + QLatin1Char('(') + r.year.toHtmlEscaped() + QLatin1Char(')');
	}
	if (!labelLine.isEmpty()) {
		html += labelLine + QLatin1String("<br/>");
	}

	// Never show a guide/rating for an AI/extra suggestion - only ever
	// populated for a dataset entry to begin with, but enforced here too.
	if (!r.isAi && !r.guide.isEmpty()) {
		QString guideText = r.guide.toHtmlEscaped();
		if (!r.edition.isEmpty()) {
			guideText += QLatin1Char(' ') + r.edition.toHtmlEscaped();
		}
		if (!r.rating.isEmpty()) {
			guideText += QLatin1String(": ") + r.rating.toHtmlEscaped();
		}
		if (!r.source.isEmpty()) {
			html += QLatin1String("<a href=\"") + r.source.toHtmlEscaped() + QLatin1String("\">") + guideText + QLatin1String("</a><br/>");
		}
		else {
			html += guideText + QLatin1String("<br/>");
		}
	}

	if (!why.isEmpty()) {
		html += TranslationService::plainTextToHtml(why) + QLatin1String("<br/>");
	}

	if (libraryMatch) {
		QString linkText = AlbumView::tr("Open in Library");
		if (libraryMatch->nowPlaying) {
			linkText += QLatin1String(" (") + AlbumView::tr("now playing") + QLatin1Char(')');
		}
		html += QLatin1String("<a href=\"") + recommendedRecordingAlbumUrl(libraryMatch->album.artist, libraryMatch->album.id).toHtmlEscaped() + QLatin1String("\">") + linkText.toHtmlEscaped() + QLatin1String("</a>");
	}
	return QLatin1String("<p>") + html + QLatin1String("</p>");
}

AlbumView::AlbumView(QWidget* p)
	: View(p), detailsReceived(0), workJob(nullptr), workExtractsJob(nullptr), workSummaryIsZh(false), workDossierStarted(false), workDossierResponded(false)
{
	engine = ContextEngine::create(this);
#ifndef Q_OS_WIN
	// Full width covers not working under windows. Issue #1252
	fullWidthCoverAction = new Action(tr("Full Width Cover"), this);
	fullWidthCoverAction->setCheckable(true);
	connect(fullWidthCoverAction, SIGNAL(toggled(bool)), this, SLOT(setScaleImage(bool)));
	fullWidthCoverAction->setChecked(Configuration(metaObject()->className()).get("fullWidthCover", false));
#endif
	refreshAction = ActionCollection::get()->createAction("refreshalbum", tr("Refresh Album Information"), Icons::self()->refreshIcon);
	connect(refreshAction, SIGNAL(triggered()), this, SLOT(refresh()));
	originalTextAction = new QAction(tr("Show original"), this);
	originalTextAction->setCheckable(true);
	connect(originalTextAction, &QAction::toggled, this, &AlbumView::showOriginalToggled);
	connect(engine, SIGNAL(searchResult(QString, QString)), this, SLOT(searchResponse(QString, QString)));
	connect(TranslationService::self(), &TranslationService::translationReady, this, &AlbumView::detailsTranslationReady);
	connect(TranslationService::self(), &TranslationService::translationReady, this, &AlbumView::workIntroTranslationReady);
	connect(TranslationService::self(), &TranslationService::translationReady, this, &AlbumView::recommendedRecordingsTranslationReady);
	connect(TranslationService::self(), &TranslationService::translationReady, this, &AlbumView::workDossierTranslationReady);
	connect(RecordingCovers::self(), &RecordingCovers::coverReady, this, &AlbumView::recordingCoverReady);
	connect(Covers::self(), SIGNAL(cover(Song, QImage, QString)), SLOT(coverRetrieved(Song, QImage, QString)));
	connect(Covers::self(), SIGNAL(coverUpdated(Song, QImage, QString)), SLOT(coverUpdated(Song, QImage, QString)));
	connect(text, SIGNAL(anchorClicked(QUrl)), SLOT(playSong(QUrl)));
	text->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(text, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
	setStandardHeader(tr("Album"));
	int imageSize = fontMetrics().height() * 18;
	setPicSize(QSize(imageSize, imageSize));
	clear();
	if (ArtistView::constCacheAge > 0) {
		clearCache();
		QTimer* timer = new QTimer(this);
		connect(timer, SIGNAL(timeout()), this, SLOT(clearCache()));
		timer->start((int)((ArtistView::constCacheAge / 2.0) * 1000 * 24 * 60 * 60));
	}
}

AlbumView::~AlbumView()
{
#ifndef Q_OS_WIN
	Configuration(metaObject()->className()).set("fullWidthCover", fullWidthCoverAction->isChecked());
#endif
}

void AlbumView::showContextMenu(const QPoint& pos)
{
	QMenu* menu = text->createStandardContextMenu();
	menu->addSeparator();
	menu->addAction(originalTextAction);
	if (cancelJobAction->isEnabled()) {
		menu->addAction(cancelJobAction);
	}
	else {
		menu->addAction(refreshAction);
	}
#ifndef Q_OS_WIN
	menu->addAction(fullWidthCoverAction);
#endif
	menu->exec(text->mapToGlobal(pos));
	delete menu;
}

void AlbumView::refresh()
{
	if (currentSong.isEmpty()) {
		return;
	}
	for (const QString& lang : engine->getLangs()) {
		QFile::remove(cacheFileName(Covers::fixArtist(currentSong.albumArtistOrComposer()), currentSong.album, engine->getPrefix(lang), false));
	}
	QString workCache = workCacheFileName(false);
	if (!workCache.isEmpty()) {
		QFile::remove(workCache);
	}
	update(currentSong, true);
}

void AlbumView::update(const Song& song, bool force)
{
	QString streamName = song.isStandardStream() && song.album.isEmpty() ? song.name() : QString();
	if (!streamName.isEmpty() && streamName != currentSong.name()) {
		abort();
		currentSong = song;
		clearDetails();
		setHeader(streamName);
		needToUpdate = false;
		detailsReceived = All;
		pic = createPicTag(QImage(), CANTATA_SYS_ICONS_DIR + QLatin1String("stream.png"));
		updateDetails();
		return;
	}

	if (song.isEmpty() || song.albumArtistOrComposer().isEmpty() || song.album.isEmpty()) {
		currentSong = song;
		clearDetails();
		abort();
		return;
	}

	if (force || song.albumArtistOrComposer() != currentSong.albumArtistOrComposer() || song.album != currentSong.album) {
		currentSong = song;
		currentArtist = currentSong.basicArtist();
		abort();
		if (!isVisible()) {
			needToUpdate = true;
			return;
		}
		clearDetails();
		setHeader(song.album.isEmpty() ? stdHeader : song.album);
		Covers::Image cImg = Covers::self()->requestImage(song, true);
		detailsReceived |= Cover;// Sometimes cover download fails, and no error?
		if (!cImg.img.isNull()) {
			detailsReceived |= Cover;
			pic = createPicTag(cImg.img, cImg.fileName);
		}
		getTrackListing();
		getDetails();

		if (All == detailsReceived) {
			hideSpinner();
		}
		else {
			showSpinner();
		}
	}
	else if (song.title != currentSong.title) {
		currentSong = song;
		getTrackListing();
		updateDetails(true);
	}
}

void AlbumView::playSong(const QUrl& url)
{
	if (url.scheme() == constScheme) {
		QUrlQuery q(url);
		if (q.hasQueryItem(QLatin1String("artist")) && q.hasQueryItem(QLatin1String("albumId"))) {
			// A "Recommended Recordings" library link - see
			// rebuildRecommendedRecordingsHtml() - rather than a track.
			emit findAlbum(q.queryItemValue(QLatin1String("artist")), q.queryItemValue(QLatin1String("albumId")));
			return;
		}
		emit playSong(url.path().mid(1));// Remove leading /
	}
	else if (CueFile::isCue(url.toString())) {
		emit playSong(url.toString());
	}
	else {
		QDesktopServices::openUrl(url);
	}
}

void AlbumView::getTrackListing()
{
	if (currentSong.isNonMPD()) {
		if (!pic.isEmpty()) {
			updateDetails();
		}
		return;
	}

	if (songs.isEmpty()) {
		songs = MpdLibraryModel::self()->getAlbumTracks(currentSong, 500);
	}

	if (!songs.isEmpty()) {
		trackList = View::subHeader(tr("Tracks")) + QLatin1String("<p><table>");
		for (const Song& s : songs) {
			if (CueFile::isCue(s.file)) {
				QUrl u(s.file);
				QUrlQuery q(u);

				q.addQueryItem("artist", s.artist);
				q.addQueryItem("albumartist", s.albumartist);
				q.addQueryItem("album", s.album);
				q.addQueryItem("title", s.title);
				q.addQueryItem("disc", QString::number(s.disc));
				q.addQueryItem("track", QString::number(s.track));
				q.addQueryItem("time", QString::number(s.time));
				q.addQueryItem("year", QString::number(s.year));
				q.addQueryItem("origYear", QString::number(s.origYear));
				u.setQuery(q);

				trackList += QLatin1String("<tr><td align='right'>") + QString::number(s.track) + QLatin1String("</td><td><a href=\"") + u.toString() + QLatin1String("\">") + ((s.albumartist == currentSong.albumartist && s.album == currentSong.album && s.title == currentSong.title) ? "<b>" + s.displayTitle() + "</b>" : s.displayTitle()) + QLatin1String("</a></td></tr>");
			}
			else {
				trackList += QLatin1String("<tr><td align='right'>") + QString::number(s.track) + QLatin1String("</td><td><a href=\"") + constScheme + QLatin1String(":///") + s.file + QLatin1String("\">") + (s.file == currentSong.file ? "<b>" + s.displayTitle() + "</b>" : s.displayTitle()) + QLatin1String("</a></td></tr>");
			}
		}

		trackList += QLatin1String("</table></p>");
		updateDetails();
	}
}

void AlbumView::getDetails()
{
	engine->cancel();
	abortWorkLookup();
	currentWork = WorkInfo::deriveWork(currentSong.composer(), currentSong.album, currentSong.title, currentSong.firstGenre());
	updateRecommendedRecordings();
	for (const QString& lang : engine->getLangs()) {
		QString prefix = engine->getPrefix(lang);
		QString cachedFile = cacheFileName(Covers::fixArtist(currentSong.albumArtistOrComposer()), currentSong.album, prefix, false);
		if (QFile::exists(cachedFile)) {
			KCompressionDevice f(cachedFile, KCompressionDevice::GZip);
			if (f.open(QIODevice::ReadOnly)) {
			    QByteArray data = f.readAll();
				if (!data.isEmpty()) {
					searchResponse(QString::fromUtf8(data), QString());
					Utils::touchFile(cachedFile);
					return;
				}
			}
		}
	}
	engine->search(QStringList() << currentSong.albumArtistOrComposer() << currentSong.album, ContextEngine::Album);
}

void AlbumView::coverRetrieved(const Song& s, const QImage& img, const QString& file)
{
	if (!s.isArtistImageRequest() && (s == currentSong && pic.isEmpty())) {
		detailsReceived |= Cover;
		if (All == detailsReceived) {
			hideSpinner();
		}
		pic = createPicTag(img, file);
		if (!pic.isEmpty()) {
			updateDetails();
		}
	}
}

void AlbumView::coverUpdated(const Song& s, const QImage& img, const QString& file)
{
	if (!s.isArtistImageRequest() && s == currentSong) {
		detailsReceived |= Cover;
		if (All == detailsReceived) {
			hideSpinner();
		}
		pic = createPicTag(img, file);
		if (!pic.isEmpty()) {
			updateDetails();
		}
	}
}

void AlbumView::searchResponse(const QString& resp, const QString& lang)
{
	detailsReceived |= Details;
	if (All == detailsReceived) {
		hideSpinner();
	}

	originalDetails.clear();
	details.clear();
	detailsSource.clear();
	detailsTranslationContext.clear();
	detailsLink.clear();

	if (!resp.isEmpty()) {
		originalDetails = engine->translateLinks(resp);
		int textEnd = originalDetails.length();
		detailsLink = extractTrailingLink(originalDetails, &textEnd);
		QTextDocument document;
		document.setHtml(originalDetails.left(textEnd));
		detailsSource = document.toPlainText().trimmed();
		detailsTranslationContext = QLatin1String("Album description for ") + currentSong.albumArtistOrComposer() + QLatin1Char(' ') + currentSong.album;
		details = originalDetails;
		if (TranslationService::self()->isEnabled() && !detailsSource.isEmpty()) {
			const QString translated = TranslationService::self()->translate(detailsSource, detailsTranslationContext);
			if (translated != detailsSource) {
				details = appendLink(TranslationService::plainTextToHtml(translated), detailsLink);
			}
		}

		if (!lang.isEmpty()) {
			KCompressionDevice f(cacheFileName(Covers::fixArtist(currentSong.albumArtistOrComposer()), currentSong.album, lang, true), KCompressionDevice::GZip);
			if (f.open(QIODevice::WriteOnly)) {
				f.write(resp.toUtf8().constData());
			}
		}
	}

	updateWorkIntroductionSource();
	updateDetails();
}

void AlbumView::detailsTranslationReady(const QString& source, const QString& context, const QString& translation)
{
	if (source != detailsSource || context != detailsTranslationContext) {
		return;
	}
	if (translation == source) {
		return;
	}
	details = appendLink(TranslationService::plainTextToHtml(translation), detailsLink);
	updateDetails();
}

void AlbumView::workIntroTranslationReady(const QString& source, const QString& context, const QString& translation)
{
	if (source != workIntroSource || context != workIntroTranslationContext) {
		return;
	}
	if (translation == source) {
		return;
	}
	workIntroHtml = appendLink(TranslationService::plainTextToHtml(translation), workIntroLink);
	updateDetails();
}

void AlbumView::showOriginalToggled()
{
	updateDetails(false);
}

// One bold small heading + its plain-text (translated to HTML) body, e.g.
// "<b>Background</b><br/>...", omitted entirely by the caller when the body
// is empty.
static QString wrapIntroductionSubPart(const QString& heading, const QString& plainTextBody)
{
	return QLatin1String("<p><b>") + heading.toHtmlEscaped() + QLatin1String("</b><br/>") + TranslationService::plainTextToHtml(plainTextBody) + QLatin1String("</p>");
}

QString AlbumView::renderStructuredIntroduction(const WorkDossier::Introduction& intro) const
{
	if (intro.isEmpty()) {
		return QString();
	}
	QString html;
	if (!intro.overview.isEmpty()) {
		html += QLatin1String("<p>") + TranslationService::plainTextToHtml(intro.overview) + QLatin1String("</p>");
	}
	if (!intro.background.isEmpty()) {
		html += wrapIntroductionSubPart(tr("Background"), intro.background);
	}
	if (!intro.structure.isEmpty()) {
		QStringList lines;
		for (const WorkDossier::MovementInfo& movement : intro.structure) {
			QString line;
			if (!movement.movement.isEmpty()) {
				line += QLatin1String("<b>") + TranslationService::plainTextToHtml(movement.movement) + QLatin1String("</b>");
			}
			if (!movement.description.isEmpty()) {
				line += (line.isEmpty() ? QString() : QLatin1String(": ")) + TranslationService::plainTextToHtml(movement.description);
			}
			if (!line.isEmpty()) {
				lines << line;
			}
		}
		if (!lines.isEmpty()) {
			html += QLatin1String("<p><b>") + tr("Movements").toHtmlEscaped() + QLatin1String("</b><br/>") + lines.join(QLatin1String("<br/>")) + QLatin1String("</p>");
		}
	}
	if (!intro.highlights.isEmpty()) {
		html += wrapIntroductionSubPart(tr("Highlights"), intro.highlights);
	}
	if (!intro.premiere.isEmpty()) {
		html += wrapIntroductionSubPart(tr("Premiere"), intro.premiere);
	}
	return html;
}

bool AlbumView::workDossierPending() const
{
	return currentWork.valid && workDossierStarted && !workDossierResponded;
}

QString AlbumView::buildWorkIntroductionSection(bool showOriginal) const
{
	if (!currentWork.valid) {
		return QString();
	}
	QString body;
	bool structured = false;
	if (!showOriginal && workDossierResult.valid) {
		// Priority 0: the work-dossier-v1 structured answer, once it has
		// arrived - see startWorkDossier()/workDossierTranslationReady().
		// "Show original" always falls back to the plain source text below.
		body = renderStructuredIntroduction(workDossierResult.introduction);
		if (!body.isEmpty()) {
			body = appendLink(body, workIntroLink);
			structured = true;
		}
	}
	if (!structured) {
		if (!originalDetails.isEmpty()) {
			// Priority 1: the album description is the work introduction - see
			// updateWorkIntroductionSource().
			body = showOriginal ? originalDetails : (details.isEmpty() ? originalDetails : details);
		}
		else if (!workIntroOriginalHtml.isEmpty()) {
			// Priority 2: the work's own Wikipedia article.
			body = showOriginal ? workIntroOriginalHtml : (workIntroHtml.isEmpty() ? workIntroOriginalHtml : workIntroHtml);
		}
	}

	QString html;
	if (!body.isEmpty()) {
		html = View::subHeader(tr("Work Introduction")) + body;
		if (workDossierPending()) {
			html += QLatin1String("<p><i>") + tr("Generating detailed introduction…").toHtmlEscaped() + QLatin1String("</i></p>");
		}
	}
	// Shown whenever the song is a valid classical work, even when there is
	// no introduction text at all yet (or ever).
	if (!recommendedRecordings.isEmpty()) {
		html += recommendedRecordings;
	}
	return html;
}

void AlbumView::updateDetails(bool preservePos)
{
	int pos = preservePos ? text->verticalScrollBar()->value() : 0;
	bool showOriginal = originalTextAction && originalTextAction->isChecked();
	QString body = buildWorkIntroductionSection(showOriginal);
	if (body.isEmpty()) {
		body = showOriginal ? originalDetails : (details.isEmpty() ? originalDetails : details);
	}
	if (!body.isEmpty()) {
		setHtml(pic + "<br>" + body + "<br>" + trackList);
	}
	else {
		setHtml(pic + trackList);
	}
	if (preservePos) {
		text->verticalScrollBar()->setValue(pos);
	}
}

void AlbumView::abort()
{
	engine->cancel();
	abortWorkLookup();
	hideSpinner();
}

void AlbumView::clearCache()
{
	Utils::clearOldCache(constCacheDir, ArtistView::constCacheAge);
}

void AlbumView::clearDetails()
{
	details.clear();
	originalDetails.clear();
	detailsSource.clear();
	detailsTranslationContext.clear();
	detailsLink.clear();
	trackList.clear();
	bio.clear();
	pic.clear();
	songs.clear();
	clear();
	engine->cancel();
	abortWorkLookup();
	currentWork = WorkInfo::Candidate();
	workIntroHtml.clear();
	workIntroOriginalHtml.clear();
	workIntroSource.clear();
	workIntroTranslationContext.clear();
	workIntroLink.clear();
	workEnglishFullText.clear();
	workZhHintText.clear();
	workDossierSource.clear();
	workDossierContext.clear();
	workDossierStarted = false;
	workDossierResponded = false;
	workDossierResult = WorkDossier::Result();
	recommendedRecordings.clear();
	recRecordings.clear();
	recAiRecordings.clear();
	recAiSource.clear();
	recAiContext.clear();
	detailsReceived = 0;
}

QString AlbumView::workCacheFileName(bool createDir) const
{
	if (!currentWork.valid) {
		return QString();
	}
	return Utils::cacheDir(constWorkCacheDir, createDir) + Covers::encodeName(currentWork.composer) + QLatin1String(" - ") + Covers::encodeName(currentWork.title) + QLatin1String(".json");
}

void AlbumView::updateWorkIntroductionSource()
{
	workIntroHtml.clear();
	workIntroOriginalHtml.clear();
	workIntroSource.clear();
	workIntroTranslationContext.clear();
	workIntroLink.clear();
	workEnglishFullText.clear();
	workZhHintText.clear();
	workDossierSource.clear();
	workDossierContext.clear();
	workDossierStarted = false;
	workDossierResponded = false;
	workDossierResult = WorkDossier::Result();
	abortWorkLookup();

	if (!currentWork.valid) {
		return;
	}

	// The work's own Wikipedia article is now always looked up, even when
	// the album description (priority 1, already fetched/translated in
	// searchResponse()) supplies the quick introduction shown immediately:
	// its full text and the dataset's recording facts are the source dossier
	// the work-dossier-v1 prompt is built from - see startWorkDossier().
	loadWorkFromCacheOrNetwork();
}

void AlbumView::loadWorkFromCacheOrNetwork()
{
	const QString cachedFile = workCacheFileName(false);
	bool haveCachedDossierSources = false;
	if (!cachedFile.isEmpty() && QFile::exists(cachedFile)) {
		QFile f(cachedFile);
		if (f.open(QIODevice::ReadOnly)) {
			const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
			if (doc.isObject()) {
				const QJsonObject obj = doc.object();
				WorkInfo::Summary summary;
				summary.extract = obj.value(QLatin1String("extract")).toString();
				summary.url = obj.value(QLatin1String("url")).toString();
				if (!summary.extract.isEmpty()) {
					applyWorkSummary(summary, obj.value(QLatin1String("lang")).toString() == QLatin1String("zh"));
					Utils::touchFile(cachedFile);
					// "fullText" is only present once the English Wikipedia
					// article's full text has actually been fetched (see
					// workExtractsFinished()) - an older cache entry from
					// before that was added lacks it, and is refreshed below
					// instead of silently never gaining a dossier.
					if (obj.contains(QLatin1String("fullText"))) {
						workEnglishFullText = obj.value(QLatin1String("fullText")).toString();
						workZhHintText = obj.value(QLatin1String("zhHint")).toString();
						haveCachedDossierSources = true;
						maybeStartWorkDossier();
					}
				}
			}
		}
	}
	if (!haveCachedDossierSources) {
		startWorkSearch();
	}
}

void AlbumView::startWorkSearch()
{
	if (currentWork.searchQuery.isEmpty()) {
		return;
	}
	QUrl url(QLatin1String("https://en.wikipedia.org/w/api.php"));
	QUrlQuery query;
	query.addQueryItem(QLatin1String("action"), QLatin1String("query"));
	query.addQueryItem(QLatin1String("list"), QLatin1String("search"));
	query.addQueryItem(QLatin1String("srsearch"), currentWork.searchQuery);
	query.addQueryItem(QLatin1String("srlimit"), QLatin1String("5"));
	query.addQueryItem(QLatin1String("format"), QLatin1String("json"));
	url.setQuery(query);

	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", constWorkUserAgent);
	workJob = NetworkAccessManager::self()->get(request);
	connect(workJob, SIGNAL(finished()), this, SLOT(workSearchFinished()));
}

void AlbumView::workSearchFinished()
{
	NetworkJob* reply = qobject_cast<NetworkJob*>(sender());
	if (!reply || reply != workJob) {
		return;
	}
	workJob = nullptr;
	reply->deleteLater();
	if (!reply->ok()) {
		return;
	}

	workSelectedTitle = WorkInfo::selectSearchResult(reply->readAll(), currentWork);
	if (workSelectedTitle.isEmpty()) {
		return;
	}

	QUrl url(QLatin1String("https://en.wikipedia.org/w/api.php"));
	QUrlQuery query;
	query.addQueryItem(QLatin1String("action"), QLatin1String("query"));
	query.addQueryItem(QLatin1String("prop"), QLatin1String("pageprops|langlinks"));
	query.addQueryItem(QLatin1String("lllang"), QLatin1String("zh"));
	query.addQueryItem(QLatin1String("titles"), workSelectedTitle);
	query.addQueryItem(QLatin1String("redirects"), QLatin1String("1"));
	query.addQueryItem(QLatin1String("format"), QLatin1String("json"));
	url.setQuery(query);

	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", constWorkUserAgent);
	workJob = NetworkAccessManager::self()->get(request);
	connect(workJob, SIGNAL(finished()), this, SLOT(workPagePropsFinished()));
}

void AlbumView::workPagePropsFinished()
{
	NetworkJob* reply = qobject_cast<NetworkJob*>(sender());
	if (!reply || reply != workJob) {
		return;
	}
	workJob = nullptr;
	reply->deleteLater();
	if (!reply->ok()) {
		return;
	}

	const WorkInfo::SiteLinks links = WorkInfo::parseSiteLinks(reply->readAll());

	QUrl url;
	url.setScheme(QLatin1String("https"));
	QNetworkRequest request;
	if (!links.zhTitle.isEmpty()) {
		workSummaryIsZh = true;
		QString path = links.zhTitle;
		path.replace(QLatin1Char(' '), QLatin1Char('_'));
		url.setHost(QLatin1String("zh.wikipedia.org"));
		url.setPath(QLatin1String("/api/rest_v1/page/summary/") + path);
		request.setRawHeader("Accept-Language", "zh-cn");
	}
	else {
		workSummaryIsZh = false;
		QString path = workSelectedTitle;
		path.replace(QLatin1Char(' '), QLatin1Char('_'));
		url.setHost(QLatin1String("en.wikipedia.org"));
		url.setPath(QLatin1String("/api/rest_v1/page/summary/") + path);
	}
	request.setUrl(url);
	request.setRawHeader("User-Agent", constWorkUserAgent);
	workJob = NetworkAccessManager::self()->get(request);
	connect(workJob, SIGNAL(finished()), this, SLOT(workSummaryFinished()));

	// The work-dossier-v1 prompt's main source (see workdossier.h) is always
	// the full English Wikipedia article text, fetched in parallel with the
	// (short) summary above regardless of whether a zh article was found.
	QUrl extractsUrl(QLatin1String("https://en.wikipedia.org/w/api.php"));
	QUrlQuery extractsQuery;
	extractsQuery.addQueryItem(QLatin1String("action"), QLatin1String("query"));
	extractsQuery.addQueryItem(QLatin1String("prop"), QLatin1String("extracts"));
	extractsQuery.addQueryItem(QLatin1String("explaintext"), QLatin1String("1"));
	extractsQuery.addQueryItem(QLatin1String("exsectionformat"), QLatin1String("wiki"));
	extractsQuery.addQueryItem(QLatin1String("redirects"), QLatin1String("1"));
	extractsQuery.addQueryItem(QLatin1String("titles"), workSelectedTitle);
	extractsQuery.addQueryItem(QLatin1String("format"), QLatin1String("json"));
	extractsUrl.setQuery(extractsQuery);

	QNetworkRequest extractsRequest(extractsUrl);
	extractsRequest.setRawHeader("User-Agent", constWorkUserAgent);
	workExtractsJob = NetworkAccessManager::self()->get(extractsRequest);
	connect(workExtractsJob, SIGNAL(finished()), this, SLOT(workExtractsFinished()));
}

void AlbumView::workExtractsFinished()
{
	NetworkJob* reply = qobject_cast<NetworkJob*>(sender());
	if (!reply || reply != workExtractsJob) {
		return;
	}
	workExtractsJob = nullptr;
	reply->deleteLater();
	if (reply->ok()) {
		const QString fullText = WorkDossier::extractPlainText(reply->readAll());
		workEnglishFullText = WorkDossier::filterAndCapSections(fullText);

		QJsonObject updates;
		updates.insert(QLatin1String("fullText"), workEnglishFullText);
		mergeWorkCacheFile(updates);
	}
	maybeStartWorkDossier();
}

void AlbumView::workSummaryFinished()
{
	NetworkJob* reply = qobject_cast<NetworkJob*>(sender());
	if (!reply || reply != workJob) {
		return;
	}
	workJob = nullptr;
	reply->deleteLater();
	if (!reply->ok()) {
		return;
	}

	const WorkInfo::Summary summary = WorkInfo::parseSummary(reply->readAll());
	if (summary.extract.isEmpty()) {
		return;
	}
	applyWorkSummary(summary, workSummaryIsZh);

	QJsonObject updates;
	updates.insert(QLatin1String("lang"), workSummaryIsZh ? QLatin1String("zh") : QLatin1String("en"));
	updates.insert(QLatin1String("extract"), summary.extract);
	updates.insert(QLatin1String("url"), summary.url);
	updates.insert(QLatin1String("zhHint"), workZhHintText);
	mergeWorkCacheFile(updates);
}

void AlbumView::applyWorkSummary(const WorkInfo::Summary& summary, bool isZh)
{
	workIntroLink = summary.url.isEmpty() ? QString() : (QLatin1String("<a href=\"") + summary.url.toHtmlEscaped() + QLatin1String("\">") + tr("Wikipedia") + QLatin1String("</a>"));

	workIntroOriginalHtml = appendLink(TranslationService::plainTextToHtml(summary.extract), workIntroLink);
	workIntroHtml = workIntroOriginalHtml;
	if (isZh) {
		// The zh article is already in Chinese - show it directly, no
		// translation needed. Its summary also doubles as the "extra hint"
		// handed to the work-dossier-v1 prompt alongside the English full
		// text - see workdossier.h.
		workIntroSource.clear();
		workIntroTranslationContext.clear();
		workZhHintText = summary.extract;
	}
	else {
		workIntroSource = summary.extract;
		workIntroTranslationContext = QLatin1String("Classical work introduction for ") + currentWork.composer + QLatin1Char(' ') + currentWork.title;
		if (TranslationService::self()->isEnabled()) {
			const QString translated = TranslationService::self()->translate(workIntroSource, workIntroTranslationContext);
			if (translated != workIntroSource) {
				workIntroHtml = appendLink(TranslationService::plainTextToHtml(translated), workIntroLink);
			}
		}
	}
	// Deliberately not calling maybeStartWorkDossier() here: this can run
	// before the (mandatory) English full-text fetch has even started - see
	// loadWorkFromCacheOrNetwork() and workExtractsFinished(), which is the
	// call site that actually gates on it.
	updateDetails();
}

void AlbumView::abortWorkLookup()
{
	if (workJob) {
		workJob->cancelAndDelete();
		workJob = nullptr;
	}
	if (workExtractsJob) {
		workExtractsJob->cancelAndDelete();
		workExtractsJob = nullptr;
	}
	workSelectedTitle.clear();
}

void AlbumView::mergeWorkCacheFile(const QJsonObject& updates) const
{
	const QString cachedFile = workCacheFileName(true);
	if (cachedFile.isEmpty()) {
		return;
	}
	QJsonObject obj;
	QFile existing(cachedFile);
	if (existing.open(QIODevice::ReadOnly)) {
		const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll());
		if (doc.isObject()) {
			obj = doc.object();
		}
	}
	for (auto it = updates.constBegin(); it != updates.constEnd(); ++it) {
		obj.insert(it.key(), it.value());
	}
	QFile f(cachedFile);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
	}
}

void AlbumView::maybeStartWorkDossier()
{
	// The English full-text fetch is mandatory (see workExtractsFinished());
	// wait for it before assembling the dossier so it is not missing its
	// main source. workExtractsJob is null both before the fetch is started
	// and once it has finished, so this is also (harmlessly) a no-op when
	// called before startWorkSearch()/loadWorkFromCacheOrNetwork() has even
	// kicked off a lookup.
	if (!currentWork.valid || workExtractsJob || workDossierStarted) {
		return;
	}
	if (workEnglishFullText.isEmpty() && workZhHintText.isEmpty() && recRecordings.isEmpty()) {
		// Nothing at all to go on - do not waste a request on a bare work
		// title with no supporting source text.
		return;
	}
	startWorkDossier();
}

void AlbumView::startWorkDossier()
{
	if (workDossierStarted || !currentWork.valid) {
		return;
	}
	workDossierStarted = true;
	workDossierResponded = false;
	workDossierResult = WorkDossier::Result();
	workDossierSource = WorkDossier::buildDossierText(currentWork.composer, currentWork.title, currentWork.catalogueNumber, workEnglishFullText, workZhHintText, recRecordings);
	workDossierContext = QLatin1String("work-dossier-v1");

	if (!TranslationService::self()->isEnabled()) {
		workDossierStarted = false;
		return;
	}

	const QString cached = TranslationService::self()->translate(workDossierSource, workDossierContext);
	if (cached != workDossierSource) {
		// Already cached - workDossierTranslationReady() will not fire for
		// this one, so parse and apply it here instead.
		workDossierResponded = true;
		workDossierResult = WorkDossier::parseResponse(cached);
		rebuildRecommendedRecordingsHtml();
	}
	updateDetails();
}

void AlbumView::updateRecommendedRecordings()
{
	recRecordings.clear();
	recAiRecordings.clear();
	recAiSource.clear();
	recAiContext.clear();
	recommendedRecordings.clear();

	if (!currentWork.valid) {
		return;
	}

	const RecommendedRecordings::Dataset dataset = loadRecommendedRecordingsDataset();
	const int matchIndex = RecommendedRecordings::findMatchingWork(dataset, currentWork.composer, currentWork.catalogueNumber, currentWork.title);
	if (matchIndex >= 0) {
		recRecordings = dataset.works.at(matchIndex).recordings;
	}
	else if (TranslationService::self()->isEnabled()) {
		// Kept running alongside the work-dossier-v1 call (see
		// startWorkDossier()) as a fallback: rebuildRecommendedRecordingsHtml()
		// only ever falls back to this when the dossier's own source-text
		// extras are unavailable.
		recAiSource = currentWork.composer + RecommendedRecordings::emDashSeparator() + currentWork.title;
		if (!currentWork.catalogueNumber.isEmpty()) {
			recAiSource += QLatin1Char(' ') + currentWork.catalogueNumber;
		}
		recAiContext = QLatin1String("recommended-recordings-v1");
		const QString cached = TranslationService::self()->translate(recAiSource, recAiContext);
		if (cached != recAiSource) {
			// Already cached - recommendedRecordingsTranslationReady() will
			// not fire for this one, so parse it here instead.
			recAiRecordings = RecommendedRecordings::parseAiRecordings(cached);
		}
	}

	rebuildRecommendedRecordingsHtml();
}

void AlbumView::recommendedRecordingsTranslationReady(const QString& source, const QString& context, const QString& translation)
{
	if (recAiContext.isEmpty() || source != recAiSource || context != recAiContext) {
		return;
	}
	if (translation == source) {
		return;
	}
	recAiRecordings = RecommendedRecordings::parseAiRecordings(translation);
	rebuildRecommendedRecordingsHtml();
	updateDetails();
}

void AlbumView::workDossierTranslationReady(const QString& source, const QString& context, const QString& translation)
{
	if (workDossierContext.isEmpty() || source != workDossierSource || context != workDossierContext) {
		return;
	}
	workDossierResponded = true;
	if (translation != source) {
		// On parse failure workDossierResult stays !valid, and rendering
		// falls back to today's plain translated introduction/AI-suggestion
		// recordings - see buildWorkIntroductionSection()/
		// rebuildRecommendedRecordingsHtml().
		workDossierResult = WorkDossier::parseResponse(translation);
	}
	rebuildRecommendedRecordingsHtml();
	updateDetails();
}

void AlbumView::recordingCoverReady(const QString& key, const QString& localPath)
{
	Q_UNUSED(localPath)
	// Cheap to just always rebuild: the section is only a handful of
	// recordings, and renderRecordingBlock() re-reads
	// RecordingCovers::cachedCover() itself - no need to track which key
	// belongs to which currently-shown recording here too.
	if (key.isEmpty() || !currentWork.valid) {
		return;
	}
	rebuildRecommendedRecordingsHtml();
	updateDetails();
}

void AlbumView::rebuildRecommendedRecordingsHtml()
{
	recommendedRecordings.clear();
	if (!currentWork.valid) {
		return;
	}

	QList<RecommendedRecordingLibraryMatch> composerMatches;
	if (!currentSong.isNonMPD()) {
		const QList<LibraryDb::Album> albums = MpdLibraryModel::self()->getArtistOrComposerAlbums(currentWork.composer);
		Song playing;
		bool havePlaying = false;
		for (const LibraryDb::Album& album : albums) {
			const WorkInfo::Candidate albumWork = WorkInfo::deriveWork(currentWork.composer, album.name);
			if (!albumWork.valid) {
				continue;
			}
			if (!RecommendedRecordings::worksMatch(albumWork.catalogueNumber, albumWork.title, QStringList(), currentWork.catalogueNumber, currentWork.title, QStringList())) {
				continue;
			}
			if (!havePlaying) {
				playing = PlayQueueModel::self()->getSongByRow(PlayQueueModel::self()->currentSongRow());
				havePlaying = true;
			}
			RecommendedRecordingLibraryMatch match;
			match.album = album;
			match.performer = !albumWork.performer.isEmpty() ? albumWork.performer : album.artist;
			match.year = !albumWork.year.isEmpty() ? albumWork.year : (album.year > 0 ? QString::number(album.year) : QString());
			match.nowPlaying = !playing.isEmpty() && playing.album == album.name && playing.albumArtistOrComposer() == album.artist;
			composerMatches << match;
		}
	}

	auto findLibraryMatch = [&composerMatches](const RecommendedRecordings::Recording& r) -> const RecommendedRecordingLibraryMatch* {
		for (const RecommendedRecordingLibraryMatch& m : composerMatches) {
			if ((!r.soloist.isEmpty() && RecommendedRecordings::performerNameMatches(m.performer, r.soloist))
			    || (!r.conductor.isEmpty() && RecommendedRecordings::performerNameMatches(m.performer, r.conductor))) {
				return &m;
			}
		}
		return nullptr;
	};

	// The work-dossier-v1 answer's "why" text for the dataset recording at
	// "id" (its index into recRecordings, as a string - see
	// WorkDossier::buildDossierText()), or empty when there is none (not yet
	// answered, or the source text gave no reason for that recording).
	auto whyForId = [this](const QString& id) -> QString {
		for (const WorkDossier::RecordingAnnotation& annotation : workDossierResult.recordings) {
			if (annotation.id == id) {
				return annotation.why;
			}
		}
		return QString();
	};

	// Kicks off/continues a cover art lookup for "r" - a no-op once a cover
	// is already cached (see RecordingCovers::request()).
	auto requestCover = [](const RecommendedRecordings::Recording& r) {
		QStringList names;
		if (!r.soloist.isEmpty()) names << r.soloist;
		if (!r.conductor.isEmpty()) names << r.conductor;
		if (!r.ensemble.isEmpty()) names << r.ensemble;
		const QString performers = names.join(QLatin1String(", "));
		const QString key = RecommendedRecordings::recordingCoverKey(performers, r.label, r.catalogue);
		if (RecordingCovers::self()->cachedCover(key).isEmpty()) {
			RecordingCovers::self()->request(key, performers, r.label, r.catalogue, r.year);
		}
	};

	QString html;
	for (int i = 0; i < recRecordings.size(); ++i) {
		const RecommendedRecordings::Recording& r = recRecordings.at(i);
		requestCover(r);
		html += renderRecordingBlock(r, whyForId(QString::number(i)), findLibraryMatch(r));
	}

	// "Extra" recordings named only in the source text: the work-dossier-v1
	// answer's entries with an empty id (only ever populated when the
	// dataset had no match for this work - see startWorkDossier()) take
	// priority; the older, dedicated recommended-recordings-v1 AI
	// suggestions (see updateRecommendedRecordings()) are the fallback for
	// when the dossier supplied none.
	QList<RecommendedRecordings::Recording> extras;
	QStringList extraWhys;
	if (recRecordings.isEmpty() && workDossierResult.valid) {
		for (const WorkDossier::RecordingAnnotation& annotation : workDossierResult.recordings) {
			if (!annotation.id.isEmpty()) {
				continue;
			}
			RecommendedRecordings::Recording r;
			r.soloist = annotation.performers;
			r.label = annotation.label;
			r.catalogue = annotation.catalogue;
			r.year = annotation.year;
			r.isAi = true;
			if (r.soloist.isEmpty() && r.label.isEmpty() && r.catalogue.isEmpty() && r.year.isEmpty()) {
				continue;
			}
			extras << r;
			extraWhys << annotation.why;
			if (extras.size() >= 5) {
				break;
			}
		}
	}
	if (extras.isEmpty()) {
		extras = recAiRecordings;
		extraWhys.clear();
	}

	if (!extras.isEmpty()) {
		QString aiHtml;
		for (int i = 0; i < extras.size(); ++i) {
			const RecommendedRecordings::Recording& r = extras.at(i);
			requestCover(r);
			aiHtml += renderRecordingBlock(r, i < extraWhys.size() ? extraWhys.at(i) : QString(), findLibraryMatch(r));
		}
		html += QLatin1String("<p><b>") + tr("AI suggestions (not verified by any guide)").toHtmlEscaped() + QLatin1String("</b></p>") + aiHtml;
	}

	if (!composerMatches.isEmpty()) {
		QStringList libraryLines;
		for (const RecommendedRecordingLibraryMatch& m : composerMatches) {
			QString line = m.performer.toHtmlEscaped();
			if (!m.year.isEmpty()) {
				line += QLatin1String(" (") + m.year.toHtmlEscaped() + QLatin1Char(')');
			}
			line += RecommendedRecordings::emDashSeparator() + QLatin1String("<a href=\"") + recommendedRecordingAlbumUrl(m.album.artist, m.album.id).toHtmlEscaped() + QLatin1String("\">") + tr("Open in Library").toHtmlEscaped() + QLatin1String("</a>");
			if (m.nowPlaying) {
				line += QLatin1String(" (") + tr("now playing").toHtmlEscaped() + QLatin1Char(')');
			}
			libraryLines << line;
		}
		html += QLatin1String("<p><b>") + tr("In Your Library").toHtmlEscaped() + QLatin1String("</b><br/>") + libraryLines.join(QLatin1String("<br/>")) + QLatin1String("</p>");
	}

	if (!html.isEmpty()) {
		html = View::subHeader(tr("Recommended Recordings")) + html;
	}
	recommendedRecordings = html;
}

#include "moc_albumview.cpp"
