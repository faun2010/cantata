// Link with the production Cantata objects, replacing gui/main.cpp.o.
// A fake provider and isolated cache exercise the real queue, deadline, and
// Covers::loaded notification without contacting external services.
// Build with scripts/run-macos-model-probe.py --build-dir build, then run the
// resulting probe with queued, missing, or early as its argument.
#include "mpd-interface/song.h"
#include "network/networkaccessmanager.h"
#include "network/translationservice.h"
#include "support/thread.h"
#include "support/utils.h"
#include <QtNetwork>
#include <QtWidgets>
#include <atomic>
#define private public
#include "gui/covers.h"
#undef private

static const QString mbid = QStringLiteral("e252e2e9-5cca-4bb6-a787-f9236d3a91e0");
static std::atomic<int> musicBrainzRequests{0};

class Reply : public QNetworkReply {
	QByteArray bytes;
	qint64 offset = 0;

public:
	Reply(const QNetworkRequest& request, const QByteArray& body, QObject* parent)
		: QNetworkReply(parent), bytes(body)
	{
		setRequest(request);
		setUrl(request.url());
		setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
		open(QIODevice::ReadOnly);
		QTimer::singleShot(0, this, [this]() { setFinished(true); emit readyRead(); emit finished(); });
	}
	void abort() override {}
	qint64 bytesAvailable() const override { return bytes.size() - offset + QNetworkReply::bytesAvailable(); }
	qint64 readData(char* out, qint64 max) override
	{
		const qint64 count = qMin(max, qint64(bytes.size()) - offset);
		if (!count) return -1;
		memcpy(out, bytes.constData() + offset, count);
		offset += count;
		return count;
	}
};

class Providers : public NetworkAccessManager {
public:
	bool missing;
	bool early;
	QByteArray png;
	Providers(bool noImage, bool earlyImage, QObject* parent)
		: NetworkAccessManager(parent), missing(noImage), early(earlyImage)
	{
		QImage image(96, 96, QImage::Format_RGB32);
		for (int y = 0; y < image.height(); ++y)
			for (int x = 0; x < image.width(); ++x) image.setPixelColor(x, y, QColor(x * 2, y * 2, 180));
		QBuffer buffer(&png);
		buffer.open(QIODevice::WriteOnly);
		image.save(&buffer, "PNG");
	}

protected:
	QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
	{
		const QUrl url = request.url();
		QByteArray response = "{}";
		if (url.host() == "musicbrainz.org") {
			++musicBrainzRequests;
			response = QJsonDocument(QJsonObject{{"id", mbid}, {"relations", QJsonArray{}}}).toJson();
		}
		else if (url.host() == "webservice.fanart.tv" && !missing) {
			response = QJsonDocument(QJsonObject{{"mbid_id", mbid}, {"artistthumb", QJsonArray{QJsonObject{{"url", "https://assets.fanart.tv/portrait.png"}}}}}).toJson();
		}
		else if (url.host() == "assets.fanart.tv" || url.host() == "commons.wikimedia.org") {
			response = png;
		}
		else if (early && url.host() == "en.wikipedia.org") {
			response = R"({"query":{"pages":[{"ns":0,"title":"Dave Heath","pageprops":{"wikibase-shortdesc":"British composer and flautist","wikibase_item":"Q123"}}]}})";
		}
		else if (early && url.host() == "www.wikidata.org") {
			response = R"({"entities":{"Q123":{"claims":{"P18":[{"mainsnak":{"datavalue":{"value":"Portrait.png"}}}]}}}})";
		}
		qInfo() << "PROVIDER" << url.host();
		return new Reply(request, response, this);
	}
};

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	app.setApplicationName(QStringLiteral("cantata-portrait-queue-probe-%1").arg(QCoreApplication::applicationPid()));
	qRegisterMetaType<Song>("Song");
	TranslationService::disableNetworkAccess();
	const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("queued");
	const bool missing = mode == "missing", early = mode == "early";
	auto* covers = Covers::self();
	auto* downloader = new CoverDownloader;
	QMetaObject::invokeMethod(downloader, [=]() {
        downloader->manager = new Providers(missing, early, downloader);
        // Simulate requests already queued by the preceding screen of artists.
        downloader->nextMusicBrainzRequest = QDateTime::currentMSecsSinceEpoch() + 13500; }, Qt::BlockingQueuedConnection);
	covers->downloader = downloader;
	QObject::connect(covers, &Covers::download, downloader, &CoverDownloader::download, Qt::QueuedConnection);
	QObject::connect(downloader, &CoverDownloader::artistImage, covers, &Covers::artistImageDownloaded, Qt::QueuedConnection);
	Song song;
	song.artist = "Dave Heath";
	song.albumartist = song.artist;
	song.setArtistImageRequest();
	QElapsedTimer elapsed;
	elapsed.start();
	int result = 2, loaded = 0;
	QObject::connect(covers, qOverload<const Song&, int>(&Covers::loaded), &app, [&](const Song&, int) { ++loaded; });
	QObject::connect(covers, &Covers::artistImage, &app, [&](const Song&, const QImage& image, const QString& file) {
		const qint64 ms = elapsed.elapsed();
		const QString failure = Utils::cacheDir(Covers::constCoverDir, false) + Covers::artistCacheName(song.artist) + ".failed";
		const bool correctTime = early ? musicBrainzRequests == 0 : musicBrainzRequests == 1;
		const bool correctResult = missing ? image.isNull() && QFile::exists(failure)
										   : !image.isNull() && QFileInfo(file).completeBaseName() == Covers::artistCacheName(song.artist) && loaded > 0 && !QFile::exists(failure)
						&& covers->get(song, 32)->toImage().pixelColor(16, 16).blue() == 180;
		result = correctTime && correctResult ? 0 : 1;
		qInfo() << "MODE" << mode << "ELAPSED_MS" << ms << "LOADED" << loaded << "IMAGE" << image.size() << "RESULT" << (result == 0 ? "PASS" : "FAIL");
		app.quit();
	});
	QTimer::singleShot(20000, &app, &QCoreApplication::quit);
	// The same lazy request path used when a new artist scrolls into view.
	covers->get(song, 32);
	app.exec();
	covers->stop();
	ThreadCleaner::self()->stopAll();
	return result;
}
