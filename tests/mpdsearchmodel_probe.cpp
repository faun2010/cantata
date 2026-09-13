#include "models/mpdsearchmodel.h"
#include "models/roles.h"
#include "network/translationservice.h"

#include <QApplication>
#include <QPersistentModelIndex>
#include <QItemSelectionModel>

class AppendProbeModel : public SearchModel {
public:
	using SearchModel::SearchModel;
	void search(const QString&, const QString&) override {}
	void append(const QList<Song>& songs) { appendResults(songs); }
};

static Song song(const QString& file, const QString& title)
{
	Song result;
	result.file = file;
	result.title = title;
	return result;
}

static void require(bool condition, const char* message)
{
	if (!condition) qFatal("%s", message);
}

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	QCoreApplication::setOrganizationName(QStringLiteral("Cantata"));
	QCoreApplication::setApplicationName(QStringLiteral("cantata"));
	TranslationService::disableNetworkAccess();

	AppendProbeModel appendModel;
	appendModel.setMultiColumn(true);
	appendModel.append({song(QLatin1String("one.flac"), QLatin1String("One"))});
	QItemSelectionModel selection(&appendModel);
	selection.setCurrentIndex(appendModel.index(0, SearchModel::COL_TITLE), QItemSelectionModel::ClearAndSelect);
	int resets = 0;
	QObject::connect(&appendModel, &QAbstractItemModel::modelReset, &app, [&]() { ++resets; });
	QPersistentModelIndex first;
	QObject::connect(&appendModel, &QAbstractItemModel::rowsAboutToBeInserted, &app,
	                 [&]() { first = appendModel.index(0, SearchModel::COL_TITLE); });
	appendModel.append({song(QLatin1String("two.flac"), QLatin1String("Two"))});
	require(first.isValid(), "incremental append invalidated a persistent index");
	require(first.data().toString() == QLatin1String("One"), "persistent index points at the wrong song after append");
	QList<Song> many;
	for (int n = 0; n < 500; ++n) many.append(song(QString::number(n), QString::number(n)));
	appendModel.append(many);
	require(resets == 0 && selection.currentIndex().data().toString() == QLatin1String("One")
	        && selection.selectedIndexes().size() == 1 && selection.selectedIndexes().first().data().toString() == QLatin1String("One"),
	        "incremental results reset the model or lost the user's selection");

	MpdSearchModel model;
	QObject::disconnect(&model, nullptr, nullptr, nullptr);
	int searching = 0;
	int searched = 0;
	QObject::connect(&model, &SearchModel::searching, &app, [&]() { ++searching; });
	QObject::connect(&model, &SearchModel::searched, &app, [&]() { ++searched; });
	QList<int> ids;
	bool replySynchronously = false;
	QObject::connect(&model, qOverload<const QString&, const QString&, int>(&MpdSearchModel::search),
	                 [&model, &ids, &replySynchronously](const QString&, const QString&, int id) {
		                 ids.append(id);
		                 if (replySynchronously)
			                 QMetaObject::invokeMethod(&model, "searchFinished", Qt::DirectConnection,
			                                           Q_ARG(int, id), Q_ARG(QList<Song>, QList<Song>()));
	                 });

	model.search(QLatin1String("artist"), QLatin1String("first"));
	require(searching == 1 && searched == 0 && ids.size() == 1,
	        "a submitted MPD request must keep the spinner active");
	const int staleId = ids.constLast();
	model.search(QLatin1String("artist"), QLatin1String("second"));
	require(searching == 2 && searched == 1 && ids.size() == 2,
	        "changing the query must finish the old spinner and start a new request");
	const int currentId = ids.constLast();

	QMetaObject::invokeMethod(&model, "searchFinished", Qt::DirectConnection,
	                          Q_ARG(int, staleId), Q_ARG(QList<Song>, QList<Song>{song(QLatin1String("stale.flac"), QLatin1String("Stale"))}));
	require(searched == 1 && model.rowCount() == 0,
	        "a stale MPD response affected the current query");
	replySynchronously = true;
	const QStringList aliases {QLatin1String("second alias 1"), QLatin1String("second alias 2")};
	QMetaObject::invokeMethod(&model, "submitSearches", Qt::DirectConnection,
	                          Q_ARG(QStringList, aliases));
	require(ids.size() == 4, "an alternatives batch did not submit every unique value");
	require(searched == 1, "a synchronous alternatives reply stopped the spinner before the original response");
	replySynchronously = false;
	QMetaObject::invokeMethod(&model, "searchFinished", Qt::DirectConnection,
	                          Q_ARG(int, currentId), Q_ARG(QList<Song>, QList<Song>{song(QLatin1String("current.flac"), QLatin1String("Current"))}));
	require(searched == 2 && model.rowCount() == 1,
	        "the spinner did not stop after the current MPD response");

	return 0;
}
