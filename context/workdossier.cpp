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

#include "workdossier.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QRegularExpression>
#include <QStringList>

namespace WorkDossier {

namespace {

// Headings whose lower-cased text contains one of these are kept; everything
// else (including the explicitly listed References/External links/Notes/
// Further reading/See also boilerplate) is dropped, along with any of its
// subsections.
const QStringList& allowedHeadingKeywords()
{
	static const QStringList keywords = {
	    QStringLiteral("background"), QStringLiteral("history"), QStringLiteral("composition"),
	    QStringLiteral("structure"), QStringLiteral("movement"), QStringLiteral("music"),
	    QStringLiteral("premiere"), QStringLiteral("première"), QStringLiteral("reception"),
	    QStringLiteral("recording"), QStringLiteral("discography")};
	return keywords;
}

const QStringList& deniedHeadingKeywords()
{
	static const QStringList keywords = {
	    QStringLiteral("reference"), QStringLiteral("external link"), QStringLiteral("note"),
	    QStringLiteral("further reading"), QStringLiteral("see also")};
	return keywords;
}

// Matches an "exsectionformat=wiki" heading line, e.g. "== Background ==" or
// "=== Second movement ===". The two "=" runs must have equal length.
const QRegularExpression& headingRx()
{
	static const QRegularExpression rx(QStringLiteral("^(=+)\\s*(.+?)\\s*\\1\\s*$"));
	return rx;
}

QString stripFence(const QString& response)
{
	QString text = response.trimmed();
	static const QRegularExpression fenceRx(QStringLiteral("```(?:json)?\\s*([\\s\\S]*?)```"), QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch fenceMatch = fenceRx.match(text);
	if (fenceMatch.hasMatch()) {
		text = fenceMatch.captured(1).trimmed();
	}
	return text;
}

}// namespace

QString extractPlainText(const QByteArray& extractsJson)
{
	const QJsonDocument document = QJsonDocument::fromJson(extractsJson);
	if (!document.isObject()) {
		return QString();
	}
	const QJsonObject pages = document.object().value(QLatin1String("query")).toObject().value(QLatin1String("pages")).toObject();
	// Only a single title is ever queried, so at most one page is present.
	for (auto it = pages.constBegin(); it != pages.constEnd(); ++it) {
		return it.value().toObject().value(QLatin1String("extract")).toString();
	}
	return QString();
}

QString filterAndCapSections(const QString& plainText, int capChars)
{
	if (plainText.isEmpty()) {
		return QString();
	}

	const QStringList lines = plainText.split(QLatin1Char('\n'));
	QString result;
	bool inLead = true;
	bool keepingCurrentSection = false;
	// One (level, kept) entry per heading currently open, so a subsection
	// (e.g. "=== I. Allegro ===", which does not itself contain an allowed
	// keyword) inherits its parent's disposition rather than being dropped
	// just because its own heading text does not match - a "Structure"
	// section's per-movement subsections are the very content that section
	// was kept for.
	QList<QPair<int, bool>> openSections;

	for (const QString& rawLine : lines) {
		const QRegularExpressionMatch match = headingRx().match(rawLine);
		if (match.hasMatch()) {
			inLead = false;
			const int level = match.captured(1).length();
			while (!openSections.isEmpty() && openSections.last().first >= level) {
				openSections.removeLast();
			}
			const bool parentKept = !openSections.isEmpty() && openSections.last().second;

			const QString heading = match.captured(2).trimmed();
			const QString lowerHeading = heading.toLower();
			bool denied = false;
			for (const QString& keyword : deniedHeadingKeywords()) {
				if (lowerHeading.contains(keyword)) {
					denied = true;
					break;
				}
			}
			bool allowed = parentKept;
			if (!denied && !allowed) {
				for (const QString& keyword : allowedHeadingKeywords()) {
					if (lowerHeading.contains(keyword)) {
						allowed = true;
						break;
					}
				}
			}
			const bool kept = allowed && !denied;
			openSections.append({level, kept});

			if (!kept) {
				keepingCurrentSection = false;
				continue;
			}
			keepingCurrentSection = true;
			if (!result.isEmpty() && !result.endsWith(QLatin1String("\n\n"))) {
				result += QLatin1String("\n\n");
			}
			result += heading + QLatin1Char('\n');
			continue;
		}

		if (inLead) {
			result += rawLine + QLatin1Char('\n');
			continue;
		}
		if (keepingCurrentSection) {
			result += rawLine + QLatin1Char('\n');
		}
	}

	result = result.trimmed();
	if (result.size() > capChars) {
		QString truncated = result.left(capChars);
		const int cut = truncated.lastIndexOf(QLatin1String("\n\n"));
		if (cut > capChars / 2) {
			truncated = truncated.left(cut);
		}
		result = truncated.trimmed();
	}
	return result;
}

QString buildDossierText(const QString& composer, const QString& workTitle, const QString& catalogueNumber, const QString& englishText, const QString& zhSummaryHint, const QList<RecommendedRecordings::Recording>& datasetRecordings)
{
	QString text = QLatin1String("Work: ") + composer + RecommendedRecordings::emDashSeparator() + workTitle;
	if (!catalogueNumber.isEmpty()) {
		text += QLatin1String(", ") + catalogueNumber;
	}
	text += QLatin1String("\n\n");

	if (!englishText.isEmpty()) {
		text += QLatin1String("English Wikipedia article (main source):\n") + englishText + QLatin1String("\n\n");
	}
	if (!zhSummaryHint.isEmpty()) {
		text += QLatin1String("Chinese Wikipedia summary (extra hint only):\n") + zhSummaryHint + QLatin1String("\n\n");
	}
	if (!datasetRecordings.isEmpty()) {
		text += QLatin1String("Guide-verified recordings dataset - one per line as \"id | performers | label | catalogue | year | guide comment\", use these exact ids in the \"id\" field of matching recordings:\n");
		for (int i = 0; i < datasetRecordings.size(); ++i) {
			const RecommendedRecordings::Recording& r = datasetRecordings.at(i);
			QStringList names;
			if (!r.soloist.isEmpty()) names << r.soloist;
			if (!r.conductor.isEmpty()) names << r.conductor;
			if (!r.ensemble.isEmpty()) names << r.ensemble;
			text += QString::number(i) + QLatin1String(" | ") + names.join(QLatin1String(", ")) + QLatin1String(" | ") + r.label + QLatin1String(" | ") + r.catalogue + QLatin1String(" | ") + r.year;
			if (!r.comment.isEmpty()) {
				text += QLatin1String(" | ") + r.comment;
			}
			text += QLatin1Char('\n');
		}
	}
	return text;
}

Result parseResponse(const QString& response)
{
	Result result;
	QString text = stripFence(response);

	const int start = text.indexOf(QLatin1Char('{'));
	const int end = text.lastIndexOf(QLatin1Char('}'));
	if (start < 0 || end < start) {
		return result;
	}
	text = text.mid(start, end - start + 1);

	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return result;
	}
	const QJsonObject root = document.object();

	const QJsonObject introObj = root.value(QLatin1String("introduction")).toObject();
	result.introduction.overview = introObj.value(QLatin1String("overview")).toString().trimmed();
	result.introduction.background = introObj.value(QLatin1String("background")).toString().trimmed();
	result.introduction.highlights = introObj.value(QLatin1String("highlights")).toString().trimmed();
	result.introduction.premiere = introObj.value(QLatin1String("premiere")).toString().trimmed();
	for (const QJsonValue& value : introObj.value(QLatin1String("structure")).toArray()) {
		if (!value.isObject()) {
			continue;
		}
		const QJsonObject obj = value.toObject();
		MovementInfo movement;
		movement.movement = obj.value(QLatin1String("movement")).toString().trimmed();
		movement.description = obj.value(QLatin1String("description")).toString().trimmed();
		if (!movement.movement.isEmpty() || !movement.description.isEmpty()) {
			result.introduction.structure << movement;
		}
	}

	for (const QJsonValue& value : root.value(QLatin1String("recordings")).toArray()) {
		if (!value.isObject()) {
			continue;
		}
		const QJsonObject obj = value.toObject();
		RecordingAnnotation recording;
		recording.id = obj.value(QLatin1String("id")).toString().trimmed();
		recording.performers = obj.value(QLatin1String("performers")).toString().trimmed();
		recording.label = obj.value(QLatin1String("label")).toString().trimmed();
		recording.catalogue = obj.value(QLatin1String("catalogue")).toString().trimmed();
		recording.year = obj.value(QLatin1String("year")).toString().trimmed();
		recording.why = obj.value(QLatin1String("why")).toString().trimmed();
		if (recording.id.isEmpty() && recording.performers.isEmpty() && recording.label.isEmpty() && recording.catalogue.isEmpty() && recording.year.isEmpty() && recording.why.isEmpty()) {
			continue;
		}
		result.recordings << recording;
	}

	result.valid = !result.introduction.isEmpty() || !result.recordings.isEmpty();
	return result;
}

}// namespace WorkDossier
