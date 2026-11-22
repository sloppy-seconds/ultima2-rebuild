#!/usr/bin/env python3
"""Emit a synthetic content tree for the demo gate: an overworld sector with one
door, and the village it opens onto.

Everything here is engine-authored filler, the same rule `cp5_fight_seed.py`
follows, so a demo can be recorded and replayed in CI without touching the
player's own game files (docs/demo-record-playback.md D9: the gate builds its own
fixture and no committed game data).

The geometry is the one `tests/play_to/cp5-town.keys` records: cell (31,31) of
sector 70 carries tile 0x14, `0x14 >> 2 == 5`, and asm/33C3 sends that digit to
the village arm, which selects sector 71 and arrives at (31,62). Walking south
from there crosses the sector edge and comes back into 70, which is the crossing
W13 needs: a demo that leaves one mode and returns to it.

Usage: demo_seed.py DIR
"""
import os
import sys

# The seeders share one offset source. Every file in this directory that builds a save
# imports u2_seed this way rather than repeating offsets, so state.h is the only place a
# DS address is written down.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402

GRASS = 0x08          # measured passable on foot (docs/passability-findings.md F10)
DOOR = 0x14           # the site tile: 0x14 >> 2 == 5, the village arm
EDGE = 64


def sector(path: str, door_at=None) -> None:
    grid = bytearray([GRASS]) * (EDGE * EDGE)
    if door_at is not None:
        x, y = door_at
        grid[y * EDGE + x] = DOOR
    with open(path, "wb") as f:
        f.write(bytes(grid))
        # The shipped sectors carry a 128-byte tail that is never read; the engine
        # reads 4096 either way, and writing the tail is what makes a short read
        # impossible to confuse with a full one.
        f.write(bytes(128))


def main(out_dir: str) -> int:
    content = os.path.join(out_dir, "content")
    save = os.path.join(out_dir, "save")
    os.makedirs(content, exist_ok=True)
    os.makedirs(os.path.join(save, "PLAYER"), exist_ok=True)

    sector(os.path.join(content, "mapx70"), door_at=(31, 31))
    sector(os.path.join(content, "mapx71"))
    for name in ("monx70", "monx71"):
        with open(os.path.join(content, name), "wb") as f:
            f.write(bytes(256))

    # The character: a named body with enough HP and food to survive whatever the gate
    # throws at it, standing in sector 70 with its arrival cell on the door. Every field
    # is named here and no offset appears, because u2_seed reads the offsets from
    # state.h at runtime — see the module's header for why that is the rule.
    p = u2_seed.PlayerRecord()
    p.set_name("GATEDE")
    p.set_at(7, 0)                     # sector 70; the second digit is the site kind
    p.set_intent("hp", 9997)
    p.set_intent("food", 999999)
    p.set_raw("entry_x", 31)           # the arrival cell: the door
    p.set_raw("entry_y", 31)
    p.set_stat("str", 0x40)
    p.set_stat("agi", 0x80)            # a hit is certain
    p.write(os.path.join(save, "PLAYER", "PLAYER"))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: demo_seed.py DIR", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
