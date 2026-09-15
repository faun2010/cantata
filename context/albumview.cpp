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
#include "mpd-interface/cuefile.h"
#include "network/networkaccessmanager.h"
#include "network/translationservice.h"
#include "support/action.h"
#include "support/actioncollection.h"
#include "support/configuration.h"
#include "support/utils.h"
#include "widgets/icons.h"
#include "widgets/textbrowser.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScrollBar>
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

AlbumView::AlbumView(QWidget* p)
	: View(p), detailsReceived(0), workJob(nullptr), workSummaryIsZh(false)
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

QString AlbumView::buildWorkIntroductionSection(bool showOriginal) const
{
	if (!currentWork.valid) {
		return QString();
	}
	QString body;
	if (!originalDetails.isEmpty()) {
		// Priority 1: the album description is the work introduction - see
		// updateWorkIntroductionSource().
		body = showOriginal ? originalDetails : (details.isEmpty() ? originalDetails : details);
	}
	else if (!workIntroOriginalHtml.isEmpty()) {
		// Priority 2: the work's own Wikipedia article.
		body = showOriginal ? workIntroOriginalHtml : (workIntroHtml.isEmpty() ? workIntroOriginalHtml : workIntroHtml);
	}
	else {
		return QString();
	}
	QString html = View::subHeader(tr("Work Introduction")) + body;
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
	abortWorkLookup();

	if (!currentWork.valid || !originalDetails.isEmpty()) {
		// Either not a classical work candidate, or priority 1 (the album
		// description, already fetched and translated in searchResponse())
		// supplies the introduction - nothing further to fetch.
		return;
	}

	loadWorkFromCacheOrNetwork();
}

void AlbumView::loadWorkFromCacheOrNetwork()
{
	const QString cachedFile = workCacheFileName(false);
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
					return;
				}
			}
		}
	}
	startWorkSearch();
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

	const QString cachedFile = workCacheFileName(true);
	if (!cachedFile.isEmpty()) {
		QJsonObject obj;
		obj.insert(QLatin1String("lang"), workSummaryIsZh ? QLatin1String("zh") : QLatin1String("en"));
		obj.insert(QLatin1String("extract"), summary.extract);
		obj.insert(QLatin1String("url"), summary.url);
		QFile f(cachedFile);
		if (f.open(QIODevice::WriteOnly)) {
			f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
		}
	}
}

void AlbumView::applyWorkSummary(const WorkInfo::Summary& summary, bool isZh)
{
	workIntroLink = summary.url.isEmpty() ? QString() : (QLatin1String("<a href=\"") + summary.url.toHtmlEscaped() + QLatin1String("\">") + tr("Wikipedia") + QLatin1String("</a>"));

	workIntroOriginalHtml = appendLink(TranslationService::plainTextToHtml(summary.extract), workIntroLink);
	workIntroHtml = workIntroOriginalHtml;
	if (isZh) {
		// The zh article is already in Chinese - show it directly, no
		// translation needed.
		workIntroSource.clear();
		workIntroTranslationContext.clear();
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
	updateDetails();
}

void AlbumView::abortWorkLookup()
{
	if (workJob) {
		workJob->cancelAndDelete();
		workJob = nullptr;
	}
	workSelectedTitle.clear();
}

#include "moc_albumview.cpp"
