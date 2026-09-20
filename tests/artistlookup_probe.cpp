// Live acceptance probe using the production WikipediaEngine. No user cache or
// translation configuration is modified. Run with run-macos-model-probe.py.
#include "context/wikipediaengine.h"
#include "network/networkaccessmanager.h"
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	app.setOrganizationName(QStringLiteral("CantataArtistLookupProbe"));
	app.setApplicationName(QStringLiteral("CantataArtistLookupProbe"));
	WikipediaEngine::setPreferedLangs({QStringLiteral("en")});
	WikipediaEngine::setIntroOnly(true);
	struct Sample { QString input; QString expected; };
	const QList<Sample> samples = {
	    {QStringLiteral("Aleksandr Borodin"), QStringLiteral("Alexander_Borodin")},
	    {QStringLiteral("Alexandre Borodine"), QStringLiteral("Alexander_Borodin")},
	    {QStringLiteral("Sergey Rachmaninov"), QStringLiteral("Sergei_Rachmaninoff")},
	    {QStringLiteral("Pyotr Il'yich Tchaikovsky"), QStringLiteral("Pyotr_Ilyich_Tchaikovsky")},
	    {QStringLiteral("Antonin Dvorak"), QStringLiteral("Antonín_Dvořák")},
	    // Not in the built-in aliases: MediaWiki itself must resolve this.
	    {QStringLiteral("Dmitry Shostakovich"), QStringLiteral("Dmitri_Shostakovich")}
	};
	bool passed = true;
	for (const Sample& sample : samples) {
		WikipediaEngine engine(nullptr);
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		QString html;
		QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
		QObject::connect(&engine, &ContextEngine::searchResult, &loop, [&](const QString& result, const QString&) { html = result; loop.quit(); });
		engine.search({sample.input}, ContextEngine::Artist);
		timer.start(20000);
		loop.exec();
		engine.cancel();
		const bool ok = html.size() > 500 && html.contains(sample.expected) && html.contains(QStringLiteral("composer"), Qt::CaseInsensitive);
		qInfo().noquote() << (ok ? "PASS" : "FAIL") << sample.input << "chars=" << html.size() << "expected=" << sample.expected;
		if (!ok) qInfo().noquote() << html.left(500);
		passed = passed && ok;
	}
	return passed ? 0 : 1;
}
