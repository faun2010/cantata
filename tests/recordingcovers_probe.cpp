// Live acceptance with production network/proxy setup and a project-local cache.
#include "context/recordingcovers.h"
#include "network/networkaccessmanager.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QPainter>
#include <QTextDocument>
#include <QTimer>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	app.setOrganizationName("CantataRecordingCoversProbe");
	app.setApplicationName("CantataRecordingCoversProbe");
	const QString cache = app.applicationDirPath() + "/recording-cover-probe-cache";
	RecordingCovers covers(nullptr, cache);
	covers.setNetworkAccessManager(NetworkAccessManager::self());
	const QString key = "weber-keilberth-no-catalogue";
	QString path = covers.cachedCover(key);
	if (path.isEmpty()) {
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
		QObject::connect(&covers, &RecordingCovers::coverReady, &loop, [&](const QString& result, const QString& file) {
			if (result == key) { path = file; loop.quit(); }
		});
		covers.request(key, "Joseph Keilberth", "EMI", "", "1958", "Carl Maria von Weber", QString::fromUtf8("Der Freischütz"));
		timer.start(75000);
		loop.exec();
	}
	const QImage cover(path);
	const QString source = covers.cachedSourceUrl(key);
	const bool ok = !cover.isNull() && source.startsWith("https://musicbrainz.org/release/");
	qInfo().noquote() << (ok ? "PASS" : "FAIL") << "No-catalogue work/performer lookup:" << cover.size() << path << source;
	if (ok) {
		RecordingCovers reopened(nullptr, cache);
		if (reopened.cachedCover(key) != path || reopened.cachedSourceUrl(key) != source) return 1;
		QTextDocument document;
		document.setHtml(QStringLiteral("<h3>Recommended Recordings</h3><p><a href=\"%1\"><img src=\"%2\" width=\"96\"/></a><br/><b>Joseph Keilberth</b><br/>EMI (1958)</p>")
		    .arg(source.toHtmlEscaped(), QUrl::fromLocalFile(path).toString().toHtmlEscaped()));
		document.setTextWidth(360);
		QImage preview(380, 280, QImage::Format_ARGB32_Premultiplied);
		preview.fill(Qt::white);
		QPainter painter(&preview);
		painter.translate(10, 10);
		document.drawContents(&painter);
		painter.end();
		preview.save(app.applicationDirPath() + "/recording-cover-preview.png");
	}
	return ok ? 0 : 1;
}
