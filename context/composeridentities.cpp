#include "composeridentities.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
QMutex mutex;
QString configuredPath;
QJsonArray people;
QByteArray lastData;
QByteArray lastExternal;
QString lastPath;
QDateTime lastModified;
qint64 lastSize = -1;
bool loaded = false;
QString text(QString value)
{
	value.remove(QRegularExpression("<[^>]*>"));
	const QRegularExpression entity("&#(x[0-9a-fA-F]+|[0-9]+);");
	auto it = entity.globalMatch(value);
	QList<QRegularExpressionMatch> matches;
	while (it.hasNext()) matches.prepend(it.next());
	for (const auto& m : matches) {
		bool ok = false;
		QString n = m.captured(1);
		uint cp = n.startsWith('x') ? n.mid(1).toUInt(&ok, 16) : n.toUInt(&ok);
		if (ok && cp <= 0xffff && !(cp >= 0xd800 && cp <= 0xdfff)) value.replace(m.capturedStart(), m.capturedLength(), QChar(cp));
	}
	value.replace("&amp;", "&").replace("&quot;", "\"").replace("&apos;", "'").replace("&nbsp;", " ");
	return value.simplified();
}
QStringList names(const QJsonObject& p)
{
	QStringList result{p.value("canonical").toString()};
	for (const auto& a : p.value("aliases").toArray()) result.append(a.toString());
	return result;
}
bool valid(const QJsonObject& p)
{
	return !p.value("canonical").toString().trimmed().isEmpty()
			&& p.value("imslp").toString().startsWith("Category:")
			&& p.value("imslp").toString().contains(',') && p.value("aliases").isArray();
}
void reload()
{
	const QString path = ComposerIdentities::configurationPath();
	const QFileInfo info(path);
	if (loaded && path == lastPath && info.lastModified() == lastModified && info.size() == lastSize) return;
	QFile bundled(":/composer-identities/taneyev.json");
	QJsonArray next;
	if (bundled.open(QIODevice::ReadOnly)) next = QJsonDocument::fromJson(bundled.readAll()).object().value("people").toArray();
	if (!loaded || path != lastPath) {
		people = next;
		lastData = QJsonDocument(QJsonObject{{"people", people}}).toJson(QJsonDocument::Compact);
	}
	// Remember even a malformed revision, retaining the last good data;
	// repeated paint/lookup calls must not continually reparse a broken file.
	lastPath = path;
	lastModified = info.lastModified();
	lastSize = info.size();
	loaded = true;
	QFile file(path);
	QByteArray data;
	if (file.exists()) {
		if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return;
		data = file.readAll();
		auto doc = QJsonDocument::fromJson(data);
		if (!doc.isObject() || doc.object().value("version").toInt() != 1 || !doc.object().value("people").isArray()) return;
		for (const auto& value : doc.object().value("people").toArray()) {
			const auto p = value.toObject();
			if (!valid(p)) return;// Keep last known good file on malformed edits.
			bool replaced = false;
			for (int i = 0; i < next.size(); ++i)
				if (next[i].toObject().value("imslp") == p.value("imslp")) {
					next[i] = p;
					replaced = true;
					break;
				}
			if (!replaced) next.append(p);
		}
	}
	people = next;
	lastData = QJsonDocument(QJsonObject{{"people", people}}).toJson(QJsonDocument::Compact);
	lastExternal = data;
	lastPath = path;
	lastModified = info.lastModified();
	lastSize = info.size();
	loaded = true;
}
}// namespace
void ComposerIdentities::setConfigurationFile(const QString& path)
{
	QMutexLocker lock(&mutex);
	configuredPath = path;
	loaded = false;
}
QString ComposerIdentities::configurationPath()
{
	if (!configuredPath.isEmpty()) return configuredPath;
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("composer-identities/taneyev.json");
}
QString ComposerIdentities::nameKey(const QString& raw)
{
	QString value = raw.trimmed();
	value.remove(QRegularExpression("\\s*\\(\\d{4}\\s*[-–]\\s*\\d{4}\\)\\s*$"));
	const auto parts = value.split(',');
	if (parts.size() == 2) value = parts[1].trimmed() + " " + parts[0].trimmed();
	QString key;
	for (QChar c : value.normalized(QString::NormalizationForm_D).toCaseFolded())
		if (c.isLetterOrNumber() && c.category() != QChar::Mark_NonSpacing) key += c;
	return key;
}
QJsonObject ComposerIdentities::lookup(const QString& name, bool* conflict)
{
	QMutexLocker lock(&mutex);
	reload();
	if (conflict) *conflict = false;
	QJsonObject found;
	const QString key = nameKey(name);
	if (key.isEmpty()) return found;
	for (const auto& item : people) {
		auto p = item.toObject();
		for (const auto& alias : names(p))
			if (nameKey(alias) == key) {
				if (!found.isEmpty() && found.value("imslp") != p.value("imslp")) {
					if (conflict) *conflict = true;
					return {};
				}
				found = p;
				break;
			}
	}
	return found;
}
QString ComposerIdentities::revision()
{
	QMutexLocker lock(&mutex);
	reload();
	return QString::fromLatin1(QCryptographicHash::hash(lastData, QCryptographicHash::Sha256).toHex());
}
QString ComposerIdentities::hints(const QString& source)
{
	QMutexLocker lock(&mutex);
	reload();
	QJsonArray relevant;
	for (const auto& item : people) {
		const auto p = item.toObject();
		for (const QString& alias : names(p))
			if (!alias.isEmpty() && source.contains(alias, Qt::CaseInsensitive)) {
				bool ambiguous = false;
				for (const auto& other : people) {
					const auto q = other.toObject();
					if (q.value("imslp") == p.value("imslp")) continue;
					for (const auto& n : names(q))
						if (nameKey(n) == nameKey(alias)) ambiguous = true;
				}
				if (ambiguous) continue;
				relevant.append(p);
				break;
			}
	}
	return relevant.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(relevant).toJson(QJsonDocument::Compact));
}
QJsonObject ComposerIdentities::fromImslp(const QByteArray& html, const QString& category, const QString& requested)
{
	const QString s = QString::fromUtf8(html);
	const QRegularExpression heading("<h1[^>]*id=[\"']firstHeading[\"'][^>]*>(.*?)</h1>", QRegularExpression::DotMatchesEverythingOption);
	if (text(heading.match(s).captured(1)).replace('_', ' ') != QString(category).replace('_', ' ')) return {};
	const QRegularExpression first("<div class=[\"']cp_firsth[\"']>\\s*<h2[^>]*>(.*?)</h2>", QRegularExpression::DotMatchesEverythingOption);
	const QString canonical = text(first.match(s).captured(1));
	if (canonical.isEmpty() || !s.contains("Compositions by:")) return {};
	const int begin = s.indexOf("Alternative Names/Transliterations:");
	const int end = s.indexOf("Authorities -", begin);
	if (begin < 0 || end < begin || end - begin > 50000) return {};
	QString header = s.mid(begin, end - begin);
	// A language entry can contain an inverted name: keep its comma inside
	// that alias rather than installing the given names as a separate person.
	const QRegularExpression languageName("<span\\b[^>]*\\btitle=[\"'][^\"']*[\"'][^>]*>([^<>]*,[^<>]*)</span>");
	auto languageMatches = languageName.globalMatch(header);
	QList<QRegularExpressionMatch> replacements;
	while (languageMatches.hasNext()) replacements.prepend(languageMatches.next());
	for (const auto& m : replacements) {
		QString name = m.captured(1);
		name.replace(',', QChar(0xe000));
		header.replace(m.capturedStart(1), m.capturedLength(1), name);
	}
	QString metadata = text(header);
	metadata.replace("Alternative Names/Transliterations:", ",").replace("Name in Other Languages:", ",").replace("Aliases:", ",").replace(QChar(0xff1d), ',');
	QJsonArray aliases;
	bool matched = nameKey(canonical) == nameKey(requested);
	QString zh;
	for (QString alias : metadata.split(',')) {
		alias = alias.trimmed();
		alias.replace(QChar(0xe000), ',');
		// Never auto-install bare surnames or initials as unambiguous aliases.
		if (alias.isEmpty() || (!alias.contains(' ') && !alias.contains(QChar(0x00b7)))) continue;
		if (alias.size() > 150 || alias.contains(QRegularExpression("\\b\\p{L}\\."))) continue;
		if (nameKey(alias) == nameKey(requested)) matched = true;
		if (!aliases.contains(alias)) aliases.append(alias);
	}
	if (!matched) return {};
	const QRegularExpression chinese("<span title=[\"']zh-hans[^\"']*[\"']>(.*?)</span>", QRegularExpression::DotMatchesEverythingOption);
	zh = text(chinese.match(s.mid(begin, end - begin)).captured(1));
	QJsonObject p{{"canonical", canonical}, {"imslp", category}, {"aliases", aliases}, {"zh", zh}, {"sources", QJsonArray{QString("https://imslp.org/wiki/") + category}}};
	const QRegularExpression wiki("href=[\"']https://en\\.wikipedia\\.org/wiki/([^\"'#]+)[\"']");
	const auto link = wiki.match(s, end);
	if (link.hasMatch()) {
		const QString title = QUrl::fromPercentEncoding(link.captured(1).toUtf8()).replace('_', ' ');
		bool same = nameKey(title) == nameKey(canonical);
		for (const auto& a : aliases)
			if (nameKey(a.toString()) == nameKey(title)) same = true;
		if (same) p.insert("wikipedia", QJsonObject{{"en", title}});
	}
	return p;
}
bool ComposerIdentities::mergeVerified(const QJsonObject& p, QString* error)
{
	QMutexLocker lock(&mutex);
	reload();
	auto fail = [&](const QString& message) {if(error)*error=message;return false; };
	if (!valid(p)) return fail("Invalid identity");
	QJsonArray next = people;
	int replace = -1;
	for (int i = 0; i < next.size(); ++i) {
		const auto other = next[i].toObject();
		if (other.value("imslp") == p.value("imslp")) {
			replace = i;
			continue;
		}
		for (const auto& a : names(other))
			for (const auto& b : names(p))
				if (nameKey(a) == nameKey(b)) return fail("Alias belongs to another IMSLP person: " + b);
	}
	if (replace >= 0) {
		auto merged = next[replace].toObject();
		for (auto it = p.begin(); it != p.end(); ++it) merged.insert(it.key(), it.value());
		next[replace] = merged;
	}
	else
		next.append(p);
	QFile existing(configurationPath());
	if (existing.exists()) {
		if (!existing.open(QIODevice::ReadOnly)) return fail("Cannot read identity file");
		const QByteArray current = existing.readAll();
		if (current != lastExternal) return fail("Identity file changed during update; retry later");
		auto doc = QJsonDocument::fromJson(current);
		if (!doc.isObject() || doc.object().value("version").toInt() != 1 || !doc.object().value("people").isArray()) return fail("Refusing to overwrite invalid identity file");
		for (const auto& entry : doc.object().value("people").toArray())
			if (!valid(entry.toObject())) return fail("Invalid identity entry");
	}
	QDir().mkpath(QFileInfo(configurationPath()).absolutePath());
	QSaveFile file(configurationPath());
	if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
	auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"people", next}}).toJson();
	if (bytes.size() > 1024 * 1024) return fail("Identity file exceeds size limit");
	if (file.write(bytes) != bytes.size() || !file.commit()) return fail(file.errorString());
	loaded = false;
	reload();
	return true;
}
