#!/usr/bin/env python3
"""
tests/site_viewport_gate.py — a site sector must not draw the world map's wrap.

Why this exists as its own gate rather than as another line in view_check.c:
view_check pins what u2_view_fill does with a mode byte it is handed, and the
defect this guards was one level up. engine/src/game/overworld.c derived the mode
only on site entry, so a save loaded inside a settlement came back in world-map
mode and rendered as a torus. In Port Boniface that pulled the town's north sign
band onto the bottom of the screen, and the owner reported it as water
surrounding the south exit. The original fills past the edge of a site with the
sector's own ground byte; see core/view.h's U2_VIEW_SITE_OFFGRID and
captures/spice86/s31-south-edge/.

The assertion is deliberately the one the capture can actually support. Rows 7, 8
and 9 of a frame taken at (31,62) in sector 31 are, in the original, three copies
of one tile: black with a sparse cyan dither, 220 cyan pixels per row, and no
grey at all. The wrapped content is the town's sign row, which is full of white
glyph pixels. So "the three rows agree with each other and carry no grey" is
exactly the property that distinguishes the two behaviours, and it is the property
that would silently come back if anyone reinstated an unconditional `& 63`.

Run from engine/:
    python3 tests/site_viewport_gate.py            # gate
    python3 tests/site_viewport_gate.py --selftest # prove it can go red
"""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
ROOT = os.path.dirname(ENGINE)

# Save-record offsets. The record is the 256 bytes the game keeps at DS:36..0x135,
# so save_offset = DS_offset - 0x36. See engine/src/core/state.h.
OFF_NAME = 0x00          # DS:36; a zero first byte is the game's own blank test
OFF_SECTOR_HI = 0x13     # DS:49 tens digit (era on Earth)
OFF_SECTOR_LO = 0x14     # DS:4A ones digit: 0 is the world map, non-zero is a site
OFF_ENTRY_X = 0x24       # DS:5A, copied to DS:00 on load
OFF_ENTRY_Y = 0x25       # DS:5B, copied to DS:01 on load
OFF_WORLD = 0x37         # DS:6D: 0 is Earth

# Port Boniface: sector 31, the street arrival cell, exactly as the live capture
# measured it after pressing E on the wilderness mouth.
TOWN_SECTOR = (3, 1)
TOWN_X, TOWN_Y = 31, 62

TILE = 16
COLS = 20
ROWS = 10
# The three rows that are off-grid in the original at this position: the player is
# at row 5, the last grid row is 6, and the viewport is 10 rows tall.
FILLER_ROWS = (7, 8, 9)
GREY_LUMA = 120     # anything at or above this on all three channels is a glyph pixel


def read_ppm(path):
    """Minimal P6 reader: returns (width, height, bytes). No third-party deps,
    because this has to run on a CI runner that has a compiler and nothing else."""
    with open(path, 'rb') as f:
        data = f.read()
    if not data.startswith(b'P6'):
        raise ValueError('%s is not a binary PPM' % path)
    # header: P6 <w> <h> <max> — three tokens, then exactly one whitespace byte.
    # Reading a fourth would eat into the pixel body and truncate it.
    pos = 2
    tokens = []
    while len(tokens) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b'#':
            while pos < len(data) and data[pos:pos + 1] != b'\n':
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    w, h, _mx = (int(t) for t in tokens[:3])
    body = data[pos + 1:]
    return w, h, body


def row_pixels(w, body, row):
    """The RGB triples of one 16-px tile row of the viewport."""
    out = []
    for y in range(row * TILE, (row + 1) * TILE):
        base = y * w * 3
        for x in range(0, min(COLS, w // TILE) * TILE):
            out.append(body[base + x * 3: base + x * 3 + 3])
    return out


def is_grey(px):
    return px[0] >= GREY_LUMA and px[1] >= GREY_LUMA and px[2] >= GREY_LUMA


def verdict(w, h, body):
    """(ok, explanation). The property is the capture's, not an invention: three
    identical rows with no glyph pixels in them."""
    if (w, h) != (COLS * TILE, 200):
        return False, 'frame is %dx%d, expected %dx200' % (w, h, COLS * TILE)
    rows = {r: row_pixels(w, body, r) for r in FILLER_ROWS}
    for r in FILLER_ROWS:
        grey = sum(1 for px in rows[r] if is_grey(px))
        if grey:
            return False, ('row %d has %d grey pixels; the town sign band is being '
                           'wrapped onto the bottom of the screen' % (r, grey))
    if rows[FILLER_ROWS[0]] != rows[FILLER_ROWS[1]] or rows[FILLER_ROWS[1]] != rows[FILLER_ROWS[2]]:
        return False, 'the off-grid rows differ from each other; a wrap draws three different tiles'
    lit = sum(1 for px in rows[FILLER_ROWS[0]] if px != b'\x00\x00\x00')
    if lit == 0:
        return False, 'the off-grid rows are blank; the original draws the ground tile, not nothing'
    return True, 'rows %s agree and carry no glyph pixels (%d lit per row)' % (
        '-'.join(str(r) for r in (FILLER_ROWS[0], FILLER_ROWS[-1])), lit)


def build_town_save(dst_dir, player_src):
    with open(player_src, 'rb') as f:
        rec = bytearray(f.read())
    if len(rec) < 256:
        raise ValueError('%s is too short to hold a record' % player_src)
    rec = rec[:384] if len(rec) >= 384 else rec + bytearray(384 - len(rec))
    if rec[OFF_NAME] == 0:
        rec[OFF_NAME:OFF_NAME + 7] = b'S31GATE'   # same length: a slice assignment that
                                                   # changed length would resize the record
    rec[OFF_SECTOR_HI] = TOWN_SECTOR[0]
    rec[OFF_SECTOR_LO] = TOWN_SECTOR[1]
    rec[OFF_ENTRY_X] = TOWN_X
    rec[OFF_ENTRY_Y] = TOWN_Y
    rec[OFF_WORLD] = 0
    os.makedirs(dst_dir, exist_ok=True)
    # saves/<slot>/PLAYER: the slot directory and the record inside it share the
    # name (docs/save-scoped-worlds.md D1), and core/slot.h owns the shape.
    slot = os.path.join(dst_dir, 'PLAYER')
    os.makedirs(slot, exist_ok=True)
    with open(os.path.join(slot, 'PLAYER'), 'wb') as f:
        f.write(bytes(rec))


def find_original():
    for cand in (os.path.join(ROOT, 'original'), os.path.join(ROOT, 'references', 'ultima2')):
        if os.path.isdir(cand) and any(
                os.path.isfile(os.path.join(cand, n)) for n in ('mapx31', 'MAPX31')):
            return cand
    return None


def run_gate():
    binary = os.path.join(ENGINE, 'u2')
    if not os.path.isfile(binary):
        print('SKIP  engine/u2 not built; run `make -C engine` first')
        return 0
    data = find_original()
    if data is None:
        print('SKIP  no original/ or references/ with mapx31 (gitignored by policy)')
        return 0
    with tempfile.TemporaryDirectory(prefix='u2-s31gate-') as tmp:
        saves = os.path.join(tmp, 'saves')
        build_town_save(saves, os.path.join(data, 'player'))
        out = os.path.join(tmp, 'town.ppm')
        # --classic, because everything below asserts the original 320x200 geometry.
        # Enhanced became the default on 2026-11-17 and its canvas is 1024x512, so an
        # unflagged run here would fail for a reason that has nothing to do with the
        # site viewport. The enhanced canvas is gated in run_cp4.sh, not here.
        proc = subprocess.run([binary, '--classic', '--headless', '--play-map',
                               '--data-dir', data, '--save-dir', saves,
                               '--dump-map', out],
                              cwd=ENGINE, capture_output=True, text=True)
        if proc.returncode != 0 or not os.path.isfile(out):
            print('FAIL  engine could not render the town: %s'
                  % (proc.stderr.strip().splitlines() or ['no output'])[-1])
            return 1
        note = (proc.stderr or '').strip().splitlines()
        if note and 'sector 31' not in note[-1]:
            print('FAIL  the dump did not load the town sector: %s' % note[-1])
            return 1
        w, h, body = read_ppm(out)
        ok, why = verdict(w, h, body)
        print('%s  site viewport at (%d,%d) sector %d%d: %s'
              % ('PASS' if ok else 'FAIL', TOWN_X, TOWN_Y,
                 TOWN_SECTOR[0], TOWN_SECTOR[1], why))
        return 0 if ok else 1


def synth_frame(filler_grey=False, filler_varies=False):
    """A 320x200 frame with the real geometry, for the controls. Each off-grid row
    is a sparse dither; the controls make one row grey (a wrapped sign row) or make
    the rows differ from each other (a wrap draws three different tiles)."""
    w, h = COLS * TILE, 200
    body = bytearray(w * h * 3)

    def put(x, y, rgb):
        i = (y * w + x) * 3
        body[i:i + 3] = struct.pack('BBB', *rgb)

    for r in FILLER_ROWS:
        if filler_grey and r == FILLER_ROWS[0]:
            ink = (255, 255, 255)          # a glyph pixel: what the sign band is made of
        elif filler_varies and r == FILLER_ROWS[2]:
            ink = (170, 0, 170)            # a different tile in the last row
        else:
            ink = (0, 170, 170)
        for y in range(r * TILE, (r + 1) * TILE):
            for x in range(w):
                # y % TILE so the three off-grid rows are genuinely identical, which is
                # the property under test; a pattern keyed on the absolute y would make
                # every row differ and the control would pass for the wrong reason.
                put(x, y, ink if (x + (y % TILE)) % 32 < 2 else (0, 0, 0))
    return w, h, bytes(body)


def run_selftest():
    """The gate has to be able to fail in the place it enforces, so each control is
    run through the same verdict() the gate uses."""
    controls = [
        ('good frame passes', synth_frame(), True),
        ('a wrapped sign row (grey glyphs) must go red',
         synth_frame(filler_grey=True), False),
        ('rows that differ from each other must go red',
         synth_frame(filler_varies=True), False),
        ('a blank filler must go red', (COLS * TILE, 200, bytes(COLS * TILE * 200 * 3)), False),
        ('a wrong frame size must go red', (320, 100, bytes(320 * 100 * 3)), False),
    ]
    bad = 0
    for label, (w, h, body), want in controls:
        got, why = verdict(w, h, body)
        ok = (got == want)
        if not ok:
            bad += 1
        print('  %s  %-46s -> %s (%s)' % ('ok  ' if ok else 'FAIL', label,
                                          'pass' if got else 'fail', why))
    print('SELFTEST: %s' % ('PASS' if bad == 0 else 'FAIL (%d)' % bad))
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(run_selftest() if '--selftest' in sys.argv else run_gate())
