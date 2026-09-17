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

#include "smartfilter.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

QString SmartFilter::buildSource(const QString& description, const QList<Candidate>& candidates, int maxCandidates)
{
	QJsonArray array;
	const int count = qMin(candidates.count(), maxCandidates);
	for (int i = 0; i < count; ++i) {
		const Candidate& c = candidates.at(i);
		QJsonObject obj;
		obj.insert(QLatin1String("i"), i);
		obj.insert(QLatin1String("title"), c.title);
		obj.insert(QLatin1String("artist"), c.artist);
		obj.insert(QLatin1String("album"), c.album);
		if (!c.composer.isEmpty()) obj.insert(QLatin1String("composer"), c.composer);
		if (!c.genre.isEmpty()) obj.insert(QLatin1String("genre"), c.genre);
		if (c.year > 0) obj.insert(QLatin1String("year"), c.year);
		if (c.secs > 0) obj.insert(QLatin1String("secs"), c.secs);
		array.append(obj);
	}

	return QLatin1String("Description:\n") + description + QLatin1String("\n\nCandidates:\n") +
	       QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QSet<int> SmartFilter::parseSelection(const QString& response, int candidateCount, bool* ok)
{
	QSet<int> selected;
	bool parsed = false;

	QString text = response.trimmed();
	const int start = text.indexOf(QLatin1Char('['));
	const int end = text.lastIndexOf(QLatin1Char(']'));
	if (start >= 0 && end > start) {
		const QJsonDocument doc = QJsonDocument::fromJson(text.mid(start, end - start + 1).toUtf8());
		if (doc.isArray()) {
			for (const QJsonValue& value : doc.array()) {
				int index = -1;
				if (value.isDouble()) {
					index = (int)value.toDouble();
				}
				else if (value.isString()) {
					bool numeric = false;
					const int fromString = value.toString().toInt(&numeric);
					if (numeric) index = fromString;
				}
				if (index >= 0 && index < candidateCount) selected.insert(index);
			}
			parsed = true;
		}
	}

	if (ok) *ok = parsed && !selected.isEmpty();
	return selected;
}
