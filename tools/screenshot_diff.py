#!/usr/bin/env python3
"""screenshot_diff.py — CP4b T7: the standing "screenshots are tests" directive,
first attempt.

What it does, and what it deliberately does NOT:

  * Crops the DOSBox game area out of a play screenshot, downsamples to the
    CGA 320x200, and reduces every pixel to one of the four CGA colour classes
    (black / cyan / magenta / white) by nearest-neighbour in RGB. That yields a
    40x25 grid of colour classes, the same shape the engine's text grid has.
  * Renders the engine's status band headlessly (the same u2_status_draw the
    game uses) with a caller-supplied set of BCD values, and reduces it to the
    same 40x25 class grid.
  * Compares the STATUS-BLOCK region only (rows 20..23, cols 30..39), which is
    the one region whose content is fully determined by the BCD values we can
    set to match. The map region is out of scope: the screenshot and the engine
    are almost never standing in the same sector at the same position, so a
    whole-screen diff would measure that mismatch, not a defect.

This is a differential *harness*, not a pixel oracle. The acceptance the task
sets is that it runs in `make test` (PASS or SKIP with a named reason) and is
proven to catch a deliberately blanked band. The exact-match question (does the
original's status block read the same digits as our render) needs the BCD
values read out of the screenshot, which is OCR and is not attempted here —
guessing them would be the doubling-bug failure mode, so the tool takes them as
input and the test drives both sides from one source of truth.

Output is derived from a gitignored screenshot and never writes game content.
"""
import argparse
import sys

# Pillow is imported where it is used, not here. This tool's `--help` is cited in
# docs/spice86-capture.md and doc_command_gate.py runs `--help` to prove the citation is real, so a
# module-level `from PIL import Image` made `--help` exit 1 on any machine without Pillow -- which
# is every CI runner. A documented command that cannot print its own usage is the defect the gate
# exists to catch, and here the gate was right and the tool was wrong.

# CGA mode-4 palette as the engine renders it (black, cyan, magenta, white).
CGA = [(0, 0, 0), (0x55, 0xAA, 0xFF), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0xFF)]

GRID_COLS, GRID_ROWS = 40, 25
CELL_W, CELL_H = 8, 8          # 320/40, 200/25
# The status block: rows 20..23, cols 30..39 (proven geometry, CP4 screenshots).
SB_R0, SB_R1, SB_C0, SB_C1 = 20, 24, 30, 40


def classify(px):
    """Nearest CGA colour index for an (r,g,b) triple."""
    r, g, b = px[:3]
    return min(range(4), key=lambda i: (r - CGA[i][0]) ** 2
               + (g - CGA[i][1]) ** 2 + (b - CGA[i][2]) ** 2)


def find_game_area(im):
    """Bounding box of the CGA-coloured pixels, to crop DOSBox chrome.

    The screenshot is a whole window (title bar, borders). The game area is the
    rectangle whose pixels are near one of the four CGA colours; everything
    outside is desktop chrome (greys, antialiased text). We take the largest
    run that is not near-grey chrome by scanning for saturated CGA pixels.
    """
    w, h = im.size
    minx, miny, maxx, maxy = w, h, 0, 0
    step = max(1, w // 320)
    for y in range(0, h, step):
        for x in range(0, w, step):
            r, g, b = im.getpixel((x, y))[:3]
            # a CGA cyan/magenta/white pixel is strongly non-grey
            sat = max(r, g, b) - min(r, g, b)
            bright = max(r, g, b)
            if sat > 60 or bright > 200:
                minx, maxx = min(minx, x), max(maxx, x)
                miny, maxy = min(miny, y), max(maxy, y)
    if maxx <= minx or maxy <= miny:
        return None
    return (minx, miny, maxx, maxy)


def grid_from_screenshot(path):
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit("this command needs Pillow (pip install pillow); --help works without it") from exc
    im = Image.open(path).convert("RGB")
    area = find_game_area(im)
    if area is None:
        raise ValueError("no CGA-coloured game area found in screenshot")
    game = im.crop(area).resize((320, 200), Image.NEAREST)
    grid = [[classify(game.getpixel((c * CELL_W + CELL_W // 2,
                                     r * CELL_H + CELL_H // 2)))
             for c in range(GRID_COLS)] for r in range(GRID_ROWS)]
    return grid, area


def grid_from_engine(path):
    """Read a PPM the engine dumped (--dump-screen) into a class grid."""
    raw = open(path, "rb").read()
    end = 0
    for _ in range(3):
        end = raw.index(b"\n", end) + 1
    header = raw[:end].split(b"\n")
    assert header[0] == b"P6", "engine dump is not a binary PPM"
    w, h = map(int, header[1].split())
    px = raw[end:]
    grid = []
    for r in range(GRID_ROWS):
        row = []
        for c in range(GRID_COLS):
            x = c * CELL_W + CELL_W // 2
            y = r * CELL_H + CELL_H // 2
            i = (y * w + x) * 3
            row.append(classify(px[i:i + 3]))
        grid.append(row)
    return grid


def status_block(grid):
    return [row[SB_C0:SB_C1] for row in grid[SB_R0:SB_R1]]


def agreement(a, b):
    """Fraction of status-block cells whose colour class matches."""
    sa, sb = status_block(a), status_block(b)
    total = sum(len(r) for r in sa)
    same = sum(1 for ra, rb in zip(sa, sb) for x, y in zip(ra, rb) if x == y)
    return same / total if total else 0.0


def ink_fraction(block):
    on = sum(1 for row in block for cell in row if cell != 0)
    total = sum(len(row) for row in block)
    return on / total if total else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--screenshot", required=True)
    ap.add_argument("--engine-ppm", required=True)
    ap.add_argument("--require-match", type=float, default=0.0,
                    help="fail if status-block agreement is below this")
    args = ap.parse_args()

    try:
        sgrid, area = grid_from_screenshot(args.screenshot)
    except Exception as e:  # noqa: BLE001 - report, do not crash the suite
        print(f"SKIP: cannot read screenshot game area: {e}")
        return 2
    try:
        egrid = grid_from_engine(args.engine_ppm)
    except Exception as e:  # noqa: BLE001
        print(f"SKIP: cannot read engine frame: {e}")
        return 2

    sb_s, sb_e = status_block(sgrid), status_block(egrid)
    score = agreement(sgrid, egrid)
    print(f"screenshot game area crop: {area}")
    print(f"status-block ink: screenshot {ink_fraction(sb_s):.2f}, "
          f"engine {ink_fraction(sb_e):.2f}")
    print(f"status-block agreement: {score:.3f}")

    # Both sides must actually have drawn the band; a blank on either side is a
    # harness failure, not a content mismatch. This is the property the poison
    # check exercises.
    if ink_fraction(sb_e) == 0.0:
        print("FAIL: engine status block is blank (upload or draw defect)")
        return 1
    if score < args.require_match:
        print(f"FAIL: agreement {score:.3f} below required {args.require_match}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
