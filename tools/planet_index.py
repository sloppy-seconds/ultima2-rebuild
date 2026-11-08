#!/usr/bin/env python3
"""planet_index.py — the hyperjump coordinate table, read out of the executable.

WHAT THIS IS. Ultima II's rocket asks for three numbers (the game calls them XENO, YAKO and
ZABO) and hyperjumps to whatever world sits at that triple. Those coordinates are not folklore:
they are a 30-byte table in the executable, and this tool reads it. Combined with the fact that
orbit is tested by **equality** with the triple and not by any tolerance, the table is the whole
inter-planet routing problem, solved from bytes.

THE LAYOUT, and the trap in reading it. The table is **column-major**: ten X values, then ten Y
values, then ten Z values, indexed by the world byte (0 = Earth ... 9 = Planet X). Reading it as
ten consecutive triples produces plausible-looking nonsense -- `(6,5,3) (6,1,2) ...` -- which is
exactly what a first attempt here did, and nothing about it looks wrong except that no planet
matches anything. The recorded signature that disambiguates it is `X[0]==Y[0]==Z[0]==6` (Earth)
and `X[9]==Y[9]==Z[9]==9` (Planet X), those two triples having been read off live screens in
`captures/spice86/g39-rocket-flight`. Scanning the whole binary for that signature returns
**exactly one offset**, so the table is located by its property rather than by a hard-coded
address that would silently drift.

THE WIKI ROWS ARE A CONTROL, NOT A SOURCE. `CODEX_COORDS` below is pinned from the Ultima Codex
planet-coordinates page and is used only to check our decode: nine of nine visible rows agree,
and the page's own digit-instance census agrees over the whole table once the Sun is added. That
census is effectively a checksum -- if one byte of the table were misread, the counts would move.
The game's own `tlkx11` line `SHE'S GOT TO BE AT 9-9-9!` is a third, in-binary confirmation for
Planet X. If the wiki ever changes, this goes red and a human decides which side is wrong.

Usage:
    python3 tools/planet_index.py                  # human summary
    python3 tools/planet_index.py --tsv disasm/planet-index.tsv
    python3 tools/planet_index.py --verify         # the control
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_index

TABLE_LEN = 30
# Pinned from https://wiki.ultimacodex.com/wiki/Planet_coordinates , fetched 2026-11-08.
# The Sun is deliberately absent: (4,4,4) is manual-derived wiki data and is NOT a game byte.
CODEX_COORDS = {
    "MERCURY": (5, 4, 5), "VENUS": (3, 3, 4), "EARTH": (6, 6, 6), "MARS": (6, 2, 3),
    "JUPITER": (1, 3, 4), "SATURN": (2, 8, 5), "URANUS": (9, 4, 6), "NEPTUNE": (4, 0, 5),
    "PLANET X": (9, 9, 9),
}
# The same page's census of every digit across its full table including the Sun. Matching it
# over our bytes plus (4,4,4) is a whole-table check rather than a row-by-row one.
CODEX_DIGITS = {0: 2, 1: 2, 2: 2, 3: 4, 4: 9, 5: 4, 6: 5, 7: 0, 8: 1, 9: 4}
SUN = (4, 4, 4)


def find_table(exe_path):
    """Locate the coordinate table by its signature. Returns (offset, rows) or (None, None).

    Every 30-byte window whose values are all 0..9 and whose first and last column entries are
    the two live-observed triples is a candidate; exactly one exists in this binary, and the
    caller asserts that count rather than trusting it.
    """
    data = open(exe_path, "rb").read()
    hits = []
    for off in range(0, len(data) - TABLE_LEN):
        t = data[off:off + TABLE_LEN]
        if any(v > 9 for v in t):
            continue
        if (t[0], t[10], t[20]) == (6, 6, 6) and (t[9], t[19], t[29]) == (9, 9, 9):
            hits.append(off)
    if len(hits) != 1:
        return None, None
    off = hits[0]
    t = data[off:off + TABLE_LEN]
    xs, ys, zs = list(t[0:10]), list(t[10:20]), list(t[20:30])
    return off, [(xs[i], ys[i], zs[i]) for i in range(10)]


def build(exe_path, content_dir):
    worlds, wsrc = place_index.world_names(exe_path)
    off, rows = find_table(exe_path)
    wiki = place_index.load_wiki(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                              "..", "disasm", "place-wiki-names.tsv"))
    # Sites per world, from the place index, so the coordinate row and the settlement row meet.
    # Keyed by (world, sector, kind) because for worlds 5-9 the root mapx and the patched MAPG
    # are the same bytes, and listing a sector twice reads as two places rather than one.
    sites = collections.defaultdict(dict)
    for r in place_index.build(content_dir, worlds, wiki):
        if r["kind"] == "overworld":
            continue
        sites[r["world"]][(r["sector"], r["kind"])] = r
    # world_names() returns the nine names the executable actually stores; world byte 9 is
    # printed as `X` from a separate routine, so it is appended here rather than invented
    # inside the shared table.
    names = list(worlds) + [place_index.WORLD_X]
    out = []
    for i, name in enumerate(names):
        coord = rows[i] if rows else (None, None, None)
        ss = list(sites.get(name, {}).values())
        settlements = [s for s in ss if s["kind"] in ("village", "town", "castle")]
        dungeons = [s for s in ss if s["kind"] in ("dungeon", "tower")]
        def label(s):
            # The game's own tile text wins; a crosswalk name is the fallback and is marked
            # as external so a reader is never told a wiki label was decoded from bytes.
            if s["candidate"]:
                return s["candidate"]
            if s["wiki"]:
                return f"{s['wiki'].split(' [')[0]} (wiki)"
            return "(unnamed)"
        # Earth is the exception the handoff recorded: a rocket arriving on Earth is hardcoded
        # to era 4, so it lands in sector 40 and not in 00.
        landing = "40" if i == 0 else f"{i}0"
        out.append({
            "world": i, "name": name,
            "xeno": coord[0], "yako": coord[1], "zebo": coord[2],
            "sector": landing,
            "settlements": "; ".join(f"{s['sector']} {label(s)}" for s in settlements),
            "dungeons_towers": "; ".join(f"{s['sector']} {s['kind']}" for s in dungeons),
            "codex": str(CODEX_COORDS.get(name)) if name in CODEX_COORDS else "not visible",
            "agree": ("yes" if CODEX_COORDS.get(name) == coord else
                      ("n/a" if name not in CODEX_COORDS else "NO")),
        })
    return off, out, wsrc


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default="original/ultimaii.exe")
    ap.add_argument("--content-dir", default="original")
    ap.add_argument("--tsv")
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.exe):
        print(f"no executable at {args.exe}", file=sys.stderr)
        return 2

    off, rows, wsrc = build(args.exe, args.content_dir)
    if off is None:
        print("the coordinate table was not located by its signature", file=sys.stderr)
        return 1

    if args.tsv:
        cols = ["world", "name", "xeno", "yako", "zebo", "sector", "settlements",
                "dungeons_towers", "codex", "agree"]
        with open(args.tsv, "w") as f:
            f.write("# generated by tools/planet_index.py -- do not hand-edit\n")
            f.write(f"# coordinate table at file offset {off:#07x}\n")
            f.write("\t".join(cols) + "\n")
            for r in rows:
                f.write("\t".join(str(r[c]).replace("\t", " ") for c in cols) + "\n")
        print(f"wrote {args.tsv}: {len(rows)} worlds, table at {off:#07x}")

    if args.verify:
        bad = []
        by = {r["name"]: (r["xeno"], r["yako"], r["zebo"]) for r in rows}
        # The two live-observed anchors. These came from a capture, not from the wiki, so they
        # are the check that would fail if the table moved or the layout reading regressed.
        if by.get("EARTH") != (6, 6, 6):
            bad.append(f"EARTH is {by.get('EARTH')}, want (6,6,6) as observed live")
        if by.get("PLANET X") != (9, 9, 9):
            bad.append(f"PLANET X is {by.get('PLANET X')}, want (9,9,9) as observed live and "
                       f"stated in tlkx11's own hint")
        for name, want in CODEX_COORDS.items():
            if by.get(name) != want:
                bad.append(f"{name}: our bytes say {by.get(name)}, the Codex says {want}")
        census = collections.Counter(
            [v for r in rows for v in (r["xeno"], r["yako"], r["zebo"])] + list(SUN))
        if {k: census.get(k, 0) for k in range(10)} != CODEX_DIGITS:
            bad.append(f"digit census {dict(census)} does not match the Codex census "
                       f"{CODEX_DIGITS}; one misread byte moves this")
        if census.get(7, 0):
            bad.append(f"7 appears in the table ({census[7]}x); it never does in this data")
        if not any(r["settlements"] for r in rows):
            bad.append("no world has a settlement row; the place-index join is broken")
        for b in bad:
            print("VERIFY FAIL:", b)
        if bad:
            return 1
        print(f"VERIFY OK: table unique at {off:#07x}; 9/9 Codex rows agree; census matches; "
              f"no 7; {sum(1 for r in rows if r['settlements'])} worlds have settlements")
        return 0

    print(f"coordinate table at file {off:#07x} (column-major: X[0..9], Y[0..9], Z[0..9])")
    print(f"world names: {wsrc}\n")
    for r in rows:
        print(f"  {r['world']} {r['name']:9s} hyperjump {r['xeno']},{r['yako']},{r['zebo']}"
              f"   -> sector {r['sector']}")
        if r["settlements"]:
            print(f"      settlements: {r['settlements']}")
        if r["dungeons_towers"]:
            print(f"      dungeons   : {r['dungeons_towers']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
