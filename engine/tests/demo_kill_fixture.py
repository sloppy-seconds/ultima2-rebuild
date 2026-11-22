#!/usr/bin/env python3
"""Seed a world whose one creature dies on exactly one of the two fidelity rules.

This is W25's fixture. `docs/demo-record-playback.md` deviation 22: a recording made in
enhanced mode used to play back under whatever mode the viewer's binary happened to
default to, and the mode is a RULE, not a skin. `apply_damage_to_slot` kills when
`dmg >= hp` in enhanced mode and when `dmg > hp` in original mode, so a blow that lands
exactly on a creature's remaining hp is a kill on one side and a live creature at zero
hit points on the other. That is the whole of record 37 of the owner's 147-record demo,
which was read for a day as a creature that "would not die".

The geometry is chosen so the equality case is forced rather than lucky:

  * the seed's strength byte is 0x40 and no weapon is readied, so the swing is
    `(0 * 8 + 0x40) >> 2` = 16 damage, exactly as `combat.c` documents;
  * the creature's hp is therefore set to 16, so the blow equals the remainder;
  * the agility byte is 0x80, and a hit needs `(rand >> 1) < agi`, which is true for
    every byte, so the swing cannot miss and the leg cannot flake.

The creature sits one cell east of the entry point, which is where `demo_seed.py` puts
the player, so the key script is ATTACK then EAST and the player never moves.

Usage: demo_kill_fixture.py DIR
"""
import os
import sys

# The seeders share one offset source: state.h, read at runtime by u2_seed. No DS address
# is written down in this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402

GRASS = 0x08
EDGE = 64
SLOTS = 32

# The monx file is the 256 bytes asm/22D5 reads straight into DS:137, so the file offset
# of a field is its distance from DS:137: x at +0, y at +32, hp at +64, type at +96,
# kind at +128. See state.h's mon_* block.
OFF_X, OFF_Y, OFF_HP, OFF_TYPE, OFF_KIND = 0, 32, 64, 96, 128

PLAYER_X, PLAYER_Y = 31, 31
MON_X, MON_Y = 32, 31
MON_HP = 16            # == the swing, which is the point of the whole file
MON_TYPE = 0x30
MON_KIND = 0x08


def sector(path):
    with open(path, "wb") as f:
        f.write(bytes([GRASS]) * (EDGE * EDGE))
        f.write(bytes(128))           # the shipped tail, never read


def monx(path):
    b = bytearray(256)
    # SLOT 1, not slot 0: asm/23E6 find_creature_at_cursor never examines slot 0 (G78), so a
    # slot-0 creature is invulnerable and the recorded kill would never land. Slot 1 is the
    # nearest attackable slot at the same cell.
    b[OFF_X + 1] = MON_X
    b[OFF_Y + 1] = MON_Y
    b[OFF_HP + 1] = MON_HP
    b[OFF_TYPE + 1] = MON_TYPE
    b[OFF_KIND + 1] = MON_KIND
    with open(path, "wb") as f:
        f.write(bytes(b))


def main(out_dir):
    content = os.path.join(out_dir, "content")
    save = os.path.join(out_dir, "save", "PLAYER")
    os.makedirs(content, exist_ok=True)
    os.makedirs(save, exist_ok=True)
    sector(os.path.join(content, "mapx70"))
    monx(os.path.join(content, "monx70"))

    # Built through u2_seed so the addresses come from state.h. Six name characters
    # because that is what this seeder wrote before; a move keeps its bytes.
    p = u2_seed.PlayerRecord()
    p.set_name("GATEDE")
    p.set_at(7, 0)                     # sector 70; second digit is the site kind
    p.set_intent("hp", 9997)
    p.set_intent("food", 999999)
    p.set_stat("str", 0x40)            # the swing is (0*8 + 0x40) >> 2 = 16
    p.set_stat("agi", 0x80)            # every roll is a hit
    p.set_raw("entry_x", PLAYER_X)
    p.set_raw("entry_y", PLAYER_Y)
    p.write(os.path.join(save, "PLAYER"))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: demo_kill_fixture.py DIR", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
