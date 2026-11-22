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
import re
import shutil
import sys

# The seeders share one offset source: state.h, read at runtime by u2_seed. No DS address
# is written down in this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_gate as G

GRASS = 0x08

# The record starts at DS:36 (the name field), so a DS offset maps to record offset
# No offset table here: the fields below are named through u2_seed, which reads them from
# state.h at runtime. The record is DS:36..DS:135, so file byte i is DS:(0x36 + i), and
# PlayerRecord carries that base so this file never writes it.

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
    p = u2_seed.PlayerRecord.load(pp)
    p.set_at(0, 0)                       # sector 00: era 0, world map (not a site)
    p.set_raw("entry_x", START_X)        # the cell the player is placed on at load
    p.set_raw("entry_y", START_Y)
    # Countdown 1 => the first turn decrements to 0, rotates the index by 2 (0 -> slot 1)
    # and stamps the door. Index 0 -> slot 1 -> era-0 cell (31,56).
    p.set_intent("gate", 0)
    p.set_intent("gate-countdown", 1)
    p.write(pp)
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

        # The pairing invariant, stated generally rather than for one sector. A map
        # overlay with no creature overlay beside it is a ghost on the next load: the
        # overlay is read ahead of content, so its stamps become terrain while the
        # creatures come back from the release monx and own nothing. A real save had
        # exactly this shape -- mapx00 and mapx10 present, monx00 and monx10 absent, ten
        # bare stamps including three sea serpents. save_body pairs the two writes; this
        # is the property that protects, and it is checked over the whole slot rather than
        # the one sector this scenario happens to touch.
        slotdir = os.path.dirname(slot(work, "monx00"))
        present = {f.lower() for f in os.listdir(slotdir)}
        orphans = [f for f in sorted(os.listdir(slotdir))
                   if re.fullmatch(r"map[xg]\d\d", f.lower())
                   and ("mon" + f.lower()[3:]) not in present]
        check(not orphans,
              "every map overlay in the slot has a creature overlay beside it (%s)"
              % (", ".join(orphans) if orphans else "none orphaned"))

        # The control for that check. No scripted scenario in this suite can produce an
        # orphan -- every path that dirties a map also syncs the table -- so the check is
        # proven red the direct way: plant the shape a real save had, a map overlay with no
        # creature overlay beside it, and require the scan to name it. Without this the
        # check above could only ever print "none orphaned".
        plant = os.path.join(slotdir, "mapx99")
        try:
            open(plant, "wb").write(b"\x00" * 4096)
            present = {f.lower() for f in os.listdir(slotdir)}
            caught = [f for f in os.listdir(slotdir)
                      if re.fullmatch(r"map[xg]\d\d", f.lower())
                      and ("mon" + f.lower()[3:]) not in present]
            check("mapx99" in caught,
                  "CONTROL: a planted orphaned map overlay is caught (%s)"
                  % (", ".join(sorted(caught)) if caught else "nothing caught"))
        finally:
            if os.path.exists(plant):
                os.remove(plant)

        kept = open(os.path.join(work, "content", "monx00"), "rb").read()
        check(kept == before["monx00"],
              "the content monx00 is untouched (the write landed in the slot, not content)")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("GATE TRANSIT SAVE: %s" % ("FAIL (%d)" % len(fails) if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
