#!/usr/bin/env python3
"""dungeon_strokes.py — decode and render the shipped `monsters` stroke file.

The dungeon has no tile-art sprites. asm/07F3 opens a file named `monsters` and reads one
0x800 block into memory, and asm/68ED draws a creature by indexing it as `(stamp & 7) * 256`.
So the executable contains one *shape* per dungeon stamp, and that shape is the only
evidence about what a dungeon creature looks like. This tool decodes the format and renders
each block so the silhouettes can be compared against candidate names.

Record format, as engine/src/game/dungeon.c dg_sprite_page consumes it:
    records are byte pairs (x, b1); x == 0 terminates the run
    y    = b1 & 0x1f
    pat  = DG_PATTERN[(b1 >> 5) & 7]      # DG_PATTERN = ff 11 88 cc 33 00 00 00
    each record paints a 4-wide x 2-tall block at (ORIGIN + x*4, ORIGIN + y*4), the four
    columns taking their 2-bit value from (pat >> (6 - 2*k)) & 3

Two things this format makes easy to get wrong, and both are asserted below:
  * A block whose first byte is 0 is NOT an empty block. Parsing stops at the first zero
    byte, but the block keeps going, and asm/68ED's depth ladder indexes into the interior
    of block 0 at offsets 0x80..0xf8. Reporting block 0 as empty is a parser artefact.
  * The shipped file is 2176 bytes and the original transfers only 2048. The trailing 128
    bytes are never read by the game; they are reported here and never rendered.

    tools/dungeon_strokes.py                 # table of blocks
    tools/dungeon_strokes.py --png DIR       # also write one PNG per block
    tools/dungeon_strokes.py --selftest      # controls, no game file needed

The rendered PNGs are derived from copyrighted game art and must never be committed; write
them under /var/tmp. See AGENTS.md licensing rules and docs/dungeon-stroke-art.md.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_FILE = os.path.join(ROOT, "original", "monsters")

DG_PATTERN = [0xFF, 0x11, 0x88, 0xCC, 0x33, 0x00, 0x00, 0x00]
DG_DEPTH_LADDER = [0x00, 0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC]
TRANSFER_BYTES = 0x800
BLOCK = 256
SCALE = 8
# shade 0..3 -> RGB. The shape is faithful; these colours are an author's choice.
SHADE = [(0, 0, 0), (90, 60, 20), (200, 150, 40), (255, 235, 160)]


def parse(block: bytes) -> list[tuple[int, int, int]]:
    """(x, y, pattern_index) per record, stopping at the first zero x byte."""
    recs: list[tuple[int, int, int]] = []
    p = 0
    while p + 1 < len(block):
        x = block[p]
        if x == 0:
            break
        b1 = block[p + 1]
        recs.append((x, b1 & 0x1F, (b1 >> 5) & 7))
        p += 2
    return recs


def extents(recs):
    xs = [r[0] for r in recs]
    ys = [r[1] for r in recs]
    return min(xs), max(xs), min(ys), max(ys)


def render(recs):
    """Normalised to the block's own bounding box; the stroke coords share one origin."""
    mx = min(r[0] for r in recs)
    my = min(r[1] for r in recs)
    w_cells = max(r[0] for r in recs) - mx + 1
    h_cells = max(r[1] for r in recs) - my + 1
    w = (w_cells * 4 + 8) * SCALE
    h = (h_cells * 4 + 8) * SCALE
    px = bytearray(w * h * 3)
    for (x, y, pi) in ((a - mx, b - my, c) for a, b, c in recs):
        pat = DG_PATTERN[pi]
        for k in range(4):
            shade = (pat >> (6 - 2 * k)) & 3
            if shade == 0:
                continue
            sx = (4 + x * 4 + k) * SCALE
            sy = (4 + y * 4) * SCALE
            for dx in range(SCALE):
                for dy in range(2 * SCALE):
                    for cc in range(3):
                        px[((sy + dy) * w + sx + dx) * 3 + cc] = SHADE[shade][cc]
    return w, h, px


def write_png(path, w, h, px):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9))
                + chunk(b"IEND", b""))


def report(data: bytes, png_dir: str | None) -> int:
    print(f"monsters: {len(data)} bytes on disk; the original transfers {TRANSFER_BYTES} "
          f"= {TRANSFER_BYTES // BLOCK} blocks of {BLOCK}")
    print(f"{'block':>5} {'recs':>5} {'x range':>10} {'y range':>10}  patterns")
    for i in range(TRANSFER_BYTES // BLOCK):
        base = i * BLOCK
        recs = parse(data[base:base + BLOCK])
        if not recs:
            print(f"{i:>5} {'0':>5}   (terminates at offset 0 -- see the depth ladder below)")
            continue
        x0, x1, y0, y1 = extents(recs)
        hist: dict[int, int] = {}
        for r in recs:
            hist[r[2]] = hist.get(r[2], 0) + 1
        print(f"{i:>5} {len(recs):>5} {x0:>4}..{x1:<5} {y0:>4}..{y1:<5}  "
              + " ".join(f"p{k}:{v}" for k, v in sorted(hist.items())))
        if png_dir:
            w, h, px = render(recs)
            write_png(os.path.join(png_dir, f"block_{i}.png"), w, h, px)

    print("\ndepth ladder (asm/68ED indexes these offsets, which land inside block 0):")
    for off in sorted(set(DG_DEPTH_LADDER)):
        recs = parse(data[off:off + BLOCK])
        print(f"  offset {hex(off):>6}: {len(recs):>3} records"
              + ("" if recs else "  (draws nothing)"))
    tail = data[TRANSFER_BYTES:]
    print(f"\nnever transferred: {len(tail)} bytes past the 0x800 read, "
          f"{sum(1 for b in tail if b)} nonzero, parses as {len(parse(tail))} records. "
          "The game never reads them; not rendered.")
    if png_dir:
        print(f"\nPNGs written to {png_dir} -- derived from copyrighted art, never commit them.")
    return 0


def selftest() -> int:
    """Controls. Each one fails loudly if the parser or the renderer drifts."""
    fails = 0

    def check(name, got, want):
        nonlocal fails
        ok = got == want
        if not ok:
            fails += 1
        print(f"  [{'ok' if ok else 'FAIL'}] {name}: got {got!r} want {want!r}")

    # A terminator is a zero x byte, and nothing before it is skipped.
    check("terminator honoured", parse(bytes([4, 0x21, 0, 9, 9])), [(4, 1, 1)])
    # y is 5 bits and the pattern index is the top 3, so 0xff must be y=31 pat=7.
    check("field split", parse(bytes([0xff, 0xff])), [(255, 31, 7)])
    # A block starting with 0 parses to nothing even when it holds data after the terminator.
    check("zero-first block parses empty", parse(bytes([0, 5, 5, 5, 5])), [])
    # ...which is exactly why "block 0 is empty" is the wrong claim: the same bytes parse
    # fine from a different base.
    check("same bytes parse from another base", parse(bytes([5, 5, 5, 5])),
          [(5, 5, 0), (5, 5, 0)])
    # Pattern 5,6,7 are all-zero, so they must render no pixels at all.
    for pi in (5, 6, 7):
        w, h, px = render([(0, 0, pi)])
        check(f"pattern {pi} draws nothing", sum(px), 0)
    # Pattern 0 (0xff) must fill all 4 columns x 2 rows at the chosen cell.
    w, h, px = render([(0, 0, 0)])
    check("pattern 0 fills 4x2 at scale", sum(1 for i in range(0, len(px), 3) if px[i]),
          4 * 2 * SCALE * SCALE)
    print(f"selftest: {'PASS' if not fails else f'{fails} FAILED'}")
    return 0 if not fails else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--png", metavar="DIR", help="also write one PNG per block into DIR")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--file", default=GAME_FILE, help="override the input file")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if not os.path.exists(a.file):
        # The game files are gitignored by policy, so a missing input is "cannot run",
        # not a failure. Exit 77 is the automake convention for exactly that.
        print(f"cannot run: {a.file} not found (supply your own game files)", file=sys.stderr)
        return 77
    data = open(a.file, "rb").read()
    if len(data) < TRANSFER_BYTES:
        print(f"cannot run: {a.file} is {len(data)} bytes, expected >= {TRANSFER_BYTES}",
              file=sys.stderr)
        return 77
    if a.png:
        os.makedirs(a.png, exist_ok=True)
    return report(data, a.png)


if __name__ == "__main__":
    sys.exit(main())
