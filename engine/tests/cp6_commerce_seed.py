#!/usr/bin/env python3
"""Emit a synthetic content directory for the CP6 commerce gate.

Engine-authored filler, same rule cp5_fight_seed.py follows: a gate that needs a shop
on a map must not depend on the player's own game files, and CI has no copy of them.
The bytes are shaped like the real ones because the engine reads them the same way.

What the geometry has to reproduce is the ray, not a town. asm/4050 steps one cell in the
aimed direction, requires that cell to be a shop sign (high-bit tile) or 0x7C, steps a
second cell and requires 0x68. So each case is three cells in a line: player, sign, marker.

The quadrant decides the shop (asm/1590), so the marker's position is not incidental —
putting the same geometry in the north-west half of a town yields the weapons shoppe and
nothing else about the cells changes.

Usage: cp6_commerce_seed.py DIR CASE
  CASE = armour | weapons | transport | grocery | pub | nomark
         | torch_lit | torch_none | torch_outside | tower_climb | tower_noladder
         | tower_descend | town_descend | town_climb | tower_both
         | tribute_low | tribute_mid | tribute_max | tribute_broke
         | enter_tower | enter_dungeon | enter_town
"""
import os
import sys

FLOOR = 0x70          # town paving, walkable, and what the live grid shows under a foot
SIGN = 0xB8           # a shop-sign letter: high bit set, which is what makes asm/407F skip
MARK = 0x68           # the merchant marker asm/409F requires
COUNTER = 0x7C        # the one non-high-bit tile the near cell also accepts

# case -> (sector digits, player cell, near cell, far cell, near tile, far tile)
#
# The last two are what make the refusal cases refusal cases: the ray needs the near cell
# to be a sign or 0x7C AND the far cell to be 0x68, and each of the two negative cases
# breaks exactly one of them.
CASES = {
    # sector 32 is a town (kind 2). North-east half -> the armour shoppe, which is the
    # one purchase this project has reproduced end to end more than once.
    "armour":    ((3, 2), (38, 5), (38, 4), (38, 3), SIGN, MARK),
    "weapons":   ((3, 2), (9, 5),  (9, 4),  (9, 3),  SIGN, MARK),
    "transport": ((3, 2), (38, 40), (38, 39), (38, 38), SIGN, MARK),
    # The pub: sector 32 is a town, and the west/south half of a town is asm/15E4.
    "pub":       ((3, 2), (9, 40), (9, 39), (9, 38), SIGN, MARK),
    # sector 31 is a village (kind 1). North-east half -> the grocery. The cells mirror
    # the real sector-31 geometry measured on 2026-10-29: marker (55,19), 0x7C at
    # (55,20), player (55,21).
    "grocery":   ((3, 1), (55, 21), (55, 20), (55, 19), COUNTER, MARK),
    # The clue seller: the west/north half of a VILLAGE is asm/1AE0, so the same quadrant
    # that makes a town the weapons shoppe makes a village the clue seller. Same cell
    # geometry as the weapons case, different site kind -- which is the whole selector.
    "clue":      ((3, 1), (9, 5), (9, 4), (9, 3), SIGN, MARK),
    # Near cell is plain floor: asm/4081 rejects before the marker is ever consulted.
    "nosign":    ((3, 2), (38, 5), (38, 4), (38, 3), FLOOR, MARK),
    # Marker missing one cell further out: asm/409F rejects.
    "nomarker":  ((3, 2), (38, 5), (38, 4), (38, 3), SIGN, FLOOR),
}

# Site cases for cmd_I, cmd_D and cmd_K: (sector digits, player cell, tile under the player,
# torches).
#
# These do not need a tower interior. The ones digit of the sector IS the site kind, so
# sector 24 opens the gates that IGNITE TORCH, DESCEND and KLIMB all test, and the tile under
# the player carries the ladder feature the two vertical commands require -- 0x10 for the up
# cell, 0x20 for the down one, both measured in the sector-24 tower.
#
# There is deliberately no floor in this table. [DS:25] sits BELOW the saved record, which is
# DS:36..DS:135, so site_floor is scratch and no seed file can set it -- which is itself the
# finding: the game does not remember which floor you were on. The two-level cases therefore
# drive the floor with keystrokes (K then K, K then D) instead of seeding it, and that turns
# out to be the better test anyway, because it exercises the pair as a round trip.
SITE_CASES = {
    # (sector digits, player cell, tile under player, torches, hp, gold)
    "torch_lit":      ((2, 4), (5, 5), 0x08, 0x03, None, None),
    "torch_none":     ((2, 4), (5, 5), 0x08, 0x00, None, None),
    "torch_outside":  ((3, 2), (5, 5), 0x08, 0x03, None, None),
    "tower_climb":    ((2, 4), (5, 5), 0x10, 0x00, None, None),
    # Both ladder features on one cell. A real tower keeps its up and down cells apart, so
    # this is engine-authored filler rather than a map the game ships -- it exists so the
    # climb/descend round trip can be driven from one standing position. On a 0x10-only cell
    # DESCEND refuses, which is correct and is what the first version of this case proved.
    "tower_both":     ((2, 4), (5, 5), 0x30, 0x00, None, None),
    "tower_noladder": ((2, 4), (5, 5), 0x08, 0x00, None, None),
    "tower_descend":  ((2, 4), (5, 5), 0x20, 0x00, None, None),
    "town_descend":   ((3, 2), (5, 5), 0x20, 0x00, None, None),
    "town_climb":     ((3, 2), (5, 5), 0x10, 0x00, None, None),
    # The tribute. Sector 33 is a castle kind, and the cleric is placed one cell NORTH of
    # the player so `T` then `Up` lands the cursor on it -- the same geometry as the live
    # capture, where the throne at (31,13) sat directly above the player at (31,14).
    "tribute_low":    ((3, 3), (31, 14), 0x70, 0x00, 0x0001, 0x9999),
    "tribute_mid":    ((3, 3), (31, 14), 0x70, 0x00, 0x5000, 0x9999),
    "tribute_max":    ((3, 3), (31, 14), 0x70, 0x00, 0x9901, 0x9999),
    "tribute_broke":  ((3, 3), (31, 14), 0x70, 0x00, 0x0001, 0x0020),
    # Entry locks. cmd_E refuses unless the sector's ones digit is 0, so these sit on sector
    # 20 and the tile UNDER the player is the entrance: site_for_tile switches on tile>>2, so
    # 0x1C is a tower mouth (digit 4) and 0x24 a dungeon mouth (digit 5). enter_town is the
    # control -- without it the lock could quietly become "refuse every site" and stay green.
    "enter_tower":    ((2, 0), (5, 5), 0x1C, 0x00, None, None),
    "enter_dungeon":  ((2, 0), (5, 5), 0x24, 0x00, None, None),
    "enter_town":     ((2, 0), (5, 5), 0x18, 0x00, None, None),
    # GET's fixture arm, in the place the shipped game actually puts the fixtures: the VAULT
    # of Lord British's castle, sector 23. asm/363B compares the ART INDEX of the cell under
    # the player, so the values here are real map bytes -- 0x54 (art 0x2a) is the drawn shield
    # rack and 0x58 (art 0x2c) the drawn sword rack, each occurring exactly 8 times in
    # mapx23 and mapx33 and nowhere else but six cells in sector 41.
    # The first version of these cases used 0x2c and 0x2a as raw tiles. That was wrong, and
    # the hand test passed anyway because the code under test made the same mistake -- a test
    # that agrees with the assumption it was written from is not evidence.
    # vault_plain is the control: a floor tile must refuse and grant nothing, or the arm could
    # quietly become "grant on any tile" and both positive legs would still pass.
    "vault_sword":    ((2, 3), (19, 13), 0x58, 0x00, None, None),
    "vault_shield":   ((2, 3), (19, 13), 0x54, 0x00, None, None),
    "vault_plain":    ((2, 3), (19, 13), 0x70, 0x00, None, None),
}

# Slots in the synthetic monx: the engine's loader reads x at 0..31, y at 32..63, hp at
# 64..95, type at 96..127, kind at 128..159 and role at 160..191.
CLERIC_SLOT = 8
CLERIC_TYPE = 0x6C


def cleric_monx(px, py):
    """A monx holding exactly one type-0x6C creature one cell north of the player."""
    m = bytearray(256)
    m[CLERIC_SLOT] = px
    m[32 + CLERIC_SLOT] = py - 1
    m[64 + CLERIC_SLOT] = 0xFF          # hp, as shipped in monx33 slot 8
    m[96 + CLERIC_SLOT] = CLERIC_TYPE
    m[160 + CLERIC_SLOT] = 0x00         # role: below 0x80, so the TYPED arm, not the talking arm
    return bytes(m)


def guard_monx(x, y):
    """A monx holding one guard at (x,y) -- the town-entry stamp check.

    Type 0x60 is what every shipped monx uses for a placed guard, and the map byte the
    engine has to overwrite at that cell is the baked 0x60 the real mapx files carry
    (measured: mapx byte == mon_type at 32/32 authored cells in sectors 31, 32 and 33).
    """
    m = bytearray(256)
    m[3] = x
    m[32 + 3] = y
    m[64 + 3] = 0xFF
    m[96 + 3] = 0x60
    return bytes(m)


def main(out_dir: str, case: str) -> int:
    if case in SITE_CASES:
        return main_site(out_dir, case)
    (hi, lo), (px, py), (nx, ny), (fx, fy), ntile, ftile = CASES[case]

    content = os.path.join(out_dir, "content")
    save = os.path.join(out_dir, "save")
    os.makedirs(content, exist_ok=True)
    os.makedirs(save, exist_ok=True)

    sector = f"{hi}{lo}"
    grid = bytearray([FLOOR]) * 4096
    grid[ny * 64 + nx] = ntile
    grid[fy * 64 + fx] = ftile
    with open(os.path.join(content, f"mapx{sector}"), "wb") as f:
        f.write(bytes(grid))

    # An empty monx: the creature scan at asm/23E6 must come up empty so the ray takes the
    # map-tile path. A creature on the cursor is the talking branch, which is not modelled.
    with open(os.path.join(content, f"monx{sector}"), "wb") as f:
        f.write(bytes(256))

    p = bytearray(384)
    p[0:7] = b"BUYTEST"                      # name at DS:36
    p[0x49 - 0x36] = hi                      # sector digits: the second one IS the site kind
    p[0x4A - 0x36] = lo
    p[0x4E - 0x36] = 0x40                    # charisma
    p[0x50 - 0x36] = 0x40                    # intelligence
    p[0x51 - 0x36] = 0x99
    p[0x52 - 0x36] = 0x99                    # HP 9999
    p[0x53 - 0x36] = 0x50
    p[0x54 - 0x36] = 0x00                    # food 5000, so a grocery buy is visible
    p[0x55 - 0x36] = 0x00
    p[0x56 - 0x36] = 0x99
    p[0x57 - 0x36] = 0x99                    # EXP 9999
    p[0x58 - 0x36] = 0x99
    p[0x59 - 0x36] = 0x99                    # GOLD 9999
    p[0x5A - 0x36] = px                      # entry cell: where the world drops you
    p[0x5B - 0x36] = py
    # Command-letter cases need the inventories the handlers gate on. All three are ordinary
    # save bytes (file offset = DS - 0x36), and each one is the byte the handler actually reads:
    p[0x4C - 0x36] = 0x40                    # agility, packed: 0x40 = 64, so digit 8 passes
    #                                       # and digit 9 (72) does not. cmd_R's gate.
    p[0x4B - 0x36] = 0x40                    # strength, for cmd_W next door
    for i in range(1, 10):
        if i == 7:
            continue                         # leave one unowned so the NOT OWNED branch is
        p[0x76 + i - 0x36] = 1               # reachable in a scripted run (7-LI)
    p[0xE3 - 0x36] = 1                       # items_held[13] = STRANGE COIN, which is what
    #                                       # NEGATE TIME spends. DS:E3 is inside items_held;
    #                                       # it is not a separate counter (see commands.c).
    rec = os.path.join(save, "PLAYER", "PLAYER")   # saves/<slot>/PLAYER; core/slot.h
    os.makedirs(os.path.dirname(rec), exist_ok=True)
    with open(rec, "wb") as f:
        f.write(bytes(p))
    return 0


def main_site(out_dir: str, case: str) -> int:
    """Seed for the IGNITE TORCH / DESCEND / KLIMB gate: one sector, one player, one tile."""
    (hi, lo), (px, py), tile, torches, hp, gold = SITE_CASES[case]

    content = os.path.join(out_dir, "content")
    save = os.path.join(out_dir, "save")
    os.makedirs(content, exist_ok=True)
    os.makedirs(save, exist_ok=True)

    def write_sector(digits, monx=None):
        grid = bytearray([FLOOR]) * 4096
        if lo in (4, 5):
            # CP7: a tower or a dungeon is not a 64x64 sector. asm/68C0 indexes the
            # same 4096 bytes as 16 pages of 16x16 with [DS:25] picking the page, and
            # the seed cannot set [DS:25] (it is below the saved record), so floor 0
            # means the cell is py*16 + px. Writing the 64-wide index here would put
            # the ladder somewhere the engine no longer looks.
            grid[py * 16 + px] = tile
        else:
            grid[py * 64 + px] = tile
        with open(os.path.join(content, f"mapx{digits}"), "wb") as f:
            f.write(bytes(grid))
        with open(os.path.join(content, f"monx{digits}"), "wb") as f:
            f.write(monx if monx is not None else bytes(256))

    write_sector(f"{hi}{lo}", cleric_monx(px, py) if case.startswith("tribute") else None)
    # Leaving a site zeroes the ones digit, so the descend-exit case lands in sector `hi0`
    # and the loader has to find something there or the run dies on a missing file rather
    # than on the behaviour under test.
    write_sector(f"{hi}0")
    # The town mouth leads to sector `<hi>2` (site digit 2). Give it a guard, or the entry
    # test proves nothing: an empty sector has no creature to fail to stamp.
    if case == "enter_town":
        write_sector(f"{hi}2", guard_monx(0x1f, 0x3e))
        # Wall the guard in. The creature sweep runs after every command and restamps when
        # anything MOVED, so a guard that wanders on the first turn hides the entry defect
        # entirely: the gate passes with or without the fix, which is the shape of gate this
        # project has been bitten by twice. Sealed on all four sides with 0x00 (water to a
        # foot stamp) it cannot move, so the only thing that can stamp it is the load path.
        gp = os.path.join(content, f"mapx{hi}2")
        grid = bytearray(open(gp, "rb").read())
        gx, gy = 0x1f, 0x3e
        for dx, dy in ((1, 0), (-1, 0), (0, -1)):
            grid[(gy + dy) * 64 + (gx + dx)] = 0x00
        with open(gp, "wb") as f:
            f.write(bytes(grid))

    p = bytearray(384)
    p[0:7] = b"SITETST"
    p[0x49 - 0x36] = hi
    p[0x4A - 0x36] = lo                      # the ones digit, which IS the site kind
    p[0x5A - 0x36] = px                      # entry cell, where a site exit drops you
    p[0x5B - 0x36] = py
    p[0x64 - 0x36] = torches                 # DS:64 torches, packed decimal
    if hp is not None:                       # DS:51 high byte, DS:52 low byte
        p[0x51 - 0x36] = hp >> 8
        p[0x52 - 0x36] = hp & 0xFF
    if gold is not None:                     # DS:58 high byte, DS:59 low (see state.h's wart)
        p[0x58 - 0x36] = gold >> 8
        p[0x59 - 0x36] = gold & 0xFF
    # No [DS:25]: it is below the saved record, so the floor always starts at whatever the
    # loader leaves there. See the SITE_CASES comment.
    rec = os.path.join(save, "PLAYER", "PLAYER")   # saves/<slot>/PLAYER; core/slot.h
    os.makedirs(os.path.dirname(rec), exist_ok=True)
    with open(rec, "wb") as f:
        f.write(bytes(p))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: cp6_commerce_seed.py DIR CASE", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1], sys.argv[2]))
