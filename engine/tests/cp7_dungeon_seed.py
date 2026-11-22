#!/usr/bin/env python3
"""cp7_dungeon_seed.py — CP7 gate seed: a dungeon you can actually walk.

The shipped mapx25/35/45 are byte-identical real mazes, and a real maze is the wrong
fixture for a gate: which cell a corridor happens to be in is a fact about the art, so
an assertion against it says nothing about the engine. This seed writes a dungeon to
answer the question instead. Every cell below is placed by address, so every assertion
in run_cp7.sh names a cell and knows what is in it.

Layout, floor 0 (page 0 = bytes 0..255, cell (x,y) at y*16 + x):

      x:  3  4  5  6  7  8  9
  y=3     #  #  #  #  #  #  #
  y=4     #  #  U  #  #  #  #     U = 0x10 up ladder
  y=5     #  #  .  .  C  .  #     . = 0x00 floor, C = 0x40 chest
  y=6     #  #  #  #  #  #  #
  y=7     #  #  #  #  #  #  #
  y=8     #  #  #  #  D  #  #     D = 0x20 down ladder, reached by turning south at the chest

Everything outside those cells is 0x80, solid rock, which is what 42% of the shipped
mapx35 is too. The player enters at (5,5) FACING EAST, which is what the original does
(captures/spice86/dungeon-35-live reads DS:00..03 = 05 05 01 00), so the first ADVANCE walks
down the y=5 corridor rather than into the wall to the north.

Floor 1 (page 1 = bytes 256..511) is a different room with its own down ladder at
(5,5), so DESCEND twice lands somewhere distinguishable from DESCEND once.

The player record carries full food and a fistful of torches, because asm/12DB charges
ten food per dungeon turn and a zero-food seed dies on the first step — which is
correct behaviour and a terrible test fixture.
"""
from __future__ import annotations

import os
import shutil
import sys

# The seeders share one offset source: state.h, read at runtime by u2_seed. No DS address
# is written down in this file.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402

ROCK  = 0x80
FLOOR = 0x00
UP    = 0x10
DOWN  = 0x20
CHEST = 0x40

# The mouth tile in the world sector. site_for_tile() switches on tile >> 2, and 0x24
# is the dungeon mouth (site digit 5) — the same value CP6's enter_dungeon case uses.
MOUTH = 0x24

ENTRY = (4, 4)   # the world-map cell the mouth sits on
GUARD = (5, 4)   # one cell north of the arrival cell, so the first ATTACK has a target
WALKER = (3, 5)  # west end of the corridor, off every other test's path

# Placed relative to the ARRIVAL cell, not the world-map cell: asm/342A gives the
# street sites (0x1f, 0x3e) and the tower and dungeon mouths both arrive at (5,5),
# which is what site_for_tile() carries.
FLOOR0 = {
    (5, 3): UP,
    (7, 5): CHEST,
    (7, 7): DOWN,
}
# Floor 1 mirrors the shaft: the same cell that was the way down is the way back up,
# which is what makes the gate's D-then-K round trip return to level 00 instead of
# descending again.
FLOOR1 = {
    (7, 7): UP,
    (5, 5): FLOOR,
}


def page(cells: dict) -> bytearray:
    """One 16x16 page of rock with the listed cells carved out."""
    p = bytearray([ROCK]) * 256
    for x in range(16):
        for y in range(16):
            if 0 < x < 15 and 0 < y < 15:
                p[y * 16 + x] = FLOOR
    for (x, y), v in cells.items():
        p[y * 16 + x] = v
    return p


def dungeon() -> bytes:
    """4096 bytes: sixteen pages of 16x16, plus the 128-byte tail the loader ignores."""
    grid = bytearray()
    grid += page(FLOOR0)
    grid += page(FLOOR1)
    while len(grid) < 4096:
        grid += page({})
    grid += bytes(128)
    return bytes(grid)


def world() -> bytes:
    """Sector 20: an open field with one dungeon mouth under the entry cell."""
    g = bytearray([0x08]) * 4096
    g[ENTRY[1] * 64 + ENTRY[0]] = MOUTH
    return bytes(g) + bytes(128)


def player(cast: bool = False) -> bytes:
    p = u2_seed.PlayerRecord()
    p.set_name("DGTST01")
    p.set_at(2, 0)                         # sector 20; ones digit 0 = the world map
    p.set_raw("entry_x", ENTRY[0])
    p.set_raw("entry_y", ENTRY[1])
    p.set_intent("hp", 9999)               # so a trap cannot end the run
    p.set_intent("food", 99999)
    p.set_intent("gold", 9999)
    # Stats. A zeroed character never lands a blow, which is the same reason CP5's fight
    # scripts had to stop running --play-map on a blank state. The bytes are packed
    # decimal, not counts: asm/2E3D's damage formula reads the raw byte, and asm/2BDA's hit
    # test is `(rand >> 1) < [DS:4C]`. 0x99 makes every swing land, which is what a gate
    # needs; a "realistic" 9 would miss 19 times in 20 and the gate would be a coin toss.
    p.set_stat("str", 0x20)
    p.set_stat("agi", 0x99)                # every scripted swing lands
    p.set_intent("readied", 1)             # so damage is non-zero
    p.set_intent("torches", 50)
    p.set_intent("keys", 5)
    p.set_intent("tools", 5)               # so the trap's escape arm is reachable
    if cast:
        # The CAST cases need a caster, and one flag carries both halves because the gate
        # order is what most of those cases assert: the wand test runs before the stock test,
        # so a seed with spells and no wand proves the refusal order, and a seed with both
        # proves everything after it. A staff rather than a wand, because asm/3139 sums the
        # two bytes and the gate should not be able to pass only for the item it is named on.
        p.set_gear("items_held", 2, 1)     # DS:D8, the STAFF slot
        for i in range(1, 10):
            p.set_gear("spell_counts", i, 1)   # one charge each, packed decimal
    return bytes(p.data)


def monx() -> bytes:
    """Two authored creatures: one the first ATTACK will look for, one that has to walk.

    asm/147F fills EMPTY slots, so an authored creature survives the spawner and
    stays exactly where it was put. The loader's layout is x at 0..31, y at 32..63,
    hp at 64..95, type at 96..127, kind at 128..159, role at 160..191.

    Slot 2 exists for the CP7 movement gate and is the reason this is not one creature.
    It sits at the far end of the y=5 corridor with enough HP to pursue, and the sweep
    must bring it down the corridor toward (5,5) a cell per turn. Slot 1 cannot do that
    job: it is already adjacent to the arrival cell, so its first pursuit step lands on
    the player and asm/1151 declines the move. A gate written on slot 1 would therefore
    pass with the sweep deleted, which is the shape of gate this project has been bitten
    by twice and refuses now.
    """
    m = bytearray(256)
    m[1] = GUARD[0]
    m[32 + 1] = GUARD[1]
    m[64 + 1] = 0x10                 # hp, the low end of the asm/147F envelope
    m[96 + 1] = 0x01                 # dungeon type 1
    m[128 + 1] = 0x00

    m[2] = WALKER[0]
    m[32 + 2] = WALKER[1]
    m[64 + 2] = 0x40                 # hp, well above the flee threshold at 0x0f
    m[96 + 2] = 0x02                 # dungeon type 2
    m[128 + 2] = 0x00                # floor 0, the page the player arrives on

    # CP7_GREMLIN=1 authors a third creature orthogonally adjacent to the arrival cell,
    # for the asm/12DB contact gate. It is opt-in through the environment rather than
    # always-on because a contact roll consumes the RNG, and every other case in this file
    # shares the seed: an always-on neighbour would shift the stream under the movement,
    # chest and floor assertions and make them depend on a 1-in-4 roll.
    #
    # Type 5 is the gremlin, not type 2, on purpose: the torch arm would blow the light out
    # mid-run and darken frames that other assertions read, and type 7 writes the sleep
    # counter. Stealing one food from a character carrying 9999 is the one contact effect
    # that cannot perturb anything else.
    if os.environ.get("CP7_GREMLIN"):
        m[3] = 4                     # one cell west of the arrival cell (5,5)
        m[32 + 3] = 5
        m[64 + 3] = 0x40             # above the flee threshold, so it pursues
        m[96 + 3] = 0x05             # dungeon type 5 == the gremlin arm
        m[128 + 3] = 0x00            # floor 0, the player's floor
    return bytes(m)


def main(out_dir: str, cast: bool = False) -> int:
    content = os.path.join(out_dir, "content")
    save = os.path.join(out_dir, "save")
    os.makedirs(content, exist_ok=True)
    os.makedirs(save, exist_ok=True)

    for name, blob in (("mapx20", world()), ("mapx25", dungeon()),
                       ("monx20", bytes(256)), ("monx25", monx())):
        with open(os.path.join(content, name), "wb") as f:
            f.write(blob)

    # The stroke table is ART and is never committed, so the gate borrows the player's
    # own copy the same way the tile tests do. Absent is a legitimate state — the engine
    # draws no sprites and says so — and run_cp7.sh asserts BOTH halves, so a sprite
    # pass that silently stopped drawing cannot pass by matching the no-art case.
    here = os.path.dirname(os.path.abspath(__file__))
    for cand in (os.path.join(here, "..", "..", "original", "monsters"),):
        if os.path.exists(cand):
            shutil.copyfile(cand, os.path.join(content, "monsters"))
            break

    rec = os.path.join(save, "PLAYER", "PLAYER")   # saves/<slot>/PLAYER; core/slot.h
    os.makedirs(os.path.dirname(rec), exist_ok=True)
    with open(rec, "wb") as f:
        f.write(player(cast))
    return 0


if __name__ == "__main__":
    argv = sys.argv[1:]
    cast = "--cast" in argv
    argv = [a for a in argv if a != "--cast"]
    if len(argv) != 1:
        print("usage: cp7_dungeon_seed.py [--cast] DIR", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(argv[0], cast))
