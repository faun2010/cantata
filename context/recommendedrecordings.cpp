/*
 * Cantata
 *
 * Copyright (c) 2026 Cantata Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "recommendedrecordings.h"
#include "workinfo.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace RecommendedRecordings {

namespace {

QString stripDiacritics(const QString& text)
{
	const QString decomposed = text.normalized(QString::NormalizationForm_D);
	QString result;
	result.reserve(decomposed.size());
	for (const QChar& c : decomposed) {
		if (c.category() != QChar::Mark_NonSpacing) {
			result += c;
		}
	}
	return result;
}

QString keepLettersAndDigits(const QString& text)
{
	QString result;
	result.reserve(text.size());
	for (const QChar& c : text) {
		if (c.isLetterOrNumber()) {
			result += c;
		}
	}
	return result;
}

const QRegularExpression& numberWordRx()
{
	// "No.5", "No. 5", "Nr. 5", "Nr 5" (case-insensitive) -> "no5".
	static const QRegularExpression rx(QStringLiteral("\\b(?:no|nr)\\.?\\s*(\\d+)"), QRegularExpression::CaseInsensitiveOption);
	return rx;
}

QString recordingFieldString(const QJsonObject& obj, const char* key)
{
	return obj.value(QLatin1String(key)).toString().trimmed();
}

// The leading letters of a catalogue number, e.g. "BWV" from "BWV 1046" or
// "Op" from "Op. 73" - lower-cased, for comparing two catalogue numbers'
// systems regardless of case.
QString catalogueSystemLetters(const QString& catalogue)
{
	static const QRegularExpression rx(QStringLiteral("^\\s*([A-Za-z]+)"));
	const QRegularExpressionMatch m = rx.match(catalogue);
	return m.hasMatch() ? m.captured(1).toLower() : QString();
}

// The numbered work(s) a catalogue number identifies, as inclusive
// [start, end] ranges - a single number becomes a one-element range, e.g.
// "BWV 1046" -> [(1046, 1046)], "BWV 1046-1051" -> [(1046, 1051)], "BWV
// 1066,1069" -> [(1066, 1066), (1069, 1069)].
QList<QPair<int, int>> catalogueNumberRanges(const QString& catalogue)
{
	QList<QPair<int, int>> ranges;
	static const QRegularExpression rx(QStringLiteral("(\\d+)(?:\\s*-\\s*(\\d+))?"));
	QRegularExpressionMatchIterator it = rx.globalMatch(catalogue);
	while (it.hasNext()) {
		const QRegularExpressionMatch m = it.next();
		const int start = m.captured(1).toInt();
		const int end = m.captured(2).isEmpty() ? start : m.captured(2).toInt();
		ranges << qMakePair(qMin(start, end), qMax(start, end));
	}
	return ranges;
}

// True when "catalogueA"/"catalogueB" name the same catalogue system (e.g.
// both "BWV") and at least one of A's numbered work(s) falls within (or
// overlaps) one of B's - so a single work's catalogue number ("BWV 1046")
// matches a dataset entry covering the whole set it belongs to ("BWV
// 1046-1051"), and a comma/dash-separated album catalogue ("BWV
// 1066,1069") matches a dataset range it is contained in ("BWV
// 1066-1069") - see findMatchingWork()/worksMatch().
bool catalogueRangesOverlap(const QString& catalogueA, const QString& catalogueB)
{
	if (catalogueA.trimmed().isEmpty() || catalogueB.trimmed().isEmpty()) {
		return false;
	}
	const QString systemA = catalogueSystemLetters(catalogueA);
	const QString systemB = catalogueSystemLetters(catalogueB);
	if (systemA.isEmpty() || systemA != systemB) {
		return false;
	}
	const QList<QPair<int, int>> rangesA = catalogueNumberRanges(catalogueA);
	const QList<QPair<int, int>> rangesB = catalogueNumberRanges(catalogueB);
	for (const QPair<int, int>& a : rangesA) {
		for (const QPair<int, int>& b : rangesB) {
			if (a.first <= b.second && b.first <= a.second) {
				return true;
			}
		}
	}
	return false;
}

}// namespace

QString normaliseCatalogue(const QString& catalogue)
{
	return keepLettersAndDigits(stripDiacritics(catalogue)).toLower();
}

QString normaliseTitle(const QString& title)
{
	QString result = title;
	result.replace(numberWordRx(), QStringLiteral("no\\1"));
	return keepLettersAndDigits(stripDiacritics(result)).toLower();
}

QString normaliseName(const QString& name)
{
	return keepLettersAndDigits(stripDiacritics(name)).toLower();
}

bool worksMatch(const QString& catalogueA, const QString& titleA, const QStringList& aliasesA, const QString& catalogueB, const QString& titleB, const QStringList& aliasesB)
{
	const QString normCatalogueA = normaliseCatalogue(catalogueA);
	const QString normCatalogueB = normaliseCatalogue(catalogueB);
	if (!normCatalogueA.isEmpty() && !normCatalogueB.isEmpty() && normCatalogueA == normCatalogueB) {
		return true;
	}
	if (catalogueRangesOverlap(catalogueA, catalogueB)) {
		return true;
	}

	QStringList candidatesA;
	candidatesA << titleA << aliasesA;
	QStringList candidatesB;
	candidatesB << titleB << aliasesB;

	for (const QString& a : candidatesA) {
		const QString normA = normaliseTitle(a);
		if (normA.isEmpty()) {
			continue;
		}
		for (const QString& b : candidatesB) {
			const QString normB = normaliseTitle(b);
			if (normB.isEmpty()) {
				continue;
			}
			if (normA.contains(normB) || normB.contains(normA)) {
				return true;
			}
		}
	}
	return false;
}

bool performerNameMatches(const QString& performerOrArtist, const QString& name)
{
	if (performerOrArtist.trimmed().isEmpty() || name.trimmed().isEmpty()) {
		return false;
	}
	const QString normPerformer = normaliseName(performerOrArtist);
	const QString surname = normaliseName(WorkInfo::composerSurname(name));
	if (surname.isEmpty()) {
		return false;
	}
	return normPerformer.contains(surname);
}

int findMatchingWork(const Dataset& dataset, const QString& composer, const QString& catalogueNumber, const QString& title)
{
	const QString surname = normaliseName(WorkInfo::composerSurname(composer));
	if (surname.isEmpty()) {
		return -1;
	}
	for (int i = 0; i < dataset.works.size(); ++i) {
		const WorkEntry& work = dataset.works.at(i);
		const QString workSurname = normaliseName(WorkInfo::composerSurname(work.composer));
		if (workSurname != surname) {
			continue;
		}
		if (worksMatch(catalogueNumber, title, QStringList(), work.catalogue, work.title, work.aliases)) {
			return i;
		}
	}
	return -1;
}

Dataset parseDataset(const QByteArray& json)
{
	Dataset dataset;
	const QJsonDocument document = QJsonDocument::fromJson(json);
	if (!document.isObject()) {
		return dataset;
	}
	const QJsonObject root = document.object();
	dataset.version = root.value(QLatin1String("version")).toInt();
	dataset.generated = root.value(QLatin1String("generated")).toString();

	const QJsonArray works = root.value(QLatin1String("works")).toArray();
	for (const QJsonValue& workValue : works) {
		if (!workValue.isObject()) {
			continue;
		}
		const QJsonObject workObj = workValue.toObject();
		WorkEntry work;
		work.composer = workObj.value(QLatin1String("composer")).toString().trimmed();
		work.title = workObj.value(QLatin1String("title")).toString().trimmed();
		work.catalogue = workObj.value(QLatin1String("catalogue")).toString().trimmed();
		for (const QJsonValue& alias : workObj.value(QLatin1String("aliases")).toArray()) {
			const QString aliasText = alias.toString().trimmed();
			if (!aliasText.isEmpty()) {
				work.aliases << aliasText;
			}
		}
		for (const QJsonValue& recordingValue : workObj.value(QLatin1String("recordings")).toArray()) {
			if (!recordingValue.isObject()) {
				continue;
			}
			const QJsonObject recObj = recordingValue.toObject();
			Recording recording;
			recording.guide = recordingFieldString(recObj, "guide");
			recording.edition = recordingFieldString(recObj, "edition");
			recording.rating = recordingFieldString(recObj, "rating");
			recording.soloist = recordingFieldString(recObj, "soloist");
			recording.conductor = recordingFieldString(recObj, "conductor");
			recording.ensemble = recordingFieldString(recObj, "ensemble");
			recording.label = recordingFieldString(recObj, "label");
			recording.catalogue = recordingFieldString(recObj, "catalogue");
			recording.year = recordingFieldString(recObj, "year");
			recording.source = recordingFieldString(recObj, "source");
			recording.comment = recordingFieldString(recObj, "comment");
			recording.isAi = false;
			work.recordings << recording;
		}
		if (!work.composer.isEmpty() && !work.title.isEmpty()) {
			dataset.works << work;
		}
	}
	return dataset;
}

QList<Recording> parseAiRecordings(const QString& response)
{
	QString text = response.trimmed();

	static const QRegularExpression fenceRx(QStringLiteral("```(?:json)?\\s*([\\s\\S]*?)```"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch fenceMatch = fenceRx.match(text);
	if (fenceMatch.hasMatch()) {
		text = fenceMatch.captured(1).trimmed();
	}

	const int start = text.indexOf(QLatin1Char('['));
	const int end = text.lastIndexOf(QLatin1Char(']'));
	if (start < 0 || end < start) {
		return {};
	}
	text = text.mid(start, end - start + 1);

	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !document.isArray()) {
		return {};
	}

	QList<Recording> result;
	for (const QJsonValue& value : document.array()) {
		if (!value.isObject()) {
			continue;
		}
		const QJsonObject obj = value.toObject();
		Recording recording;
		recording.soloist = recordingFieldString(obj, "soloist");
		recording.conductor = recordingFieldString(obj, "conductor");
		recording.ensemble = recordingFieldString(obj, "ensemble");
		recording.label = recordingFieldString(obj, "label");
		recording.year = recordingFieldString(obj, "year");
		recording.isAi = true;
		if (recording.soloist.isEmpty() && recording.conductor.isEmpty() && recording.ensemble.isEmpty() && recording.label.isEmpty() && recording.year.isEmpty()) {
			continue;
		}
		result << recording;
		if (result.size() >= 5) {
			break;
		}
	}
	return result;
}

QString recordingCoverKey(const QString& performers, const QString& label, const QString& catalogue)
{
	const QString normalised = normaliseName(label) + QLatin1Char('|') + normaliseCatalogue(catalogue) + QLatin1Char('|') + normaliseName(performers);
	return normalised;
}

QString middleDotSeparator()
{
	QString separator;
	separator += QLatin1Char(' ');
	separator += QChar(0x00B7);
	separator += QLatin1Char(' ');
	return separator;
}

QString emDashSeparator()
{
	QString separator;
	separator += QLatin1Char(' ');
	separator += QChar(0x2014);
	separator += QLatin1Char(' ');
	return separator;
}

}// namespace RecommendedRecordings
