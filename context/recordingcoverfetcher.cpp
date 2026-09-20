#include "recordingcoverfetcher.h"
#include "recordingcovers.h"
#include "recommendedrecordings.h"
#include "network/networkaccessmanager.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

namespace {
QString coverKey(const QJsonObject& r)
{
	return RecommendedRecordings::recordingCoverKey(r.value("performers").toString(), r.value("label").toString(),
	    r.value("catalogue").toString(), r.value("composer").toString(), r.value("work").toString(), r.value("year").toString());
}

void printResult(const QJsonObject& result)
{
	QTextStream out(stdout);
	out << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
}
}

RecordingCoverFetcher::RecordingCoverFetcher(QObject* parent, const QString& program, const QString& cache)
	: QObject(parent), executable(program.isEmpty() ? QCoreApplication::applicationFilePath() : program),
	  cacheDirectory(cache.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("recording-covers-v2") : cache)
{
	connect(&process, &QProcess::started, this, [this]() {
		process.write(running);
		process.closeWriteChannel();
	});
	connect(&process, &QProcess::readyReadStandardOutput, this, &RecordingCoverFetcher::readResults);
	connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this]() {
		readResults();
		if (running == pending) {
			QSaveFile result(QDir(cacheDirectory).filePath("current-results.jsonl"));
			if (result.open(QIODevice::WriteOnly)) { result.write(report); result.commit(); }
		}
		if (running != pending) startPending();
	});
}

RecordingCoverFetcher::~RecordingCoverFetcher()
{
	process.kill();
	process.waitForFinished(1000);
}

void RecordingCoverFetcher::setRecordings(const QJsonArray& recordings)
{
	const QByteArray next = recordings.isEmpty() ? QByteArray() : QJsonDocument(QJsonObject{{"recordings", recordings}}).toJson(QJsonDocument::Compact);
	if (next == pending) return;
	pending = next;
	keys.clear();
	for (const QJsonValue& value : recordings) keys.insert(coverKey(value.toObject()));
	if (process.state() != QProcess::NotRunning) {
		// No configuration or MPD state lives in the child. QSaveFile makes
		// cancellation safe even while a thumbnail is being written.
		process.kill();
	}
	else startPending();
}

void RecordingCoverFetcher::startPending()
{
	running = pending;
	output.clear();
	report.clear();
	if (running.isEmpty() || !RecordingCovers::networkAccessEnabled()) return;
	const QString directory = cacheDirectory;
	QDir().mkpath(directory);
	// Retain a concrete manifest so the same request can be rerun manually.
	QSaveFile manifest(QDir(directory).filePath("current-request.json"));
	if (manifest.open(QIODevice::WriteOnly)) { manifest.write(running); manifest.commit(); }
	process.setProgram(executable);
	process.setArguments({"--fetch-recording-covers", "--input", "-", "--cache-dir", directory});
	process.start();
}

void RecordingCoverFetcher::readResults()
{
	output += process.readAllStandardOutput();
	while (output.contains('\n')) {
		const int end = output.indexOf('\n');
		report += output.left(end + 1);
		const QJsonObject result = QJsonDocument::fromJson(output.left(end)).object();
		output.remove(0, end + 1);
		const QString key = result.value("key").toString();
		const QString path = result.value("path").toString();
		if (running == pending && keys.contains(key) && !path.isEmpty() && QFile::exists(path)) emit coverReady(key, path);
	}
}

int runRecordingCoverBatch(QCoreApplication& app)
{
	QCommandLineParser parser;
	parser.setApplicationDescription("Download reference-recording covers. Input: {\"recordings\":[{\"composer\":...,\"work\":...,\"performers\":...}]}. Output: JSON Lines.");
	parser.addHelpOption();
	parser.addOption({"fetch-recording-covers", "Run the cover batch worker without opening a window."});
	parser.addOption({"input", "JSON manifest, or - for stdin.", "file", "-"});
	parser.addOption({"cache-dir", "Use an isolated cache directory.", "directory"});
	parser.addOption({"no-network", "Only inspect cached results."});
	parser.addOption({"retry-missing", "Retry cached misses; keep successful downloads."});
	parser.process(app);
	QFile input;
	const QString inputPath = parser.value("input");
	if (inputPath == "-") input.open(stdin, QIODevice::ReadOnly);
	else { input.setFileName(inputPath); input.open(QIODevice::ReadOnly); }
	if (!input.isOpen()) { printResult({{"error", "Cannot open input manifest"}}); return 2; }
	QJsonParseError parseError;
	const QByteArray data = input.read(1024 * 1024 + 1);
	const auto document = QJsonDocument::fromJson(data, &parseError);
	if (data.size() > 1024 * 1024 || parseError.error != QJsonParseError::NoError || !document.isObject() || !document.object().value("recordings").isArray()) {
		printResult({{"error", "Expected a JSON object with a recordings array"}}); return 2;
	}
	const QJsonArray recordings = document.object().value("recordings").toArray();
	if (recordings.size() > 50) { printResult({{"error", "At most 50 recordings per batch"}}); return 2; }
	QMap<QString, QJsonObject> requests;
	for (const QJsonValue& value : recordings) {
		const auto r = value.toObject();
		bool valid = value.isObject();
		for (const char* field : {"composer", "work", "performers"}) valid = valid && r.value(field).isString() && !r.value(field).toString().trimmed().isEmpty();
		for (const char* field : {"label", "catalogue", "year"}) valid = valid && (!r.contains(field) || r.value(field).isString());
		if (!valid) { printResult({{"error", "Each recording requires composer, work and performers strings; optional label, catalogue and year must be strings"}}); return 2; }
		requests.insert(coverKey(r), r);
	}
	if (requests.isEmpty()) return 0;
	const QString directory = parser.value("cache-dir").isEmpty()
	    ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("recording-covers-v2") : parser.value("cache-dir");
	if (parser.isSet("no-network")) RecordingCovers::disableNetworkAccess();
	RecordingCovers covers(nullptr, directory);
	covers.setNetworkAccessManager(NetworkAccessManager::self());
	QSet<QString> finished;
	QObject::connect(&covers, &RecordingCovers::requestFinished, &app, [&](const QString& key, const QString& status) {
		finished.insert(key);
		printResult({{"key", key}, {"status", status}, {"path", covers.cachedCover(key)}, {"source", covers.cachedSourceUrl(key)}});
		if (finished.size() == requests.size()) app.quit();
	});
	QTimer deadline;
	deadline.setSingleShot(true);
	QObject::connect(&deadline, &QTimer::timeout, &app, [&]() {
		for (auto it = requests.constBegin(); it != requests.constEnd(); ++it) if (!finished.contains(it.key())) printResult({{"key", it.key()}, {"status", "timeout"}});
		app.exit(3);
	});
	deadline.start(10 * 60 * 1000);
	// A short delay also limits rapid album changes to one search per second.
	QTimer::singleShot(parser.isSet("no-network") ? 0 : 1100, &app, [&]() {
		for (auto it = requests.constBegin(); it != requests.constEnd(); ++it) {
			if (parser.isSet("retry-missing")) {
				const QString hash = QString::fromLatin1(QCryptographicHash::hash(it.key().toUtf8(), QCryptographicHash::Sha1).toHex());
				QFile::remove(QDir(directory).filePath(hash + ".none"));
			}
			const QJsonObject r = it.value();
			covers.request(it.key(), r.value("performers").toString(), r.value("label").toString(), r.value("catalogue").toString(),
			    r.value("year").toString(), r.value("composer").toString(), r.value("work").toString());
		}
	});
	return app.exec();
}
