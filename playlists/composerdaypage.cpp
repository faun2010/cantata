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

static const QLatin1String constWorksContext("composer-works-v1");

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

	view->setModel(&model);
	view->setMode(ItemView::Mode_DetailedTree);
	view->alwaysShowHeader();
	view->setInfoText(tr("This list follows the calendar: it shows the composers whose birth or death anniversary falls on today's date, "
	                     "each with their most famous works and one recording of each taken from your library.")
	                  + QLatin1String("\n\n\n") + tr("No composer in the calendar was born or died on this date."));
	init(ReplacePlayQueue | AppendToPlayQueue | Refresh);
	controlActions();

	Configuration config(metaObject()->className());
	view->load(config);
	calendar = ComposerDay::parseCalendar(readCalendar());

	dayTimer = new QTimer(this);
	dayTimer->setSingleShot(true);
	connect(dayTimer, SIGNAL(timeout()), this, SLOT(dayChanged()));
}

ComposerDayPage::~ComposerDayPage()
{
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

	const QList<ComposerDay::Anniversary> anniversaries = ComposerDay::anniversariesFor(calendar, today);
	QList<ComposerDayModel::Composer> composers;
	for (const ComposerDay::Anniversary& anniversary : anniversaries) {
		Lookup lookup;
		lookup.anniversary = anniversary;
		lookups.append(lookup);

		ComposerDayModel::Composer composer;
		composer.name = displayName(anniversary.name, anniversary.zh);
		composer.description = (anniversary.birth ? tr("Born %1 years ago today, on %2") : tr("Died %1 years ago today, on %2")).arg(anniversary.years).arg(anniversary.date);
		composers.append(composer);
	}
	model.setComposers(composers);

	if (lookups.isEmpty()) {
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

	// The composer tag is spelled every which way ("Sibelius", "Sibelius,
	// Jean", "Jean Sibelius"), so search on the surname and resolve what
	// comes back - that also keeps a namesake's tracks out.
	const QString surname = WorkInfo::composerSurname(lookup.anniversary.name);
	emit search("search Composer " + MPDConnection::encodeName(surname.isEmpty() ? lookup.anniversary.name : surname),
	            QLatin1String("CD:") + QString::number(generation) + QLatin1Char(':') + QString::number(index));

	lookup.llmSource = ComposerDay::buildWorksSource(lookup.anniversary.name);
	if (!TranslationService::self()->isEnabled()) {
		lookup.haveWorks = true;
		lookup.llmSource.clear();
		return;
	}
	const QString answer = TranslationService::self()->translate(lookup.llmSource, constWorksContext);
	if (answer != lookup.llmSource) {
		// Cache hit - handled synchronously, via the same path as the signal.
		worksReady(lookup.llmSource, constWorksContext, answer);
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
	const QString surname = WorkInfo::composerSurname(lookup.anniversary.name);
	for (const Song& song : songs) {
		if (!song.hasComposer()) {
			continue;
		}
		const QString tagged = song.composer();
		const QString resolved = ComposerTable::resolve(tagged);
		// A composer the table does not know can only be matched on the
		// surname; one it does know must resolve to this very composer, so a
		// namesake (J.S. vs J.C. Bach) is not swept up.
		if (resolved.isEmpty() ? !tagged.contains(surname, Qt::CaseInsensitive) : resolved != lookup.anniversary.name) {
			continue;
		}
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
	lookup.haveTracks = true;
	maybeFinish(index);
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
		return;
	}
}

void ComposerDayPage::maybeFinish(int index)
{
	Lookup& lookup = lookups[index];
	if (!lookup.haveTracks || !lookup.haveWorks || lookup.finished) {
		return;
	}
	lookup.finished = true;

	// Without an LLM answer (disabled, offline, or unparseable) the library
	// itself names the works: the ones the user owns the most recordings of.
	QList<ComposerDay::Work> works = lookup.works;
	if (works.isEmpty()) {
		works = ComposerDay::worksFromLibrary(lookup.tracks);
	}

	ComposerDayModel::Composer composer = model.composers().at(index);
	composer.loading = false;
	composer.works.clear();
	int found = 0;
	for (const ComposerDay::Work& work : works) {
		ComposerDayModel::Work entry;
		entry.title = displayName(work.displayTitle(), work.zh);
		const QList<int> selected = ComposerDay::selectWorkTracks(lookup.tracks, work);
		if (selected.isEmpty()) {
			entry.description = tr("No recording of this work in your library");
		}
		else {
			const Song& first = lookup.songs.at(selected.first());
			quint32 secs = 0;
			for (int trackIndex : selected) {
				entry.files.append(lookup.tracks.at(trackIndex).file);
				secs += lookup.songs.at(trackIndex).time;
			}
			entry.description = tr("%1 - %2 (%3 tracks, %4)").arg(first.albumArtist(), first.album).arg(selected.count()).arg(Utils::formatTime(secs));
			++found;
		}
		composer.works.append(entry);
	}

	if (composer.works.isEmpty()) {
		composer.description += QLatin1String(" - ") + tr("nothing by this composer in your library");
	}
	else {
		composer.description += QLatin1String(" - ") + tr("%1 of %n work(s) in your library", "", composer.works.count()).arg(found);
	}
	model.updateComposer(index, composer);

	for (const Lookup& other : lookups) {
		if (!other.finished) {
			return;
		}
	}
	view->hideSpinner();
	// Only open up the composers the library can actually play; one with no
	// recordings stays a single collapsed line rather than ten dead rows.
	for (int row = 0; row < model.composers().count(); ++row) {
		if (!model.composers().at(row).files().isEmpty()) {
			view->expand(model.index(row, 0, QModelIndex()), true);
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
