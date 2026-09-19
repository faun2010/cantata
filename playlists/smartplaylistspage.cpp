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

#include "smartplaylistspage.h"
#include "gui/stdactions.h"
#include "models/mpdlibrarymodel.h"
#include "mpd-interface/mpdconnection.h"
#include "network/translationservice.h"
#include "playlistrulesdialog.h"
#include "smartfilter.h"
#include "smartplaylists.h"
#include "support/action.h"
#include "support/configuration.h"
#include "support/messagebox.h"
#include "support/utils.h"
#include "widgets/icons.h"
#include <QRandomGenerator>
#include <algorithm>

SmartPlaylistsPage::SmartPlaylistsPage(QWidget* p)
	: SinglePageWidget(p)
{
	addAction = new Action(Icons::self()->addNewItemIcon, tr("Add"), this);
	editAction = new Action(Icons::self()->editIcon, tr("Edit"), this);
	removeAction = new Action(Icons::self()->removeIcon, tr("Remove"), this);

	ToolButton* addBtn = new ToolButton(this);
	ToolButton* editBtn = new ToolButton(this);
	ToolButton* removeBtn = new ToolButton(this);

	addBtn->setDefaultAction(addAction);
	editBtn->setDefaultAction(editAction);
	removeBtn->setDefaultAction(removeAction);

	connect(this, SIGNAL(search(QByteArray, QString)), MPDConnection::self(), SLOT(search(QByteArray, QString)));
	connect(MPDConnection::self(), SIGNAL(searchResponse(QString, QList<Song>)), this, SLOT(searchResponse(QString, QList<Song>)));
	connect(this, SIGNAL(getRating(QString)), MPDConnection::self(), SLOT(getRating(QString)));
	connect(MPDConnection::self(), SIGNAL(rating(QString, quint8)), this, SLOT(rating(QString, quint8)));
	connect(TranslationService::self(), SIGNAL(translationReady(QString, QString, QString)), this, SLOT(llmFilterReady(QString, QString, QString)));
	connect(view, SIGNAL(itemsSelected(bool)), this, SLOT(controlActions()));
	connect(view, SIGNAL(headerClicked(int)), SLOT(headerClicked(int)));
	connect(addAction, SIGNAL(triggered()), SLOT(addNew()));
	connect(editAction, SIGNAL(triggered()), SLOT(edit()));
	connect(removeAction, SIGNAL(triggered()), SLOT(remove()));

	proxy.setSourceModel(SmartPlaylists::self());
	view->setModel(&proxy);
	connect(&proxy, &ProxyModel::filterUpdatedAsync, this, &SmartPlaylistsPage::doSearch);
	view->setDeleteAction(removeAction);
	view->setMode(ItemView::Mode_List);
	controlActions();
	Configuration config(metaObject()->className());
	view->load(config);
	controls = QList<QWidget*>() << addBtn << editBtn << removeBtn;
	init(ReplacePlayQueue | AppendToPlayQueue, QList<QWidget*>(), controls);

	view->addAction(editAction);
	view->addAction(removeAction);
	view->alwaysShowHeader();
	view->setInfoText(tr("A 'smart' playlist contains a set of rules to select tracks from your music library to play. "
	                     "The playlist also controls the order in which tracks are added. "
	                     "Unlike 'dynamic' playlists, the play queue is not dynamically updated.")
	                  + QLatin1String("\n\n\n") + tr("Use the + icon (below) to create a new 'smart' playlist."));
}

SmartPlaylistsPage::~SmartPlaylistsPage()
{
	Configuration config(metaObject()->className());
	view->save(config);
}

void SmartPlaylistsPage::doSearch()
{
	QString text = view->searchText().trimmed();
	proxy.update(text);
	if (proxy.enabled() && !proxy.filterText().isEmpty()) {
		view->expandAll();
	}
}

void SmartPlaylistsPage::controlActions()
{
	QModelIndexList selected = qobject_cast<RulesPlaylists*>(sender()) ? QModelIndexList() : view->selectedIndexes(false);// Dont need sorted selection here...
	StdActions::self()->enableAddToPlayQueue(1 == selected.count());
	editAction->setEnabled(1 == selected.count());
	removeAction->setEnabled(selected.count());
}

void SmartPlaylistsPage::addNew()
{
	PlaylistRulesDialog* dlg = new PlaylistRulesDialog(this, SmartPlaylists::self());
	dlg->edit(QString());
}

void SmartPlaylistsPage::edit()
{
	QModelIndexList selected = view->selectedIndexes(false);// Dont need sorted selection here...

	if (1 != selected.count()) {
		return;
	}

	PlaylistRulesDialog* dlg = new PlaylistRulesDialog(this, SmartPlaylists::self());
	dlg->edit(selected.at(0).data(Qt::DisplayRole).toString());
}

void SmartPlaylistsPage::remove()
{
	QModelIndexList selected = view->selectedIndexes();

	if (selected.isEmpty() || MessageBox::No == MessageBox::warningYesNo(this, tr("Are you sure you wish to remove the selected rules?\n\nThis cannot be undone."), tr("Remove Smart Rules"), StdGuiItem::remove(), StdGuiItem::cancel())) {
		return;
	}

	QStringList names;
	for (const QModelIndex& idx : selected) {
		names.append(idx.data(Qt::DisplayRole).toString());
	}

	for (const QString& name : names) {
		SmartPlaylists::self()->del(name);
	}
}

void SmartPlaylistsPage::headerClicked(int level)
{
	if (0 == level) {
		emit close();
	}
}

void SmartPlaylistsPage::enableWidgets(bool enable)
{
	for (QWidget* c : controls) {
		c->setEnabled(enable);
	}

	view->setEnabled(enable);
}

void SmartPlaylistsPage::searchResponse(const QString& id, const QList<Song>& songs)
{
	if (id.length() < 3 || id.mid(2).toUInt() != command.id || command.isEmpty()) {
		return;
	}

	if (id.startsWith("I:")) {
		command.songs.unite(Utils::listToSet(songs));
	}
	else if (id.startsWith("E:")) {
		command.songs.subtract(Utils::listToSet(songs));
	}

	if (command.includeRules.isEmpty()) {
		if (command.songs.isEmpty()) {
			command.clear();
			emit error(tr("Failed to locate any matching songs"));
			return;
		}
		if (command.excludeRules.isEmpty()) {
			filterCommand();
		}
		else {
			emit search(command.excludeRules.takeFirst(), "E:" + QString::number(command.id));
		}
	}
	else {
		emit search(command.includeRules.takeFirst(), "I:" + QString::number(command.id));
	}
}

void SmartPlaylistsPage::filterCommand()
{
	bool filterDuration = command.minDuration > 0 || command.maxDuration > 0;
	bool filterAge = command.maxAge > 0;

	if (filterDuration || filterAge) {
		uint maxAge = time(nullptr) - (command.maxAge * 24 * 60 * 60);
		QSet<Song> toRemove;
		for (const auto& s : command.songs) {
			if ((filterAge && s.lastModified < maxAge) || (filterDuration && (((quint32)command.minDuration > s.time || (command.maxDuration > 0 && s.time > (quint32)command.maxDuration))))) {
				toRemove.insert(s);
			}
			else {
				command.toCheck.append(s.file);
			}
		}
		command.songs.subtract(toRemove);
		if (command.songs.isEmpty()) {
			command.clear();
			emit error(tr("Failed to locate any matching songs"));
			return;
		}
	}

	if (command.filterRating || command.fetchRatings || command.includeUnrated) {
		if (command.toCheck.isEmpty()) {
			for (const auto& s : command.songs) {
				command.toCheck.append(s.file);
			}
		}
		command.checking = command.toCheck.takeFirst();
		emit getRating(command.checking);
	}
	else {
		maybeStartLlmFilter();
	}
}

void SmartPlaylistsPage::rating(const QString& file, quint8 val)
{
	if (command.isEmpty() || file != command.checking) {
		return;
	}

	for (auto& s : command.songs) {
		if (s.file == file) {
			s.rating = val;
			if (command.filterRating && (val < command.ratingFrom || val > command.ratingTo) && !(command.includeUnrated && val == 0)) {
				command.songs.remove(s);
			}
			break;
		}
	}

	if (command.toCheck.isEmpty()) {
		command.checking.clear();
		maybeStartLlmFilter();
	}
	else {
		command.checking = command.toCheck.takeFirst();
		emit getRating(command.checking);
	}
}

static bool sortAscending = true;
static bool composerSort(const Song& s1, const Song& s2)
{
	const QString v1 = s1.hasComposer() ? s1.composer() : QString();
	const QString v2 = s2.hasComposer() ? s2.composer() : QString();
	int c = Utils::compare(v1, v2);
	return sortAscending ? (c < 0 || (c == 0 && s1 < s2)) : (c > 0 || (c == 0 && s1 < s2));
}

static bool artistSort(const Song& s1, const Song& s2)
{
	const QString v1 = s1.hasArtistSort() ? s1.artistSort() : s1.artist;
	const QString v2 = s2.hasArtistSort() ? s2.artistSort() : s2.artist;
	int c = Utils::compare(v1, v2);
	return sortAscending ? (c < 0 || (c == 0 && s1 < s2)) : (c > 0 || (c == 0 && s1 < s2));
}

static bool albumArtistSort(const Song& s1, const Song& s2)
{
	const QString v1 = s1.hasAlbumArtistSort() ? s1.albumArtistSort() : s1.albumArtistOrComposer();
	const QString v2 = s2.hasAlbumArtistSort() ? s2.albumArtistSort() : s2.albumArtistOrComposer();
	int c = Utils::compare(v1, v2);
	return sortAscending ? (c < 0 || (c == 0 && s1 < s2)) : (c > 0 || (c == 0 && s1 < s2));
}

static bool albumSort(const Song& s1, const Song& s2)
{
	const QString v1 = s1.hasAlbumSort() ? s1.albumSort() : s1.album;
	const QString v2 = s2.hasAlbumSort() ? s2.albumSort() : s2.album;
	int c = Utils::compare(v1, v2);
	return sortAscending ? (c < 0 || (c == 0 && s1 < s2)) : (c > 0 || (c == 0 && s1 < s2));
}

static bool titleSort(const Song& s1, const Song& s2)
{
	const QString v1 = s1.title;
	const QString v2 = s2.title;
	int c = Utils::compare(v1, v2);
	return sortAscending ? (c < 0 || (c == 0 && s1 < s2)) : (c > 0 || (c == 0 && s1 < s2));
}

static bool genreSort(const Song& s1, const Song& s2)
{
	int c = s1.compareGenres(s2);
	return sortAscending ? (c < 0 || (c == 0 && s1 < s2)) : (c > 0 || (c == 0 && s1 < s2));
}

static bool dateSort(const Song& s1, const Song& s2)
{
	return sortAscending ? (s1.year < s2.year || (s1.year == s2.year && s1 < s2)) : (s1.year > s2.year || (s1.year == s2.year && s1 < s2));
}

static bool ratingSort(const Song& s1, const Song& s2)
{
	return sortAscending ? (s1.rating < s2.rating || (s1.rating == s2.rating && s1 < s2))
						 : (s1.rating > s2.rating || (s1.rating == s2.rating && s1 < s2));
}

static bool ageSort(const Song& s1, const Song& s2)
{
	return sortAscending ? (s1.lastModified < s2.lastModified || (s1.lastModified == s2.lastModified && s1 < s2))
						 : (s1.lastModified > s2.lastModified || (s1.lastModified == s2.lastModified && s1 < s2));
}

static void sortSongs(QList<Song>& songs, RulesPlaylists::Order order, bool ascending)
{
	sortAscending = ascending;
	switch (order) {
	case RulesPlaylists::Order_AlbumArtist:
		std::sort(songs.begin(), songs.end(), albumArtistSort);
		break;
	case RulesPlaylists::Order_Artist:
		std::sort(songs.begin(), songs.end(), artistSort);
		break;
	case RulesPlaylists::Order_Album:
		std::sort(songs.begin(), songs.end(), albumSort);
		break;
	case RulesPlaylists::Order_Composer:
		std::sort(songs.begin(), songs.end(), composerSort);
		break;
	case RulesPlaylists::Order_Date:
		std::sort(songs.begin(), songs.end(), dateSort);
		break;
	case RulesPlaylists::Order_Genre:
		std::sort(songs.begin(), songs.end(), genreSort);
		break;
	case RulesPlaylists::Order_Rating:
		std::sort(songs.begin(), songs.end(), ratingSort);
		break;
	case RulesPlaylists::Order_Title:
		std::sort(songs.begin(), songs.end(), titleSort);
		break;
	case RulesPlaylists::Order_Age:
		std::sort(songs.begin(), songs.end(), ageSort);
		break;
	default:
	case RulesPlaylists::Order_Random:
		std::shuffle(songs.begin(), songs.end(), *QRandomGenerator::global());
	}
}

void SmartPlaylistsPage::maybeStartLlmFilter()
{
	if (command.description.isEmpty() || !TranslationService::self()->isEnabled()) {
		addSongsToPlayQueue();
		return;
	}
	if (command.songs.isEmpty()) {
		command.clear();
		emit error(tr("Failed to locate any matching songs"));
		return;
	}

	QList<Song> candidates = command.songs.values();
	sortSongs(candidates, command.order, command.orderAscending);
	if (candidates.count() > SmartFilter::constMaxCandidates) {
		candidates.resize(SmartFilter::constMaxCandidates);
	}
	command.llmCandidates = candidates;
	// The candidate list is what we asked the LLM about, so it is also what we
	// must fall back to if it fails - otherwise a >constMaxCandidates match
	// would queue a different set of songs depending on whether the LLM worked.
	command.songs = QSet<Song>(candidates.constBegin(), candidates.constEnd());
	QList<SmartFilter::Candidate> llmInput;
	for (const Song& s : candidates) {
		SmartFilter::Candidate c;
		c.title = s.title;
		c.artist = s.artist;
		c.album = s.album;
		if (s.hasComposer()) c.composer = s.composer();
		c.genre = s.displayGenre();
		c.year = s.year;
		c.secs = s.time;
		llmInput.append(c);
	}
	command.llmSource = SmartFilter::buildSource(command.description, llmInput, llmInput.count());
	command.awaitingLlm = true;
	const QString answer = TranslationService::self()->translate(command.llmSource, QLatin1String("smart-filter-v1"));
	if (answer != command.llmSource) {
		// Cache hit - handle synchronously via the same path as the signal.
		llmFilterReady(command.llmSource, QLatin1String("smart-filter-v1"), answer);
	}
}

void SmartPlaylistsPage::llmFilterReady(const QString& source, const QString& context, const QString& translation)
{
	if (!command.awaitingLlm || QLatin1String("smart-filter-v1") != context || source != command.llmSource) {
		return;
	}
	command.awaitingLlm = false;

	bool ok = false;
	QList<int> selected;
	if (translation != source) {
		selected = SmartFilter::parseSelection(translation, command.llmCandidates.count(), &ok);
	}
	if (ok) {
		QList<Song> filtered;
		for (int i : selected) {
			filtered.append(command.llmCandidates.at(i));
		}
		if (!filtered.isEmpty()) {
			command.songs = QSet<Song>(filtered.constBegin(), filtered.constEnd());
			command.orderedSongs = filtered;
		}
		else {
			ok = false;
		}
	}
	command.llmCandidates.clear();
	command.llmSource.clear();
	// !ok: the LLM failed or returned nothing usable - fall back to the
	// unfiltered rule results rather than adding nothing.
	addSongsToPlayQueue();
}

void SmartPlaylistsPage::addSongsToPlayQueue()
{
	if (command.songs.isEmpty()) {
		command.clear();
		emit error(tr("Failed to locate any matching songs"));
		return;
	}

	QList<Song> songs = command.orderedSongs.isEmpty() ? command.songs.values() : command.orderedSongs;
	command.songs.clear();
	const bool llmOrdered = !command.orderedSongs.isEmpty();
	command.orderedSongs.clear();

	// A curated order from the LLM is the answer to the user's description -
	// re-sorting it by 'order' (random, by default) would throw it away.
	if (!llmOrdered) {
		sortSongs(songs, command.order, command.orderAscending);
	}

	QStringList files;
	for (int i = 0; i < command.numTracks && !songs.isEmpty(); ++i) {
		files.append(songs.takeFirst().file);
	}
	if (!files.isEmpty()) {
		emit add(files, command.action, command.priority, command.decreasePriority);
		view->clearSelection();
	}
	command.clear();
}

void SmartPlaylistsPage::addSelectionToPlaylist(const QString& name, int action, quint8 priority, bool decreasePriority)
{
	if (!name.isEmpty()) {
		return;
	}

	QModelIndexList selected = view->selectedIndexes(false);
	if (1 != selected.count()) {
		return;
	}

	QModelIndex idx = proxy.mapToSource(selected.at(0));
	if (!idx.isValid()) {
		return;
	}
	RulesPlaylists::Entry pl = SmartPlaylists::self()->entry(idx.row());
	if (pl.name.isEmpty() || pl.numTracks <= 0) {
		return;
	}

	if (command.awaitingLlm) {
		TranslationService::self()->cancel(command.llmSource, QLatin1String("smart-filter-v1"));
	}
	command = Command(pl, action, priority, decreasePriority, command.id + 1);

	QList<RulesPlaylists::Rule>::ConstIterator it = pl.rules.constBegin();
	QList<RulesPlaylists::Rule>::ConstIterator end = pl.rules.constEnd();
	QSet<QString> mpdGenres;

	for (; it != end; ++it) {
		QList<int> dates;
		QByteArray match = "find";
		bool isInclude = true;
		RulesPlaylists::Rule::ConstIterator rIt = (*it).constBegin();
		RulesPlaylists::Rule::ConstIterator rEnd = (*it).constEnd();
		QByteArray baseRule;
		QStringList genres;

		for (; rIt != rEnd; ++rIt) {
			if (RulesPlaylists::constDateKey == rIt.key()) {
				QStringList parts = rIt.value().trimmed().split(RulesPlaylists::constRangeSep);
				if (2 == parts.length()) {
					int from = parts.at(0).toInt();
					int to = parts.at(1).toInt();
					if (from > to) {
						for (int i = to; i <= from; ++i) {
							dates.append(i);
						}
					}
					else {
						for (int i = from; i <= to; ++i) {
							dates.append(i);
						}
					}
				}
				else if (1 == parts.length()) {
					dates.append(parts.at(0).toInt());
				}
			}
			else if (RulesPlaylists::constGenreKey == rIt.key() && rIt.value().trimmed().endsWith("*")) {
				QString find = rIt.value().left(rIt.value().length() - 1);
				if (!find.isEmpty()) {
					if (mpdGenres.isEmpty()) {
						mpdGenres = MpdLibraryModel::self()->getGenres();
					}
					for (const QString& g : mpdGenres) {
						if (g.startsWith(find, Qt::CaseInsensitive)) {
							genres.append(g);
						}
					}
				}
				if (genres.isEmpty()) {
					// No genres matching pattern - add dummy genre, so that no tracks will be found
					genres.append("XXXXXXXXX");
				}
			}
			else if (RulesPlaylists::constArtistKey == rIt.key() || RulesPlaylists::constAlbumKey == rIt.key() || RulesPlaylists::constAlbumArtistKey == rIt.key() || RulesPlaylists::constComposerKey == rIt.key() || RulesPlaylists::constCommentKey == rIt.key() || RulesPlaylists::constTitleKey == rIt.key() || RulesPlaylists::constSimilarArtistsKey == rIt.key() || RulesPlaylists::constGenreKey == rIt.key() || RulesPlaylists::constFileKey == rIt.key()) {
				baseRule += " " + rIt.key().toUtf8() + " " + MPDConnection::encodeName(rIt.value());
			}
			else if (RulesPlaylists::constExactKey == rIt.key()) {
				if ("false" == rIt.value()) {
					match = "search";
				}
			}
			else if (RulesPlaylists::constExcludeKey == rIt.key()) {
				if ("true" == rIt.value()) {
					isInclude = false;
				}
			}
		}

		if (!baseRule.isEmpty() || !genres.isEmpty() || !dates.isEmpty()) {
			QList<QByteArray> rules;
			if (genres.isEmpty()) {
				if (dates.isEmpty()) {
					rules.append(match + baseRule);
				}
				else {
					for (int d : dates) {
						rules.append(match + baseRule + " Date \"" + QByteArray::number(d) + "\"");
					}
				}
			}
			else {
				for (const QString& genre : genres) {
					QByteArray rule = match + baseRule + " Genre  " + MPDConnection::encodeName(genre);
					if (dates.isEmpty()) {
						rules.append(rule);
					}
					else {
						for (int d : dates) {
							rules.append(rule + " Date \"" + QByteArray::number(d) + "\"");
						}
					}
				}
			}
			if (!rules.isEmpty()) {
				if (isInclude) {
					command.includeRules += rules;
				}
				else {
					command.excludeRules += rules;
				}
			}
		}
	}

	command.filterRating = command.haveRating();
	command.fetchRatings = RulesPlaylists::Order_Rating == command.order;
	if (command.includeRules.isEmpty()) {
		if (command.haveRating()) {
			command.includeRules.append("RATING:" + QByteArray::number(command.ratingFrom) + ":" + QByteArray::number(command.ratingTo));
			command.filterRating = false;
			command.fetchRatings = false;
		}
		else {
			command.includeRules.append(QByteArray());
		}
	}
	emit search(command.includeRules.takeFirst(), "I:" + QString::number(command.id));
}

#include "moc_smartplaylistspage.cpp"
