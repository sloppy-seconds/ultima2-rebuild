#!/usr/bin/env python3
"""Read the census tables as data.

`disasm/census/*.tsv` is a committed, generated artefact: `tools/census.py` is its only writer and
`census.py --check-fresh` regenerates it to a temp dir and diffs, so a stale table goes red in CI.
That makes it the right thing for a bot to read directly.

The alternative — which `bot/launch.py` used until 2026-11-16 — was shelling out to
`tools/census.py --what vehicle` and parsing stdout. Three things were wrong with that: it paid a
Python interpreter start and a full re-derivation per query; it coupled the bot to a CLI's
human-readable output format, which is not a contract anything guards; and it meant every bot run
silently re-ran the generator, so a bot could pass against a table the committed tree did not
have. Reading the file reads the thing that was actually reviewed and gated.

Row counts are checked against the `# {"rows": N}` line the generator writes. That is free, and a
truncated file is otherwise indistinguishable from a small table.
"""
import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CENSUS_DIR = os.path.join(REPO, "disasm", "census")

TABLES = ("world", "sector", "place", "npc", "tile", "creature", "gate", "vehicle", "site",
          "vendor")

_cache = {}


class CensusError(RuntimeError):
    pass


def _parse(path, name):
    rows, header, declared = [], None, None
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("#"):
                if line.startswith('# {"rows"'):
                    declared = json.loads(line[2:])["rows"]
                continue
            parts = line.split("\t")
            if header is None:
                header = parts
                continue
            rows.append(dict(zip(header, parts)))
    if header is None:
        raise CensusError("%s: no header row" % name)
    if declared is not None and declared != len(rows):
        raise CensusError("%s: header declares %d rows, file holds %d — truncated or half-written"
                          % (name, declared, len(rows)))
    return rows, header


def table(name):
    """The named census table as a list of dicts. Cached; returns the same list each call."""
    if name not in TABLES:
        raise CensusError("no census table %r (have: %s)" % (name, ", ".join(TABLES)))
    if name not in _cache:
        path = os.path.join(CENSUS_DIR, name + ".tsv")
        if not os.path.exists(path):
            raise CensusError("%s missing — run `python3 tools/census.py`" % path)
        _cache[name] = _parse(path, name)[0]
    return _cache[name]


def rows(name, **where):
    """Rows of `name` whose fields match every keyword. Values compare as strings."""
    out = table(name)
    for k, v in where.items():
        out = [r for r in out if r.get(k) == str(v)]
    return out


def one(name, **where):
    """Exactly one row, or raise. Ambiguity is refused rather than resolved by taking the first,
    because 'pick the first match' is how a bot ends up at the wrong rocket pad believing it
    picked the right one — the census lists several pads per sector and says nothing about which
    is convenient."""
    r = rows(name, **where)
    if not r:
        raise CensusError("no %s row matching %s" % (name, where))
    if len(r) > 1:
        raise CensusError("%d %s rows match %s — narrow the query, do not take the first"
                          % (len(r), name, where))
    return r[0]


def ints(name, *fields):
    """The same rows with the named columns converted to int, so callers stop re-parsing hex."""
    out = []
    for r in table(name):
        r = dict(r)
        for f in fields:
            v = r.get(f, "")
            try:
                r[f] = int(v, 16) if str(v).startswith("0x") else int(v)
            except ValueError:
                pass                      # a non-numeric cell stays a string, unconverted
        out.append(r)
    return out


if __name__ == "__main__":
    fails = []

    def ck(c, m):
        if not c:
            fails.append(m)

    for t in TABLES:
        r = table(t)
        ck(len(r) > 0, "%s is non-empty" % t)
        ck(all(all(k in row for k in r[0]) for row in r), "%s rows are rectangular" % t)

    # The grades are the point of the census, so a loader that dropped them would be useless.
    ck(all("grade" in r for r in table("gate")), "gate rows carry a grade")
    ck(any(r["grade"] == "measured" for r in table("gate")), "gate has measured rows")

    # Every rocket pad the census lists must be a real coordinate, and there must be more than
    # one: a loader that silently filtered to one sector would still return "a pad".
    pads = rows("vehicle", what="rocket")
    ck(len(pads) > 1, "more than one rocket pad is listed (got %d)" % len(pads))
    ck(all(int(p["x"]) >= 0 and int(p["y"]) >= 0 for p in pads), "pad coordinates parse")

    ck(one("gate", era_index="0", gate_index="0")["dest_era_index"] == "1",
       "the Legends->Pangea gate reads back as the measured row")

    try:
        one("vehicle", what="rocket")
        ck(False, "an ambiguous query must raise, not pick")
    except CensusError:
        ck(True, "ambiguous one() raises")
    try:
        table("nope")
        ck(False, "unknown table must raise")
    except CensusError:
        ck(True, "unknown table raises")

    print("census loader selftest: %d failures" % len(fails))
    for f in fails:
        print("  FAIL " + f)
    raise SystemExit(1 if fails else 0)
