#!/usr/bin/env python3
"""
board_stamp.py — the drawn player art must follow DS:13, not a constant.

The staging buffer holds DS:13 at the player's cell and staging halves to art, so the
figure drawn at the player's position is `player_stamp >> 1`. On foot a Fighter's stamp
is 0x78, which halves to art 60 -- and the engine passed the literal 60 to both map
draws. Invisible for a Fighter on foot, which is every capture and every seeded
character this project has ever used, and immediately wrong the moment anything boards
anything: `BOARD ROCKET` set DS:13 to 0x28 and the screen kept drawing a person.

Owner-reported 2026-11-10 while playtesting the rocket: "when i board a boat it shows
me, not the boat ... i hit B to board and it still shows me".

This is the same shape as foot_stamp.py, one layer along: a constant that is correct for
the single case anyone tested. That file is the precedent for gating it rather than just
fixing it.

Why a source gate and not a pixel test: the art value is computed inside the frame draw,
and there is no seam that reports which art landed on a cell. Asserting the two call
sites refuse the constant, and asserting the mapping the expression encodes, is what
actually goes red if the constant comes back. The mapping half is derived from the mode
constants' own tile comments rather than hardcoded here, so it is a check and not a
restatement of the fix.

Control (see the bottom of this file): reintroducing U2_OVERWORLD_PLAYER_ART at either
map draw turns leg 1 red. Run it that way before believing it.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src", "game", "overworld.c")
HDR = os.path.join(HERE, "..", "src", "game", "overworld.h")

fails = []


def check(cond, msg):
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def read(path):
    with open(path) as fh:
        return fh.read()


# ---- leg 1: neither map draw may pass the constant ------------------------------
src = read(SRC)

draws = re.findall(r"u2_frame_draw_view(?:_ega)?\(([^;]*);", src, re.S)
check(len(draws) == 2, f"found {len(draws)} map draw calls, expected 2 "
                       "(if the draw moved, this gate must move with it)")

for i, body in enumerate(draws, 1):
    check("U2_OVERWORLD_PLAYER_ART" not in body,
          f"map draw {i} passes a stamp-derived art, not the hardcoded constant")

# The constant must still exist and still be used by the panel, which is deliberately
# left alone: it is a different routine with no evidence behind it. Silently deleting
# it would hide that question rather than record it.
check("U2_OVERWORLD_PLAYER_ART" in src,
      "the panel still names the constant, so the open question stays visible")

# ---- leg 2: the mapping the expression encodes ----------------------------------
hdr = read(HDR)
modes = re.findall(r"#define\s+U2_MODE_(HORSE|SHIP|PLANE|ROCKET)\s+(0x[0-9A-Fa-f]+)u?"
                   r"[^\n]*tile\s+(0x[0-9A-Fa-f]+)", hdr)
check(len(modes) == 4, f"found {len(modes)} vehicle mode+tile pairs in overworld.h, "
                       "expected 4 (the header's own comments are the evidence)")

for name, stamp_s, tile_s in modes:
    stamp = int(stamp_s, 16)
    tile = int(tile_s, 16)
    # tile is the MAP byte; map>>1 is the staging byte; staging>>1 is the art.
    check((stamp >> 1) == (tile >> 2),
          f"{name}: stamp {stamp_s} >> 1 == art {tile >> 2:#x}, "
          f"which is what tile {tile_s} >> 2 says")

# Foot: the old constant must equal what the expression now produces for a Fighter,
# otherwise this change altered the case that was already working.
foot = int(re.search(r"#define\s+U2_OVERWORLD_PLAYER_ART\s+(\d+)", hdr).group(1))
check(0x78 >> 1 == foot,
      f"on foot 0x78 >> 1 == {foot}, so the Fighter case is unchanged")

# ---- leg 3: the expression is really in the source ------------------------------
check(re.search(r"player_art\s*=\s*ctx->state\.player_stamp\s*>>\s*1", src) is not None,
      "overworld.c derives the art from ctx->state.player_stamp")

if fails:
    print(f"\nBOARD STAMP: FAIL ({len(fails)})")
    sys.exit(1)
print("\nBOARD STAMP: PASS")
