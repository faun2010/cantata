#include "network/translationservice.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

static bool writeConfiguration(const QString& path, const QString& url)
{
	QSettings settings(path, QSettings::IniFormat);
	settings.beginGroup(QLatin1String("Translation"));
	settings.setValue(QLatin1String("enabled"), true);
	settings.setValue(QLatin1String("provider"), QLatin1String("ollama"));
	settings.setValue(QLatin1String("url"), url);
	settings.setValue(QLatin1String("model"), QLatin1String("qwen3.8:27b"));
	settings.setValue(QLatin1String("targetLanguage"), QLatin1String("Simplified Chinese"));
	settings.setValue(QLatin1String("promptVersion"), QLatin1String("2"));
	settings.setValue(QLatin1String("timeoutMs"), 240000);
	settings.setValue(QLatin1String("cooldownSeconds"), 30);
	settings.setValue(QLatin1String("maxConcurrentRequests"), 1);
	settings.setValue(QLatin1String("maxQueuedRequests"), 16);
	settings.endGroup();
	settings.sync();
	return settings.status() == QSettings::NoError;
}

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	QCoreApplication::setOrganizationName(QLatin1String("Cantata"));
	QCoreApplication::setApplicationName(QLatin1String("Cantata"));
	qInfo().noquote() << "default_config=" << QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1String("/translation.ini");
	qInfo().noquote() << "default_cache=" << QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1String("/translations");
	QTemporaryDir temporary;
	if (!temporary.isValid()) {
		qCritical("Unable to create temporary directory");
		return 1;
	}

	const QString configuredUrl = qEnvironmentVariable("CANTATA_OLLAMA_URL", "http://127.0.0.1:11434");
	const QUrl url(configuredUrl);
	if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
		qCritical().noquote() << "Invalid CANTATA_OLLAMA_URL:" << configuredUrl;
		return 1;
	}
	const QString configFile = temporary.filePath(QLatin1String("translation.ini"));
	const QString cacheDirectory = temporary.filePath(QLatin1String("cache"));
	if (!writeConfiguration(configFile, configuredUrl)) {
		qCritical("Unable to write smoke-test configuration");
		return 1;
	}

	const QString source = QLatin1String("Ludwig van Beethoven was a German composer and pianist. He is a crucial figure in the history of Western classical music.");
	const QString context = QLatin1String("Artist biography for Ludwig van Beethoven");
	QString firstTranslation;
	qint64 modelElapsedMs = -1;

	{
		TranslationService service(nullptr, configFile, cacheDirectory);
		QEventLoop loop;
		QTimer timeout;
		timeout.setSingleShot(true);
		int resultSignals = 0;
		QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
		QObject::connect(&service, &TranslationService::translationReady, &loop,
		                 [&](const QString& resultSource, const QString& resultContext, const QString& translation) {
			if (resultSource == source && resultContext == context) {
				++resultSignals;
				firstTranslation = translation;
				loop.quit();
			}
		});

		QElapsedTimer modelTimer;
		modelTimer.start();
		if (service.translate(source, context) != source) {
			qCritical("Unexpected cache hit before first model request");
			return 1;
		}
		timeout.start(245000);
		loop.exec();
		modelElapsedMs = modelTimer.elapsed();
		if (resultSignals != 1 || firstTranslation.isEmpty() || firstTranslation == source) {
			qCritical("Ollama request failed or returned no translation");
			return 1;
		}

		QElapsedTimer memoryTimer;
		memoryTimer.start();
		const QString memoryTranslation = service.translate(source, context);
		const qint64 memoryElapsedUs = memoryTimer.nsecsElapsed() / 1000;
		if (memoryTranslation != firstTranslation || resultSignals != 1) {
			qCritical("Memory cache did not return the first translation");
			return 1;
		}
		qInfo().noquote() << "memory_cache_us=" << memoryElapsedUs;
	}

	TranslationService restarted(nullptr, configFile, cacheDirectory);
	int restartedSignals = 0;
	QObject::connect(&restarted, &TranslationService::translationReady, [&restartedSignals]() { ++restartedSignals; });
	QElapsedTimer diskTimer;
	diskTimer.start();
	const QString diskTranslation = restarted.translate(source, context);
	const qint64 diskElapsedUs = diskTimer.nsecsElapsed() / 1000;
	QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
	if (diskTranslation != firstTranslation || restartedSignals != 0) {
		qCritical("Disk cache did not survive service reconstruction");
		return 1;
	}

	qInfo().noquote() << "model=qwen3.8:27b";
	qInfo().noquote() << "model_request_ms=" << modelElapsedMs;
	qInfo().noquote() << "disk_cache_us=" << diskElapsedUs;
	qInfo().noquote() << "model_result_signals=1";
	qInfo().noquote() << "translation=" << firstTranslation;
	return 0;
}
