#!/usr/bin/env python3
"""gate_graph.py — the time-door destination graph, derived from bytes and checked against the Codex.

WHAT WAS OPEN. `docs/moongates.md` section 3 decoded the *cells*: five eras, four gates each, from
the table at file `0x2998`, with twelve of twenty confirmed live. What it could not say was which
era a given gate leads to. Three transits were watched and the rest was blank, so a bot could stand
on a door and not know where it would land.

THE RULE, read out of the code. `asm/26B7` + `asm/26C3` + `asm/26C6` are one contiguous routine
(26B7..26CB) that the Phase 2 map cut into three fragments; read together they say:

    k = DS:6E >> 1                 ; the active gate index for this era, 0..3
    target = k if k < DS:49 else k + 1
    DS:49 = target                 ; and the player is placed at index k of the TARGET era

The destination era is a function of the gate index and nothing else, which is why the observed
pairs looked reciprocal: gate k is the two-way link between era k and era k+1.

WHY THIS FILE EXISTS RATHER THAN A PARAGRAPH. The rule is three instructions, and three
instructions is exactly the size of thing that gets misread confidently. So the corroboration is
mechanical: the derived graph is checked against the two transits actually watched live, against
the five general rules the Ultima Codex states, and against its four named two-way connections.
The Codex is a **control here, not a source** -- the graph is computed from `original/ultimaii.exe`
and the wiki rows are only asserted to agree. Notably the rule reproduces both of the wiki's
stated *exceptions* (Legends->Pangea, and the Aftermath gate into 1990 A.D.) without either being
coded in, which is the part that would be easy to get wrong and hard to notice.

Usage:
    python3 tools/gate_graph.py            # print the graph
    python3 tools/gate_graph.py --verify   # the control
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

CELLS_OFF = 0x2998
ERAS = ["Legends", "Pangea", "1423BC", "1990AD", "Aftermath"]

# The Codex's five general rules, pinned from
# https://wiki.ultimacodex.com/wiki/List_of_time_gate_locations (fetched 2026-11-08).
# Each is (description, predicate over the derived graph).
CODEX_RULES = [
    ("each era reaches the other four exactly once",
     lambda g: all(sorted(g[e]) == [x for x in range(5) if x != e] for e in range(5))),
    ("travel to Legends always arrives at gate 0",
     lambda g: all(g[e][0] == 0 for e in range(1, 5))),
    ("travel to Pangea always arrives at gate 1, except from Legends",
     lambda g: all(g[e][1] == 1 for e in range(2, 5)) and g[0][0] == 1),
    ("travel to the Aftermath always arrives at gate 3",
     lambda g: all(g[e][3] == 4 for e in range(4))),
    ("gates into 1990 all arrive at gate 2 except the Aftermath one, which arrives at gate 3",
     lambda g: all(g[e][2] == 3 for e in range(3)) and g[4][3] == 3),
]
# The Codex's four named two-way connections, as era pairs.
CODEX_TWOWAY = [(0, 1), (1, 2), (2, 3), (3, 4)]
# The two transits watched live in captures/spice86/moongate-cycle (docs/moongates.md section 3).
OBSERVED = [((2, 2), 3), ((2, 1), 1)]


def cells(exe_path):
    d = open(exe_path, "rb").read()
    t = d[CELLS_OFF:CELLS_OFF + 40]
    if len(t) != 40 or any(v > 63 for v in t):
        return None
    return [[(t[8 * e + 2 * k], t[8 * e + 2 * k + 1]) for k in range(4)] for e in range(5)]


def graph():
    """The destination graph, straight from asm/26B7+26C3+26C6. era -> [era per gate index]."""
    return [[k if k < e else k + 1 for k in range(4)] for e in range(5)]


def twoway(g):
    """Era pairs a single gate links in both directions.

    Not "reachable both ways" -- the graph is complete, so every pair is reachable both ways and
    that test returns all ten pairs and means nothing. Two-way, as observed live (era2 gate2 and
    era3 gate2 are each other's), means the **same index** round-trips: gate k of era a lands in
    era b, and gate k of era b lands back in era a.
    """
    return sorted({(a, g[a][k]) for a in range(5) for k in range(4)
                   if a < g[a][k] and g[g[a][k]][k] == a})


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default="original/ultimaii.exe")
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()
    g = graph()
    cc = cells(args.exe) if os.path.exists(args.exe) else None

    if not args.verify:
        print("destination era, by gate index (the rotation order DS:6E = 0,2,4,6):")
        for e in range(5):
            row = "  ".join(f"gate{k}->{ERAS[d][:6]:6s}" for k, d in enumerate(g[e]))
            cells_s = ""
            if cc:
                cells_s = "   cells " + " ".join(f"{xy}" for xy in cc[e])
            print(f"  {ERAS[e]:10s} {row}{cells_s}")
        return 0

    bad = []
    if cc is None:
        bad.append("cell table at 0x2998 not readable; cannot check the cells line up")
    # The live transits. These are the only two that were ever watched, so they are the anchor.
    for (era, k), want in OBSERVED:
        if g[era][k] != want:
            bad.append(f"live transit era{era} gate{k} -> {g[era][k]}, observed {want}")
    for desc, pred in CODEX_RULES:
        if not pred(g):
            bad.append(f"Codex rule failed: {desc}")
    if twoway(g) != sorted(CODEX_TWOWAY):
        bad.append(f"two-way pairs {twoway(g)}, Codex names {sorted(CODEX_TWOWAY)}")
    # The reciprocity the rule implies: gate k links era k and era k+1, and nothing else does.
    for k in range(4):
        if not (g[k][k] == k + 1 and g[k + 1][k] == k):
            bad.append(f"gate {k} is not the two-way link between eras {k} and {k+1}")
    for b in bad:
        print("VERIFY FAIL:", b)
    if bad:
        return 1
    print("VERIFY OK: rule k<era?k:k+1 reproduces 2 live transits, all 5 Codex general rules "
          "(both exceptions included) and all 4 Codex two-way pairs")
    if cc:
        print(f"           cell table read at {CELLS_OFF:#07x}: 5 eras x 4 gates")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
