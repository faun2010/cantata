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

#include "composerdaymodel.h"
#include "models/roles.h"
#include "support/icon.h"
#include <QSet>

QStringList ComposerDayModel::Composer::files() const
{
	QStringList result;
	for (const Work& work : works) {
		result += work.files;
	}
	return result;
}

ComposerDayModel::ComposerDayModel(QObject* p)
	: ActionModel(p)
{
	composerIcon = Icon::fa(fa::fa_solid, fa::fa_birthday_cake);
	workIcon = Icon::fa(fa::fa_solid, fa::fa_music);
}

void ComposerDayModel::setComposers(const QList<Composer>& composers)
{
	beginResetModel();
	composerList = composers;
	endResetModel();
}

void ComposerDayModel::updateComposer(int row, const Composer& composer)
{
	if (row < 0 || row >= composerList.count()) {
		return;
	}

	// The work rows are replaced wholesale, so remove the old ones before
	// swapping the composer in and insert the new ones afterwards.
	const QModelIndex parent = index(row, 0, QModelIndex());
	const int oldCount = composerList.at(row).works.count();
	if (oldCount > 0) {
		beginRemoveRows(parent, 0, oldCount - 1);
		composerList[row].works.clear();
		endRemoveRows();
	}
	composerList[row] = composer;
	const int newCount = composer.works.count();
	if (newCount > 0) {
		beginInsertRows(parent, 0, newCount - 1);
		composerList[row].works = composer.works;
		endInsertRows();
	}
	emit dataChanged(parent, parent);
}

QStringList ComposerDayModel::files(const QModelIndexList& indexes) const
{
	QStringList result;
	QSet<QString> seen;
	for (const QModelIndex& idx : indexes) {
		if (!idx.isValid()) {
			continue;
		}
		QStringList files;
		const int parent = parentRow(idx);
		if (parent < 0) {
			if (idx.row() < composerList.count()) {
				files = composerList.at(idx.row()).files();
			}
		}
		else if (parent < composerList.count() && idx.row() < composerList.at(parent).works.count()) {
			files = composerList.at(parent).works.at(idx.row()).files;
		}
		for (const QString& file : files) {
			if (!seen.contains(file)) {
				seen.insert(file);
				result.append(file);
			}
		}
	}
	return result;
}

int ComposerDayModel::parentRow(const QModelIndex& index)
{
	return (int)index.internalId() - 1;
}

QModelIndex ComposerDayModel::index(int row, int column, const QModelIndex& parent) const
{
	if (row < 0 || column != 0) {
		return QModelIndex();
	}
	if (!parent.isValid()) {
		return row < composerList.count() ? createIndex(row, column, quintptr(0)) : QModelIndex();
	}
	if (parentRow(parent) >= 0 || parent.row() >= composerList.count()) {
		return QModelIndex();
	}
	return row < composerList.at(parent.row()).works.count() ? createIndex(row, column, quintptr(parent.row() + 1)) : QModelIndex();
}

QModelIndex ComposerDayModel::parent(const QModelIndex& index) const
{
	const int row = index.isValid() ? parentRow(index) : -1;
	return row < 0 || row >= composerList.count() ? QModelIndex() : createIndex(row, 0, quintptr(0));
}

int ComposerDayModel::rowCount(const QModelIndex& parent) const
{
	if (!parent.isValid()) {
		return composerList.count();
	}
	if (parentRow(parent) >= 0 || parent.row() >= composerList.count()) {
		return 0;
	}
	return composerList.at(parent.row()).works.count();
}

bool ComposerDayModel::hasChildren(const QModelIndex& parent) const
{
	return rowCount(parent) > 0;
}

QVariant ComposerDayModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid()) {
		return ActionModel::data(index, role);
	}

	const int parent = parentRow(index);
	if (parent < 0) {
		if (index.row() >= composerList.count()) {
			return QVariant();
		}
		const Composer& composer = composerList.at(index.row());
		switch (role) {
		case Qt::DisplayRole:
		case Cantata::Role_MainText:
		case Cantata::Role_BriefMainText:
			return composer.name;
		case Qt::ToolTipRole:
		case Cantata::Role_SubText:
			return composer.description;
		case Qt::DecorationRole:
			return composerIcon;
		case Cantata::Role_Actions:
			return ActionModel::data(index, role);
		default:
			return QVariant();
		}
	}

	if (parent >= composerList.count() || index.row() >= composerList.at(parent).works.count()) {
		return QVariant();
	}
	const Work& work = composerList.at(parent).works.at(index.row());
	switch (role) {
	case Qt::DisplayRole:
	case Cantata::Role_MainText:
	case Cantata::Role_BriefMainText:
		return work.title;
	case Qt::ToolTipRole:
	case Cantata::Role_SubText:
		return work.description;
	case Qt::DecorationRole:
		return workIcon;
	case Cantata::Role_Actions:
		return ActionModel::data(index, role);
	default:
		return QVariant();
	}
}

Qt::ItemFlags ComposerDayModel::flags(const QModelIndex& index) const
{
	if (!index.isValid()) {
		return Qt::NoItemFlags;
	}
	// A work with no recording in the library is shown, so the user can see
	// what is missing, but it cannot be selected and queued.
	const int parent = parentRow(index);
	const bool playable = parent < 0
			? index.row() < composerList.count() && !composerList.at(index.row()).files().isEmpty()
			: parent < composerList.count() && index.row() < composerList.at(parent).works.count() && !composerList.at(parent).works.at(index.row()).files.isEmpty();
	return playable ? (Qt::ItemIsSelectable | Qt::ItemIsEnabled) : Qt::ItemIsEnabled;
}

#include "moc_composerdaymodel.cpp"
