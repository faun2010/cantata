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

#include "composerday.h"
#include "context/recommendedrecordings.h"
#include "context/workinfo.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {

// The month/day an anniversary is shown on: a February 29 date falls back to
// March 1 in a non-leap year, so those composers are not skipped.
bool fallsOn(const QDate& anniversary, const QDate& date)
{
	if (anniversary.month() == date.month() && anniversary.day() == date.day()) {
		return true;
	}
	return 2 == anniversary.month() && 29 == anniversary.day() && 3 == date.month() && 1 == date.day() && !QDate::isLeapYear(date.year());
}

QDate parseDate(const QString& iso)
{
	return QDate::fromString(iso, QLatin1String("yyyy-MM-dd"));
}

// A key identifying the album a track belongs to - the album tag plus its
// album artist, so two different recordings of the same work do not merge.
QString albumKey(const ComposerDay::Track& track)
{
	const QString performer = track.albumArtist.isEmpty() ? track.artist : track.albumArtist;
	return RecommendedRecordings::normaliseTitle(track.album) + QLatin1Char('\n') + RecommendedRecordings::normaliseName(performer) + QLatin1Char('\n') + QString::number(track.year);
}

bool trackOrder(const ComposerDay::Track& a, const ComposerDay::Track& b)
{
	if (a.disc != b.disc) return a.disc < b.disc;
	if (a.track != b.track) return a.track < b.track;
	return a.file < b.file;
}

// The work an album group describes, derived from the tags of its first
// track (WorkInfo::deriveWork() strips a trailing "(Performer - Year)").
WorkInfo::Candidate albumWork(const ComposerDay::Track& track)
{
	return WorkInfo::deriveWork(track.composer, track.artist, track.albumArtist, track.album, track.title, track.genre);
}

// True when a single track's own title names "work" - used to pick the right
// tracks out of an album covering several works.
bool trackNamesWork(const ComposerDay::Track& track, const ComposerDay::Work& work)
{
	const QString title = RecommendedRecordings::normaliseTitle(track.title);
	if (title.isEmpty()) return false;
	const QString wanted = RecommendedRecordings::normaliseTitle(work.title);
	if (!wanted.isEmpty() && (title.contains(wanted) || wanted.contains(title))) return true;
	const QString catalogue = RecommendedRecordings::normaliseCatalogue(work.catalogue);
	return !catalogue.isEmpty() && title.contains(catalogue);
}

}// namespace

QString ComposerDay::Work::displayTitle() const
{
	if (catalogue.isEmpty()) return title;
	if (RecommendedRecordings::normaliseTitle(title).contains(RecommendedRecordings::normaliseCatalogue(catalogue))) return title;
	return title + QLatin1String(", ") + catalogue;
}

QList<ComposerDay::Composer> ComposerDay::parseCalendar(const QByteArray& json)
{
	QList<Composer> composers;
	const QJsonDocument doc = QJsonDocument::fromJson(json);
	if (!doc.isObject()) return composers;

	const QJsonArray array = doc.object().value(QLatin1String("composers")).toArray();
	for (const QJsonValue& value : array) {
		if (!value.isObject()) continue;
		const QJsonObject obj = value.toObject();
		Composer composer;
		composer.name = obj.value(QLatin1String("name")).toString().trimmed();
		composer.zh = obj.value(QLatin1String("zh")).toString().trimmed();
		composer.born = obj.value(QLatin1String("born")).toString().trimmed();
		composer.died = obj.value(QLatin1String("died")).toString().trimmed();
		if (composer.name.isEmpty()) continue;
		if (!parseDate(composer.born).isValid()) composer.born.clear();
		if (!parseDate(composer.died).isValid()) composer.died.clear();
		if (composer.born.isEmpty() && composer.died.isEmpty()) continue;
		composers.append(composer);
	}
	return composers;
}

QList<ComposerDay::Anniversary> ComposerDay::anniversariesFor(const QList<Composer>& composers, const QDate& date)
{
	QList<Anniversary> deaths;
	QList<Anniversary> births;
	if (!date.isValid()) return deaths;

	for (const Composer& composer : composers) {
		for (int pass = 0; pass < 2; ++pass) {
			const bool birth = 0 == pass;
			const QDate when = parseDate(birth ? composer.born : composer.died);
			if (!when.isValid() || !fallsOn(when, date) || when > date) continue;
			Anniversary anniversary;
			anniversary.name = composer.name;
			anniversary.zh = composer.zh;
			anniversary.birth = birth;
			anniversary.date = when.toString(QLatin1String("yyyy-MM-dd"));
			anniversary.years = date.year() - when.year();
			(birth ? births : deaths).append(anniversary);
		}
	}

	return deaths + births;
}

namespace {
QString htmlText(QString text)
{
	text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
	static const QRegularExpression entity(QStringLiteral("&#(x[0-9a-fA-F]+|[0-9]+);"));
	auto matches = entity.globalMatch(text);
	QList<QRegularExpressionMatch> replacements;
	while (matches.hasNext()) replacements.prepend(matches.next());
	for (const auto& match : replacements) {
		const QString number = match.captured(1);
		bool ok = false;
		const uint code = number.startsWith(QLatin1Char('x')) ? number.mid(1).toUInt(&ok, 16) : number.toUInt(&ok);
		if (ok && code > 0 && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff)) {
			QString decoded;
			if (code <= 0xffff) decoded += QChar(ushort(code));
			else { decoded += QChar(QChar::highSurrogate(code)); decoded += QChar(QChar::lowSurrogate(code)); }
			text.replace(match.capturedStart(), match.capturedLength(), decoded);
		}
	}
	text.replace(QLatin1String("&quot;"), QLatin1String("\""));
	text.replace(QLatin1String("&apos;"), QLatin1String("'"));
	text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
	text.replace(QLatin1String("&ndash;"), QString::fromUtf8("–"));
	text.replace(QLatin1String("&mdash;"), QString::fromUtf8("—"));
	text.replace(QLatin1String("&amp;"), QLatin1String("&"));
	return text.simplified();
}

QString identity(QString name)
{
	const QStringList inverted = name.split(QLatin1Char(','));
	if (inverted.count() == 2) name = inverted.at(1).trimmed() + QLatin1Char(' ') + inverted.at(0).trimmed();
	return RecommendedRecordings::normaliseName(name);
}
}

bool ComposerDay::musicianMatches(const QString& tag, const QString& name, const QStringList& aliases)
{
	QStringList wanted = aliases;
	wanted.prepend(name);
	const QStringList credited = tag.split(QRegularExpression(QStringLiteral("[;/\\n]|\\s+&\\s+")), Qt::SkipEmptyParts);
	for (const QString& person : credited) {
		const QString key = identity(person);
		if (key.isEmpty()) continue;
		for (const QString& candidate : wanted) {
			if (!candidate.trimmed().isEmpty() && key == identity(candidate)) return true;
		}
	}
	return false;
}

QList<ComposerDay::Anniversary> ComposerDay::parseOnThisDay(const QByteArray& html, const QDate& date, bool birth)
{
	QList<Anniversary> result;
	if (!date.isValid()) return result;
	const QString document = QString::fromUtf8(html);
	const QString path = QLatin1String("https://www.onthisday.com/music/") + (birth ? QLatin1String("birthdays/") : QLatin1String("deaths/"))
			+ QLocale::c().monthName(date.month()).toLower() + QLatin1Char('/') + QString::number(date.day());
	const QRegularExpression canonical(QStringLiteral("<link\\b[^>]*\\brel=[\"']canonical[\"'][^>]*>"), QRegularExpression::CaseInsensitiveOption);
	const QString link = canonical.match(document).captured();
	const QRegularExpression href(QStringLiteral("\\bhref=[\"']([^\"']+)[\"']"));
	if (href.match(link).captured(1) != path) return result;

	const QRegularExpression entries(QStringLiteral("<li\\b[^>]*class=[\"']person(?:\\s[^\"']*)?[\"'][^>]*>(.*?)</li>|<h2\\b[^>]*class=[\"']poi__heading[\"'][^>]*>(.*?)</h2>\\s*</header>\\s*<p[^>]*>(.*?)</p>"),
			QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
	const QRegularExpression yearPrefix(QStringLiteral("^(\\d{3,4})\\s+(.+)$"));
	const QRegularExpression life(QString::fromUtf8("\\((\\d{3,4})\\s*[-–]\\s*(\\d{3,4})\\)"));
	const QRegularExpression writer(QStringLiteral("\\b(composer|songwriter|lyricist)\\b"), QRegularExpression::CaseInsensitiveOption);
	QSet<QString> seen;
	auto matches = entries.globalMatch(document);
	while (matches.hasNext()) {
		const auto match = matches.next();
		Anniversary item;
		item.birth = birth;
		int year = 0;
		if (!match.captured(1).isEmpty()) {
			const auto prefix = yearPrefix.match(htmlText(match.captured(1)));
			if (!prefix.hasMatch()) continue;
			year = prefix.captured(1).toInt();
			const QString body = prefix.captured(2);
			const int comma = body.indexOf(QLatin1Char(','));
			if (comma < 1) continue;
			item.name = body.left(comma).trimmed();
			item.description = body.mid(comma + 1).trimmed();
		} else {
			item.name = htmlText(match.captured(2));
			item.description = htmlText(match.captured(3));
			const auto lifetime = life.match(item.name);
			const auto prefix = yearPrefix.match(item.description);
			if (prefix.hasMatch()) { year = prefix.captured(1).toInt(); item.description = prefix.captured(2); }
			else if (lifetime.hasMatch()) year = lifetime.captured(birth ? 1 : 2).toInt();
			item.name.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)\\s*$")));
		}
		const QDate eventDate(year, date.month(), date.day());
		if (!eventDate.isValid() || eventDate > date) continue;
		// Bracketed birth names may be either a full name or an alternative surname.
		const QRegularExpression bracket(QStringLiteral("\\s*\\[([^]]+)\\]"));
		const auto alternate = bracket.match(item.name);
		if (alternate.hasMatch()) {
			QString alias = alternate.captured(1).trimmed();
			item.name.remove(bracket);
			if (!alias.contains(QLatin1Char(' ')) && item.name.contains(QLatin1Char(' '))) alias = item.name.section(QLatin1Char(' '), 0, -2) + QLatin1Char(' ') + alias;
			item.aliases.append(alias);
		}
		// Site convention: (given names) "stage name" surname.
		const QRegularExpression nickname(QStringLiteral("^\\(([^)]+)\\)\\s*\"([^\"]+)\"\\s+(.+)$"));
		const auto nick = nickname.match(item.name);
		if (nick.hasMatch()) {
			item.aliases.append(nick.captured(1) + QLatin1Char(' ') + nick.captured(3));
			item.name = nick.captured(2) + QLatin1Char(' ') + nick.captured(3);
		}
		else {
			const QRegularExpression embeddedNickname(QStringLiteral("^(.+?)\\s+\"([^\"]+)\"\\s+(.+)$"));
			const auto embedded = embeddedNickname.match(item.name);
			if (embedded.hasMatch()) {
				item.aliases.append(embedded.captured(2) + QLatin1Char(' ') + embedded.captured(3));
				item.name = embedded.captured(1) + QLatin1Char(' ') + embedded.captured(3);
			}
		}
		item.name = item.name.trimmed();
		const QString key = identity(item.name);
		if (key.isEmpty() || seen.contains(key)) continue;
		seen.insert(key);
		item.composer = writer.match(item.description).hasMatch();
		item.date = eventDate.toString(QLatin1String("yyyy-MM-dd"));
		item.years = date.year() - year;
		result.append(item);
	}
	return result;
}

QString ComposerDay::buildWorksSource(const QString& composer, int count)
{
	return QLatin1String("Composer: ") + composer + QLatin1String("\nWorks wanted: ") + QString::number(count);
}

QList<ComposerDay::Work> ComposerDay::parseWorks(const QString& response, int max)
{
	QList<Work> works;
	max = qMin(max, constWorksPerComposer);
	if (max <= 0) return works;
	QSet<QString> seen;

	const QString text = response.trimmed();
	const int start = text.indexOf(QLatin1Char('['));
	const int end = text.lastIndexOf(QLatin1Char(']'));
	if (start < 0 || end <= start) return works;

	const QJsonDocument doc = QJsonDocument::fromJson(text.mid(start, end - start + 1).toUtf8());
	if (!doc.isArray()) return works;

	for (const QJsonValue& value : doc.array()) {
		Work work;
		if (value.isString()) {
			work.title = value.toString().trimmed();
		}
		else if (value.isObject()) {
			const QJsonObject obj = value.toObject();
			work.title = obj.value(QLatin1String("title")).toString().trimmed();
			work.catalogue = obj.value(QLatin1String("catalogue")).toString().trimmed();
			work.zh = obj.value(QLatin1String("zh")).toString().trimmed();
		}
		if (work.title.isEmpty()) continue;

		const QString key = RecommendedRecordings::normaliseCatalogue(work.catalogue).isEmpty()
				? RecommendedRecordings::normaliseTitle(work.title)
				: RecommendedRecordings::normaliseCatalogue(work.catalogue);
		if (key.isEmpty() || seen.contains(key)) continue;
		seen.insert(key);
		works.append(work);
		if (works.count() >= max) break;
	}
	return works;
}

QList<int> ComposerDay::selectWorkTracks(const QList<Track>& tracks, const Work& work)
{
	if (work.title.isEmpty()) return QList<int>();

	// Album groups, in first-seen order so the score comparison below is
	// stable for equally good candidates.
	QList<QString> order;
	QMap<QString, QList<int>> groups;
	for (int i = 0; i < tracks.count(); ++i) {
		const QString key = albumKey(tracks.at(i));
		if (!groups.contains(key)) order.append(key);
		groups[key].append(i);
	}

	QList<int> best;
	int bestScore = 0;
	for (const QString& key : order) {
		const QList<int>& indexes = groups.value(key);
		const WorkInfo::Candidate derived = albumWork(tracks.at(indexes.first()));
		const bool albumMatches = derived.valid && RecommendedRecordings::worksMatch(work.catalogue, work.title, QStringList(), derived.catalogueNumber, derived.title, QStringList());
		const bool catalogueMatches = albumMatches && !RecommendedRecordings::normaliseCatalogue(work.catalogue).isEmpty() && RecommendedRecordings::normaliseCatalogue(work.catalogue) == RecommendedRecordings::normaliseCatalogue(derived.catalogueNumber);

		QList<int> matchingTracks;
		for (int index : indexes) {
			if (trackNamesWork(tracks.at(index), work)) matchingTracks.append(index);
		}
		if (!albumMatches && matchingTracks.isEmpty()) continue;

		// A whole album devoted to the work beats a single track of a
		// compilation; a catalogue-number match beats a title-only one.
		int score = 1;
		if (albumMatches) score += 2;
		if (catalogueMatches) score += 4;
		if (albumMatches && matchingTracks.isEmpty()) score += 1;
		if (score <= bestScore) continue;

		bestScore = score;
		best = albumMatches && matchingTracks.isEmpty() ? indexes : matchingTracks;
	}

	std::sort(best.begin(), best.end(), [&tracks](int a, int b) { return trackOrder(tracks.at(a), tracks.at(b)); });
	return best;
}

QList<ComposerDay::Work> ComposerDay::worksFromLibrary(const QList<Track>& tracks, int max)
{
	max = qMin(max, constWorksPerComposer);
	if (max <= 0) return QList<Work>();
	struct Derived {
		Work work;
		QSet<QString> recordings;
		int tracks = 0;
	};
	QList<Derived> derivedWorks;
	QMap<QString, int> byWorkKey;
	// A bare movement cannot identify a work. Prefer a complete track title
	// over an album title: box sets and compilations contain many works.
	static const QRegularExpression movementOnly(QStringLiteral("^(?:(?:[IVXLCDM]+|[0-9]+)[.)]\\s*|(?:allegro|allegretto|andante|andantino|adagio|largo|larghetto|presto|prestissimo|vivace|moderato|maestoso|scherzo|menuetto|menuet|minuet|finale|lento|grave)\\b)"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression movementSuffix(QString::fromUtf8("\\s*(?::|[-–—])\\s*(?:[IVXLCDM]+|[0-9]+)[.)]\\s+.*$"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression collection(QStringLiteral("\\b(?:complete|collection|compilation|favourites|favorites|best of|greatest|integrale|edition|anthology|box set|symphonies|concertos|sonatas|quartets)\\b"), QRegularExpression::CaseInsensitiveOption);
	for (const Track& track : tracks) {
		QString title = track.title.trimmed();
		WorkInfo::Candidate candidate;
		if (!title.isEmpty() && !movementOnly.match(title).hasMatch() && !collection.match(title).hasMatch()) {
			title.remove(movementSuffix);
			candidate = WorkInfo::deriveWork(track.composer, track.artist, track.albumArtist, title, QString(), QString());
			// A singer's song is a work too, even without a composer tag.
			if (!candidate.valid) {
				candidate.title = title;
				candidate.valid = true;
			}
		} else {
			candidate = WorkInfo::deriveWork(track.composer, track.artist, track.albumArtist, track.album, track.title, QString());
			// With only movement tags, require an identifiable album work;
			// never use an undifferentiated box-set title as one work.
			if (collection.match(candidate.title).hasMatch() || (candidate.catalogueNumber.isEmpty() && candidate.genreKeyword.isEmpty())) continue;
		}
		if (!candidate.valid || candidate.title.isEmpty()) continue;
		const QString workKey = RecommendedRecordings::normaliseCatalogue(candidate.catalogueNumber).isEmpty()
				? RecommendedRecordings::normaliseTitle(candidate.title)
				: RecommendedRecordings::normaliseCatalogue(candidate.catalogueNumber);
		if (workKey.isEmpty()) continue;
		if (!byWorkKey.contains(workKey)) {
			Derived derived;
			derived.work.title = candidate.title;
			derived.work.catalogue = candidate.catalogueNumber;
			byWorkKey.insert(workKey, derivedWorks.count());
			derivedWorks.append(derived);
		}
		Derived& derived = derivedWorks[byWorkKey.value(workKey)];
		derived.recordings.insert(albumKey(track));
		++derived.tracks;
	}

	// Most-recorded first: owning three readings of a work says more about
	// how central it is than owning one long one.
	std::stable_sort(derivedWorks.begin(), derivedWorks.end(), [](const Derived& a, const Derived& b) {
		return a.recordings.count() == b.recordings.count() ? a.tracks > b.tracks : a.recordings.count() > b.recordings.count();
	});

	QList<Work> works;
	for (const Derived& derived : derivedWorks) {
		works.append(derived.work);
		if (works.count() >= max) break;
	}
	return works;
}
