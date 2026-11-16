#!/usr/bin/env python3
"""Gate for `u2 --passmap`: the engine answers passability, the bot does search.

Every tile byte used here is taken from foot_ladder's own named cases, not invented:

    grid byte 0x00 -> mode 0x00  open sea      refused on foot, ACCEPTED by ship
    grid byte 0x10 -> mode 0x08  mountain      refused on foot
    grid byte 0x04 -> mode 0x02  (unnamed)     accepted on foot, REFUSED by horse and plane
    grid byte 0x70 -> mode 0x38  town paving   accepted on foot, refused by ship
    rocket                                   unconditional reject, every cell

That set is chosen so the per-mode arms are each exercised by at least one cell: a build that
collapsed the dispatch into one table would still pass a foot-only test.
"""
import json
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(REPO, "engine")
U2_BIN = os.environ.get("U2_BIN", os.path.join(ENGINE, "u2"))
DS_FULL = 10240
GRID_OFF = 0x1800
SEA, MOUNTAIN, MODE2, PAVING = 0x00, 0x10, 0x04, 0x70

checks = 0
fails = []


def ok(cond, msg):
    global checks
    checks += 1
    if not cond:
        fails.append(msg)


def blob(stamp=0x78, grid_byte=PAVING, grid=None, size=DS_FULL):
    b = bytearray(size)
    for i, c in enumerate(b"BOT"):
        b[0x36 + i] = c
    b[0x00], b[0x01] = 10, 10
    b[0x49], b[0x4A] = 4, 0            # sector 40, Earth wilderness
    b[0x13] = stamp
    g = grid if grid is not None else [grid_byte] * 4096
    if size >= GRID_OFF + 4096:
        for i, v in enumerate(g):
            b[GRID_OFF + i] = v
    return bytes(b)


def passmap(b):
    path = "/var/tmp/u2-passmap.bin"
    with open(path, "wb") as f:
        f.write(b)
    return subprocess.run([U2_BIN, "--headless", "--ds-json", path, "--passmap"],
                          cwd=ENGINE, capture_output=True, text=True, timeout=60)


def bits(out):
    for line in out.stdout.splitlines():
        i = line.find('{"u2api"')
        if i >= 0:
            o = json.loads(line[i:])
            return [ (int(o["bits"][k:k+2], 16) >> (7 - j)) & 1
                     for k in range(0, len(o["bits"]), 2) for j in range(8) ]
    return None


# --- 1. the flat cases, one per dispatch arm -----------------------------------
for stamp, grid_byte, want, why in (
        (0x78, SEA, 0, "sea refused on foot"),
        (0x78, MOUNTAIN, 0, "mountain refused on foot"),
        (0x78, MODE2, 1, "mode 0x02 accepted on foot"),
        (0x78, PAVING, 1, "town paving accepted on foot"),
        (0x24, SEA, 1, "sea accepted by ship"),
        (0x24, PAVING, 0, "paving refused by ship"),
        (0x22, MODE2, 0, "mode 0x02 refused on horse"),
        (0x26, MODE2, 0, "mode 0x02 refused to a grounded plane"),
        (0x28, PAVING, 0, "rocket refuses everything"),
):
    out = passmap(blob(stamp=stamp, grid_byte=grid_byte))
    m = bits(out)
    ok(m is not None and len(m) == 4096, "passmap is 4096 cells for %s" % why)
    if m:
        ok(all(m), "all 4096 cells agree for %s (got %d set)" % (why, sum(m))) if want else \
            ok(not any(m), "no cell passable for %s (got %d set)" % (why, sum(m)))

# --- 2. a pattern: bit order, stride and offset -------------------------------
# A single passable cell at a known coordinate is the only way to pin MSB-first
# bit order and row-major stride at once. A wrong bit order or stride produces a
# set bit somewhere plausible, which is exactly the failure this pins.
pat = [SEA] * 4096
pat[7 * 64 + 5] = PAVING          # (x=5, y=7)
out = passmap(blob(stamp=0x78, grid=pat))
m = bits(out)
ok(m is not None and sum(m) == 1, "exactly one passable cell in a one-cell pattern (got %s)"
   % (sum(m) if m else None))
if m and sum(m) == 1:
    idx = m.index(1)
    ok(idx == 7 * 64 + 5, "the passable cell decodes at (x=5,y=7), so bit order and stride are "
                          "both right (got index %d = x%d,y%d)" % (idx, idx % 64, idx // 64))

# --- 3. a blob that is only the mirror must refuse, not guess ------------------
out = passmap(blob(size=1616))
ok(out.returncode == 2, "a mirror-sized blob exits 2 rather than reading past the end "
                        "(got %d)" % out.returncode)
ok("stamped grid" in out.stderr or "0x1800" in out.stderr,
   "the refusal names the reason (got %r)" % out.stderr[:120])

# --- 4. the stamp is read from the blob, not assumed ---------------------------
ok(json.loads(next(l[l.find('{"u2api"'):] for l in
   passmap(blob(stamp=0x24)).stdout.splitlines() if '{"u2api"' in l))["stamp"] == 0x24,
   "the response echoes the mount byte it used")

if fails:
    print("  FAIL  %d checks passed, %d failed" % (checks, len(fails)))
    for f in fails:
        print("        " + f)
    sys.exit(1)
print("ok  %d checks: engine answers passability, bot does search" % checks)
