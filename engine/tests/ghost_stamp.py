#!/usr/bin/env python3
"""
ghost_stamp.py — CP4 gate: a saved map never holds a creature byte with no creature behind it.

asm/2233 restores a departure cell from `mon_kind[slot]`, the terrain byte the creature saved
when it arrived (`mov 0x1b7(%di),%al` then `mov %al,(%bx,%si)`), and that routine's own header
says "THIS is what DS:1B7 is for". The engine restored `map_base` instead. `map_base` is
loaded from the sector file, and the shipped files already carry baked creature stamps --
mapx00 holds 0x3c x13, 0xf8 x5, 0x38 x5, 0xfc x3, the residue of someone's play session in
1983 -- so every departure re-planted a creature byte into a cell no creature occupies. The
step gate reads the stamped grid, so each one is a permanent wall: the owner watched an orc
refuse a phantom at (29,25) in sector 00, 618 times, on open grass.

The assertion is the one that matters to a player rather than to the code: after creatures
have moved and the game has been saved, walk the saved map and ask of every cell holding a
creature-type byte whether the saved creature table has someone standing there. Zero
unbacked cells is the rule. It is measured on the written files, because the bug only existed
in what reached the disk -- ai_check's M3 covers the same property in memory, and the two
together are what makes this hard to regress.

Control, and it is the regression itself: point the departure restore back at map_base and
this goes red with an unbacked cell on the same seed. That is the state that shipped.
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ROOT)
fails = []


def check(cond, msg):
    print("  %s  %s" % ("PASS" if cond else "FAIL", msg))
    if not cond:
        fails.append(msg)


def main():
    work = "/var/tmp/u2-ghost-stamp"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work + "/save/PLAYER")
    # Seed from the repo's own working copy of the game, never a machine-specific scratch
    # path: a gate that only runs on one laptop is a gate that is not running.
    seed_src = os.path.join(REPO, "original", "player")
    if not os.path.exists(seed_src):
        print("  FAIL  no original/player to seed from, so this gate cannot run. A skip "
              "here would hide the defect it exists to catch, so it is a failure.")
        return 1
    shutil.copy(seed_src, work + "/save/PLAYER/PLAYER")
    for args in (["maxout", work + "/save/PLAYER/PLAYER"],
                 ["set", work + "/save/PLAYER/PLAYER", "sector", "0"]):
        subprocess.run(["python3", os.path.join(REPO, "tools", "u2_edit.py")] + args,
                       capture_output=True, cwd=REPO)

    # 30 idle turns with the auto-pass on so the authored creatures actually walk, then Q.
    open(work + "/k.keys", "w").write("WAIT\n" * 30 + "q\n")
    subprocess.run(["./u2", "--headless", "--play-map", "--data-dir", "../original",
                    "--save-dir", work + "/save", "--script", work + "/k.keys",
                    "--auto-pass-polls", "2"],
                   cwd=ROOT, capture_output=True, text=True)

    slot = work + "/save/PLAYER"
    mp, mm = slot + "/mapx00", slot + "/monx00"
    check(os.path.exists(mp) and os.path.exists(mm),
          "a save after creature movement writes BOTH mapx00 and monx00 (asm/2305 names both)")
    if not (os.path.exists(mp) and os.path.exists(mm)):
        print("  FAIL  cannot continue without the written pair")
        return 1

    ov = open(mp, "rb").read()
    mon = open(mm, "rb").read()
    live = {(mon[i], mon[32 + i]) for i in range(32) if mon[96 + i]}
    types = {mon[96 + i] for i in range(32) if mon[96 + i]}
    check(len(live) > 0, "the saved table still holds creatures (%d)" % len(live))

    ghosts = [(j % 64, j // 64, "%02x" % ov[j]) for j in range(4096)
              if ov[j] in types and (j % 64, j // 64) not in live]
    check(len(ghosts) == 0,
          "no unbacked creature byte in the saved map (found %d: %s)" % (len(ghosts), ghosts[:5]))

    # And the movement has to have happened, or the check above is vacuous: a run where
    # nothing moved would pass with the bug still in place.
    pr = open(os.path.join(REPO, "original", "mapx00"), "rb").read()
    moved = sum(1 for j in range(4096) if pr[j] != ov[j])
    check(moved > 0, "the run really did move creatures (map differs from pristine in %d cells)" % moved)

    if fails:
        print("  FAIL  %d ghost-stamp checks are wrong" % len(fails))
        return 1
    print("  PASS  a saved map never walls off a living creature with a ghost")
    return 0


if __name__ == "__main__":
    sys.exit(main())
