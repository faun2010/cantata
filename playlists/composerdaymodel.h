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

#ifndef COMPOSER_DAY_MODEL_H
#define COMPOSER_DAY_MODEL_H

#include "models/actionmodel.h"
#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>

// Today's birthdays and death anniversaries, each containing musicians and
// their works with recordings available in the library. Only today is held.
class ComposerDayModel : public ActionModel {
	Q_OBJECT

public:
	struct Work {
		// "Violin Concerto in D minor, Op.47" (plus the Chinese title when
		// the LLM supplied one).
		QString title;
		// The matched recording ("Karajan - 1965 - Sibelius: Violin
		// Concerto"), or the reason there is none.
		QString description;
		// The recording's tracks, in playing order - empty when the library
		// has no recording of this work.
		QStringList files;
	};

	struct Composer {
		QString name;
		// "Died 69 years ago today - 1957-09-20"
		QString description;
		QList<Work> works;
		// Set while the composer's works are still being looked up.
		bool loading = true;
		bool birth = true;
		QStringList files() const;
	};

	ComposerDayModel(QObject* p = nullptr);
	~ComposerDayModel() override {}

	// Keep only musicians with at least one matched recording. All exposed
	// flat indices refer to this visible list, not the lookup results.
	void setComposers(const QList<Composer>& composers);
	void updateComposer(int row, const Composer& composer);
	const QList<Composer>& composers() const { return composerList; }
	QModelIndex composerIndex(int flatRow) const;
	bool isEmpty() const { return composerList.isEmpty(); }
	// Every file of the selection, de-duplicated, in list order: a composer
	// or anniversary group contributes all of its works' recordings.
	QStringList files(const QModelIndexList& indexes) const;

	QModelIndex index(int row, int column, const QModelIndex& parent) const override;
	QModelIndex parent(const QModelIndex& index) const override;
	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex&) const override { return 1; }
	bool hasChildren(const QModelIndex& parent) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
	// IDs: 0 for groups, 1/2 for musicians in each group, flat row + 3
	// for works. composerList contains only visible musicians.
	int flatRow(const QModelIndex& index) const;
	QStringList rowFiles(const QModelIndex& index) const;

	QList<Composer> composerList;
	QIcon composerIcon;
	QIcon workIcon;
};

#endif
