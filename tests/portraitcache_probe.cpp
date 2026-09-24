// Exercise the production disk cache across processes, without network requests.
// Default: isolated profile. --audit: read existing user name-based portraits.
#include "context/artistlookup.h"
#include "gui/artworkquality.h"
#include "gui/artistimageprovider.h"
#include "gui/covers.h"
#include "network/networkaccessmanager.h"
#include "network/translationservice.h"
#include "support/thread.h"
#include "support/utils.h"
#include <QtWidgets>
#include <QProcess>

static Song portrait(const QString& artist)
{
	Song song;
	song.artist = song.albumartist = artist;
	song.setArtistImageRequest();
	return song;
}

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	const QStringList args = app.arguments();
	const bool audit = args.contains("--audit");
	const bool resume = args.contains("--resume");
	app.setOrganizationName(audit ? "Cantata" : "CantataPortraitCacheProbe");
	app.setApplicationName(audit ? "cantata" : resume ? args.last()
	    : QString("portrait-cache-%1").arg(app.applicationPid()));
	TranslationService::disableNetworkAccess();
	auto* covers = Covers::self();
	int downloads = 0;
	QObject::connect(covers, &Covers::download, &app, [&](const Song&) { ++downloads; });
	bool passed = true;
	auto check = [&](bool ok, const QString& label) {
		qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
		passed = passed && ok;
	};
	const QString dir = Utils::cacheDir(Covers::constCoverDir, !audit);
	if (dir.isEmpty()) {
		qCritical() << "Cannot access portrait cache directory";
		return 2;
	}
	if (audit) {
		int checked = 0;
		for (const QString& name : QDir(dir).entryList({"*.jpg", "*.png"}, QDir::Files)) {
			if (name.startsWith("artist-identity-")) continue;
			const auto found = Covers::locateImage(portrait(QFileInfo(name).completeBaseName()));
			if (ArtworkQuality::isStarPlaceholder(QImage(dir + name))) {
				check(found.fileName != dir + name, "reject legacy star placeholder: " + name);
				continue;
			}
			check(!found.img.isNull() && QFileInfo(found.fileName).completeBaseName() == QFileInfo(name).completeBaseName(), name);
			++checked;
		}
		check(checked > 0, QString("existing name-based portraits: %1").arg(checked));
	}
	else {
		const Song existing = portrait("Cache Existing Artist");
		const QString existingPath = dir + existing.artist + ".png";
		QImage original(96, 96, QImage::Format_RGB32);
		original.fill(QColor(30, 90, 150));
		if (!resume) {
			check(!dir.isEmpty() && original.save(existingPath), "seed yesterday's name-based original");
			QImage other(96, 96, QImage::Format_RGB32);
			other.fill(QColor(170, 80, 20));
			check(other.save(dir + ArtistLookup::imageCacheToken(existing.artist) + ".jpg"), "seed competing identity cache");
		}
		const auto found = covers->requestImage(existing, true);
		check(found.fileName == existingPath && found.img == original, "name-based PNG wins over identity JPEG");
		if (!resume) covers->saveScaledCover(original.scaled(64, 64), existing, 64);
		const QPixmap* scaled = covers->getScaledCover(existing, 64);
		check(scaled && scaled->toImage().pixelColor(32, 32) == original.pixelColor(32, 32), "name-based thumbnail survives restart");
		for (const QString& name : {QString("Current Hash Artist"), QString("Legacy Hash Artist")}) {
			const Song song = portrait(name);
			const QString hash = name.startsWith("Current") ? ArtistLookup::imageCacheToken(name) : ArtistLookup::legacyImageCacheToken(name);
			const QString oldPath = dir + hash + ".png";
			if (!resume) check(original.save(oldPath), "seed " + name);
			const auto migrated = covers->requestImage(song, true);
			check(migrated.fileName == dir + name + ".png" && migrated.img == original
			    && QFile::exists(oldPath), "reuse hash original as artist name: " + name);
		}
		if (!resume) {
			// Identity metadata can change without losing a successfully cached portrait.
			QTemporaryDir config;
			const QString configPath = config.filePath("identities.json");
			QFile file(configPath);
			check(file.open(QIODevice::WriteOnly), "open isolated identity config");
			file.write(R"({"version":1,"people":[{"canonical":"Cache Existing Artist","imslp":"Category:Artist,_Cache_Existing","aliases":[],"wikipedia":{"en":"Changed Page"}}]})");
			file.close();
			ComposerIdentities::setConfigurationFile(configPath);
			covers->clearNameCache();
			covers->clearScaleCache();
			check(covers->requestImage(existing, true).fileName == existingPath, "identity update preserves original");
			check(covers->getScaledCover(existing, 64) != nullptr, "identity update preserves thumbnail");
			QProcess child;
			child.start(app.applicationFilePath(), {"--resume", app.applicationName()});
			const bool finished = child.waitForFinished(15000);
			qInfo().noquote() << child.readAllStandardError();
			check(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, "fresh process reuses cache");
		}
	}
	check(downloads == 0, "zero portrait downloads");
	if (!audit && !resume) {
		NetworkAccessManager::disableNetworkAccess();
		const Song failed = portrait("Refresh Failed Artist");
		const QString failurePath = dir + failed.artist + ".failed";
		check(ArtistImageProvider::cacheFailure(failurePath, QDateTime::currentMSecsSinceEpoch()), "seed recent failed request");
		covers->requestImage(failed, true);
		check(downloads == 0, "ordinary paint respects failure cooldown");
		covers->requestImage(failed, true, true);
		check(downloads == 1 && !QFile::exists(failurePath), "explicit refresh retries failed portrait");
	}
	covers->stop();
	ThreadCleaner::self()->stopAll();
	return passed ? 0 : 1;
}
