#!/usr/bin/env python3
"""Regenerate playlists/composercalendar.json from Wikidata.

The "Composer of the Day" page (playlists/composerdaypage.cpp) only ever shows
the entries whose birth or death falls on today's month/day, so the calendar
needs enough composers that most days of the year have somebody. Typing that
by hand does not scale; this script pulls it from Wikidata instead:

  1. SPARQL: every item with occupation "composer" (Q36834) and at least
     --min-sitelinks Wikipedia/Wikimedia sitelinks (a fame proxy).
  2. wbgetentities: dates (with calendar model and precision), genres,
     occupations and en/zh labels for each of them.
  3. Keep the classical composers only: "composer" must be their first
     (most prominent) occupation, they need an exact classical genre
     (classical music, opera, symphony, ...) or an IMSLP page, and must be
     neither pop musicians nor famous for something else (poets, rulers,
     scientists). This drops the pop songwriters Wikidata also files under
     "composer", and performers/conductors who composed on the side.
  4. Merge with the entries already in the calendar: an existing entry wins
     (its name matches ComposerTable's canonical spelling and its dates were
     checked by hand), generated entries are only added for new composers.

Only day-precision dates are used; Julian-calendar dates from 1582 on are
converted to the Gregorian calendar, and dates before 1582 are dropped, since
the anniversary of a pre-Gregorian date is ambiguous.

The downloads are cached under --cache (default: build-tests/wikidata-cache),
so re-running to tweak the filters does not hit Wikidata again.

Usage:
  scripts/gen-composer-calendar.py            # preview counts, write nothing
  scripts/gen-composer-calendar.py --apply    # rewrite the calendar
"""

import argparse
import datetime
import json
import os
import sys
import time
import unicodedata
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CALENDAR = os.path.join(ROOT, "playlists", "composercalendar.json")
USER_AGENT = "cantata-composer-calendar/1.0 (https://github.com/nullobsi/cantata)"
SPARQL = "https://query.wikidata.org/sparql"
API = "https://www.wikidata.org/w/api.php"
JULIAN = "http://www.wikidata.org/entity/Q1985786"

QUERY = """
SELECT ?item ?links WHERE {
  ?item wdt:P106 wd:Q36834 ; wikibase:sitelinks ?links .
  FILTER(?links >= %d)
}
"""

# Genres that mark a composer as classical, compared EXACTLY against the
# English labels of the item's P136 (genre) values - a substring match would
# let "rock opera" or "symphonic rock" through.
CLASSICAL_GENRES = {
    "classical music", "western classical music", "art music", "opera",
    "contemporary classical music", "20th-century classical music",
    "21st-century classical music", "chamber music", "art song", "lied",
    "sacred music", "choral music", "church music", "oratorio", "symphony",
    "ballet", "operetta", "zarzuela", "serialism", "twelve-tone technique",
    "minimalism", "minimal music", "neoclassicism", "impressionism in music",
    "romantic music", "baroque music", "renaissance music", "medieval music",
    "early music", "avant-garde music", "musique concrète", "spectral music",
    "electroacoustic music", "concerto", "sonata", "requiem", "mass",
}

# Occupations that, without a classical genre, mark a popular musician.
POPULAR_OCCUPATIONS = (
    "singer", "rapper", "disc jockey", "record producer", "songwriter",
    "guitarist", "drummer", "bassist", "actor", "television presenter",
    "rock musician", "pop musician", "jazz musician",
)

# Occupations of people whose fame lies outside music - poets, rulers,
# scientists - who composed on the side and would crowd out real composers.
OTHER_FAME = (
    "poet", "writer", "novelist", "philosopher", "politician", "monarch",
    "sovereign", "emperor", "king", "queen", "sultan", "ruler", "aristocrat",
    "statesperson", "military", "astronomer", "physicist", "chemist",
    "mathematician", "painter", "playwright", "dramatist", "sociologist",
    "theologian", "physician", "journalist", "diplomat", "lawyer",
)

IMSLP = "P839"


def fetch(url, data=None, cache=None, retries=4):
    if cache and os.path.exists(cache):
        with open(cache, "rb") as handle:
            return json.load(handle)
    request = urllib.request.Request(url, data=data, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(request, timeout=180) as reply:
                payload = reply.read()
            result = json.loads(payload)
            break
        except Exception as error:  # noqa: BLE001 - retried, then re-raised
            if attempt + 1 == retries:
                raise
            print(f"  retry after {error}", file=sys.stderr)
            time.sleep(5 * (attempt + 1))
    if cache:
        with open(cache, "w", encoding="utf-8") as handle:
            json.dump(result, handle)
    return result


def get_entities(ids, props, cache_dir, tag):
    """wbgetentities in batches of 50, cached per batch."""
    entities = {}
    for start in range(0, len(ids), 50):
        batch = ids[start:start + 50]
        params = urllib.parse.urlencode({
            "action": "wbgetentities", "format": "json", "ids": "|".join(batch),
            "props": props, "languages": "en|zh-hans|zh-cn|zh",
        })
        cache = os.path.join(cache_dir, f"{tag}-{start:05d}.json")
        entities.update(fetch(API + "?" + params, cache=cache).get("entities", {}))
    return entities


def label(entity, languages):
    labels = entity.get("labels", {})
    for language in languages:
        if language in labels:
            return labels[language]["value"]
    return ""


def claim_ids(entity, prop):
    result = []
    for claim in entity.get("claims", {}).get(prop, []):
        value = claim.get("mainsnak", {}).get("datavalue", {}).get("value")
        if isinstance(value, dict) and "id" in value:
            result.append(value["id"])
    return result


def julian_to_gregorian(year, month, day):
    # Via the Julian day number of the Julian-calendar date.
    a = (14 - month) // 12
    y = year + 4800 - a
    m = month + 12 * a - 3
    jdn = day + (153 * m + 2) // 5 + 365 * y + y // 4 - 32083
    return datetime.date.fromordinal(jdn - 1721425)


def claim_date(entity, prop):
    """The first day-precision date of prop as ISO, or '' when there is none."""
    claims = entity.get("claims", {}).get(prop, [])
    # Prefer a "preferred" rank statement when there are conflicting dates.
    claims = sorted(claims, key=lambda claim: claim.get("rank") != "preferred")
    for claim in claims:
        value = claim.get("mainsnak", {}).get("datavalue", {}).get("value")
        if not isinstance(value, dict) or value.get("precision") != 11:
            continue
        text = value.get("time", "")
        if not text.startswith("+"):
            continue
        try:
            year, month, day = (int(part) for part in text[1:11].split("-"))
        except ValueError:
            continue
        if year < 1582 or month < 1 or day < 1:
            return ""
        try:
            if value.get("calendarmodel") == JULIAN:
                date = julian_to_gregorian(year, month, day)
            else:
                date = datetime.date(year, month, day)
        except ValueError:
            continue
        return date.isoformat()
    return ""


def fold(name):
    decomposed = unicodedata.normalize("NFD", name)
    return "".join(c for c in decomposed if c.isalpha()).lower()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--apply", action="store_true", help="rewrite playlists/composercalendar.json")
    parser.add_argument("--min-sitelinks", type=int, default=20)
    parser.add_argument("--dump", help="write the entries that would be added to this file, for review")
    parser.add_argument("--cache", default=os.path.join(ROOT, "build-tests", "wikidata-cache"))
    args = parser.parse_args()
    os.makedirs(args.cache, exist_ok=True)

    print(f"Querying Wikidata for composers with >= {args.min_sitelinks} sitelinks...")
    sparql = fetch(SPARQL + "?" + urllib.parse.urlencode({"query": QUERY % args.min_sitelinks, "format": "json"}),
                   cache=os.path.join(args.cache, f"sparql-{args.min_sitelinks}.json"))
    links = {}
    for row in sparql["results"]["bindings"]:
        qid = row["item"]["value"].rsplit("/", 1)[-1]
        links[qid] = max(links.get(qid, 0), int(row["links"]["value"]))
    ids = sorted(links)
    print(f"  {len(ids)} candidates")

    entities = get_entities(ids, "labels|claims", args.cache, f"items-{args.min_sitelinks}")
    related = sorted({qid for entity in entities.values() for prop in ("P136", "P106") for qid in claim_ids(entity, prop)})
    print(f"  resolving {len(related)} genre/occupation labels...")
    related_labels = {qid: label(entity, ["en"]).lower()
                      for qid, entity in get_entities(related, "labels", args.cache, "labels").items()}

    generated = []
    rejected = 0
    for qid in ids:
        entity = entities.get(qid)
        if not entity:
            continue
        name = label(entity, ["en"])
        born = claim_date(entity, "P569")
        died = claim_date(entity, "P570")
        if not name or not born:
            continue
        genres = {related_labels.get(g, "") for g in claim_ids(entity, "P136")}
        occupations = [related_labels.get(o, "") for o in claim_ids(entity, "P106")]
        classical_genre = bool(genres & CLASSICAL_GENRES)
        popular = any(fragment in occupation for occupation in occupations for fragment in POPULAR_OCCUPATIONS)
        other_fame = any(fragment in occupation for occupation in occupations for fragment in OTHER_FAME)
        on_imslp = bool(entity.get("claims", {}).get(IMSLP))
        # Wikidata lists occupations roughly by prominence: a performer or
        # conductor who also composed (Toscanini, Yo-Yo Ma, Glenn Gould) has
        # "composer" further down, and has no ten famous works to list.
        composer_first = bool(occupations) and "composer" in occupations[0]
        # A classical genre or an IMSLP page marks the composer as classical
        # (IMSLP also lists poets as lyricists, hence the other-fame check).
        if not composer_first or popular or other_fame or not (classical_genre or on_imslp):
            rejected += 1
            continue
        entry = {"name": name}
        chinese = label(entity, ["zh-hans", "zh-cn", "zh"])
        if chinese and chinese != name:
            entry["zh"] = chinese
        entry["born"] = born
        if died:
            entry["died"] = died
        entry["wikidata"] = qid
        entry["_links"] = links[qid]
        generated.append(entry)
    print(f"  kept {len(generated)} classical composers, rejected {rejected}")

    with open(CALENDAR, encoding="utf-8") as handle:
        calendar = json.load(handle)
    existing = calendar["composers"]
    # The same composer is often spelled differently ("Rachmaninov" vs
    # "Rachmaninoff", with or without a middle name): treat a generated entry
    # as a duplicate when its full name folds the same as an existing one, or
    # when its surname matches one and so does a birth or death year.
    known = {fold(entry["name"]) for entry in existing}
    known_years = set()
    for entry in existing:
        surname = fold(entry["name"].split()[-1])
        for key in ("born", "died"):
            if entry.get(key):
                known_years.add((surname, entry[key][:4]))

    def duplicate(entry):
        if fold(entry["name"]) in known:
            return True
        surname = fold(entry["name"].split()[-1])
        return any((surname, entry[key][:4]) in known_years for key in ("born", "died") if entry.get(key))

    added = [entry for entry in generated if not duplicate(entry)]
    # Most famous first, then drop the helper field.
    added.sort(key=lambda entry: -entry["_links"])
    for entry in generated:
        entry.pop("_links", None)
    merged = existing + added

    days = set()
    for entry in merged:
        for key in ("born", "died"):
            if entry.get(key):
                days.add(entry[key][5:])
    print(f"Existing {len(existing)} + new {len(added)} = {len(merged)} composers; "
          f"{len(days)} of 366 days have an anniversary")

    if args.dump:
        with open(args.dump, "w", encoding="utf-8") as handle:
            for entry in added:
                handle.write(json.dumps(entry, ensure_ascii=False) + "\n")
        print("Wrote the additions to", args.dump)
    if not args.apply:
        print("Preview only - rerun with --apply to rewrite", os.path.relpath(CALENDAR, ROOT))
        return
    calendar["composers"] = merged
    with open(CALENDAR, "w", encoding="utf-8") as handle:
        handle.write("{\n")
        handle.write(f'  "version": {calendar.get("version", 1)},\n')
        handle.write(f'  "comment": {json.dumps(calendar.get("comment", ""), ensure_ascii=False)},\n')
        handle.write('  "composers": [\n')
        handle.write(",\n".join("    " + json.dumps(entry, ensure_ascii=False) for entry in merged))
        handle.write("\n  ]\n}\n")
    print("Wrote", os.path.relpath(CALENDAR, ROOT))


if __name__ == "__main__":
    main()
