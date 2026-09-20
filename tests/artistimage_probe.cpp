// Live acceptance through the production downloader, including image decoding
// and disk cache. Uses a separate application profile; never changes MPD tags.
#include "gui/covers.h"
#include "mpd-interface/mpdconnection.h"
#include "support/thread.h"
#include <QApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	app.setOrganizationName("CantataArtistImageProbe");
	app.setApplicationName("CantataArtistImageProbe");
	app.setApplicationVersion("1.0");
	Covers::enableDebug(false);
	MPDConnection::self();
	CoverDownloader* downloader = new CoverDownloader;
	bool passed = true;
	for (const QString& name : {QStringLiteral("Aleksandr Borodin"), QStringLiteral("Alexandre Borodine")}) {
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		bool received = false;
		Song song;
		song.setArtistImageRequest();
		song.albumartist = name;
		QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
		const auto connection = QObject::connect(downloader, &CoverDownloader::artistImage, &loop,
		    [&](const Song& result, const QImage& image, const QString& file) {
			    received = result.albumartist == name && image.width() >= 32 && QFileInfo(file).size() > 0 && !QImage(file).isNull();
			    qInfo().noquote() << (received ? "PASS" : "FAIL") << name << image.size() << file;
			    loop.quit();
		    });
		QMetaObject::invokeMethod(downloader, [downloader, song]() { downloader->download(song); }, Qt::QueuedConnection);
		timer.start(90000);
		loop.exec();
		QObject::disconnect(connection);
		passed = passed && received;
		if (!received) break;
	}
	downloader->stop();
	ThreadCleaner::self()->stopAll();
	return passed ? 0 : 1;
}
