/*
 * Cantata
 *
 * Copyright (c) 2011-2026 Craig Drummond <craig.p.drummond@gmail.com>
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

#include "composerdaypage.h"
#include "dayrecordings.h"
#include "network/networkaccessmanager.h"
#include <QSaveFile>
#include <QSet>
#include "context/composertable.h"
#include "context/workinfo.h"
#include "gui/stdactions.h"
#include "mpd-interface/mpdconnection.h"
#include "network/translationservice.h"
#include "support/configuration.h"
#include "support/utils.h"
#include "widgets/icons.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QStandardPaths>
#include <QTimer>

static const QLatin1String constWorksContext("musician-works-v1");

// The anniversary calendar: a user override in the config directory when one
// exists (so a user can add composers we do not ship), else the bundled
// resource.
static QByteArray readCalendar()
{
	const QString overridePath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QLatin1String("composercalendar.json"));
	QFile override(overridePath);
	if (override.exists() && override.open(QIODevice::ReadOnly)) {
		const QByteArray data = override.readAll();
		if (!data.isEmpty()) {
			return data;
		}
	}
	QFile bundled(QLatin1String(":/composercalendar.json"));
	return bundled.open(QIODevice::ReadOnly) ? bundled.readAll() : QByteArray();
}

static bool showChineseNames()
{
	return QLocale::Chinese == QLocale().language();
}

// "西贝柳斯 (Jean Sibelius)" for a Chinese UI, "Jean Sibelius" otherwise.
static QString displayName(const QString& name, const QString& chinese)
{
	return chinese.isEmpty() || !showChineseNames() ? name : chinese + QLatin1String(" (") + name + QLatin1Char(')');
}

ComposerDayPage::ComposerDayPage(QWidget* p)
	: SinglePageWidget(p)
{
	connect(this, SIGNAL(search(QByteArray, QString)), MPDConnection::self(), SLOT(search(QByteArray, QString)));
	connect(MPDConnection::self(), SIGNAL(searchResponse(QString, QList<Song>)), this, SLOT(searchResponse(QString, QList<Song>)));
	connect(MPDConnection::self(), SIGNAL(stateChanged(bool)), this, SLOT(connectionStateChanged(bool)));
	connect(TranslationService::self(), SIGNAL(translationReady(QString, QString, QString)), this, SLOT(worksReady(QString, QString, QString)));
	connect(view, SIGNAL(itemsSelected(bool)), this, SLOT(controlActions()));
	connect(view, &ItemView::headerClicked, this, [this](int level) {
		if (level == 0) emit close();
	});

	view->setModel(&model);
	view->setMode(ItemView::Mode_DetailedTree);
	view->alwaysShowHeader();
	view->setInfoText(tr("Two daily lists from On This Day: birthdays and death anniversaries. Only recordings in your library are playable."));
	init(ReplacePlayQueue | AppendToPlayQueue | Refresh);
	controlActions();

	Configuration config(metaObject()->className());
	view->load(config);
	calendar = ComposerDay::parseCalendar(readCalendar());
	QFile recommendations(QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("recommendedrecordings.json"));
	if (!recommendations.exists()) recommendations.setFileName(":/recommendedrecordings.json");
	if (recommendations.open(QIODevice::ReadOnly)) recordings = RecommendedRecordings::parseDataset(recommendations.readAll());

	dayTimer = new QTimer(this);
	dayTimer->setSingleShot(true);
	connect(dayTimer, SIGNAL(timeout()), this, SLOT(dayChanged()));
}

ComposerDayPage::~ComposerDayPage()
{
	cancelLookups();
	Configuration config(metaObject()->className());
	view->save(config);
}

void ComposerDayPage::showEvent(QShowEvent* e)
{
	rebuild(false);
	SinglePageWidget::showEvent(e);
}

void ComposerDayPage::refresh()
{
	rebuild(true);
}

void ComposerDayPage::connectionStateChanged(bool connected)
{
	if (connected && isVisible()) {
		rebuild(true);
	}
}

void ComposerDayPage::scheduleDayChange()
{
	// A couple of seconds past midnight, so currentDate() has rolled over.
	const QDateTime now = QDateTime::currentDateTime();
	const QDateTime next(now.date().addDays(1), QTime(0, 0, 2));
	dayTimer->start((int)qBound(qint64(1000), now.msecsTo(next), qint64(24 * 60 * 60 * 1000)));
}

void ComposerDayPage::dayChanged()
{
	// A hidden page catches up in showEvent(), so only rebuild a visible one.
	if (isVisible()) {
		rebuild(false);
	}
	else {
		scheduleDayChange();
	}
}

void ComposerDayPage::controlActions()
{
	StdActions::self()->enableAddToPlayQueue(!view->selectedIndexes(false).isEmpty());
}

void ComposerDayPage::cancelLookups()
{
	for (NetworkJob* job : dayJobs) { disconnect(job, nullptr, this, nullptr); job->cancelAndDelete(); }
	dayJobs.clear();
	view->hideSpinner();
	for (const Lookup& lookup : lookups) {
		if (!lookup.llmSource.isEmpty() && !lookup.haveWorks) {
			TranslationService::self()->cancel(lookup.llmSource, constWorksContext);
		}
	}
	lookups.clear();
}

void ComposerDayPage::rebuild(bool force)
{
	const QDate today = QDate::currentDate();
	if (!force && today == builtFor && !model.isEmpty()) {
		return;
	}

	cancelLookups();
	++generation;
	builtFor = today;
	scheduleDayChange();

	dayPeople.clear();
	model.setComposers({});
	pendingDays = 2;
	view->showSpinner();
	loadDay(true);
	loadDay(false);
}

void ComposerDayPage::loadDay(bool birth)
{
	const QString kind = birth ? QStringLiteral("birthdays") : QStringLiteral("deaths");
	const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/music-day/";
	const QString cachePath = cacheDir + kind + ".html";
	const QByteArray stamp = builtFor.toString(Qt::ISODate).toUtf8() + '\n';
	QFile cache(cachePath);
	if (cache.open(QIODevice::ReadOnly)) {
		const QByteArray data = cache.readAll();
		if (data.startsWith(stamp)) {
			const auto people = ComposerDay::parseOnThisDay(data.mid(stamp.size()), builtFor, birth);
			if (!people.isEmpty()) {
				dayPeople.append(people);
				if (--pendingDays == 0) populate();
				return;
			}
		}
	}
	const QString month = QLocale(QLocale::English).monthName(builtFor.month()).toLower();
	NetworkJob* job = NetworkAccessManager::self()->get(QUrl("https://www.onthisday.com/music/" + kind + '/' + month + '/' + QString::number(builtFor.day())), 15000);
	dayJobs.append(job);
	const quint32 requestGeneration = generation;
	connect(job, &NetworkJob::finished, this, [this, job, requestGeneration, birth, cacheDir, cachePath, stamp]() {
		dayJobs.removeAll(job);
		const QByteArray data = job->readAll();
		const bool ok = job->ok();
		job->deleteLater();
		if (generation != requestGeneration) return;
		const auto people = ok ? ComposerDay::parseOnThisDay(data, builtFor, birth) : QList<ComposerDay::Anniversary>();
		if (!people.isEmpty()) {
			dayPeople.append(people);
			QDir().mkpath(cacheDir);
			QSaveFile cache(cachePath);
			if (cache.open(QIODevice::WriteOnly)) { cache.write(stamp + data); cache.commit(); }
		} else {
			for (const auto& person : ComposerDay::anniversariesFor(calendar, builtFor)) {
				if (person.birth == birth) dayPeople.append(person);
			}
			emit error(tr("Could not load On This Day %1; using the bundled composer calendar.").arg(birth ? tr("birthdays") : tr("deaths")));
		}
		if (--pendingDays == 0) populate();
	});
}

void ComposerDayPage::populate()
{
	const auto anniversaries = dayPeople;
	for (const ComposerDay::Anniversary& anniversary : anniversaries) {
		Lookup lookup;
		lookup.anniversary = anniversary;
		lookup.result.name = displayName(anniversary.name, anniversary.zh);
		lookup.result.birth = anniversary.birth;
		lookup.result.description = (anniversary.birth ? tr("Born %1 years ago today, on %2") : tr("Died %1 years ago today, on %2")).arg(anniversary.years).arg(anniversary.date);
		lookups.append(lookup);
	}
	model.setComposers({});

	if (lookups.isEmpty()) {
		view->hideSpinner();
		return;
	}

	view->showSpinner();
	for (int i = 0; i < lookups.count(); ++i) {
		startLookup(i);
	}
}

void ComposerDayPage::startLookup(int index)
{
	Lookup& lookup = lookups[index];

	const QString surname = WorkInfo::composerSurname(lookup.anniversary.name);
	const QStringList fields = {QStringLiteral("Composer"), QStringLiteral("Artist"), QStringLiteral("AlbumArtist")};
	lookup.pendingSearches = fields.size();
	for (const QString& field : fields) {
		emit search("search " + field.toUtf8() + " " + MPDConnection::encodeName(surname.isEmpty() ? lookup.anniversary.name : surname),
		            "CD:" + QString::number(generation) + ':' + QString::number(index));
	}
}

void ComposerDayPage::searchResponse(const QString& id, const QList<Song>& songs)
{
	const QStringList parts = id.split(QLatin1Char(':'));
	if (3 != parts.count() || QLatin1String("CD") != parts.at(0) || parts.at(1).toUInt() != generation) {
		return;
	}
	const int index = parts.at(2).toInt();
	if (index < 0 || index >= lookups.count() || lookups.at(index).haveTracks) {
		return;
	}

	Lookup& lookup = lookups[index];
	QSet<QString> seen;
	for (const auto& song : lookup.songs) seen.insert(song.file);
	for (const Song& song : songs) {
		if (seen.contains(song.file)) continue;
		const QString tagged = song.hasComposer() ? song.composer() : QString();
		const QString resolved = ComposerTable::resolve(tagged);
		const bool composerMatch = lookup.anniversary.composer &&
		    (resolved.isEmpty() ? ComposerDay::musicianMatches(tagged, lookup.anniversary.name, lookup.anniversary.aliases) : resolved == lookup.anniversary.name);
		if (!composerMatch && !ComposerDay::musicianMatches(song.artist, lookup.anniversary.name, lookup.anniversary.aliases)
		    && !ComposerDay::musicianMatches(song.albumArtist(), lookup.anniversary.name, lookup.anniversary.aliases)) continue;
		seen.insert(song.file);
		ComposerDay::Track track;
		track.file = song.file;
		track.title = song.title;
		track.album = song.album;
		track.artist = song.artist;
		track.albumArtist = song.albumArtist();
		track.composer = tagged;
		track.genre = song.displayGenre();
		track.disc = song.disc;
		track.track = song.track;
		track.year = song.year;
		track.secs = song.time;
		lookup.songs.append(song);
		lookup.tracks.append(track);
	}
	if (--lookup.pendingSearches > 0) return;
	lookup.haveTracks = true;
	if (lookup.tracks.isEmpty() || !TranslationService::self()->isEnabled()) {
		lookup.haveWorks = true;
		maybeFinish(index);
		return;
	}
	lookup.llmSource = QStringLiteral("Musician: ") + lookup.anniversary.name + "\nWorks wanted: 10\nProfession: " + (lookup.anniversary.description.isEmpty() ? QStringLiteral("composer") : lookup.anniversary.description);
	const QString answer = TranslationService::self()->translate(lookup.llmSource, constWorksContext);
	if (answer != lookup.llmSource) worksReady(lookup.llmSource, constWorksContext, answer);
}

void ComposerDayPage::worksReady(const QString& source, const QString& context, const QString& translation)
{
	if (constWorksContext != context) {
		return;
	}
	for (int i = 0; i < lookups.count(); ++i) {
		Lookup& lookup = lookups[i];
		if (lookup.haveWorks || lookup.llmSource != source) {
			continue;
		}
		lookup.works = ComposerDay::parseWorks(translation);
		lookup.haveWorks = true;
		lookup.llmSource.clear();
		maybeFinish(i);
	}
}

void ComposerDayPage::maybeFinish(int index)
{
	Lookup& lookup = lookups[index];
	if (!lookup.haveTracks || !lookup.haveWorks || lookup.finished) {
		return;
	}
	lookup.finished = true;

	ComposerDayModel::Composer& composer = lookup.result;
	composer.loading = false;
	composer.works.clear();
	QSet<QString> usedFiles;
	const auto addWorks = [&](const QList<ComposerDay::Work>& works) {
		for (const auto& work : works) {
			if (composer.works.size() >= ComposerDay::constWorksPerComposer) break;
			ComposerDayModel::Work entry;
			entry.title = displayName(work.displayTitle(), work.zh);
			QStringList descriptions;
			const auto selectedRecordings = DayRecordings::selectRecordings(lookup.anniversary.name, lookup.tracks, work, recordings);
			for (const auto& recording : selectedRecordings) {
				bool overlaps = false;
				for (int i : recording.indices) if (usedFiles.contains(lookup.tracks.at(i).file)) overlaps = true;
				if (recording.indices.isEmpty() || overlaps) continue;
				const Song& first = lookup.songs.at(recording.indices.first());
				quint32 secs = 0;
				for (int i : recording.indices) {
					entry.files.append(lookup.tracks.at(i).file);
					usedFiles.insert(lookup.tracks.at(i).file);
					secs += lookup.songs.at(i).time;
				}
				descriptions.append(tr("%1 - %2 (%3 tracks, %4)").arg(first.albumArtist(), first.album).arg(recording.indices.size()).arg(Utils::formatTime(secs))
				    + " - " + (recording.recommended ? tr("Recommended recording") : tr("Library recording (recommendation unverified)")));
			}
			if (entry.files.isEmpty()) continue;
			entry.description = descriptions.join(QStringLiteral("\n"));
			composer.works.append(entry);
		}
	};
	// Famous works take precedence; without playable suggestions use the
	// library's most-recorded works, never an unbounded whole-album fallback.
	addWorks(lookup.works);
	if (composer.works.isEmpty()) addWorks(ComposerDay::worksFromLibrary(lookup.tracks));
	composer.description += " - " + tr("%1 works in your library").arg(composer.works.size());

	for (const Lookup& other : lookups) {
		if (!other.finished) {
			return;
		}
	}
	QList<ComposerDayModel::Composer> available;
	for (const auto& item : lookups) available.append(item.result);
	model.setComposers(available);
	view->hideSpinner();
	controlActions();
	for (int row = 0; row < model.composers().count(); ++row) {
		if (!model.composers().at(row).files().isEmpty()) {
			view->expand(model.composerIndex(row).parent(), true);
			view->expand(model.composerIndex(row), true);
		}
	}
}

QStringList ComposerDayPage::selectedFiles(bool allowPlaylists) const
{
	Q_UNUSED(allowPlaylists)
	return model.files(view->selectedIndexes());
}

void ComposerDayPage::addSelectionToPlaylist(const QString& name, int action, quint8 priority, bool decreasePriority)
{
	const QStringList files = selectedFiles();
	if (files.isEmpty()) {
		return;
	}
	if (name.isEmpty()) {
		emit add(files, action, priority, decreasePriority);
	}
	else {
		emit addSongsToPlaylist(name, files);
	}
	view->clearSelection();
}

#include "moc_composerdaypage.cpp"
