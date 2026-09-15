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

#include "workinfo.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace WorkInfo {

namespace {

// Longer/more specific phrases are listed first so they are preferred over
// a shorter word they happen to contain (e.g. "Symphonic Poem" over
// "Symphony").
const QStringList& workTypeKeywords()
{
	static const QStringList keywords = {
	    QStringLiteral("Concerto Grosso"), QStringLiteral("Symphonic Poem"),
	    QStringLiteral("Piano Trio"), QStringLiteral("String Quartet"), QStringLiteral("String Quintet"),
	    QStringLiteral("Concerto"), QStringLiteral("Symphony"), QStringLiteral("Sonata"),
	    QStringLiteral("Quartet"), QStringLiteral("Quintet"), QStringLiteral("Trio"),
	    QStringLiteral("Overture"), QStringLiteral("Suite"), QStringLiteral("Nocturne"),
	    QStringLiteral("Prelude"), QStringLiteral("Etudes"), QStringLiteral("Etude"),
	    QStringLiteral("Mass"), QStringLiteral("Requiem"), QStringLiteral("Opera"),
	    QStringLiteral("Cantata"), QStringLiteral("Fugue"), QStringLiteral("Variations"),
	    QStringLiteral("Rhapsody"), QStringLiteral("Ballade"), QStringLiteral("Serenade"),
	    QStringLiteral("Partita"), QStringLiteral("Toccata"), QStringLiteral("Impromptu"),
	    QStringLiteral("Waltz"), QStringLiteral("Mazurka"), QStringLiteral("Polonaise"),
	    QStringLiteral("Oratorio"), QStringLiteral("Ballet"), QStringLiteral("Fantasia")};
	return keywords;
}

QString findGenreKeyword(const QString& text)
{
	if (text.isEmpty()) {
		return QString();
	}
	for (const QString& keyword : workTypeKeywords()) {
		const QRegularExpression rx(QLatin1String("\\b") + QRegularExpression::escape(keyword) + QLatin1String("\\b"), QRegularExpression::CaseInsensitiveOption);
		if (text.contains(rx)) {
			return keyword;
		}
	}
	return QString();
}

const QRegularExpression& catalogueNumberRx()
{
	static const QRegularExpression rx(
	    QStringLiteral("\\b(?:Op\\.?\\s?\\d+[A-Za-z]?(?:,?\\s?No\\.?\\s?\\d+)?"
	                   "|BWV\\s?\\d+[A-Za-z]?"
	                   "|KV\\.?\\s?\\d+"
	                   "|K\\.\\s?\\d+"
	                   "|Hob\\.?\\s?[IVXLCDM]+\\s?:\\s?\\d+[A-Za-z]?"
	                   "|D\\.\\s?\\d+"
	                   "|RV\\s?\\d+"
	                   "|S\\.\\s?\\d+"
	                   "|WoO\\s?\\d+[A-Za-z]?)\\b"));
	return rx;
}

// Matches an album's trailing "(Performer - Year)" decoration, e.g.
// " (Serkin - 1981)".
const QRegularExpression& trailingPerformerYearRx()
{
	static const QRegularExpression rx(QStringLiteral("^(.*?)\\s*\\(([^()]+?)\\s*-\\s*(\\d{4})\\)\\s*$"));
	return rx;
}

// A quoted nickname, e.g. 'Emperor' or the curly-quote equivalents.
const QRegularExpression& quotedNicknameRx()
{
	static const QRegularExpression rx(QStringLiteral("['‘’][^'‘’]*['‘’]"));
	return rx;
}

QString buildSearchQuery(const QString& surname, const QString& title)
{
	QString cleaned = title;
	cleaned.remove(quotedNicknameRx());
	cleaned.remove(QLatin1Char(','));
	cleaned = cleaned.simplified();
	if (surname.isEmpty()) {
		return cleaned;
	}
	return cleaned.isEmpty() ? surname : (surname + QLatin1Char(' ') + cleaned);
}

}// namespace

QString composerSurname(const QString& composer)
{
	static const QSet<QString> particles = {
	    QStringLiteral("van"), QStringLiteral("von"), QStringLiteral("de"), QStringLiteral("der"),
	    QStringLiteral("den"), QStringLiteral("di"), QStringLiteral("du"), QStringLiteral("la"),
	    QStringLiteral("le"), QStringLiteral("y")};
	const QStringList words = composer.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (words.isEmpty()) {
		return QString();
	}
	// Normally the surname is simply the last word ("Ludwig van Beethoven"
	// -> "Beethoven"), which already places any particle correctly since it
	// always precedes the surname. Guard against a name that happens to end
	// in a bare particle so that is never returned on its own.
	if (words.size() > 1 && particles.contains(words.last().toLower())) {
		return words.at(words.size() - 2) + QLatin1Char(' ') + words.last();
	}
	return words.last();
}

Candidate deriveWork(const QString& composer, const QString& album, const QString& songTitle, const QString& genre)
{
	Candidate work;
	work.composer = composer.trimmed();
	work.valid = !work.composer.isEmpty();
	if (!work.valid) {
		return work;
	}

	work.surname = composerSurname(work.composer);

	const QString trimmedAlbum = album.trimmed();
	const QRegularExpressionMatch match = trailingPerformerYearRx().match(trimmedAlbum);
	if (match.hasMatch()) {
		work.title = match.captured(1).trimmed();
		work.performer = match.captured(2).trimmed();
		work.year = match.captured(3);
	}
	else {
		work.title = trimmedAlbum;
	}

	const QRegularExpressionMatch catalogue = catalogueNumberRx().match(work.title);
	if (catalogue.hasMatch()) {
		work.catalogueNumber = catalogue.captured(0).trimmed();
	}

	work.genreKeyword = findGenreKeyword(work.title);
	if (work.genreKeyword.isEmpty()) {
		work.genreKeyword = findGenreKeyword(genre);
	}
	if (work.genreKeyword.isEmpty()) {
		work.genreKeyword = findGenreKeyword(songTitle);
	}

	work.searchQuery = buildSearchQuery(work.surname, work.title);
	return work;
}

QString selectSearchResult(const QByteArray& searchResponseJson, const Candidate& work)
{
	const QJsonDocument document = QJsonDocument::fromJson(searchResponseJson);
	if (!document.isObject()) {
		return QString();
	}
	const QJsonArray results = document.object().value(QLatin1String("query")).toObject().value(QLatin1String("search")).toArray();

	QString normalizedCatalogue = work.catalogueNumber;
	normalizedCatalogue.remove(QLatin1Char('.')).remove(QLatin1Char(' '));

	for (const QJsonValue& value : results) {
		const QString title = value.toObject().value(QLatin1String("title")).toString();
		if (title.isEmpty()) {
			continue;
		}
		if (!work.genreKeyword.isEmpty() && !title.contains(work.genreKeyword, Qt::CaseInsensitive)) {
			continue;
		}
		const bool surnameMatch = !work.surname.isEmpty() && title.contains(work.surname, Qt::CaseInsensitive);
		bool catalogueMatch = false;
		if (!normalizedCatalogue.isEmpty()) {
			QString normalizedTitle = title;
			normalizedTitle.remove(QLatin1Char('.')).remove(QLatin1Char(' '));
			catalogueMatch = normalizedTitle.contains(normalizedCatalogue, Qt::CaseInsensitive);
		}
		if (surnameMatch || catalogueMatch) {
			return title;
		}
	}
	return QString();
}

SiteLinks parseSiteLinks(const QByteArray& pagePropsJson)
{
	SiteLinks links;
	const QJsonDocument document = QJsonDocument::fromJson(pagePropsJson);
	if (!document.isObject()) {
		return links;
	}
	const QJsonObject pages = document.object().value(QLatin1String("query")).toObject().value(QLatin1String("pages")).toObject();
	// Only a single title is ever queried, so at most one page is present.
	for (auto it = pages.constBegin(); it != pages.constEnd(); ++it) {
		const QJsonObject page = it.value().toObject();
		links.wikidataId = page.value(QLatin1String("pageprops")).toObject().value(QLatin1String("wikibase_item")).toString();
		const QJsonArray langlinks = page.value(QLatin1String("langlinks")).toArray();
		for (const QJsonValue& value : langlinks) {
			const QJsonObject link = value.toObject();
			if (link.value(QLatin1String("lang")).toString() == QLatin1String("zh")) {
				links.zhTitle = link.contains(QLatin1String("*")) ? link.value(QLatin1String("*")).toString() : link.value(QLatin1String("title")).toString();
				break;
			}
		}
		break;
	}
	return links;
}

Summary parseSummary(const QByteArray& summaryJson)
{
	Summary summary;
	const QJsonDocument document = QJsonDocument::fromJson(summaryJson);
	if (!document.isObject()) {
		return summary;
	}
	const QJsonObject root = document.object();
	summary.extract = root.value(QLatin1String("extract")).toString();
	const QJsonObject contentUrls = root.value(QLatin1String("content_urls")).toObject();
	QString url = contentUrls.value(QLatin1String("desktop")).toObject().value(QLatin1String("page")).toString();
	if (url.isEmpty()) {
		url = contentUrls.value(QLatin1String("mobile")).toObject().value(QLatin1String("page")).toString();
	}
	summary.url = url;
	return summary;
}

}// namespace WorkInfo
