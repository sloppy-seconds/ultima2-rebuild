#!/usr/bin/env python3
"""
cannon_kill.py — CP5 gate for what a frigate cannon shot does, lethal and not.

Why this exists. `combat.c` carried an `award` parameter that made a cannon kill pay
nothing, on the static reading that asm/2C19's `jae` lands on asm/2CA4 (stamp the emptied
cell) and that "there is no reward roll anywhere on that path". The reading was wrong in
the way this project keeps being wrong: the reward is reached by FALL-THROUGH, not by a
call. asm/2CA4 ends at `call L_0052b3` and the very next bytes are
asm/2CB5_combat_kill_cell_restore, then the drop dispatch at asm/2CD3 / asm/2D22, and both
of those arms tail-jump to combat_kill_reward at asm/2D8C.

Settled live 2026-11-11 by captures/spice86/cannon-kill-live: one cell, a planted creature
at hp 0x20, `F` + `Right`, and the original printed `KILLED--GOLD+13--EXP.+03` and moved
DS:56..59 exactly as a melee reward does. So a cannon kill pays, and because the Minax
gate sits on the same fall-through, a cannon kill of Minax wins (G75).

The second half is the owner's enhancement request of the same day: "maybe we can add a
HIT to the fire when it hits so its more obvious". The original is silent on a hit that
does not kill — measured, `cannon-range-live`, the frame reads `FIRE DIRECT-EAST` and
nothing more — so original mode stays silent and enhanced mode gets the amount through the
one announcement seam (game/feedback.h, E3).

Everything here asserts on what the player sees, not on which function ran. Note the
`KILLED--GOLD` match stops before the plus sign: G.flatten() turns '+' into a space, and a
gate that asserted the literal `GOLD+` would fail forever while the engine printed it.

Controls, all four demonstrated rather than claimed:
  * pass award=0 back into the cannon's apply_damage_to_slot call and the two lethal
    KILLED assertions go red while the silence assertions stay green;
  * delete the announce_kill call from the cannon's kill branch and the same two go red;
  * delete the u2_feedback call in the FIRE arm and the enhanced announcement goes red
    while the original-mode silence stays green;
  * put the u2_feedback call inside `if (fired == 1)`'s opposite branch and the
    original-mode silence goes red.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import u2_gate as G          # noqa: E402

fails = []


def check(cond, msg):
    print("  %s  %s" % ("PASS" if cond else "FAIL", msg))
    if not cond:
        fails.append(msg)


def seed_cannon(work, hp):
    """A ship tile under the player, the tassle to board it with, and a creature in
    slot 5 one cell east at the hp the scenario needs.

    Slot 5 and not slot 0 on purpose: asm/23E6's cannon probe scans DI from 31 down and
    stops before 0, so a creature in slot 0 is invisible to the gun and the test would
    measure a MISS forever. The fight seed's creature lives in slot 0, so G.seed(monx=...)
    parks it out of the way.

    hp 0x05 is below the cannon's floor (bit 5 is forced, so damage >= 0x20) and kills in
    both fidelities; hp 0x99 leaves a survivor in both, because the original's lethal test
    is damage > hp and the enhanced relaxation is damage >= hp.
    """
    G.seed(work, tiles={G.ENTRY: G.TILE_SHIP, G.EAST: 0x00},
           items={G.ITEM_BLUE_TASSLE: 1}, monx=(30, 30))
    mp = os.path.join(work, "content", "monx00")
    m = bytearray(open(mp, "rb").read())
    slot = 5
    m[0 + slot] = 3            # x: the cell east of the entry
    m[32 + slot] = 0           # y
    m[64 + slot] = hp
    m[96 + slot] = 0x68        # type: Fighter, the same art the fight seed uses
    m[128 + slot] = 0x70       # kind
    m[160 + slot] = 1          # role: an active slot, so the scan does not skip it
    open(mp, "wb").write(bytes(m))


def main():
    work = "/var/tmp/u2-cannon-kill"

    # ---- a kill pays, on both fidelities, and says so -----------------------
    seed_cannon(work, hp=0x05)
    for label, extra in (("original", ()), ("enhanced", ("--enhanced",))):
        out = G.run_stdout(work, ["B", "F", "RIGHT"], name=label, extra=extra)
        screen = G.flatten(out)
        st = G.state_from_text(out)
        check("MISS" not in screen,
              "%s/kill: the shot reached the planted slot (one cell east, no range gate)" % label)
        check("KILLED--GOLD" in screen and "EXP." in screen,
              "%s/kill: the original's KILLED--GOLD+n--EXP.+m line is on screen "
              "(asm/2CA4 falls through to asm/2D8C, it does not stop at the stamp)" % label)
        check(st is not None and G.raw(st, "mon_type")[5] == 0,
              "%s/kill: and the dead slot is cleared, so the line is not decoration" % label)
        check("[cannon" not in screen,
              "%s/kill: no bracketed line — a kill already announces itself, in both modes" % label)

    # ---- a hit that does not kill: silent in original, announced in enhanced --
    seed_cannon(work, hp=0x99)
    for label, extra, want in (("original", (), False), ("enhanced", ("--enhanced",), True)):
        out = G.run_stdout(work, ["B", "F", "RIGHT"], name="hit-" + label, extra=extra)
        screen = G.flatten(out)
        st = G.state_from_text(out)
        check("MISS" not in screen and st is not None and G.raw(st, "mon_type")[5] == 0x68,
              "%s/hit: the target took the shot and survived" % label)
        check("KILLED--GOLD" not in screen,
              "%s/hit: no reward line for a survivor" % label)
        check(("[cannon -" in screen) == want,
              "%s/hit: %s the bracketed damage line (the original prints nothing; "
              "enhanced announces through feedback.h, owner request 2026-11-11)"
              % (label, "shows" if want else "does not show"))

    if fails:
        print("  FAIL  %d of the cannon-shot checks are wrong" % len(fails))
        return 1
    print("  PASS  cannon shot: kill pays on both fidelities, hit announced in enhanced only")
    return 0


if __name__ == "__main__":
    sys.exit(main())
