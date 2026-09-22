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
	QList<Composer> visible;
	for (const Composer& composer : composers) {
		if (!composer.files().isEmpty()) {
			visible.append(composer);
		}
	}
	beginResetModel();
	composerList = visible;
	endResetModel();
}

void ComposerDayModel::updateComposer(int row, const Composer& composer)
{
	if (row < 0 || row >= composerList.count()) {
		return;
	}
	if (composer.files().isEmpty()) {
		beginResetModel();
		composerList.removeAt(row);
		endResetModel();
		return;
	}
	if (composerList.at(row).birth != composer.birth) {
		beginResetModel();
		composerList[row] = composer;
		endResetModel();
		return;
	}

	const QModelIndex musician = composerIndex(row);
	const int oldCount = composerList.at(row).works.count();
	if (oldCount > 0) {
		beginRemoveRows(musician, 0, oldCount - 1);
		composerList[row].works.clear();
		endRemoveRows();
	}
	Composer metadata = composer;
	metadata.works.clear();
	composerList[row] = metadata;
	const int newCount = composer.works.count();
	if (newCount > 0) {
		beginInsertRows(musician, 0, newCount - 1);
		composerList[row].works = composer.works;
		endInsertRows();
	}
	emit dataChanged(musician, musician);
	const QModelIndex group = parent(musician);
	emit dataChanged(group, group);
}

QStringList ComposerDayModel::rowFiles(const QModelIndex& idx) const
{
	if (!idx.isValid() || idx.model() != this) {
		return QStringList();
	}
	if (idx.internalId() == 0) {
		QStringList result;
		for (const Composer& composer : composerList) {
			if (composer.birth == (idx.row() == 0)) {
				result += composer.files();
			}
		}
		return result;
	}
	const int row = flatRow(idx);
	if (row < 0 || row >= composerList.count()) {
		return QStringList();
	}
	const Composer& composer = composerList.at(row);
	return idx.internalId() < 3 ? composer.files()
		: idx.row() < composer.works.count() ? composer.works.at(idx.row()).files : QStringList();
}

QStringList ComposerDayModel::files(const QModelIndexList& indexes) const
{
	QStringList result;
	QSet<QString> seen;
	for (const QModelIndex& idx : indexes) {
		for (const QString& file : rowFiles(idx)) {
			if (!seen.contains(file)) {
				seen.insert(file);
				result.append(file);
			}
		}
	}
	return result;
}

int ComposerDayModel::flatRow(const QModelIndex& idx) const
{
	if (!idx.isValid() || idx.model() != this || idx.internalId() == 0) {
		return -1;
	}
	if (idx.internalId() >= 3) {
		return int(idx.internalId() - 3);
	}
	int groupRow = 0;
	for (int i = 0; i < composerList.count(); ++i) {
		if (composerList.at(i).birth == (idx.internalId() == 1)) {
			if (groupRow++ == idx.row()) {
				return i;
			}
		}
	}
	return -1;
}

QModelIndex ComposerDayModel::composerIndex(int flatRow) const
{
	if (flatRow < 0 || flatRow >= composerList.count()) {
		return QModelIndex();
	}
	const bool birth = composerList.at(flatRow).birth;
	int row = 0;
	for (int i = 0; i < flatRow; ++i) {
		if (composerList.at(i).birth == birth) {
			++row;
		}
	}
	return createIndex(row, 0, quintptr(birth ? 1 : 2));
}

QModelIndex ComposerDayModel::index(int row, int column, const QModelIndex& parent) const
{
	if (row < 0 || column != 0 || row >= rowCount(parent)) {
		return QModelIndex();
	}
	if (!parent.isValid()) {
		return createIndex(row, column, quintptr(0));
	}
	if (parent.internalId() == 0) {
		return createIndex(row, column, quintptr(parent.row() + 1));
	}
	return createIndex(row, column, quintptr(flatRow(parent) + 3));
}

QModelIndex ComposerDayModel::parent(const QModelIndex& idx) const
{
	if (!idx.isValid() || idx.model() != this || idx.internalId() == 0) {
		return QModelIndex();
	}
	return idx.internalId() < 3 ? createIndex(int(idx.internalId()) - 1, 0, quintptr(0))
		: composerIndex(flatRow(idx));
}

int ComposerDayModel::rowCount(const QModelIndex& parent) const
{
	if (!parent.isValid()) {
		return 2;
	}
	if (parent.model() != this || parent.column() != 0 || parent.internalId() >= 3) {
		return 0;
	}
	if (parent.internalId() == 0) {
		int count = 0;
		for (const Composer& composer : composerList) {
			if (composer.birth == (parent.row() == 0)) {
				++count;
			}
		}
		return count;
	}
	const int row = flatRow(parent);
	return row >= 0 && row < composerList.count() ? composerList.at(row).works.count() : 0;
}

bool ComposerDayModel::hasChildren(const QModelIndex& parent) const
{
	return rowCount(parent) > 0;
}

QVariant ComposerDayModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid()) {
		switch (role) {
		case Cantata::Role_TitleText:
			return tr("Today in Music");
		case Cantata::Role_SubText:
			return tr("Birthdays and death anniversaries from On This Day");
		case Qt::DecorationRole:
			return composerIcon;
		default:
			return QVariant();
		}
	}
	if (index.model() != this) {
		return QVariant();
	}

	if (index.internalId() == 0) {
		switch (role) {
		case Qt::DisplayRole:
		case Cantata::Role_MainText:
		case Cantata::Role_BriefMainText:
			return index.row() == 0 ? tr("Today’s Birthdays") : tr("Today’s Death Anniversaries");
		case Qt::DecorationRole:
			return composerIcon;
		case Cantata::Role_Actions:
			return rowFiles(index).isEmpty() ? QVariant() : ActionModel::data(index, role);
		default:
			return QVariant();
		}
	}
	const int parent = flatRow(index);
	if (index.internalId() < 3) {
		if (parent < 0 || parent >= composerList.count()) {
			return QVariant();
		}
		const Composer& composer = composerList.at(parent);
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
			return rowFiles(index).isEmpty() ? QVariant() : ActionModel::data(index, role);
		default:
			return QVariant();
		}
	}

	if (parent < 0 || parent >= composerList.count() || index.row() >= composerList.at(parent).works.count()) {
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
		return rowFiles(index).isEmpty() ? QVariant() : ActionModel::data(index, role);
	default:
		return QVariant();
	}
}

Qt::ItemFlags ComposerDayModel::flags(const QModelIndex& index) const
{
	if (!index.isValid()) {
		return Qt::NoItemFlags;
	}
	// Empty musicians, groups and unmatched works remain visible but cannot
	// be selected or queued.
	return rowFiles(index).isEmpty() ? Qt::ItemIsEnabled : (Qt::ItemIsSelectable | Qt::ItemIsEnabled);
}

#include "moc_composerdaymodel.cpp"
