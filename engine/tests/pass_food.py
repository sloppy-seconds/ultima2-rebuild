#!/usr/bin/env python3
"""
pass_food.py — the two PASSes are not one cost (G70).

The engine used to charge U2_FOOD_PER_PASS (10) on every PASS, because cmd_P and the idle
auto-pass shared one function and the code assumed both reached the -10 at asm/090D. They
do not. The bytes and a live capture (captures/spice86/g70-pass-vs-move) split them:

  letter 'P'  cmd_P (asm/3DBC) prints PASS then jmps to asm/0C33, the status-counter tick.
              It charges NOTHING. Measured live: food flat across three letter-P passes,
              with a move control dropping 19 in the same session.
  SPACE       the walk loop's own first arm (asm/0936 cmp $0x20). Charges 10. Measured
              live: one space dropped exactly 10.
  idle        asm/08FF falls into asm/090D, the packed-BCD -10. Charges 10. Not exercised
              here: headless runs are exempt from the idle auto-pass by design (the CP2
              baseline must stay byte-exact), so there is no idle arm to drive in a gate.

The move arm is the odometer control: it proves the food triple is live and that the letter-P
zero is a real zero, not a stale read. A gate that only checked the letter-P zero could pass
on a harness that never charged food at all; the move arm is what makes the zero mean
something.

Headless runs fire no idle auto-pass, so a single key is exactly one command and nothing else.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_gate as G

# (name, keys, expected food after the run). The seed's food is 999999 (cp5_fight_seed).
CASES = [
    ("letter-p", ["p"],      999999, "cmd_P charges nothing (asm/3DBC -> asm/0C33)"),
    ("space",    ["SPACE"],  999989, "space bar charges 10 (asm/0936 cmp $0x20)"),
    ("move",     ["RIGHT"],  999980, "a foot step charges 19 (odometer control)"),
    ("none",     [],         999999, "no input charges nothing (no idle auto-pass headless)"),
]


def main():
    fails = 0

    def check(label, ok, detail):
        nonlocal fails
        if not ok:
            fails += 1
        print("  %s  %s - %s" % ("PASS" if ok else "FAIL", label, detail))

    for name, keys, want, why in CASES:
        work = G.seed("/var/tmp/u2-passfood-%s" % name)
        text = G.run_stdout(work, keys, name)
        s = G.state_from_text(text)
        got = G.food(s) if s else -1
        check(name, s and got == want, "food %d (want %d) - %s" % (got, want, why))

    if fails:
        print("  FAIL  %d of %d PASS-food cases wrong" % (fails, len(CASES)))
        return 1
    print("  PASS  PASS food: letter-P 0, space 10, move 19 (odometer control), idle exempt headless")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
