#!/usr/bin/env python3
"""
passability_ladder.py — CP5 gate for the on-foot passability ladder.

Why this is a gate and not a unit test: the thing under test is a verdict the engine reaches by
walking, and the failure mode it guards against is a *shape* error — someone replacing the ladder
with a per-tile table again, which is what caused every passability regression this project has had
(docs/passability-findings.md F14). A unit test on the C function would pass for a table that
happened to agree on the rows someone thought of. This drives the real engine and checks whether the
player actually moved.

Method, one fresh seed per tile: put the candidate byte in the cell directly east of the entry,
press RIGHT once, and read the position back out of the turn dump. Moved means accepted. A fresh
save dir every time is not tidiness — the engine rewrites the save in place, and a reused dir was
already responsible for one wrong count in this session.

The expected column is the ladder's verdict, cross-checked against the seven live measurements in
F14's agreement table. Water and mountain are the two named rejections; the eight named
acceptances include the four vehicle tiles, which the old table blocked and which is the whole
reason this was rewritten.
"""
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_gate as G

# (map byte, expected accept?, what it is)
CASES = [
    (0x00, False, "open water — 22 live rejects (F11)"),
    (0x04, True,  "shallows — re-proven live, sector 10"),
    (0x08, True,  "grass — walked, F11"),
    (0x0C, True,  "forest — five steps, F11"),
    (0x10, False, "mountain — blocked at (11,20), F11"),
    (0x28, True,  "art 0x0A monument — below the 0x16 threshold"),
    (0x2C, False, "art 0x0B doorway — first byte at/above the threshold"),
    (0x44, True,  "horse tile — named accept, was blocked by the old table"),
    (0x48, True,  "ship tile — named accept, was blocked by the old table"),
    (0x4C, True,  "PLANE tile — named accept, was blocked by the old table"),
    (0x50, True,  "rocket tile — named accept, was blocked by the old table"),
    (0x5C, True,  "force field — g29-field, six crossings"),
    (0x70, True,  "town paving — owner, standing in town"),
    (0x78, False, "building wall art 0x1E — owner-reported blocker"),
    (0x80, False, "letter A, first of the A-Z signs"),
    (0xA0, False, "closed door — asm/433A accepts only 0xA0 as a door"),
]

ENGINE = os.environ.get("U2_BIN", "./u2")
SEED = "tests/cp5_fight_seed.py"


def run_case(work, tile):
    if os.path.isdir(work):
        shutil.rmtree(work)
    subprocess.run([sys.executable, SEED, work], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # The seeded monx00 spawns its one creature at (3,0) and the spawn-cell clear legitimately
    # writes 0x70 over it, which would silently replace the tile under test. Move the creature.
    mp = os.path.join(work, "content", "mapx00")
    d = bytearray(open(mp, "rb").read())
    d[0 * 64 + 3] = tile
    open(mp, "wb").write(bytes(d))
    fp = os.path.join(work, "content", "monx00")
    m = bytearray(open(fp, "rb").read())
    m[0] = 30
    m[32] = 30
    open(fp, "wb").write(bytes(m))

    keys = os.path.join(work, "k")
    open(keys, "w").write("0xf3\n")          # U2_KEY_RIGHT, from the entry at (2,0)
    dbg = os.path.join(work, "dbg")
    subprocess.run([ENGINE, "--headless", "--play-map", "--cleanroom",
                    "--data-dir", os.path.join(work, "content"),
                    "--save-dir", os.path.join(work, "save"),
                    "--script", keys, "--debug", "cli", "--debug-path", dbg,
                    "--debug-limit", "0"],
                   check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    dumps = sorted(subprocess.run(["find", dbg, "-name", "turn-*.json"],
                                   capture_output=True, text=True).stdout.split())
    if not dumps:
        return None
    with open(dumps[-1]) as f:
        return json.load(f)["state"]["raw"]["pos"].strip()


def run_creature_case(work, stamp):
    """The same verdict, but the byte arrives as a creature's stamp, not as the map.

    The walk gate used to consult the creature table directly. That agreed with the
    original by coincidence — almost no creature's type byte is a walkable tile — and
    broke on the one that is: 0x48 is both the Frigate's type byte and the moored-ship
    tile, so a ship you can walk aboard at Port Boniface became a wall the moment it
    was a creature in the open sea. This is the discriminator the old comment asked for.
    """
    if os.path.isdir(work):
        shutil.rmtree(work)
    subprocess.run([sys.executable, SEED, work], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Ground under test is grass, so the only thing that can refuse the step is the stamp.
    mp = os.path.join(work, "content", "mapx00")
    d = bytearray(open(mp, "rb").read())
    d[0 * 64 + 3] = 0x08
    open(mp, "wb").write(bytes(d))

    # Slot 0 sits on the cell, dormant (role 0) so it cannot walk away before the key.
    fp = os.path.join(work, "content", "monx00")
    m = bytearray(open(fp, "rb").read())
    m[0], m[32], m[64], m[96], m[128], m[160] = 3, 0, 40, stamp, 0x70, 0
    open(fp, "wb").write(bytes(m))

    keys = os.path.join(work, "k")
    open(keys, "w").write("0xf3\n")
    dbg = os.path.join(work, "dbg")
    subprocess.run([ENGINE, "--headless", "--play-map", "--cleanroom",
                    "--data-dir", os.path.join(work, "content"),
                    "--save-dir", os.path.join(work, "save"),
                    "--script", keys, "--debug", "cli", "--debug-path", dbg,
                    "--debug-limit", "0"],
                   check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dumps = sorted(subprocess.run(["find", dbg, "-name", "turn-*.json"],
                                  capture_output=True, text=True).stdout.split())
    if not dumps:
        return None
    with open(dumps[-1]) as f:
        return json.load(f)["state"]["raw"]["pos"].strip()


# (creature type byte, expected accept?, what it is)
CREATURE_CASES = [
    (0x48, True,  "Frigate — 0x48>>1 = 0x24, the ship tile the ladder names"),
    (0x30, False, "Orc — 0x30>>1 = 0x18, at the threshold"),
    (0xFC, False, "Thief — 0xFC>>1 = 0x7E, well past it"),
]


def creature_main():
    fails = 0
    for stamp, want, why in CREATURE_CASES:
        pos = run_creature_case("/var/tmp/u2-cp5-stamp-%02x" % stamp, stamp)
        moved = (pos == "03 00")
        if moved != want:
            fails += 1
        print("  %s  creature stamp %#04x -> %-8s %s (%s)" %
              ("PASS" if moved == want else "FAIL", stamp, pos or "no dump",
               "moved" if moved else "refused", why))
    if fails:
        print("  FAIL  %d of %d creature-stamp cases wrong — the gate is reading the "
              "table again, not the stamped byte" % (fails, len(CREATURE_CASES)))
        return 1
    print("  PASS  creature stamps: %d/%d verdicts come from the byte, not the table" %
          (len(CREATURE_CASES), len(CREATURE_CASES)))
    return 0


def main():
    fails = 0
    for tile, want, why in CASES:
        pos = run_case("/var/tmp/u2-cp5-ladder-%02x" % tile, tile)
        moved = (pos == "03 00")
        ok = (moved == want)
        if not ok:
            fails += 1
        print("  %s  map %#04x -> %-8s %s (%s)" %
              ("PASS" if ok else "FAIL", tile, pos or "no dump",
               "moved" if moved else "refused", why))
    if fails:
        print("  FAIL  %d of %d passability ladder cases wrong" % (fails, len(CASES)))
        return 1
    print("  PASS  passability ladder: %d/%d tiles match asm/2423's verdicts" %
          (len(CASES), len(CASES)))
    return mode_main() or creature_main()


# ---- the travel-mode arms (W2) ---------------------------------------------------
#
# The foot ladder above is only one of five arms. asm/253B branches on [DS:13] first, and
# the four vehicle arms disagree with the summary they were first given in two ways that
# cost this project a wrong table each time it was written from a header instead of a body:
#
#   * horse and plane are NOT "the same ladder as foot". asm/2626 calls the foot tail (so
#     the ladder runs) and then rejects mode 0x02 (map 0x04, shallow water) and 0x60 of its
#     own accord. The plane adds mode 0x06 (map 0x0C, forest) at asm/2604.
#   * the rocket accepts NOTHING. asm/2542 jumps to the reject return; the unconditional
#     accept that asm/25F8 is is the foot ladder's own fall-through, three instructions
#     ahead of the reject the rocket arm lands on.
#   * the ship is water only: asm/2619 accepts [DS:29] == 0x00 and its second compare,
#     0x80, is unreachable because the value is a map byte shifted right once.
#
# Each case boards the vehicle on its own tile at the entry cell, then probes the candidate
# one step east, and reads the position back. Fresh seed per case, same rule as above.

MODE_CASES = [
    # (mode, tile under the player, items, candidate tile east, accept?, why)
    ("horse",  G.TILE_HORSE,  {},                       0x08, True,
     "asm/2626 runs the foot ladder, grass is on it"),
    ("horse",  G.TILE_HORSE,  {},                       0x0C, True,
     "forest is below the 0x16 threshold, horse takes the ladder's verdict"),
    ("horse",  G.TILE_HORSE,  {},                       0x04, False,
     "shallows: asm/2626's own reject at runtime 0x2855, the finding the old table missed"),
    ("horse",  G.TILE_HORSE,  {},                       0x00, False,
     "open water, rejected by the ladder the mounted arm calls into"),
    ("plane",  G.TILE_PLANE,  {G.ITEM_SKULL_KEY: 1},    0x08, True,
     "asm/2604 adds only forest"),
    ("plane",  G.TILE_PLANE,  {G.ITEM_SKULL_KEY: 1},    0x0C, False,
     "forest: asm/2604 runtime 0x2813, the one tile the plane alone refuses"),
    ("plane",  G.TILE_PLANE,  {G.ITEM_SKULL_KEY: 1},    0x04, False,
     "shallows, inherited from asm/2626"),
    ("ship",   G.TILE_SHIP,   {G.ITEM_BLUE_TASSLE: 1},  0x00, True,
     "water: measured live in captures/spice86/ship-port-boniface"),
    ("ship",   G.TILE_SHIP,   {G.ITEM_BLUE_TASSLE: 1},  0x04, False,
     "shallows is not 0x00, so the ship refuses it too — water ONLY"),
    ("ship",   G.TILE_SHIP,   {G.ITEM_BLUE_TASSLE: 1},  0x08, False,
     "grass: refused live in the same capture, including the ship's own dock tile"),
    ("rocket", G.TILE_ROCKET, {G.ITEM_ANKH: 1},        0x08, False,
     "the rocket refuses every step: asm/2542 -> the reject return, not the accept"),
    ("rocket", G.TILE_ROCKET, {G.ITEM_ANKH: 1},        0x00, False,
     "and that includes water, which is what distinguishes it from the ship"),
]


def mode_case(name, tile, items, cand, want, why):
    work = G.seed("/var/tmp/u2-cp5-ladder-%s-%02x" % (name, cand),
                  tiles={G.ENTRY: tile, G.EAST: cand}, items=items)
    d = G.state(work, ["B", "RIGHT"], name=name)
    if d is None:
        return None
    return G.pos(d) == G.EAST


def mode_main():
    fails = 0
    for name, tile, items, cand, want, why in MODE_CASES:
        moved = mode_case(name, tile, items, cand, want, why)
        ok = (moved == want)
        if not ok:
            fails += 1
        print("  %s  %-6s steps onto %#04x: %s (%s)" %
              ("PASS" if ok else "FAIL", name, cand,
               "moved" if moved else "refused", why))
    if fails:
        print("  FAIL  %d of %d travel-mode passability cases disagree with asm/253B's arms"
              % (fails, len(MODE_CASES)))
        return 1
    print("  PASS  travel-mode passability: %d/%d cases match asm/253B's four arms" %
          (len(MODE_CASES), len(MODE_CASES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
