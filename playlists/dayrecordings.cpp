#include "dayrecordings.h"
#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace {
QString words(const QString& value)
{
	QString result;
	for (const QChar c : value.normalized(QString::NormalizationForm_D).toCaseFolded()) {
		if (c.category() == QChar::Mark_NonSpacing) continue;
		result += c.isLetterOrNumber() ? c : QLatin1Char(' ');
	}
	return QLatin1Char(' ') + result.simplified() + QLatin1Char(' ');
}

QString groupKey(const ComposerDay::Track& t)
{
	const int slash = t.file.lastIndexOf(QLatin1Char('/'));
	return t.file.left(slash < 0 ? 0 : slash) + QLatin1Char('\n') + t.album
			+ QLatin1Char('\n') + (t.albumArtist.isEmpty() ? t.artist : t.albumArtist)
			+ QLatin1Char('\n') + QString::number(t.year);
}

bool matchesRecording(const QList<ComposerDay::Track>& tracks, const QList<int>& indices,
		const RecommendedRecordings::Recording& recording)
{
	const QStringList names = {recording.soloist, recording.conductor, recording.ensemble};
	bool hasName = false;
	for (const QString& name : names) {
		if (name.trimmed().isEmpty()) continue;
		hasName = true;
		bool found = false;
		for (const int index : indices) {
			const ComposerDay::Track& t = tracks.at(index);
			if (words(t.artist).contains(words(name)) || words(t.albumArtist).contains(words(name))
					|| words(t.album).contains(words(name))) {
				found = true;
				break;
			}
		}
		if (!found) return false;
	}
	if (!hasName) return false;
	const QString year = recording.year.trimmed();
	if (year.isEmpty()) return true;
	// Ambiguous ranges and prose are not enough evidence for an exact recording.
	if (!QRegularExpression(QStringLiteral("^[12][0-9]{3}$")).match(year).hasMatch()) return false;
	for (const int index : indices) {
		const ComposerDay::Track& t = tracks.at(index);
		if (t.year && QString::number(t.year) != year) return false;
		if (QString::number(t.year) == year || words(t.album).contains(words(year))) return true;
	}
	return false;
}
}

QList<DayRecordings::Selection> DayRecordings::selectRecordings(const QString& composer,
		const QList<ComposerDay::Track>& tracks, const ComposerDay::Work& work,
		const RecommendedRecordings::Dataset& dataset)
{
	QMap<QString, QList<int>> groups;
	QStringList order;
	for (int i = 0; i < tracks.size(); ++i) {
		const QString key = groupKey(tracks.at(i));
		if (!groups.contains(key)) order.append(key);
		groups[key].append(i);
	}
	QMap<QString, QList<int>> matches;
	for (const QString& key : order) {
		const QList<int>& original = groups[key];
		QList<ComposerDay::Track> group;
		for (const int i : original) group.append(tracks.at(i));
		for (const int i : ComposerDay::selectWorkTracks(group, work)) matches[key].append(original.at(i));
	}
	QList<Selection> results;
	QSet<QString> selectedGroups;
	QSet<QString> selectedFiles;
	const auto append = [&](const QString& key, bool recommended) {
		if (selectedGroups.contains(key) || matches.value(key).isEmpty()) return;
		Selection result;
		result.recommended = recommended;
		for (const int i : matches.value(key)) {
			if (selectedFiles.contains(tracks.at(i).file)) return;
		}
		QSet<QString> files;
		for (const int i : matches.value(key)) {
			if (files.contains(tracks.at(i).file)) continue;
			files.insert(tracks.at(i).file);
			result.indices.append(i);
		}
		selectedFiles.unite(files);
		selectedGroups.insert(key);
		results.append(result);
	};
	for (const RecommendedRecordings::WorkEntry& entry : dataset.works) {
		if (composer.trimmed().isEmpty() || RecommendedRecordings::normaliseName(composer)
				!= RecommendedRecordings::normaliseName(entry.composer)) continue;
		if (!RecommendedRecordings::worksMatch(work.catalogue, work.title, {}, entry.catalogue, entry.title, entry.aliases)) continue;
		for (const RecommendedRecordings::Recording& recording : entry.recordings) {
			for (const QString& key : order) {
				if (matches.value(key).isEmpty() || !matchesRecording(tracks, matches.value(key), recording)) continue;
				append(key, true);
				if (results.size() == 2) return results;
			}
		}
	}
	// Retain ComposerDay's catalogue/title scoring across the remaining
	// library, rather than picking whichever weak match appeared first.
	QSet<QString> attempted = selectedGroups;
	while (results.size() < 2) {
		QList<ComposerDay::Track> remaining;
		QList<int> original;
		for (int i = 0; i < tracks.size(); ++i) {
			const QString key = groupKey(tracks.at(i));
			if (attempted.contains(key) || matches.value(key).isEmpty()) continue;
			remaining.append(tracks.at(i));
			original.append(i);
		}
		const QList<int> best = ComposerDay::selectWorkTracks(remaining, work);
		if (best.isEmpty()) break;
		const QString key = groupKey(tracks.at(original.at(best.first())));
		attempted.insert(key);
		append(key, false);
	}
	return results;
}

DayRecordings::Selection DayRecordings::select(const QString& composer,
		const QList<ComposerDay::Track>& tracks, const ComposerDay::Work& work,
		const RecommendedRecordings::Dataset& dataset)
{
	const QList<Selection> results = selectRecordings(composer, tracks, work, dataset);
	return results.isEmpty() ? Selection() : results.first();
}
