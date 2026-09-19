/*
 * Cantata
 *
 * Copyright (c) 2026 Cantata Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "context/recordingcovers.h"
#include <QTest>

using RC = RecordingCovers;

namespace {

// Real MusicBrainz "release/?fmt=json" responses, captured live with curl
// (User-Agent: Cantata-RecordingCovers/1.0) and trimmed down to the fields
// RecordingCovers actually looks at. Built as plain (non-raw) string
// literals rather than R"(...)" raw strings: moc's simplified C++ parser
// mishandles raw string literals containing braces/quotes and silently
// stops generating meta-object code for the whole translation unit (see
// tests/recommendedrecordings_test.cpp for the same pitfall with a raw
// string containing a URL).

// query=catno:"ARL1-4711" AND label:"RCA" - the TAS "RCA ARL1-4711"
// (Rubinstein/Barenboim, Beethoven Piano Concerto No.2) recording is not in
// MusicBrainz under that catalogue number at all.
const QByteArray arl14711CatalogueResponse = "{\"created\":\"2026-09-15T09:48:49.542Z\",\"count\":0,\"offset\":0,\"releases\":[]}";

// query=artist:"Rubinstein" AND label:"RCA" - the fallback search used once
// the catalogue search above comes back empty.
const QByteArray rubinsteinRcaFallbackResponse =
    "{\"count\":245,\"releases\":["
    "{\"id\":\"d79844b8-2593-482e-b092-41f4a4e66afc\",\"score\":100,\"title\":\"The Rubenstein Collection\",\"release-group\":{\"id\":\"5bda1f46-584a-464e-b7b0-cf44eb54afc1\"},\"label-info\":[{\"catalog-number\":\"5673-2-RC\",\"label\":{\"name\":\"RCA Red Seal\"}}]},"
    "{\"id\":\"fc068eea-74bd-47ca-857c-35abf2a3e2c1\",\"score\":100,\"title\":\"The Chopin Ballades & Scherzos\",\"release-group\":{\"id\":\"24bc6d65-ace8-4700-a068-0b0941591b7d\"},\"label-info\":[{\"catalog-number\":null,\"label\":{\"name\":\"RCA Red Seal\"}}]},"
    "{\"id\":\"beda8a64-5296-49c3-a573-c2b569a98f92\",\"score\":98,\"title\":\"Au coeur de la musique de chambre\",\"release-group\":{\"id\":\"69189357-c1d6-4710-a503-9013587b7ee3\"},\"label-info\":[{\"catalog-number\":null,\"label\":{\"name\":\"RCA\"}}]}"
    "]}";

// query=catno:"SLX 2002" AND label:"Decca" - the TAS "Decca SLX 2002"
// (Curzon/Knappertsbusch, Beethoven "Emperor" concerto) recording is not in
// MusicBrainz under that catalogue number either - Decca's own catalogue
// number for it is actually "SXL 2002" (see below).
const QByteArray slx2002CatalogueResponse = "{\"created\":\"2026-09-15T09:48:54.139Z\",\"count\":0,\"offset\":0,\"releases\":[]}";

// query=artist:"Curzon" AND label:"Decca" AND Beethoven - four real
// candidates: two CD reissues (score 100, catalogue "467 126-2"), an LP
// (score 81, "LC0171") and the original "SXL 2002" LP (score 72) - none
// carry a "cover-art-archive" field at all in this particular response.
const QByteArray curzonDeccaFallbackResponse =
    "{\"count\":4,\"releases\":["
    "{\"id\":\"fada85ef-b858-42d6-90b6-d8f974bab49c\",\"score\":100,\"title\":\"Piano Concertos nos. 4 and 5 Emperor\",\"release-group\":{\"id\":\"20cad695-ef4e-4e80-a42a-2c7af1684e2f\"},\"label-info\":[{\"catalog-number\":\"467 126-2\",\"label\":{\"name\":\"Decca Classics\"}}]},"
    "{\"id\":\"e947b342-0191-4881-bd34-b4dd6eb535f1\",\"score\":100,\"title\":\"Piano Concertos 4 and 5\",\"release-group\":{\"id\":\"20cad695-ef4e-4e80-a42a-2c7af1684e2f\"},\"label-info\":[{\"catalog-number\":\"467 126-2\",\"label\":{\"name\":\"Decca Classics\"}}]},"
    "{\"id\":\"05515b6c-0936-4625-9911-b88118990a6a\",\"score\":81,\"title\":\"Emperor Concerto, Eroica Variations\",\"release-group\":{\"id\":\"8cdf1a51-311c-40ad-b558-9f30b683e318\"},\"label-info\":[{\"catalog-number\":\"LC0171\",\"label\":{\"name\":\"Decca Classics\"}}]},"
    "{\"id\":\"b8eb27e9-186c-463b-87ab-0c0cbc4eb623\",\"score\":72,\"title\":\"Emperor\",\"release-group\":{\"id\":\"df60066a-a423-46aa-b540-d206aedc3cf1\"},\"label-info\":[{\"catalog-number\":\"SXL 2002\",\"label\":{\"name\":\"Decca Records\"}}]}"
    "]}";

// Synthetic (not a captured response): two releases share catalogue "ABC",
// only the lower-scored one flagged as having front cover art; a third has
// catalogue "XYZ" and no cover art.
const QByteArray frontFlagResponse =
    "{\"releases\":["
    "{\"id\":\"no-front\",\"score\":100,\"label-info\":[{\"catalog-number\":\"XYZ\"}]},"
    "{\"id\":\"abc-no-front\",\"score\":90,\"label-info\":[{\"catalog-number\":\"ABC\"}]},"
    "{\"id\":\"has-front\",\"score\":50,\"label-info\":[{\"catalog-number\":\"ABC\"}],\"cover-art-archive\":{\"front\":true}}"
    "]}";

}// namespace

class RecordingCoversTest : public QObject {
	Q_OBJECT

private Q_SLOTS:

	void normalisesCatalogueNumbers()
	{
		QCOMPARE(RC::normaliseCatalogueNumber(QStringLiteral("ARL1-4711")), QString("ARL14711"));
		QCOMPARE(RC::normaliseCatalogueNumber(QStringLiteral("arl1 4711")), QString("ARL14711"));
		QCOMPARE(RC::normaliseCatalogueNumber(QStringLiteral("SLX 2002")), QString("SLX2002"));
		// A single transposed letter - the real Decca catalogue is "SXL
		// 2002" - normalises differently, which is exactly why the
		// dataset's "SLX 2002" never matches it by catalogue number alone.
		QCOMPARE(RC::normaliseCatalogueNumber(QStringLiteral("SXL 2002")), QString("SXL2002"));
		QVERIFY(RC::normaliseCatalogueNumber(QString()).isEmpty());
	}

	void picksThePrimaryLabelFromASlashJoinedDisplayName()
	{
		QCOMPARE(RC::primaryLabelToken(QStringLiteral("RCA/Analogue Productions")), QString("RCA"));
		QCOMPARE(RC::primaryLabelToken(QStringLiteral("Columbia/Impex")), QString("Columbia"));
		QCOMPARE(RC::primaryLabelToken(QStringLiteral("Decca")), QString("Decca"));
		QCOMPARE(RC::primaryLabelToken(QStringLiteral(" RCA / Analogue Productions ")), QString("RCA"));
		QVERIFY(RC::primaryLabelToken(QString()).isEmpty());
	}

	void buildsTheCatalogueQuery()
	{
		QCOMPARE(RC::buildCatalogueQuery(QStringLiteral("ARL1-4711"), QStringLiteral("RCA")),
		         QString("(catno:\"ARL1-4711\" OR catno:\"ARL14711\") AND label:\"RCA\""));
		// The label is split to its first "/"-joined component.
		QCOMPARE(RC::buildCatalogueQuery(QStringLiteral("ARL1-4711"), QStringLiteral("RCA/Analogue Productions")),
		         QString("(catno:\"ARL1-4711\" OR catno:\"ARL14711\") AND label:\"RCA\""));
		// Stripping spaces/hyphens makes no difference here, so only one
		// catno clause is used.
		QCOMPARE(RC::buildCatalogueQuery(QStringLiteral("6011"), QStringLiteral("Columbia/Impex")),
		         QString("catno:\"6011\" AND label:\"Columbia\""));
		QCOMPARE(RC::buildCatalogueQuery(QString(), QStringLiteral("RCA")), QString("label:\"RCA\""));
		QCOMPARE(RC::buildCatalogueQuery(QStringLiteral("ARL1-4711"), QString()),
		         QString("(catno:\"ARL1-4711\" OR catno:\"ARL14711\")"));
		QVERIFY(RC::buildCatalogueQuery(QString(), QString()).isEmpty());
	}

	void parsesAnEmptyMusicBrainzResponse()
	{
		QVERIFY(RC::parseReleaseSearchResponse(arl14711CatalogueResponse).isEmpty());
		QVERIFY(RC::parseReleaseSearchResponse(slx2002CatalogueResponse).isEmpty());
	}

	void parsesRealMusicBrainzReleaseCandidates()
	{
		const auto candidates = RC::parseReleaseSearchResponse(curzonDeccaFallbackResponse);
		QCOMPARE(candidates.size(), 4);
		QCOMPARE(candidates.at(0).id, QString("fada85ef-b858-42d6-90b6-d8f974bab49c"));
		QCOMPARE(candidates.at(0).score, 100);
		QCOMPARE(candidates.at(0).releaseGroupId, QString("20cad695-ef4e-4e80-a42a-2c7af1684e2f"));
		QCOMPARE(candidates.at(0).catalogNumbers, QStringList{QStringLiteral("467 126-2")});
		QVERIFY(!candidates.at(0).hasCoverArtFrontField);

		QCOMPARE(candidates.at(3).id, QString("b8eb27e9-186c-463b-87ab-0c0cbc4eb623"));
		QCOMPARE(candidates.at(3).catalogNumbers, QStringList{QStringLiteral("SXL 2002")});
	}

	void selectingWithNoCandidatesReturnsAnEmptyCandidate()
	{
		const RC::ReleaseCandidate chosen = RC::selectBestRelease({}, QStringLiteral("ARL14711"));
		QVERIFY(chosen.id.isEmpty());
	}

	void selectsTheExactCatalogueMatchEvenWhenItIsNotTheTopScore()
	{
		const auto candidates = RC::parseReleaseSearchResponse(curzonDeccaFallbackResponse);
		// "SXL 2002" (score 72, last in the list) is the exact catalogue
		// match, and must win over the two score-100 CD reissues.
		const RC::ReleaseCandidate chosen = RC::selectBestRelease(candidates, RC::normaliseCatalogueNumber(QStringLiteral("SXL 2002")));
		QCOMPARE(chosen.id, QString("b8eb27e9-186c-463b-87ab-0c0cbc4eb623"));
	}

	void returnsNothingWithoutAnExactCatalogueMatch()
	{
		// A wrong cover is worse than none: the dataset's "SLX 2002" (a
		// variant of the real "SXL 2002") matches none of these, so no
		// release is chosen even though same-artist releases exist.
		const auto candidates = RC::parseReleaseSearchResponse(curzonDeccaFallbackResponse);
		QVERIFY(RC::selectBestRelease(candidates, RC::normaliseCatalogueNumber(QStringLiteral("SLX 2002"))).id.isEmpty());

		const auto rubinsteinCandidates = RC::parseReleaseSearchResponse(rubinsteinRcaFallbackResponse);
		QCOMPARE(rubinsteinCandidates.size(), 3);
		QVERIFY(RC::selectBestRelease(rubinsteinCandidates, RC::normaliseCatalogueNumber(QStringLiteral("ARL1-4711"))).id.isEmpty());
		QVERIFY(RC::selectBestRelease(rubinsteinCandidates, QString()).id.isEmpty());
	}

	void prefersACoverArtArchiveFrontAmongExactCatalogueMatches()
	{
		const auto candidates = RC::parseReleaseSearchResponse(frontFlagResponse);
		QCOMPARE(candidates.size(), 3);
		QCOMPARE(RC::selectBestRelease(candidates, QStringLiteral("ABC")).id, QString("has-front"));
		// The front flag never promotes a release with another catalogue number.
		QCOMPARE(RC::selectBestRelease(candidates, QStringLiteral("XYZ")).id, QString("no-front"));
	}

	void ignoresMalformedOrEmptyResponses()
	{
		QVERIFY(RC::parseReleaseSearchResponse(QByteArray("not json")).isEmpty());
		QVERIFY(RC::parseReleaseSearchResponse(QByteArray("[1,2,3]")).isEmpty());
		QVERIFY(RC::parseReleaseSearchResponse(QByteArray("{}")).isEmpty());
		// An entry without an "id" is skipped rather than crashing.
		QVERIFY(RC::parseReleaseSearchResponse(QByteArray("{\"releases\":[{\"score\":100}]}")).isEmpty());
	}
};

QTEST_GUILESS_MAIN(RecordingCoversTest)
#include "recordingcovers_test.moc"
