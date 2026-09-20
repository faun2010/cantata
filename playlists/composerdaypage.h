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

#ifndef COMPOSER_DAY_PAGE_H
#define COMPOSER_DAY_PAGE_H

#include "composerday.h"
#include "composerdaymodel.h"
#include "mpd-interface/song.h"
#include "widgets/singlepagewidget.h"
#include <QDate>
#include <QList>

// "Composer of the Day": the composers whose birth or death anniversary is
// today, each with their ten most famous works and one recording of each
// taken from the library. The list only ever holds today's composers - it is
// rebuilt when the day rolls over, or when the user refreshes.
class ComposerDayPage : public SinglePageWidget {
	Q_OBJECT

public:
	ComposerDayPage(QWidget* p);
	~ComposerDayPage() override;
	void setView(int) override {}
	QStringList selectedFiles(bool allowPlaylists = false) const override;

Q_SIGNALS:
	void search(const QByteArray& query, const QString& id);
	void error(const QString& str);

public Q_SLOTS:
	void refresh() override;

private Q_SLOTS:
	void searchResponse(const QString& id, const QList<Song>& songs);
	void worksReady(const QString& source, const QString& context, const QString& translation);
	void connectionStateChanged(bool connected);

private:
	void showEvent(QShowEvent* e) override;
	void controlActions() override;
	void addSelectionToPlaylist(const QString& name, int action, quint8 priority, bool decreasePriority) override;
	void rebuild(bool force);
	void startLookup(int index);
	void maybeFinish(int index);
	void cancelLookups();

private:
	// One composer's in-flight lookup: the library tracks tagged with them,
	// and the LLM's list of their most famous works.
	struct Lookup {
		ComposerDay::Anniversary anniversary;
		QList<Song> songs;
		QList<ComposerDay::Track> tracks;
		QList<ComposerDay::Work> works;
		QString llmSource;
		bool haveTracks = false;
		bool haveWorks = false;
		bool finished = false;
	};

	ComposerDayModel model;
	QList<ComposerDay::Composer> calendar;
	QList<Lookup> lookups;
	QDate builtFor;
	// Bumped on every rebuild, so late replies to a previous day's (or a
	// cancelled) lookup are ignored.
	quint32 generation = 0;
};

#endif
