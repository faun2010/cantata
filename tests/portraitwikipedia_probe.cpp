// Production Wikipedia queue: spacing, HTTP 429 backoff, no title retry on 429.
#include "mpd-interface/song.h"
#include "network/networkaccessmanager.h"
#include "network/translationservice.h"
#include "support/thread.h"
#include <QtWidgets>
#include <QtNetwork>
#define private public
#include "gui/covers.h"
#undef private

class Reply : public QNetworkReply {
	QByteArray body;
	qint64 offset = 0;
public:
	Reply(const QNetworkRequest& request, QByteArray data, bool limited, QObject* parent)
	    : QNetworkReply(parent), body(data)
	{
		setRequest(request);
		setUrl(request.url());
		setAttribute(QNetworkRequest::HttpStatusCodeAttribute, limited ? 429 : 200);
		if (limited) {
			setError(QNetworkReply::UnknownContentError, "Too many requests");
			setRawHeader("Retry-After", "120");
		}
		open(QIODevice::ReadOnly);
		QTimer::singleShot(0, this, [this]() { setFinished(true); emit readyRead(); emit finished(); });
	}
	void abort() override {}
	qint64 bytesAvailable() const override { return body.size() - offset + QNetworkReply::bytesAvailable(); }
	qint64 readData(char* out, qint64 max) override {
		const qint64 count = qMin(max, qint64(body.size()) - offset);
		if (!count) return -1;
		memcpy(out, body.constData() + offset, count); offset += count; return count;
	}
};

class Providers : public NetworkAccessManager {
public:
	QList<qint64> starts;
	QStringList titles;
	using NetworkAccessManager::NetworkAccessManager;
protected:
	QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
		QByteArray data = "{}";
		bool limited = false;
		if (request.url().host() == "en.wikipedia.org") {
			starts.append(QDateTime::currentMSecsSinceEpoch());
			const QString title = QUrlQuery(request.url()).queryItemValue("titles");
			titles.append(title);
			limited = starts.size() == 2;
			const QJsonObject page{{"ns", 0}, {"title", title}, {"pageprops", QJsonObject{{"wikibase-shortdesc", "composer"}, {"wikibase_item", "Q123"}}}};
			data = QJsonDocument(QJsonObject{{"query", QJsonObject{{"pages", QJsonArray{page}}}}}).toJson();
		}
		return new Reply(request, data, limited, this);
	}
};

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	TranslationService::disableNetworkAccess();
	qRegisterMetaType<Song>("Song");
	auto* downloader = new CoverDownloader;
	Providers* provider = nullptr;
	QMetaObject::invokeMethod(downloader, [&]() {
		provider = new Providers(downloader);
		downloader->manager = provider;
		for (const QString& name : {QString("First Musician"), QString("Second Musician"), QString("Third Musician")}) {
			Song song; song.albumartist = name; song.setArtistImageRequest();
			CoverDownloader::Job job(song, QString());
			downloader->downloadViaWikipedia(job);
		}
	}, Qt::BlockingQueuedConnection);
	bool passed = false;
	QTimer::singleShot(2500, &app, [&]() {
		QMetaObject::invokeMethod(downloader, [&]() {
			passed = provider->starts.size() == 2 && provider->starts[1] - provider->starts[0] >= 950
			    && downloader->nextWikipediaRequest - QDateTime::currentMSecsSinceEpoch() > 115000
			    && downloader->wikipediaQueue.size() == 1 && provider->titles == QStringList{"First Musician", "Second Musician"};
		}, Qt::BlockingQueuedConnection);
		app.quit();
	});
	app.exec();
	downloader->stop();
	ThreadCleaner::self()->stopAll();
	qInfo() << (passed ? "PASS" : "FAIL") << "Wikipedia spacing, Retry-After and no immediate 429 title retry";
	return passed ? 0 : 1;
}
