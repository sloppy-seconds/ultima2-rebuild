#!/usr/bin/env python3
"""
tile_rules.py — does the substitute art conform to the rules the spec states?

Why this exists
---------------
Arts `13` and `14` were named from a guess about shape and the wrong noun got baked into
generated art and a legal record before anyone looked. The answer is not "look harder
next time", it is: when the owner states a requirement, encode it so the next redraw is
checked against the requirement instead of against nobody's memory.

THE ONE RULE THAT KEEPS THIS CLEANROOM-DEFENSIBLE
-------------------------------------------------
This checker verifies **conformance to a stated rule**, never similarity to the original
art. That distinction is the whole game:

* A rule is a *class* of patterns described in the abstract — "single-pixel dashes, ink
  confined to odd columns, inked rows periodic with period 4". Many different drawings
  satisfy it. Writing one is an act of authorship.
* A pixel diff against `tmp/tiles/` would be a copying mechanism wearing a lab coat. It
  would take the original's exact 32 coordinates as the target and reward whoever
  reproduced them, which is precisely what § 2 of the spec forbids and what this file
  must never become.

So: rules are authored from observation of *behaviour* ("a field of these must read as
one continuous wave, not per-tile static") and expressed as structure. The originals are
never an input here. `--src` reads only the cleanroom SVGs.

Pending ledger
--------------
A rule added before the art is redrawn would fail immediately, and a gate that is red
between "specified" and "redrawn" gets muted rather than fixed. So non-conformant tiles
listed in PENDING_REDRAW are reported as debt and do not fail the build. Any tile NOT in
the ledger that breaks its rule is a hard failure, and a pending tile that has quietly
become conformant is reported as ready to promote. Debt stays visible; regressions stay
red.

Usage:
    python3 tools/tile_rules.py                 # gate mode, exit 1 on violation
    python3 tools/tile_rules.py --verbose       # per-tile detail
"""
from __future__ import annotations

import argparse
import os
import re
import sys

INK_NON_BLACK = True  # any colour other than #000000 counts as ink

# Rules, one entry per art index. Each is derived from what the owner said the tile must
# read as, expressed as structure. Cite the spec amendment in the comment.
RULES = {
    0x00: {
        # Spec amendment A2. Owner, 2026-10-22: "the original the dashes still look like
        # wavy lines and the ends of the lines line up so when two tiles are left and right
        # of each other they appear to be one long dashed line." The mechanism that
        # produces that, once measured, is regularity rather than long strokes: a field of
        # these must be periodic across the seam in both axes.
        "max_h_run": 1,        # dashes are single pixels; no 2px blobs
        "column_parity": 1,    # ink only on odd columns, so horizontal tiling keeps phase
        "row_period": 4,       # inked rows repeat with period 4, cyclically
        "density": (0.09, 0.16),
    },
    0x01: {
        # Spec amendment A3. Owner, 2026-10-22: "the wave lines are horizontal in the
        # original, kind of looks like four or five waves crashing with the white dots
        # looking a bit like whitewash, vertically the lines should appear to line up at
        # the edges." Measured: ink sits on even rows only (cyclic period 2) and odd
        # columns only, one pixel at a time, and white OUTNUMBERS cyan about 2:1 -- the
        # foam is the feature. The current substitute inverts that ratio (23% white)
        # and is 2.4x too dense.
        "max_h_run": 1,
        "column_parity": 1,
        "row_period": 2,       # horizontal lines that continue across the vertical seam
        "density": (0.06, 0.13),
        "colour_share": {"#FFFFFF": (0.50, 1.00)},   # whitewash must dominate
    },
    0x02: {
        # Spec amendment A4. Owner, 2026-10-22: art 02 is OK as drawn. Rule recorded
        # anyway, and one it already satisfies, so a future redraw cannot quietly make
        # the calmest tile in the set busy. No parity requirement: the original stipple
        # sits on odd columns with column period 8, the substitute scatters, and the
        # owner judged that difference acceptable at 4% density.
        "max_h_run": 1,
        "density": (0.03, 0.06),
    },
    0x03: {
        # Spec amendment A5. Owner, 2026-10-22, dictating the design outright:
        # "break the tile into two columns and three rows, all background black. In the
        # first column there's a blue circle top row, a black second row, and a blue
        # circle bottom row; the second column has a blue dot in the second row."
        # Verified against the original: three rounded crowns at rows 0-4/cols 1-7,
        # rows 10-14/cols 1-7, and rows 5-9/cols 9-15 -- exactly that 2x3 layout. The
        # substitute is scattered 2x2 confetti instead, 19% density against 32%.
        # This is a LAYOUT requirement, not a pixel trace: many drawings satisfy it, so
        # a clean implementer can author one without ever seeing the original.
        "components": {
            "count": 3,
            "cells": {("left", "top"), ("left", "bottom"), ("right", "middle")},
            "min_area": 12,     # each must read as one object, not confetti
            "min_width": 4,
        },
        "density": (0.26, 0.38),
        "colour_share": {("#55FFFF", "#00AAAA"): (0.90, 1.00)},
    },
    0x04: {
        # Spec amendment A6. Owner, 2026-10-22: "it should look like overlapping zigzagged
        # white lines that represent mountain peaks, or randomly sized diamonds stacked on
        # top of each other so you can only see the top corners of the diamond."
        # Both of those are the same structure, and the original has it: no horizontal
        # adjacency at all (h-run 1) but diagonals of 6 and 9 px -- two families of
        # parallel lines crossing, which is what draws the stacked diamonds. The
        # substitute has h-run 4 and a down-left diagonal of 1: blobs, not a crosshatch.
        # Byte 0x10 is one of only two bytes that stop a player on foot, so "impassable"
        # has to read instantly; a fine all-over lattice does that, scattered chunks do not.
        "max_h_run": 1,
        "min_diag_run": 4,     # both diagonal families must actually run, i.e. crosshatch
        "row_period": 1,       # every row carries ink -- an all-over mass, no gaps
        "col_period": 1,
        "density": (0.18, 0.28),
        "colour_share": {("#FFFFFF",): (0.90, 1.00)},
    },
    0x05: {
        # Spec amendment A7. Owner, 2026-10-22: "5 white rings that look a bit like the 5
        # side of a dice but slightly crooked." That is a quincunx, and the original is
        # one: ring centres at (3.5,2.5) (10.5,3.5) (6.5,7.5) (3.5,11.5) (11.5,11.5) --
        # four corners plus a centre, the top pair offset a row from each other, which is
        # the crookedness. All five are hollow and all five are exactly 8px.
        # The substitute has the right count but one component is a stray single pixel and
        # the four real rings are 12px, so it reads as "four rings and a dot".
        "components": {"count": 5, "min_hollow": 5, "size_spread": 3},
        "quincunx": 0.20,      # centres within 20%% of the five dice-5 positions
        "density": (0.10, 0.20),
        "colour_share": {("#FFFFFF",): (0.90, 1.00)},
    },
}

# Tiles whose rule is agreed but whose art has not been redrawn yet. Reported as debt,
# not failure. Remove an entry the same commit that redraws the tile.
PENDING_REDRAW = {0x00, 0x01, 0x03, 0x04, 0x05}

# Spec amendment A8 -- the letter tiles, as a batch. Owner, 2026-10-22: "the letters are
# close, they're missing a long [line] across the top and bottom of the tile." Measured:
# EVERY original letter tile has row 0 and row 15 fully inked (16/16) with rows 1 and 14
# clear, i.e. a sign board: rule, gutter, glyph, gutter, rule. The substitute has rows 0
# and 15 empty and the glyph running to the edges. The spec made the rules "optional
# decoration, not a requirement", so the implementer was entitled to omit them; the owner
# has now made them mandatory. Arts 20-2F are A-P and 31-39 are R-Z; art 30 is the wall
# tile where Q would fall and is not a letter.
LETTER_ARTS = list(range(0x20, 0x30)) + list(range(0x31, 0x3A))
for _a in LETTER_ARTS:
    RULES[_a] = {
        "full_rows": [0, 15],   # the sign-board rules
        "empty_rows": [1, 14],  # gutters that separate rule from glyph
        "colour_share": {("#FFFFFF",): (0.90, 1.00)},
    }
PENDING_REDRAW |= set(LETTER_ARTS)

# Spec amendment A9 -- art 30 is a MOONGATE, not a wall. Owner, 2026-10-22: "art 30 is I
# believe the moongate, so it's a square that's missing the bottom line; in that square are
# alternating white and black bars." The pixels agree: rows 0-1 solid across, left and
# right borders, NO bottom row, interior columns alternating with period 2.
# This re-identifies a cold case. The spec had guessed "wall / bulk structure"; the tsv
# never did, it says "vertical bars, no letter". And byte 0xC0 is the tile that threw a
# player y=20->57 in a probe (docs/archive/handoff-2026-10-21-evening.md item 6), which a
# teleporter explains and a wall does not. Grade: recalled (owner) corroborated by
# observed (probe). Proving it needs one capture of stepping onto a 0xC0 tile.
RULES[0x30] = {
    "full_rows_min": {0: 14, 1: 14},   # the arch top
    "row_fill_max": {15: 12},          # the opening: no bottom rule
    "x_period": {"region": (2, 2, 13, 16), "period": 2, "min_match": 0.80},
    "colour_share": {("#FFFFFF",): (0.90, 1.00)},
}
PENDING_REDRAW.add(0x30)

# Spec amendment A10 -- art 06 is an EMBLEM, not masonry. Owner, 2026-10-22: it "looks like
# the apple command key or saint johns arms", and of the two the command-key looped square
# is chosen. The owner believes it represents a town or castle in the game, which the
# bytes support: byte 0x18 rejects walking, and towns are already established as impassable
# with T (talk) still working through them (docs/passability-findings.md). Grade: recalled
# (owner) corroborated by observed (rejection).
# The old brief -- "masonry: a block of squared masonry, regular and man-made" -- was a
# misidentification, so any substitute drawn from it was going to be wrong however well
# executed. The current edge-to-edge grid is a competent rendering of a wrong instruction.
# The rule below is topology and symmetry, not pixels, so it stays cleanroom-safe.
RULES[0x06] = {
    "single_component": True,
    "symmetry": "4fold",
    "holes": {"count": 5, "corner_count": 4, "centre": True, "corner_spread": 2},
    "gutter": 2,          # clear margin so tiled emblems stay separate objects
    "density": (0.22, 0.34),
    "colour_share": {("#FFFFFF",): (0.90, 1.00)},
}
PENDING_REDRAW.add(0x06)

# Spec amendment A11 -- art 07 stays a solid rook silhouette. Owner, 2026-10-22, on seeing
# both renders: "looks like a castle or a rook from chess" -- i.e. the ORIGINAL is right and
# the substitute is wrong. The spec's "a single upright tower" was read as "outline the
# silhouette of a tower", producing a hollow body with windows and, worse, THREE pieces:
# the top-right prong is detached and floats. Same defect class as the stray pixel on the
# rings -- fine at 1x, falls apart in a 2x2.
RULES[0x07] = {
    "single_component": True,
    "max_holes": 0,        # a silhouette is filled; an outline is not this tile
    "top_runs": 2,         # the two prongs of the crenellated top
    "widest_at_base": True,
    "density": (0.28, 0.42),
    "colour_share": {("#FFFFFF",): (0.90, 1.00)},
}
PENDING_REDRAW.add(0x07)

# Spec amendment A12 -- art 08 is a gateway FRAME with corner tabs, not a slab on posts.
# Owner, 2026-10-22, describing the original: "a square-like outline or gateway with an
# opening at the bottom center ... at all four corners the lines extend slightly outward to
# form small, blocky, square tabs/projections ... commonly represents an enclosure, gated
# structure, town wall, or entrance/gateway in classic RPG map tile sets."
# The original matches that: a frame inset one pixel at the mid-rows, blocky tabs at all
# four corners, feet flaring outward, and the bottom centre open. The substitute drew a
# 4-row solid slab on two thin posts running to the bottom edge -- no tabs, no feet, no
# gutter, and 36%% density against 28%%.
RULES[0x08] = {
    "single_component": True,
    "bottom_runs": 2,      # the opening at the bottom centre
    "corner_ink": 2,       # blocky tabs present at all four corners
    "centre_gap_rows": True,  # mid-rows are open between the posts, not filled
    "density": (0.24, 0.32),
    "colour_share": {("#FFFFFF",): (0.90, 1.00)},
}
PENDING_REDRAW.add(0x08)

# Spec amendment A13 -- art 09 is the mountain lattice WITH an arch, and the arch is the
# whole point: it is the entrance. The original is art 04's crosshatch (diagonals 6 and 9)
# plus two legs and an open centre at rows 9-15. The substitute has a weak lattice
# (diagonals 4 and 3) and seals the bottom with a solid bar -- the exact opposite of an
# opening, and the kind of error that makes a pass look like a wall.
RULES[0x09] = {
    "min_diag_run": 4,          # the mountain texture must still crosshatch
    "bottom_centre_gap": 2,     # the pass: open centre at the bottom, not a sealed bar
    "row_fill_max": {15: 10},   # no solid bottom line
    "density": (0.22, 0.30),
    "colour_share": {("#FFFFFF",): (0.90, 1.00)},
}
PENDING_REDRAW.add(0x09)

# Spec amendment A14 -- art 0A is a SIGNPOST. Owner, 2026-10-22: "its a sign post ... so if
# the base is thinner and the crossbar is thicker it'll work."
# Note the loop this closes: the name "signpost" had been wrongly attached to art 13, which
# is the plane (amendment A1). This is the tile that actually earns the name.
# Original: ONE thick crossbar high up (rows 3-7, 10px wide) on a 2px stem with a base flare
# -- a Latin-cross signpost. Substitute: a thin 2-row bar at the top AND a second bar near
# the foot, which reads as a patriarchal cross, plus a heavier base.
RULES[0x0A] = {
    "single_component": True,
    "crossbars": {"count": 1, "min_thickness": 4},
    "density": (0.28, 0.38),
    "colour_share": {("#FFFFFF",): (0.90, 1.00)},
}
PENDING_REDRAW.add(0x0A)

# Spec amendment A15 -- art 0B is a SEA MONSTER IN WATER, not an arched doorway.
# Owner, 2026-10-22: "the original is the water tile with a sea monster say like the loch
# ness monster so you can see the body sticking out of the water on the left, and on the
# right his neck head and open mouth."
# Measured, with colours separated: MAGENTA is the monster -- exactly 2 components, a 28px
# neck-and-head on the right spanning rows 5-13 with a gap that reads as the open mouth, and
# a 21px body low-left. WHITE is the water -- 20 components totalling 22px, i.e. a sparse
# single-pixel stipple of the same family as arts 00-02.
# ROOT CAUSE of the bad substitute: tile-art.tsv says "magenta field with a white arch and a
# white mound" -- the two colours are SWAPPED. It calls the monster the field and the water
# the shapes. The spec inherited the inversion as "an arch set in a filled field", and the
# implementer then faithfully drew a 71%% solid magenta slab with an arch punched out. A
# competent rendering of an inverted description.
RULES[0x0B] = {
    "colour_components": {("#FF55FF", "#AA00AA"): 2},   # body, and neck+head
    "stipple": {("#FFFFFF",): {"min_components": 8, "max_area": 3}},
    "colour_share": {("#FF55FF", "#AA00AA"): (0.55, 0.80), ("#FFFFFF",): (0.15, 0.45)},
    "density": (0.20, 0.34),
}
PENDING_REDRAW.add(0x0B)

# Spec amendment A16 -- the MONSTER SPRITE set, as a shared brief rather than eleven
# individual ones. See docs deltas D9/D11: arts 0C-0F, 10, 18, 1A and 3C-3F are the
# overworld monster sprites (art = monster_type >> 2, tools/monster_shapes.py), not the
# people the spec named them after. The spec's "Creatures" section therefore needs
# splitting: these are enemies, and one of them (3F, type 0xFC) is the item-drop monster.
#
# Bands below are MEASURED from the eleven originals, not invented: components 1 (only 0E is
# 2), density 20.3-48.4%, aspect h/w 1.00-1.56, sprite height 14-16 of 16 rows. The point of
# a shared rule is that a monster must read as an upright figure that fills its cell -- not a
# small icon floating in it, and not a wide flat blob.
MONSTER_ARTS = [0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x18, 0x1A, 0x3C, 0x3D, 0x3E, 0x3F]
for _a in MONSTER_ARTS:
    RULES[_a] = {
        "max_components": 2,
        "density": (0.18, 0.52),
        "aspect_min": 0.95,     # upright or square; never wide and flat
        "min_height": 12,       # fills the cell: a sprite, not an icon
        # Not "white": art 0E (type 56, ARMS caster) is a MAGENTA sprite, 65 magenta px
        # against 6 white. What every one of the eleven shares is that a single colour
        # dominates the figure, so that is what the rule requires.
        "dominant_colour": 0.85,
    }
PENDING_REDRAW |= set(MONSTER_ARTS)

# Spec amendment A17 -- the remaining fifteen tiles, rules derived from measurement.
# Where the owner has not yet given a brief, the rule encodes the STRUCTURE we measured and
# deliberately does not settle a contested name. A rule about topology cannot be wrong about
# what a tile means; it only constrains what a substitute may look like.
RULES.update({
    # Vehicles and objects: silhouette proportions are the discriminator here. A horse is
    # wide and low, a rocket is tall and narrow -- that difference is measurable and it is
    # what makes each readable at 16x16.
    0x11: {"max_components": 1, "density": (0.24, 0.32), "aspect_max": 0.95,
           "colour_share": {("#FFFFFF",): (0.85, 1.00)}},          # horse: low, four-legged
    0x12: {"max_components": 1, "density": (0.30, 0.40), "holes_min": 4,
           "colour_share": {("#FFFFFF",): (0.85, 1.00)}},          # ship: hull with detail
    0x13: {"max_components": 1, "density": (0.26, 0.35), "aspect_min": 1.00,
           "colour_share": {("#FFFFFF",): (0.85, 1.00)}},          # plane (proven, A1)
    0x14: {"max_components": 1, "density": (0.12, 0.20), "aspect_min": 1.30,
           "colour_share": {("#FFFFFF",): (0.85, 1.00)}},          # rocket: tall, narrow
    # 0x15: spec says "dungeon mouth"; Gemini read a shield with an S-squiggle and called it
    # a sign. Naming is contested, so the rule is structural only: two pieces, mid density.
    0x15: {"max_components": 2, "density": (0.21, 0.30), "aspect_min": 1.00,
           "colour_share": {("#FFFFFF",): (0.85, 1.00)}},
    # 0x16: spec "marker", Gemini "sword" (diagonal with a crossbar). Both agree it is a
    # small sparse mark; the density is the load-bearing property.
    0x16: {"max_components": 2, "density": (0.04, 0.09),
           "colour_share": {("#FFFFFF",): (0.85, 1.00)}},
    0x17: {"density": (0.45, 0.55), "period_x": 2, "period_y": 8},  # diagonal dot lattice
    0x19: {"max_components": 1, "density": (0.19, 0.27), "aspect_min": 1.10,
           "colour_share": {("#FFFFFF",): (0.85, 1.00)}},
    0x1B: {"max_components": 1, "density": (0.27, 0.35), "aspect_min": 1.10,
           "colour_share": {("#FFFFFF",): (0.85, 1.00)}},
    # 0x1C: MAGENTA stipple, 26 pieces. Spec says marsh/reeds, Gemini said forest and
    # explicitly rejected swamp -- but art 03 is already forest, so Gemini's label collides
    # with an established tile. Structural rule only; naming left open.
    0x1C: {"min_components": 10, "density": (0.17, 0.27),
           "dominant_colour": 0.85},
    # 0x1D is the hard one: the original is ENTIRELY black, and the spec forbids emitting
    # #000000 in a tile because black is the backdrop the engine paints. So the only
    # conforming substitute is a tile that draws nothing at all. That is a legitimate answer
    # (empty SVG, zero rects, decodes to backdrop) but it must be stated explicitly or the
    # next implementer will "fix" it by adding texture.
    0x1D: {"density": (0.0, 0.005)},
    0x1E: {"density": (0.99, 1.0), "symmetry": "4fold"},            # solid white (ice?)
    0x1F: {"max_components": 3, "density": (0.09, 0.16)},           # edge strip
    0x3A: {"max_components": 4, "density": (0.14, 0.22)},           # dark fill A
    0x3B: {"max_components": 4, "density": (0.14, 0.22)},           # dark fill B
})
PENDING_REDRAW |= set(RULES) - PENDING_REDRAW

# Tiles whose substitute already satisfies its rule are removed from the ledger so the rule
# is actually enforced on them. This list is deliberately hardcoded rather than computed from
# the SVGs: if it were derived automatically, every failing tile would classify itself as
# pending and the gate could never go red -- the vacuous-gate failure this project has already
# been bitten by once. When a redraw fixes a tile, the gate prints a NOTE naming it and the
# entry is deleted here in the same commit.
PENDING_REDRAW -= {0x02, 0x10, 0x1D, 0x1E, 0x3C}

# Revision 2 of the substitute set (cleanroom rebuild against the amended spec) fixed these
# twelve, so their rules are now enforced rather than recorded as debt.
PENDING_REDRAW -= {0x04, 0x05, 0x09, 0x0C, 0x0D, 0x0E, 0x16, 0x18, 0x1A, 0x1C, 0x3D, 0x3F}


def load_grid(path):
    """Parse a cleanroom SVG into (ink grid, colour grid) for a 16x16 cell.

    Handles BOTH fill placements: a group-level fill attribute inherited by bare <rect>
    elements (what gen/tiles.py emits), and a fill written directly on the rect (what a
    hand edit produces). An earlier version tested for `fill=` first and `continue`d, which
    silently skipped every inline-fill rect -- a hand-edited tile would parse as empty and
    pass the gate vacuously. Found by deliberately tampering with a conforming tile.
    """
    g = [[False] * 16 for _ in range(16)]
    col = [[None] * 16 for _ in range(16)]
    text = open(path).read()
    cur = None
    for line in text.splitlines():
        rm = re.search(r'<rect\b[^>]*>', line)
        if not rm:
            fm = re.search(r'fill="(#\w{6})"', line)
            if fm:
                cur = fm.group(1).upper()
            continue
        attrs = rm.group(0)
        def num(name):
            m = re.search(name + r'="(\d+)"', attrs)
            return int(m.group(1)) if m else None
        x, y, w, h = num("x"), num("y"), num("width"), num("height")
        if None in (x, y, w, h):
            continue
        fm = re.search(r'fill="(#\w{6})"', attrs)          # rect-local fill wins
        colour = fm.group(1).upper() if fm else cur
        if colour is None or colour == "#000000":
            continue                                        # black is backdrop, not ink
        for yy in range(y, min(16, y + h)):
            for xx in range(x, min(16, x + w)):
                g[yy][xx] = True
                col[yy][xx] = colour
    return g, col


def cyclic_period(rows, n=16):
    """Smallest p>0 with rows[i] == rows[(i+p)%n] for all i, else n."""
    for p in range(1, n + 1):
        if all(rows[i] == rows[(i + p) % n] for i in range(n)):
            return p
    return n


def diag_runs(g):
    """Longest contiguous run along each diagonal direction."""
    def best(dx, dy):
        hi = 0
        for y in range(16):
            for x in range(16):
                if not g[y][x]:
                    continue
                n, px, py = 1, x + dx, y + dy
                while 0 <= px < 16 and 0 <= py < 16 and g[py][px]:
                    n += 1
                    px += dx
                    py += dy
                hi = max(hi, n)
        return hi
    return best(1, 1), best(-1, 1)


def is_hollow(g, comp):
    """True if the component's bounding box contains an un-inked pixel -- a ring, not a blob."""
    _, x0, y0, x1, y1 = comp
    return any(not g[y][x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1))


QUINCUNX = ((0.25, 0.25), (0.75, 0.25), (0.50, 0.50), (0.25, 0.75), (0.75, 0.75))


def matches_quincunx(comps, tol):
    """Greedy match of component centres to the five dice-5 positions."""
    centres = [(((c[1] + c[3]) / 2.0) / 16.0, ((c[2] + c[4]) / 2.0) / 16.0) for c in comps]
    used = set()
    for qx, qy in QUINCUNX:
        best, bi = None, None
        for i, (cx, cy) in enumerate(centres):
            if i in used:
                continue
            d = max(abs(cx - qx), abs(cy - qy))
            if d <= tol and (best is None or d < best):
                best, bi = d, i
        if bi is None:
            return False
        used.add(bi)
    return True


def crossbar_bands(g):
    """Horizontal bars, excluding the base.

    A band is a run of consecutive rows wider than a stem. The band that contains the
    bottom-most inked row is the BASE, not a crossbar -- that is what a base is, the wide
    part the thing stands on. Defining it structurally rather than by a magic row offset
    keeps the rule honest: it is the same statement the owner made in words, "thinner base,
    thicker crossbar".
    """
    widths = [sum(1 for c in row if c) for row in g]
    inked = [y for y in range(16) if widths[y]]
    if not inked:
        return []
    last = inked[-1]
    bands, run = [], 0
    for y in range(16):
        if widths[y] >= 5:
            run += 1
        elif run:
            bands.append((run, y - run, y - 1))
            run = 0
    if run:
        bands.append((run, 15 - run + 1, 15))
    return [b for b in bands if not (b[1] <= last <= b[2])]


def bbox(g):
    xs = [x for y in range(16) for x in range(16) if g[y][x]]
    ys = [y for y in range(16) for x in range(16) if g[y][x]]
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def h_runs(row):
    """Number of separate inked runs in a row."""
    return sum(1 for i, c in enumerate(row) if c and (i == 0 or not row[i - 1]))


def sprite_extent(g):
    """(width, height) of the ink bounding box."""
    x0, y0, x1, y1 = bbox(g)
    return x1 - x0 + 1, y1 - y0 + 1


def colour_grid(col, hexes):
    """Boolean grid of the pixels whose fill is one of hexes."""
    if isinstance(hexes, str):
        hexes = (hexes,)
    return [[c in hexes for c in row] for row in col]


def empty_holes(g):
    """Empty components that do not touch the border -- i.e. enclosed holes."""
    out = []
    for area, x0, y0, x1, y1 in components([[not c for c in row] for row in g]):
        if x0 == 0 or y0 == 0 or x1 == 15 or y1 == 15:
            continue
        out.append((area, x0, y0, x1, y1))
    return out


def symmetric(g, kind):
    if kind != "4fold":
        return True
    h = all(g[y][x] == g[y][15 - x] for y in range(16) for x in range(16))
    v = all(g[y][x] == g[15 - y][x] for y in range(16) for x in range(16))
    d = all(g[y][x] == g[x][y] for y in range(16) for x in range(16))
    return h and v and d


def components(g):
    """8-connected ink components; each as (area, x0, y0, x1, y1)."""
    seen = [[False] * 16 for _ in range(16)]
    out = []
    for y in range(16):
        for x in range(16):
            if not g[y][x] or seen[y][x]:
                continue
            stack, cells = [(x, y)], []
            seen[y][x] = True
            while stack:
                cx, cy = stack.pop()
                cells.append((cx, cy))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < 16 and 0 <= ny < 16 and g[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            xs = [c[0] for c in cells]
            ys = [c[1] for c in cells]
            out.append((len(cells), min(xs), min(ys), max(xs), max(ys)))
    return out


def cell_of(comp):
    """Name a component's cell in the owner's 2x3 layout vocabulary."""
    _, x0, y0, x1, y1 = comp
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    col = "left" if cx < 8 else "right"
    row = "top" if cy < 16 / 3 else ("middle" if cy < 32 / 3 else "bottom")
    return col, row


def check(g, rule, col=None):
    """Return a list of violation strings; empty means conformant."""
    v = []
    inked_rows = [any(g[y]) for y in range(16)]
    # was: any(g[x] for y in range(16)) -- the generator ignored y and re-tested row x, so
    # every column check was silently a row check. Caught only because the originals failed.
    inked_cols = [any(g[y][x] for y in range(16)) for x in range(16)]

    if "max_h_run" in rule:
        best = 0
        for y in range(16):
            run = 0
            for x in range(16):
                run = run + 1 if g[y][x] else 0
                best = max(best, run)
        if best > rule["max_h_run"]:
            v.append("max_h_run: longest horizontal run is %d, limit %d"
                     % (best, rule["max_h_run"]))

    if "column_parity" in rule:
        bad = [x for x in range(16) if inked_cols[x] and x % 2 != rule["column_parity"]]
        if bad:
            v.append("column_parity: ink on columns %s, expected only %s"
                     % (",".join(map(str, bad)),
                        "odd" if rule["column_parity"] else "even"))

    if "row_period" in rule:
        p = cyclic_period(inked_rows)
        if p != rule["row_period"]:
            v.append("row_period: inked rows have cyclic period %d, expected %d"
                     % (p, rule["row_period"]))

    if "col_period" in rule:
        p = cyclic_period(inked_cols)
        if p != rule["col_period"]:
            v.append("col_period: inked columns have cyclic period %d, expected %d"
                     % (p, rule["col_period"]))

    if "components" in rule:
        want = rule["components"]
        comps = components(g)
        if len(comps) != want["count"]:
            v.append("components: found %d, expected %d"
                     % (len(comps), want["count"]))
        else:
            if "cells" in want:
                got = sorted(cell_of(c) for c in comps)
                if got != sorted(want["cells"]):
                    v.append("components: layout %s, expected %s"
                             % (got, sorted(want["cells"])))
            for c in comps:
                if c[0] < want.get("min_area", 0):
                    v.append("components: a piece is %d px, minimum %d (reads as confetti)"
                             % (c[0], want["min_area"]))
                if (c[3] - c[1] + 1) < want.get("min_width", 0):
                    v.append("components: a piece is %d px wide, minimum %d"
                             % (c[3] - c[1] + 1, want["min_width"]))
            if "min_hollow" in want:
                nh = sum(1 for c in comps if is_hollow(g, c))
                if nh < want["min_hollow"]:
                    v.append("components: %d of %d are hollow, expected >= %d -- a ring must "
                             "enclose empty space, not be a solid blob or a stray pixel"
                             % (nh, len(comps), want["min_hollow"]))
            if "size_spread" in want:
                sizes = [c[0] for c in comps]
                spread = max(sizes) - min(sizes)
                if spread > want["size_spread"]:
                    v.append("components: sizes %s spread %d, expected <= %d -- the pieces "
                             "should read as one group, not a group plus debris"
                             % (sizes, spread, want["size_spread"]))

    if "quincunx" in rule:
        comps = components(g)
        if len(comps) == rule.get("components", {}).get("count", len(comps)):
            if not matches_quincunx(comps, rule["quincunx"]):
                v.append("quincunx: centres do not sit on the four corners plus a centre")

    if "min_diag_run" in rule:
        dr, dl = diag_runs(g)
        lo = rule["min_diag_run"]
        if dr < lo or dl < lo:
            v.append("min_diag_run: diagonals run %d (down-right) and %d (down-left), "
                     "both need >= %d -- a crosshatch needs both families to actually cross"
                     % (dr, dl, lo))

    if "full_rows" in rule:
        missing = [y for y in rule["full_rows"] if not all(g[y])]
        if missing:
            v.append("full_rows: rows %s are not fully inked -- the sign-board rule is "
                     "missing, so this reads as a floating glyph not a notice"
                     % missing)

    if "empty_rows" in rule:
        dirty = [y for y in rule["empty_rows"] if any(g[y])]
        if dirty:
            v.append("empty_rows: rows %s should be clear gutters" % dirty)

    if "full_rows_min" in rule:
        for y, need in rule["full_rows_min"].items():
            got = sum(1 for c in g[y] if c)
            if got < need:
                v.append("full_rows_min: row %d has %d inked, needs >= %d" % (y, got, need))

    if "row_fill_max" in rule:
        for y, cap in rule["row_fill_max"].items():
            got = sum(1 for c in g[y] if c)
            if got > cap:
                v.append("row_fill_max: row %d has %d inked, must stay <= %d -- this tile "
                         "is open at the bottom, so a closed bottom line is wrong"
                         % (y, got, cap))

    if "x_period" in rule:
        cfg = rule["x_period"]
        x0, y0, x1, y1 = cfg["region"]
        p = cfg["period"]
        same = tot = 0
        for y in range(y0, min(y1, 16)):
            for x in range(x0, min(x1, 16) - p):
                tot += 1
                same += 1 if g[y][x] == g[y][x + p] else 0
        match = (same / tot) if tot else 0.0
        if match < cfg["min_match"]:
            v.append("x_period: interior columns repeat with period %d only %.0f%% of the "
                     "time, expected >= %.0f%% -- the bars must alternate"
                     % (p, 100 * match, 100 * cfg["min_match"]))

    if "single_component" in rule:
        n = len(components(g))
        if n != 1:
            v.append("single_component: found %d pieces, expected 1 -- this tile is one "
                     "emblem, not a repeating pattern" % n)

    if "symmetry" in rule:
        if not symmetric(g, rule["symmetry"]):
            v.append("symmetry: not symmetric under %s" % rule["symmetry"])

    if "holes" in rule:
        want = rule["holes"]
        holes = empty_holes(g)
        if len(holes) != want["count"]:
            v.append("holes: %d enclosed holes, expected %d" % (len(holes), want["count"]))
        else:
            centre = [h for h in holes if h[1] <= 7 <= h[3] and h[2] <= 7 <= h[4]]
            if want.get("centre") and len(centre) != 1:
                v.append("holes: expected exactly one hole at the centre, found %d" % len(centre))
            corners = [h for h in holes if h not in centre]
            if len(corners) != want.get("corner_count", len(corners)):
                v.append("holes: %d corner holes, expected %d" % (len(corners), want.get("corner_count")))
            if corners:
                sizes = sorted(h[0] for h in corners)
                spread = sizes[-1] - sizes[0]
                if spread > want.get("corner_spread", 99):
                    v.append("holes: corner hole sizes %s spread %d, expected <= %d -- the "
                             "four loops must read as equal"
                             % (sizes, spread, want.get("corner_spread")))

    if "max_holes" in rule:
        nh = len(empty_holes(g))
        if nh > rule["max_holes"]:
            v.append("max_holes: %d enclosed holes, expected <= %d -- this is a filled "
                     "silhouette, not an outline with windows"
                     % (nh, rule["max_holes"]))

    if "top_runs" in rule:
        rows = [y for y in range(16) if any(g[y])]
        if rows:
            got = h_runs(g[rows[0]])
            if got != rule["top_runs"]:
                v.append("top_runs: first inked row has %d runs, expected %d -- the two "
                         "prongs of the crenellated top"
                         % (got, rule["top_runs"]))

    if "crossbars" in rule:
        want = rule["crossbars"]
        bands = crossbar_bands(g)
        thick = [b[0] for b in bands]
        if len(bands) != want["count"]:
            v.append("crossbars: %d horizontal bars %s, expected %d -- a second bar near "
                     "the foot reads as a patriarchal cross, not a signpost"
                     % (len(bands), thick, want["count"]))
        elif thick and min(thick) < want.get("min_thickness", 0):
            v.append("crossbars: thinnest bar is %d rows, expected >= %d -- the crossbar "
                     "should be the heavy part, not the base"
                     % (min(thick), want.get("min_thickness")))

    if "bottom_centre_gap" in rule:
        rows = [y for y in range(16) if any(g[y])]
        if rows:
            y = rows[-1]
            lo, hi = 5, 11          # the central band of the cell
            if all(g[y][x] for x in range(lo, hi)):
                v.append("bottom_centre_gap: the bottom row is solid through the centre -- "
                         "this tile is an entrance and must be open there")
            else:
                gap = run = 0
                for x in range(lo, hi):
                    run = run + 1 if not g[y][x] else 0
                    gap = max(gap, run)
                if gap < rule["bottom_centre_gap"]:
                    v.append("bottom_centre_gap: widest clear run in the bottom centre is %d, "
                             "expected >= %d" % (gap, rule["bottom_centre_gap"]))

    if "bottom_runs" in rule:
        rows = [y for y in range(16) if any(g[y])]
        if rows:
            got = h_runs(g[rows[-1]])
            if got != rule["bottom_runs"]:
                v.append("bottom_runs: last inked row has %d runs, expected %d -- the "
                         "opening at the bottom centre is what makes this a gateway"
                         % (got, rule["bottom_runs"]))

    if "corner_ink" in rule:
        bb = bbox(g)
        k = rule["corner_ink"]
        if bb:
            x0, y0, x1, y1 = bb
            corners = {
                "top-left": any(g[y][x] for y in range(y0, y0 + k) for x in range(x0, x0 + k)),
                "top-right": any(g[y][x] for y in range(y0, y0 + k) for x in range(x1 - k + 1, x1 + 1)),
                "bottom-left": any(g[y][x] for y in range(y1 - k + 1, y1 + 1) for x in range(x0, x0 + k)),
                "bottom-right": any(g[y][x] for y in range(y1 - k + 1, y1 + 1) for x in range(x1 - k + 1, x1 + 1)),
            }
            miss = [c for c, ok in corners.items() if not ok]
            if miss:
                v.append("corner_ink: no ink within %dpx of %s -- the four blocky corner tabs "
                         "are missing" % (k, ", ".join(miss)))

    if rule.get("centre_gap_rows"):
        rows = [y for y in range(16) if any(g[y])]
        if len(rows) >= 4:
            mid = rows[len(rows) // 4: 3 * len(rows) // 4]
            filled = sum(1 for y in mid if h_runs(g[y]) == 1 and sum(g[y]) > 10)
            if filled:
                v.append("centre_gap_rows: %d mid rows are a single wide bar -- the middle "
                         "must stay open between the posts, not become a slab" % filled)

    if rule.get("widest_at_base"):
        rows = [y for y in range(16) if any(g[y])]
        if rows:
            widths = {y: sum(1 for c in g[y] if c) for y in rows}
            widest = max(widths.values())
            wide_rows = [y for y, w in widths.items() if w == widest]
            if max(wide_rows) < rows[-1] - 2:
                v.append("widest_at_base: the widest rows are %s but the base is row %d "
                         "-- a rook flares toward its foot"
                         % (wide_rows, rows[-1]))

    if "gutter" in rule:
        k = rule["gutter"]
        dirty = []
        for i in range(k):
            if any(g[i]) or any(g[15 - i]):
                dirty.append(i)
            if any(g[y][i] for y in range(16)) or any(g[y][15 - i] for y in range(16)):
                dirty.append(i)
        if dirty:
            v.append("gutter: rows/cols %d..%d must be clear so tiled tiles stay separate "
                     "objects -- %s" % (0, k - 1, "violated at offsets %s" % sorted(set(dirty))))

    if "colour_components" in rule and col is not None:
        for hexes, want_n in rule["colour_components"].items():
            n = len(components(colour_grid(col, hexes)))
            if n != want_n:
                v.append("colour_components %s: %d pieces, expected %d"
                         % ("/".join(hexes) if not isinstance(hexes, str) else hexes,
                            n, want_n))

    if "stipple" in rule and col is not None:
        for hexes, want in rule["stipple"].items():
            comps = components(colour_grid(col, hexes))
            if len(comps) < want["min_components"]:
                v.append("stipple: only %d specks, expected >= %d -- the water must be a "
                         "sparse stipple, not a solid field"
                         % (len(comps), want["min_components"]))
            big = [c[0] for c in comps if c[0] > want["max_area"]]
            if big:
                v.append("stipple: specks of size %s exceed max %d px -- stipple is "
                         "single pixels, not blobs" % (sorted(big), want["max_area"]))

    if "min_components" in rule:
        n = len(components(g))
        if n < rule["min_components"]:
            v.append("min_components: %d pieces, expected >= %d -- this is a stipple of many "
                     "separate marks, not a solid mass"
                     % (n, rule["min_components"]))

    if "holes_min" in rule:
        nh = len(empty_holes(g))
        if nh < rule["holes_min"]:
            v.append("holes_min: %d enclosed holes, expected >= %d" % (nh, rule["holes_min"]))

    if "aspect_max" in rule:
        w, h = sprite_extent(g)
        if w and (h / w) > rule["aspect_max"]:
            v.append("aspect_max: sprite is %dx%d (h/w %.2f), expected <= %.2f -- this one "
                     "is low and wide"
                     % (w, h, h / w, rule["aspect_max"]))

    if "max_components" in rule:
        n = len(components(g))
        if n > rule["max_components"]:
            v.append("max_components: %d pieces, expected <= %d -- a monster is one figure, "
                     "not a scatter"
                     % (n, rule["max_components"]))

    if "aspect_min" in rule or "min_height" in rule:
        w, h = sprite_extent(g)
        if "aspect_min" in rule and w and (h / w) < rule["aspect_min"]:
            v.append("aspect_min: sprite is %dx%d (h/w %.2f), expected >= %.2f -- a figure "
                     "stands up, it does not lie flat"
                     % (w, h, h / w, rule["aspect_min"]))
        if "min_height" in rule and h < rule["min_height"]:
            v.append("min_height: sprite is %d rows tall, expected >= %d -- it must fill the "
                     "cell, not float in it"
                     % (h, rule["min_height"]))

    if "dominant_colour" in rule and col is not None:
        from collections import Counter
        census = Counter(c for row in col for c in row if c)
        total = sum(census.values())
        top = census.most_common(1)[0] if census else (None, 0)
        share = (top[1] / total) if total else 0.0
        if share < rule["dominant_colour"]:
            v.append("dominant_colour: the busiest colour is only %.0f%% of the ink "
                     "(%s), expected >= %.0f%% -- a sprite is one figure in one colour"
                     % (100 * share, dict(census), 100 * rule["dominant_colour"]))

    for axis, key in (("x", "period_x"), ("y", "period_y")):
        if key in rule:
            p = rule[key]
            if axis == "x":
                bad = sum(1 for y in range(16) for x in range(16)
                          if g[y][x] != g[y][(x + p) % 16])
            else:
                bad = sum(1 for y in range(16) for x in range(16)
                          if g[y][x] != g[(y + p) % 16][x])
            if bad:
                v.append("%s: %d of 256 pixels differ from their position shifted by %d -- "
                         "this tile must repeat on that axis or it will not tile seamlessly"
                         % (key, bad, p))

    if "density" in rule:
        lo, hi = rule["density"]
        d = sum(sum(1 for x in row if x) for row in g) / 256.0
        if not (lo <= d <= hi):
            v.append("density: %.1f%% of cells inked, expected %.0f-%.0f%%"
                     % (100 * d, 100 * lo, 100 * hi))

    if "colour_share" in rule and col is not None:
        total = sum(sum(1 for x in row if x) for row in g)
        for hexes, (lo, hi) in rule["colour_share"].items():
            # a key may name several spellings of one colour (the SVGs use the spec's
            # bright #55FFFF; the engine displays #00AAAA). Sum them so the pending
            # palette decision cannot make a correct tile fail.
            if isinstance(hexes, str):
                hexes = (hexes,)
            n = sum(sum(1 for x in row if x in hexes) for row in col)
            share = (n / total) if total else 0.0
            if not (lo <= share <= hi):
                v.append("colour_share %s: %.0f%% of ink, expected %.0f-%.0f%%"
                         % ("/".join(hexes), 100 * share, 100 * lo, 100 * hi))

    return v


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default="engine/assets/tiles/cleanroom/tiles")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    fails, pending, promote, ok = [], [], [], []
    for art, rule in sorted(RULES.items()):
        path = os.path.join(a.src, "tile_%02X.svg" % art)
        if not os.path.exists(path):
            fails.append((art, ["missing SVG %s" % path]))
            continue
        g, col = load_grid(path)
        v = check(g, rule, col)
        if v:
            if art in PENDING_REDRAW:
                pending.append((art, v))
            else:
                fails.append((art, v))
        else:
            if art in PENDING_REDRAW:
                promote.append(art)
            else:
                ok.append(art)
        if a.verbose:
            print("art %02X: %s" % (art, "; ".join(v) if v else "conformant"))

    print("tile rules: %d rule(s) defined, %d conformant, %d pending redraw, %d violating"
          % (len(RULES), len(ok), len(pending), len(fails)))
    for art, v in pending:
        print("  PENDING art %02X (awaiting clean redraw):" % art)
        for line in v:
            print("      - %s" % line)
    for art in promote:
        print("  NOTE art %02X now conforms - remove it from PENDING_REDRAW" % art)
    for art, v in fails:
        print("  FAIL art %02X:" % art)
        for line in v:
            print("      - %s" % line)

    if fails:
        print("tile rules: FAILED")
        return 1
    print("tile rules: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
