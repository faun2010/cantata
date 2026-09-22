#ifndef DAY_RECORDINGS_H
#define DAY_RECORDINGS_H

#include "composerday.h"
#include "context/recommendedrecordings.h"

// Pure QtCore selection helpers. A recommendation is claimed only when
// the dataset's full performer names and known year are present in tags.
namespace DayRecordings {
struct Selection {
	QList<int> indices;
	bool recommended = false;
};

// At most two distinct recording groups, recommended recordings first in
// dataset order, then other library groups by work-match score (stable on
// ties). Each result
// contains the matching work's complete tracks in disc/track order.
QList<Selection> selectRecordings(const QString& composer, const QList<ComposerDay::Track>& tracks,
		const ComposerDay::Work& work, const RecommendedRecordings::Dataset& dataset);

Selection select(const QString& composer, const QList<ComposerDay::Track>& tracks,
		const ComposerDay::Work& work, const RecommendedRecordings::Dataset& dataset);
}

#endif
