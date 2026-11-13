#!/usr/bin/env python3
"""gate_transit_save.py — CP5 gate: a time-door transit flushes the outgoing sector.

The original saves on a time-door transit, and it writes MORE than a site edge. An
isolated three-point md5 (captures/spice86/g64-timedoor-autosave) showed the gate
rotation writes nothing, while the transit step writes the player record plus the
outgoing sector's mapx and monx. A site edge writes only the player record
(site_state.py); a transit writes the sector too.

The engine's u2_gate_transit used to persist nothing about the sector being left -- a
divergence from the original and from docs/save-scoped-worlds.md, which says a sector
write belongs to a save and lands in saves/<slot>/. The fix (engine/src/game/gates.c)
syncs the creature table and autosaves before the era digit moves. u2_monsters_store
always sets mon_dirty, so the monx write is deterministic rather than dependent on a
creature having happened to move.

Two halves, so the gate can fail in both directions:

* the transit writes the outgoing sector's monx into the save-slot overlay. Without the
  fix the slot holds no monx00 and this leg is red (the red control).
* the content copy is untouched -- the write landed in the slot, never the content dir
  (the content-immutability invariant, docs/save-scoped-worlds.md).

Everything is engine-authored filler from cp5_fight_seed.py in a throwaway dir, so it
runs in CI with no game media present.
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_gate as G

GRASS = 0x08

# The record starts at DS:36 (the name field), so a DS offset maps to record offset
# DS - 0x36. Same convention site_state.py uses for DS:4A / DS:5A.
R_SECTOR_HI = 0x49 - 0x36      # sector tens digit == era on Earth
R_SECTOR_LO = 0x4A - 0x36      # sector ones digit (0 => world map, not a site)
R_ENTRY_X = 0x5A - 0x36        # the cell the player is placed on at load
R_ENTRY_Y = 0x5B - 0x36
R_GATE_INDEX = 0x6E - 0x36     # TIME-DOOR rotation index (steps by 2)
R_GATE_CD = 0x6F - 0x36        # TIME-DOOR countdown (0 at load; first tick -> 0xff)

# Era 0 slot 1 gate cell is (31,56). The player starts one cell west at (30,56).
GATE_X, GATE_Y = 31, 56
START_X, START_Y = 30, 56


def build(work):
    """Sector 00 (era 0, world map) with a gate that stamps on the first turn."""
    if os.path.isdir(work):
        shutil.rmtree(work)
    # A creature near the player so monx is loaded (monsters_store then dirties it).
    G.seed(work, monx=(29, 56))
    content = os.path.join(work, "content")

    mp = bytearray(open(os.path.join(content, "mapx00"), "rb").read())
    for (x, y) in [(30, 56), (31, 56), (30, 55), (31, 55)]:
        mp[y * 64 + x] = GRASS
    open(os.path.join(content, "mapx00"), "wb").write(bytes(mp))

    pp = G.record(work)
    p = bytearray(open(pp, "rb").read())
    p[R_SECTOR_HI] = 0
    p[R_SECTOR_LO] = 0
    p[R_ENTRY_X] = START_X
    p[R_ENTRY_Y] = START_Y
    # Countdown 1 => the first turn decrements to 0, rotates the index by 2 (0 -> slot 1)
    # and stamps the door. Index 0 -> slot 1 -> era-0 cell (31,56).
    p[R_GATE_INDEX] = 0
    p[R_GATE_CD] = 1
    open(pp, "wb").write(bytes(p))
    return {n: open(os.path.join(content, n), "rb").read()
            for n in ("mapx00", "monx00")}


def slot(work, name):
    return os.path.join(work, "save", "PLAYER", name)


def main():
    print("gate_transit_save: a time-door transit writes the outgoing sector to the slot")
    fails = []

    def check(cond, msg):
        print(("  ok    " if cond else "  FAIL  ") + msg)
        if not cond:
            fails.append(msg)

    work = tempfile.mkdtemp(prefix="u2-gts-")
    try:
        before = build(work)
        # turn 1 UP   -> rotate + stamp the door at (31,56); player moves to (30,55)
        # turn 2 DOWN -> back to (30,56), no rotation (countdown reloaded to 8)
        # turn 3 RIGHT-> step onto (31,56) -> the transit fires
        out = G.run_stdout(work, ["UP", "DOWN", "RIGHT"], name="t")
        st = G.state_from_text(out)
        check(st is not None, "the run produced a state dump")
        if st:
            sec = st.get("state", {}).get("sector")
            # sector is the two-digit string "NN" (tens then ones); era moved 0 -> 2.
            era = int(str(sec)[0]) if sec else -1
            check(era == 2,
                  "the transit moved era 0 -> 2 (sector now %r)" % (sec,))

        check(os.path.exists(slot(work, "monx00")),
              "the transit wrote monx00 into the save slot (saves/PLAYER/monx00)")

        kept = open(os.path.join(work, "content", "monx00"), "rb").read()
        check(kept == before["monx00"],
              "the content monx00 is untouched (the write landed in the slot, not content)")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("GATE TRANSIT SAVE: %s" % ("FAIL (%d)" % len(fails) if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
