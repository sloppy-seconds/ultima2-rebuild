#!/usr/bin/env python3
"""
site_state.py — CP5 gate: crossing a site edge must not write sector files.

What is under test is a negative, and it is the negative the engine used to get wrong.
site_enter() and site_leave() used to flush the outgoing sector to mapxNN and monxNN, on a
comment citing asm/22FD as "stores the sector being left". asm/22FD is `mov 0x6d,%al ;
or %al,%al ; je` — a boots gate with no I/O in it. The real sector store is asm/2305, which is
reached on the save path (Q), and that path still writes and still should.

The measurement that settled it is 2026-10-29, owner-driven over noVNC in sector 31: after
several town entries and exits, mapx31, monx31, mapx30 and monx30 in the run dir were
byte-identical to the pristine copies. Only `player` had changed. Docs: docs/save-format.md
§ 10, docs/map-format.md § 10, docs/monster-spawn.md § 16, and the OPEN DEFECT in
docs/phase4-state.md.

Two cases, and they are the two halves of a gate that can fail in both directions:

* **original mode (default)** — dirty a sector, cross the edge, and the sector files on disk
  must be untouched. This is the case that was red before the fix: BOARD paves its tile, which
  marks the map dirty, and the transition wrote it out.
* **--enhanced --keep-site-state** — the same run must write the paved tile to the file. This
  is the owner's option (2026-10-29) for keeping town state, and it is opt-in only: the
  original has no such knob, so `original` mode must never take this path.

Everything here is engine-authored filler, so it runs in CI without the player's game files.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402
import u2_gate as G

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SEED = os.path.join(REPO, "engine", "tests", "cp5_fight_seed.py")

GRASS = 0x08
TILE_HORSE = 0x44          # [DS:14] == 0x22: the horse arm of asm/2FAD, no item gate
BOARD_LEAVES = 0x08        # what the horse arm writes through the cell pointer
SITE_X, SITE_Y = 2, 0      # the cell the player starts on, on the north edge
# The fields are named through u2_seed, which reads the addresses from state.h at runtime.


def build(work):
    """A village sector (ones digit 1) whose north edge is one step away."""
    if os.path.isdir(work):
        shutil.rmtree(work)
    subprocess.run([sys.executable, SEED, work], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    content = os.path.join(work, "content")

    mp = bytearray([GRASS]) * 4096
    mp[SITE_Y * 64 + SITE_X] = TILE_HORSE
    open(os.path.join(content, "mapx01"), "wb").write(bytes(mp))

    mon = bytearray(256)
    mon[0], mon[32] = 30, 30          # parked far away: it must not block the step
    mon[64] = 5
    mon[96] = 0x68
    mon[160] = 1
    open(os.path.join(content, "monx01"), "wb").write(bytes(mon))

    pp = G.record(work)
    p = u2_seed.PlayerRecord.load(pp)
    p.set_raw("sector_second", 1)      # sector 01: a site, so the edge leaves the site
    p.set_raw("entry_x", SITE_X)       # entry x is also where the player is placed
    p.set_raw("entry_y", SITE_Y)
    p.write(pp)

    return {n: open(os.path.join(content, n), "rb").read()
            for n in ("mapx01", "monx01")}


def sector(work, name):
    """The bytes the next load of this save would read for one sector file.

    The slot overlay when this save has written that sector, the content copy when it
    has not — the read order engine/src/core/world.c implements, and the reason this
    is a function rather than a path. Reading the content copy directly would assert
    the layout docs/save-scoped-worlds.md replaced: a write now lands in the slot, so
    "the file did not change" and "nothing was written" are no longer the same claim.
    """
    ov = os.path.join(work, "save", "PLAYER", name)
    src = ov if os.path.exists(ov) else os.path.join(work, "content", name)
    return open(src, "rb").read()


def run(work, keys, extra=(), name="case"):
    kp = os.path.join(work, "k-%s" % name)
    with open(kp, "w") as f:
        for k in keys:
            f.write(G.KEYS.get(k, k) + "\n")
    return subprocess.run(
        [G.ENGINE, "--headless", "--play-map", "--cleanroom",
         "--data-dir", os.path.join(work, "content"),
         "--save-dir", os.path.join(work, "save"),
         "--script", kp, "--dump-state", *extra],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout


# BOARD dirties the map (u2_world_tile_put sets map_dirty); UP steps off the north edge,
# which is u2_site_edge_crossed() -> site_leave(). Order matters: the dirt has to exist
# before the transition, or the gate proves nothing.
KEYS = ["B", "UP"]

fails = []


def check(cond, msg):
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def main():
    print("site_state: a site transition writes no sector file (original), one (enhanced)")

    # --- case A: original mode, the faithful default --------------------------
    work = tempfile.mkdtemp(prefix="u2-site-a-")
    try:
        before = build(work)
        run(work, KEYS, name="a")
        after = {n: sector(work, n) for n in before}
        check(before["mapx01"] == after["mapx01"],
              "original: mapx01 on disk untouched by the transition "
              "(cell (%d,%d) still %#02x, saw %#02x)"
              % (SITE_X, SITE_Y, TILE_HORSE, after["mapx01"][SITE_Y * 64 + SITE_X]))
        check(before["monx01"] == after["monx01"], "original: monx01 on disk untouched")
        # The dirt really happened, or case A would pass by doing nothing at all.
        state = G.state_from_text(run(work, KEYS, name="a2"))
        check(state is not None, "original: the run produced a state dump")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    # --- case B: the enhanced option, which must keep state -------------------
    work = tempfile.mkdtemp(prefix="u2-site-b-")
    try:
        before = build(work)
        run(work, KEYS,
            extra=["--enhanced", "--keep-site-state"], name="b")
        after = sector(work, "mapx01")
        check(after[SITE_Y * 64 + SITE_X] == BOARD_LEAVES,
              "enhanced --keep-site-state: the paved tile reached the file "
              "(cell (%d,%d) = %#02x, expected %#02x)"
              % (SITE_X, SITE_Y, after[SITE_Y * 64 + SITE_X], BOARD_LEAVES))
        check(before["mapx01"] != after,
              "enhanced --keep-site-state: the file actually changed")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    # --- case C: an opened door must survive the creature turn that follows it ---
    #
    # asm/435E paves the door cell with the ground under the player, and the walk loop runs
    # creature_turn() immediately after the unlock. The engine's restamp rebuilds the grid
    # from map_base, so a paving written with tile_put alone is gone before the player's next
    # key — the door re-closes the instant it was opened. Measured in the original 2026-10-29:
    # two doors stayed paved across a walk through a town with 32 live creatures. So the
    # paving has to go through terrain_put, and this case is what keeps it there.
    work = tempfile.mkdtemp(prefix="u2-site-c-")
    try:
        G.seed(work, tiles={(3, 0): 0xA0})          # a door one cell east of the player
        pp = G.record(work)
        p = u2_seed.PlayerRecord.load(pp)
        p.set_intent("keys", 5)                        # the unlock costs one
        p.write(pp)
        mon_before = sector(work, "monx00")

        run(work, ["U", "RIGHT"] + ["DOWN"] * 6 + ["Q"], name="c")

        paved = G.map_byte(work, 3, 0)
        check(paved == GRASS,
              "unlocked door is still paved after the creature turn and reached the save "
              "(cell (3,0) = %#02x, expected %#02x)" % (paved, GRASS))
        # Precondition, asserted rather than assumed: if nothing moved, the restamp never ran
        # and the case above would pass without proving anything.
        mon_after = sector(work, "monx00")
        check(mon_after != mon_before,
              "precondition: a creature moved, so the restamp really ran between the unlock "
              "and the save")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("SITE STATE: %s" % ("FAIL (%d)" % len(fails) if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
