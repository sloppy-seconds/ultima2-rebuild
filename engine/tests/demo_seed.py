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

    p = bytearray(384)
    p[0:6] = b"GATEDE"[:6]            # name at DS:36
    p[0x49 - 0x36] = 7                # sector first digit
    p[0x4A - 0x36] = 0                # sector second digit: the world map
    p[0x51 - 0x36] = 0x99             # HP high BCD byte
    p[0x52 - 0x36] = 0x97             # HP low  -> H.P.=9997
    p[0x53 - 0x36] = 0x99             # food, three bytes
    p[0x54 - 0x36] = 0x99
    p[0x55 - 0x36] = 0x99
    p[0x5A - 0x36] = 31               # entry x: the door cell
    p[0x5B - 0x36] = 31               # entry y
    p[0x4B - 0x36] = 0x40             # strength
    p[0x4C - 0x36] = 0x80             # agility: a hit is certain
    with open(os.path.join(save, "PLAYER", "PLAYER"), "wb") as f:
        f.write(bytes(p))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: demo_seed.py DIR", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
