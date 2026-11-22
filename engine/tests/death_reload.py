#!/usr/bin/env python3
"""
death_reload.py — CP4 gate for what the game does when you die.

Owner ruling, 2026-11-11: **death reloads the last save, unconditionally.** Not back to
the menu, not quit — the live process re-reads the record and drops you back in.

Why this needed its own gate. The reload was implemented, and gated, and every test stayed
green — while the noVNC play session quit on death. The reason is that the gates drove
`title.c`'s 'P' path and the session boots through `--play-map`, which was the one entry
with the reload deliberately switched off so that other gates could read the corpse out of
the final dump. A behaviour that exists only on the path nobody plays is not a behaviour,
which is the general lesson and also the specific bug.

So the assertions here are paired, and the DEFAULT is the product:

  * default: the run ends with a live character at the saved position, and the death frame
    is gone — the reload cleared it and drew a new world over it.
  * --quit-on-death: the run ends on the corpse, which is what CP4 G2 and swamp_damage.py
    depend on. The flag is a dev affordance and nothing else.

The control that must be seen to fail: remove the reload from main.c's --play-map loop and
the default case goes red on both of its checks while the flag case stays green. Leaving the
flag in and the reload out is exactly the state that shipped, and it is what this gate now
makes impossible.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402
import u2_gate as G          # noqa: E402

# The HP pair is named through u2_seed (state.h's hp at DS:51 and the low byte it has not
# been able to name, unk_52 at DS:52).
fails = []


def check(cond, msg):
    print("  %s  %s" % ("PASS" if cond else "FAIL", msg))
    if not cond:
        fails.append(msg)


def hp(dump):
    return int("".join("%02d" % ((b >> 4) * 10 + (b & 0xF)) for b in G.raw(dump, "hp")))


def seed_dying(name):
    """A character on a swamp tile's west neighbour with HP 0004: one step east
    underflows the BCD pair, and asm/0C62 owns the tail. The record on disk holds
    the same HP 0004, so a reload comes back alive — which is the whole point."""
    work = G.seed("/var/tmp/u2-death-reload-%s" % name, tiles={G.EAST: 0x04})
    pp = G.record(work)
    p = u2_seed.PlayerRecord.load(pp)
    p.set_raw("hp", 0x00)
    p.set_raw("unk_52", 0x04)
    p.write(pp)
    return work


def main():
    for label, extra, want_dead in (("default", (), False),
                                    ("quit-on-death", ("--quit-on-death",), True)):
        work = seed_dying(label)
        text = G.run_stdout(work, ["RIGHT"], name=label, extra=extra)
        screen = G.flatten(text)
        st = G.state_from_text(text)

        if want_dead:
            check("IS DEAD!" in screen,
                  "%s: the run ends on the corpse, which is what the corpse-reading gates need" % label)
            check(st is not None and hp(st) == 4 or st is not None,
                  "%s: and the run still produced a final state (it did not crash)" % label)
        else:
            check(st is not None and hp(st) == 4,
                  "%s: death reloaded the save — the final state is the saved character at HP 0004, not a corpse"
                  % label)
            check("IS DEAD!" not in screen,
                  "%s: and the death frame was cleared by the reload rather than left on screen" % label)
        check(st is not None, "%s: the process ended with a state dump, so it did not exit early" % label)

    if fails:
        print("  FAIL  %d death-reload checks are wrong" % len(fails))
        return 1
    print("  PASS  death reloads the last save by default; --quit-on-death keeps the corpse for the gates")
    return 0


if __name__ == "__main__":
    sys.exit(main())
