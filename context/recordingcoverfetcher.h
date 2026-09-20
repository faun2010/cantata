#ifndef RECORDING_COVER_FETCHER_H
#define RECORDING_COVER_FETCHER_H

#include <QJsonArray>
#include <QObject>
#include <QProcess>
#include <QSet>

class QCoreApplication;

// Runs the same batch downloader exposed by scripts/fetch-recording-covers.sh.
// Each view owns one worker; replacing the album cancels the obsolete batch.
class RecordingCoverFetcher : public QObject {
	Q_OBJECT
public:
	explicit RecordingCoverFetcher(QObject* parent = nullptr, const QString& executable = QString(), const QString& cacheDirectory = QString());
	~RecordingCoverFetcher() override;
	void setRecordings(const QJsonArray& recordings);

Q_SIGNALS:
	void coverReady(const QString& key, const QString& path);

private:
	void startPending();
	void readResults();
	QProcess process;
	QString executable;
	QString cacheDirectory;
	QByteArray pending;
	QByteArray running;
	QByteArray output;
	QByteArray report;
	QSet<QString> keys;
};

int runRecordingCoverBatch(QCoreApplication& app);

#endif
