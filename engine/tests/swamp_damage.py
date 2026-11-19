#!/usr/bin/env python3
"""
swamp_damage.py — CP5 gate E1: the two HP-costing tiles of asm/2423's effect ladder.

What is under test is the ENTRY asymmetry and the amount, not just the happy path. The
owner's three-way control (2026-11-02, docs/handoff-tile-effects.md § 3) is the shape of
this gate on purpose: a naive implementation that charges for STANDING on the swamp, or
for LEAVING it, passes only the middle case — and the middle case is exactly the one that
would fool a reviewer. So all three run, plus the controls that make the gate able to fail:

  enter   safe -> swamp: moved, HP 9997 -> 9992. The observed delta: nine live steps
          went 0370..0330, every one -5 (asm/2495 header).
  pass    PASS while standing on swamp: no move, HP unchanged. Owner control.
  exit    swamp -> safe: moved, HP unchanged. Owner control.
  grass   safe -> grass: moved, HP unchanged. Proves the cost is the tile, not the step.
  none    no key at all: HP unchanged. If HP drains by itself the gate cannot fail.
  death   HP 0004 onto a swamp: "IS DEAD!". The borrow out of [DS:51] falls through to
          `jmp L_000e62` (asm/0C62), so underflow is death, not a wrap.

The force-field rung of the same ladder (asm/24B1 -> asm/24B5) is gated here too, because
the step-site call is one code path and the sector gate covers both rungs:

  field   safe -> 0x5C: HP 9997 -> 8997 (sbb 0x10 on the HIGH BCD byte = -1000) and the
          "FIELD CAUSES 1000 DAMAGE!" string reaches the screen.
  ring    same with items_held[0] = 1: HP unchanged and "RING PROTECTS" prints — the
          [DS:D6] gate at asm/24B5.

Fresh seed dir per case: the engine rewrites the save in place (u2_gate.py's rule, and
handoff trap 4). HP is read from the raw dump pair DS:51/DS:52, high byte first, and
decoded as the four decimal digits it is.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_gate as G

HP_HI_OFF = 0x51 - 0x36   # PLAYER file byte of DS:51, the high BCD byte
HP_LO_OFF = 0x52 - 0x36


def hp(dump):
    """The DS:51/DS:52 packed-BCD pair as the number the status band prints."""
    return int("".join("%02d" % ((b >> 4) * 10 + (b & 0xF)) for b in G.raw(dump, "hp")))


def seed_case(name, tiles=None, items=None, hp_pair=None):
    work = G.seed("/var/tmp/u2-swamp-%s" % name, tiles=tiles, items=items)
    if hp_pair:
        pp = G.record(work)
        p = bytearray(open(pp, "rb").read())
        p[HP_HI_OFF], p[HP_LO_OFF] = hp_pair
        open(pp, "wb").write(bytes(p))
    return work


def state_case(name, keys, tiles=None, items=None, hp_pair=None, extra=()):
    work = seed_case(name, tiles, items, hp_pair)
    text = G.run_stdout(work, keys, name, extra=extra)
    return text, G.state_from_text(text)


def seed_sector(name, hi, lo, tiles=None, items=None):
    """Seed a character standing in sector hi*10+lo, on a copy of the sector-00 grid.

    The sector digits are record bytes DS:49 and DS:4A, and the ones digit is also the
    site kind. The grid is copied rather than taken from the real mapx03 so the cell
    under test is the only thing in the sector the case has to trust.
    """
    work = seed_case(name, tiles=None, items=items)
    content = os.path.join(work, "content")
    grid = bytearray(open(os.path.join(content, "mapx00"), "rb").read())
    for (x, y), v in (tiles or {}).items():
        grid[y * 64 + x] = v
    with open(os.path.join(content, "mapx%02d" % (hi * 10 + lo)), "wb") as f:
        f.write(bytes(grid))
    pp = G.record(work)
    p = bytearray(open(pp, "rb").read())
    p[0x49 - 0x36] = hi
    p[0x4A - 0x36] = lo
    open(pp, "wb").write(bytes(p))
    return work


def sector_case(name, hi, lo, keys, tiles=None, items=None):
    text = G.run_stdout(seed_sector(name, hi, lo, tiles, items), keys, name)
    return text, G.state_from_text(text)


def main():
    fails = 0

    def check(label, ok, detail):
        nonlocal fails
        if not ok:
            fails += 1
        print("  %s  %s — %s" % ("PASS" if ok else "FAIL", label, detail))

    # enter: one step east onto the swamp. Original mode: silent, like the original —
    # no announcement AND no flash (the swamp provably does not flash; see
    # captures/spice86/swamp-flash-20261102).
    text, s = state_case("enter", ["RIGHT"], tiles={G.EAST: 0x04})
    check("enter", s and G.pos(s) == G.EAST and hp(s) == 9992
          and "[swamp" not in G.flatten(text) and s["state"]["flashes"] == 0,
          "pos %s hp %d, no announcement, no flash (want moved, 9992, silent)" %
          (G.pos(s) if s else None, hp(s) if s else -1))

    # enter-enhanced: the E3 seam AND the enhanced swamp ring. Enhanced mode
    # announces what the original left silent and flashes the wound ring on the
    # player's cell — an enhancement, not fidelity; original mode must do neither.
    work = G.seed("/var/tmp/u2-swamp-enter-enh", tiles={G.EAST: 0x04})
    text = G.run_stdout(work, ["RIGHT"], "enter-enh", extra=["--enhanced"])
    s = G.state_from_text(text)
    check("enter-enhanced", s and hp(s) == 9992 and "[swamp -5 HP]" in G.flatten(text)
          and s["state"]["flashes"] >= 1,
          "hp %d, announcement %s, flashes %s (want 9992, the line, >=1 ring)" %
          (hp(s) if s else -1,
           "present" if s and "[swamp -5 HP]" in G.flatten(text) else "MISSING",
           s["state"]["flashes"] if s else "-"))

    # pass: standing ON the swamp and pressing PASS must cost nothing (owner control).
    text, s = state_case("pass", ["SPACE"], tiles={G.ENTRY: 0x04})
    check("pass", s and G.pos(s) == G.ENTRY and hp(s) == 9997,
          "pos %s hp %d (want no move, 9997)" % (G.pos(s) if s else None, hp(s) if s else -1))

    # exit: leaving the swamp must not charge again (owner control).
    text, s = state_case("exit", ["RIGHT"], tiles={G.ENTRY: 0x04})
    check("exit", s and G.pos(s) == G.EAST and hp(s) == 9997,
          "pos %s hp %d (want moved, 9997)" % (G.pos(s) if s else None, hp(s) if s else -1))

    # grass control: an ordinary accepted step is free of HP.
    text, s = state_case("grass", ["RIGHT"])
    check("grass", s and G.pos(s) == G.EAST and hp(s) == 9997,
          "pos %s hp %d (want moved, 9997)" % (G.pos(s) if s else None, hp(s) if s else -1))

    # none control: no input at all. If this arm loses HP the gate measures nothing.
    text, s = state_case("none", [])
    check("none", s and hp(s) == 9997, "hp %d (want 9997)" % (hp(s) if s else -1))

    # death: HP 0004 onto a swamp underflows the BCD pair and asm/0C62 owns the tail.
    #
    # --quit-on-death because this case asserts on the corpse in the final dump, and the
    # game now reloads the last save on death by default (owner ruling 2026-11-11). The
    # flag is the dev affordance for exactly this; without it the reload would put a live
    # character where the evidence is read and the check would pass while proving nothing.
    text, s = state_case("death", ["RIGHT"], tiles={G.EAST: 0x04}, hp_pair=(0x00, 0x04),
                         extra=("--quit-on-death",))
    check("death", "IS DEAD!" in text, "death tail %s" %
          ("printed" if "IS DEAD!" in text else "MISSING"))

    # field: the second rung of the same ladder, -1000 via the high byte.
    text, s = state_case("field", ["RIGHT"], tiles={G.EAST: 0x5C})
    check("field", s and hp(s) == 8997 and "1000" in G.flatten(text),
          "hp %d, screen %s (want 8997 and the FIELD line)" %
          (hp(s) if s else -1, "ok" if "1000" in G.flatten(text) else "missing"))

    # ring: [DS:D6] non-zero protects and prints instead of subtracting.
    text, s = state_case("ring", ["RIGHT"], tiles={G.EAST: 0x5C}, items={0: 1})
    check("ring", s and hp(s) == 9997 and "RING" in G.flatten(text),
          "hp %d, screen %s (want 9997 and the RING line)" %
          (hp(s) if s else -1, "ok" if "RING" in G.flatten(text) else "missing"))

    # field-in-site-03: the ladder has no sector gate, and this is the case that proves
    # it. Every force-field tile in the shipped data is in sector 03 (72 of them) or
    # sector 71 (4) — ones digits 3 and 1 — and the only live observation of this
    # effect, captures/spice86/g29-field, reads DS:49 = 0 and DS:4A = 3 in its own
    # memory dump. The ten cases above all seed mapx00, so a `sector_lo != 0` early
    # return passed every one of them while switching the effect off in the only sectors
    # that have it. The owner hit exactly this on 2026-11-20: ring held, walked onto a
    # field in Shadow Guard, and the screen said nothing.
    text, s = sector_case("field-site-03", 0, 3, ["RIGHT"], {G.EAST: 0x5C})
    check("field-site-03", s and hp(s) == 8997 and "1000" in G.flatten(text),
          "sector 03: hp %d, screen %s (want 8997 and the FIELD line)" %
          (hp(s) if s else -1, "ok" if "1000" in G.flatten(text) else "missing"))

    # ring-in-site-03: the arm the owner actually met. Protection is a message, not a
    # silence — the original prints RING PROTECTS FROM FIELD! and takes nothing.
    text, s = sector_case("ring-site-03", 0, 3, ["RIGHT"], {G.EAST: 0x5C}, items={0: 1})
    check("ring-site-03", s and hp(s) == 9997 and "RING" in G.flatten(text),
          "sector 03 with ring: hp %d, screen %s (want 9997 and the RING line)" %
          (hp(s) if s else -1, "ok" if "RING" in G.flatten(text) else "missing"))

    # refused-swamp (G55): the ladder sits at asm/2423 entry, ahead of the verdict, so a
    # MOUNTED step the horse refuses onto a swamp still pays the 5. Board the horse (B),
    # then step east onto the swamp the horse will not enter (asm/2626 refuses mode 0x02).
    # The move is refused (position unchanged) yet HP still falls by 5.
    text, s = state_case("refused-swamp", ["B", "RIGHT"],
                         tiles={G.ENTRY: G.TILE_HORSE, G.EAST: 0x04})
    check("refused-swamp", s and G.pos(s) == G.ENTRY and hp(s) == 9992,
          "pos %s hp %d (want refused at entry, 9992: the -5 fires before the verdict)" %
          (G.pos(s) if s else None, hp(s) if s else -1))

    # refused-mountain control: a refused step onto a NON-charging tile must stay free,
    # or the charge is just per-attempt rather than the swamp tile effect (G55 control).
    text, s = state_case("refused-mountain", ["B", "RIGHT"],
                         tiles={G.ENTRY: G.TILE_HORSE, G.EAST: 0x10})
    check("refused-mountain", s and G.pos(s) == G.ENTRY and hp(s) == 9997,
          "pos %s hp %d (want refused at entry, 9997: mountain is not a charging tile)" %
          (G.pos(s) if s else None, hp(s) if s else -1))

    if fails:
        print("  FAIL  %d of 11 tile-effect cases wrong" % fails)
        return 1
    print("  PASS  tile effects: swamp -5 on entry (3 owner controls, silent in original "
          "mode, announced in enhanced), underflow is death, field -1000 and its ring gate, "
          "and the ladder firing on a refused mounted step (G55) with its mountain control")
    return 0


if __name__ == "__main__":
    sys.exit(main())
