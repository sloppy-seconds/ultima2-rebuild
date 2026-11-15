#!/usr/bin/env python3
"""G43: does the plane's forest rule apply while airborne, or only on the ground?

The byte answer has been on disk for months and says "airborne flight has no tile rule":
asm/2604 is the routine that refuses forest, and asm/38B0's flight loop consults no
passability routine at all. But a loop that does not call a check is not proof the check is
unreachable -- asm/38B0 has only ever been filmed launching and landing, never crossing a
woodland tile, so the honest grade on "airborne ignores terrain" has stayed *inferred*.

This drives it. The same character, the same four tiles, twice:

  grounded   BOARD the plane, then walk it into the forest tile          -> asm/2604 should refuse
  airborne   LAUNCH, then fly it into a forest tile                     -> asm/38B0, if the bytes are right, accepts

and the answer is read out of DS, not off the screen: position is DS:00/01 and the bot
confirms each step by whether the pair moved. That matters here more than usual, because
"the game refused the move" and "the key never arrived" are the same observation from the
screen and different ones from memory.

Sector and pad come from the census and the map bytes rather than from this file, so a map
edit moves the experiment instead of breaking it. The line is chosen as the shortest
straight run from a plane pad to a forest tile, and the tiles stepped over are printed, so
the result can be re-checked against the map by hand.

Usage:  python3 bot/plane_forest.py [--sector 41]
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from launch import build_save, run                      # noqa: E402
from target import make                                # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
FOREST = 0x0C          # map byte for forest, tile table: proven via tile-art-spec §7
PLANE = 0x4C           # map byte for a plane pad
DIRS = {"Right": (1, 0), "Left": (-1, 0), "Down": (0, 1), "Up": (0, -1)}


def grid(sector):
    for name in (f"mapx{sector:02d}", f"MAPG{sector:02d}"):
        p = ROOT / "original" / name
        if p.exists():
            return p.read_bytes(), name
    raise SystemExit(f"no sector map for {sector}")


def plane_pads(b):
    return [(i % 64, i // 64) for i, v in enumerate(b) if v == PLANE]


def nearest_forest(b, pos):
    """(x, y) of the forest tile closest to pos, by Chebyshev distance.

    Distance only, not a path: the walker below lets the game decide what is walkable, which
    is the whole point. A first version of this script planned a straight line across the map
    and reported REFUSED five times, which was the map being right and the plan being wrong --
    the pad sits inside a ring of 0x78 and the forest was outside it.
    """
    x0, y0 = pos
    best, bd = None, 999
    for i, v in enumerate(b):
        if v != FOREST:
            continue
        x, y = i % 64, i // 64
        d = max(abs(x - x0), abs(y - y0))
        if d < bd:
            best, bd = (x, y), d
    return best, bd


def order_toward(pos, target):
    """The four keys, best-first, for closing on target."""
    x, y = pos
    tx, ty = target
    scored = []
    for key, (dx, dy) in DIRS.items():
        nx, ny = x + dx, y + dy
        scored.append((max(abs(nx - tx), abs(ny - ty)), key))
    scored.sort()
    return [k for _, k in scored]


def navigate(t, o, target, b, budget=48):
    """Walk to target using the game's own refusal as the passability oracle.

    Each candidate key is confirmed by whether DS:00/01 moved, never by the send returning ok
    -- a dropped key and a refused move are the same thing on screen and different in memory.
    A step that moves is kept; a step that does not is recorded as a refusal by the game and
    the next-best direction is tried from where we actually are.
    """
    tried = set()
    steps = 0
    while steps < budget:
        pos = t.pos(o)
        if pos == target:
            return o, steps, True
        for key in order_toward(pos, target):
            if (pos, key) in tried:
                continue
            tried.add((pos, key))
            o = t.key(key)
            steps += 1
            if t.pos(o) != pos:
                break                      # moved; re-plan from the new cell
        else:
            return o, steps, False         # boxed in
    return o, steps, t.pos(o) == target


def step_onto(t, o, key, b):
    """One press aimed at a named tile, returning whether the game allowed it."""
    dx, dy = DIRS[key]
    x, y = t.pos(o)
    tx, ty = x + dx, y + dy
    tile = b[ty * 64 + tx] if 0 <= tx < 64 and 0 <= ty < 64 else None
    o = t.key(key)
    moved = t.pos(o) != (x, y)
    print("  %-6s (%2d,%2d) -> (%2d,%2d) tile %s : %s  now (%2d,%2d)"
          % (key, x, y, tx, ty,
             "FOREST" if tile == FOREST else ("0x%02X" % tile if tile is not None else "edge"),
             "MOVED " if moved else "REFUSED", *t.pos(o)))
    return o, moved, tile


def press_forest(t, o, b, tries=6):
    """From wherever we are, try to step onto an adjacent forest tile."""
    x, y = t.pos(o)
    for key, (dx, dy) in DIRS.items():
        tx, ty = x + dx, y + dy
        if 0 <= tx < 64 and 0 <= ty < 64 and b[ty * 64 + tx] == FOREST:
            return step_onto(t, o, key, b)
    return o, None, None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sector", type=int, default=41)
    ap.add_argument("--name", default="planefor")
    args = ap.parse_args()

    b, mapname = grid(args.sector)
    pads = plane_pads(b)
    if not pads:
        raise SystemExit(f"{mapname} has no plane tile")
    # Prefer a pad with the shortest open route to woodland; the route is discovered live, so
    # all this does is avoid picking one that is walled in on the far side of the map.
    px, py = min(pads, key=lambda p: nearest_forest(b, p)[1])
    print("%s: %d plane pads; starting from (%d,%d), nearest forest %d tiles away"
          % (mapname, len(pads), px, py, nearest_forest(b, (px, py))[1]))

    save = "/var/tmp/u2/bot-plane-player"
    build_save(save, args.sector, px, py, 10)
    if (px, py + 1) == (px, py):
        raise SystemExit("degenerate pad position")

    print("starting the original ...")
    r = run(["bash", "tools/spice86_start.sh", "--headless", "--patch-arrows",
             "--player", save, "--auto-play", "--name", args.name])
    if r.returncode != 0:
        raise SystemExit("spice86_start failed: " + (r.stderr or r.stdout)[-400:])

    t = make("original")
    o = t.state()
    print("as %r sector %s mode %s at %s" % (o["char"]["name"], t.sector(o), o["mode"],
                                             t.pos(o)))
    if o["mode"] == "title":
        print("FAIL still at the title screen -- the character did not load.")
        t.close()
        return 1

    o = t.key("Up")
    print("Up onto pad -> %s" % (t.pos(o),))
    o = t.key("B")
    print("B (board) -> mode %s at %s" % (o["mode"], t.pos(o)))

    target, _ = nearest_forest(b, t.pos(o))
    print("\nGROUNDED -- boarding the plane does not change the mode, so this is asm/2604's")
    print("passability with the plane as the travel mode. Navigating to %s." % (target,))
    o, steps, arrived = navigate(t, o, target, b)
    print("  navigate: %d presses, arrived=%s, now at %s" % (steps, arrived, t.pos(o)))
    o, g_moved, g_tile = press_forest(t, o, b)
    if g_moved is None:
        print("  no forest tile adjacent from here; the route stopped short")

    o = t.key("L")
    print("\nL (launch) -> mode %s at %s" % (o["mode"], t.pos(o)))
    airborne = o["mode"] == "space"

    a_moved = None
    if airborne:
        print("\nAIRBORNE -- asm/38B0's flight loop; the byte claim is that it consults no")
        print("passability routine at all.")
        target, _ = nearest_forest(b, t.pos(o))
        o, steps, arrived = navigate(t, o, target, b, budget=64)
        print("  navigate: %d presses, arrived=%s, now at %s" % (steps, arrived, t.pos(o)))
        o, a_moved, _ = press_forest(t, o, b)
    else:
        print("  launch did not take; the airborne half cannot be measured from this run.")

    print("\n---- G43 verdict ----")
    print("grounded onto forest : %s" % ({True: "ACCEPTED", False: "REFUSED",
                                          None: "not reached"}[g_moved]))
    print("airborne onto forest : %s" % ({True: "ACCEPTED", False: "REFUSED",
                                          None: "not reached (no launch)"}[a_moved]))
    print("\nThe byte claim predicts REFUSED then ACCEPTED: asm/2604 refuses forest and")
    print("asm/38B0's flight loop calls no passability routine. Anything else is a finding.")
    t.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
