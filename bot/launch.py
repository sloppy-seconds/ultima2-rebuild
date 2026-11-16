#!/usr/bin/env python3
"""Put the bot in orbit, by playing.

This is the launch path: no memory poke, no invented flag. It seeds a character next to a
rocket, walks onto it, boards, launches, and then *checks* it is in orbit using the mode the
engine derives -- which is the point of having built the discriminator first. Before
`u2_state_mode()` existed there was no way to confirm a launch had worked, because the sector,
position, DS:12, DS:13 and DS:14 are byte-identical between the pad and orbit.

Why this unlocks more than it looks like. G43, G49, G77, G83 and G87 are all "be in a vehicle
and observe something", and none of them could be driven because nothing could reach a
vehicle. The recipe is the one w6-orbit-dive proved, with the pad coordinates read from
`disasm/census/*.tsv` rather than copied, because a hardcoded (38,3) silently becomes a lie the
day the map table is regenerated.

The session is left running on purpose. G83 (what a frame of the star field costs) and G87
(the warp's tone divisor) are both breakpoint reads taken *during* a hyperwarp, and tearing
down the only thing that can reach one to save a few MB is a bad trade.

    python3 bot/launch.py                 # launch and stop in orbit
    python3 bot/launch.py --hyperwarp     # ...and fire the hyperwarp, then leave it up
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "bot"))
from target import make                                    # noqa: E402
import census                                              # noqa: E402

PRISTINE = os.path.join(REPO, "references", "ultima2", "player")


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, **kw)


def rocket_pad():
    """A rocket tile as (sector, x, y), preferring the one a capture has already proved.

    Looked up rather than hardcoded: sector 41 has three pads and sector 03 has one, and a
    literal (38,3) silently becomes a lie the day the map table is regenerated. Sector 41 is
    preferred because captures/spice86/w6-orbit-dive launched from it successfully, so it is
    a proven pad rather than a plausible one -- the census lists both and says nothing about
    which one a character can actually reach and board.
    """
    pads = [d for d in census.ints("vehicle", "x", "y", "sector") if d["layer"] == "mapx"
            and d["what"] == "rocket"]
    if not pads:
        raise SystemExit("census found no rocket tile on the mapx layer")
    pad = next((d for d in pads if d["sector"] == 41), pads[0])
    return int(pad["sector"]), int(pad["x"]), int(pad["y"])


def build_save(path, sector, pad_x, pad_y, fuel):
    """maxout, name the character, drop it one row below a rocket pad, and give it fuel.

    The name is not decoration. `references/ultima2/player` is a blank template with an empty
    name field, and the game will not load a nameless character -- it sits at the title screen
    and every key the bot sends is swallowed by a menu, which looks exactly like a bot that
    cannot walk. The first run of this script did exactly that and reported a step that did not
    land, which was the honest answer to the wrong question.

    Standing at (pad_x, pad_y + 1) means one `Up` walks onto the pad, which is what the
    original requires: boarding is standing ON the rocket tile and pressing B.
    """
    shutil.copyfile(PRISTINE, path)
    steps = [["maxout", path],
             ["set", path, "name", "LAUNCHER"],
             ["set", path, "sector", str(sector)],
             ["setpos", path, str(pad_x), str(pad_y + 1)],
             ["set", path, "item", "TRI-LITHIUM", str(fuel)]]
    for s in steps:
        r = run(["python3", "tools/u2_edit.py"] + s)
        if r.returncode != 0:
            raise SystemExit("u2_edit %s failed: %s" % (" ".join(s), r.stderr.strip()[:200]))


def world_triple(name):
    """A hyperjump destination as (xeno, yako, zebo), from the coordinate table in the exe.

    asm/7030's orbit determination is exact: the typed triple must EQUAL a world's table entry
    or the ship lands in deep space. So there is no approximate answer to type, and guessing a
    triple is not a slightly-wrong input but a different outcome.
    """
    for d in census.ints("world", "xeno", "yako", "zebo"):
        if d["name"].upper() == name.upper() and d["xeno"]:
            return int(d["xeno"]), int(d["yako"]), int(d["zebo"])
    raise SystemExit("no hyperjump triple known for %r" % name)


def fuel_for(on_course):
    """TRI-LITHIUM to seed so the first hyperwarp arrives on course, or does not.

    The off-course gate is arithmetic, not chance: asm/6DFD fires `SHIP OFF COURSE!` iff
    `(fuel & 3) == 3` on the post-decrement count. Launch spends one and the hyperwarp spends
    one, so from a seed of S the warp tests (S-2) & 3. Seeding 9 -- which is what
    captures/spice86/w6-orbit-dive used -- gives 7 & 3 == 3, i.e. every hyperwarp from that
    seed goes off course, which is worth knowing before it is mistaken for a bug.

    10 gives 8 & 3 == 0, which is on course. Not a magic number, just the smallest value
    above 9 that clears the gate.
    """
    for s in range(9, 20):
        if (((s - 2) & 3) == 3) != on_course:
            return s
    raise SystemExit("no fuel seed in range produces on_course=%s" % on_course)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hyperwarp", metavar="WORLD",
                    help="after launch, hyperwarp to WORLD (e.g. Mars); omit to stay in orbit")
    ap.add_argument("--off-course", action="store_true",
                    help="seed fuel so the hyperwarp is guaranteed to miss, for the deep-space path")
    ap.add_argument("--name", default="launch")
    args = ap.parse_args()

    sector, px, py = rocket_pad()
    fuel = fuel_for(on_course=not args.off_course)
    print("rocket pad from census: sector %d at (%d,%d); standing at (%d,%d); fuel seed %d"
          % (sector, px, py, px, py + 1, fuel))

    save = "/var/tmp/u2/bot-launch-player"
    build_save(save, sector, px, py, fuel)

    print("starting the original under Spice86 ...")
    start = run(["bash", "tools/spice86_start.sh", "--headless", "--patch-arrows",
                 "--player", save, "--auto-play", "--name", args.name])
    if start.returncode != 0:
        raise SystemExit("spice86_start failed: " + (start.stderr or start.stdout)[-400:])
    for line in start.stdout.splitlines():
        if "MCP endpoint" in line:
            print("  " + line.strip())

    t = make("original")
    o = t.state()
    print("in world as %r, sector %s, mode %s" % (o["char"]["name"], t.sector(o), o["mode"]))
    # Say this loudly and stop. A nameless record leaves the game at the title screen, where
    # every key is eaten by a menu, and a bot that walks on from there reports "the step did
    # not land" -- a true statement about the wrong thing, which is the kind of finding that
    # costs an afternoon.
    if o["mode"] == "title":
        print("FAIL the game is still at the title screen: the character did not load.")
        print("     name is %r -- a blank record will not load." % o["char"]["name"])
        t.close()
        return 1

    # Walk onto the pad. One key at a time, confirming from memory each time, because a key
    # that was dropped looks exactly like a key the game refused.
    before = t.pos(o)
    o = t.key("Up")
    after = t.pos(o)
    print("Up: %s -> %s  (mode %s)" % (before, after, o["mode"]))
    if after == before:
        print("  the step did not land; aborting rather than boarding blind")
        t.close()
        return 1

    o = t.key("B")
    print("B (board): mode %s" % o["mode"])
    o = t.key("L")
    print("L (launch): mode %s" % o["mode"])

    ok = o["mode"] == "space"
    print("\n%s  launch %s: mode is %r" % ("ok  " if ok else "FAIL", "confirmed" if ok else "NOT confirmed", o["mode"]))
    if not ok:
        print("     the discriminator is the only way to know this, and it says no.")
        print("     screen tail: %s" % [r for r in o["screen"] if r.strip()][-3:])

    if ok and args.hyperwarp:
        triple = world_triple(args.hyperwarp)
        print("\nhyperwarp to %s: triple %s (must match the table exactly or the ship misses)"
              % (args.hyperwarp, triple))
        o = t.key("H")
        for d in triple:
            o = t.key(str(d))
        # The space screen is glyphs, so the text rows come back empty and the bot is blind
        # here in a way it is not on the ground. The frames are the record; see G76.
        print("  after the triple, mode %s" % o["mode"])

    print("\nsession left up on the run dir above -- G83 and G87 are breakpoint reads taken")
    print("during a hyperwarp, so tearing this down to save memory costs the only thing that")
    print("can reach them. tools/spice86_stop.sh when done.")
    t.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
