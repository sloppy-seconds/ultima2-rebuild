#!/usr/bin/env python3
"""apple2_seeds.py — function seed list for the flat 6502 image, so Ghidra finds anything.

Why this exists: a raw Apple II BIN has no entry point, so Ghidra's auto-analysis produces
zero functions (docs/apple2-reference.md § 8). `tools/ghidra/Seed6502.java` takes a list of
addresses and creates a function at each one, but the scan that produced the list was never
in the tree — the § 8 run wrote it to /var/tmp and it was gone. This is that scan.

The rule, reverse-engineered against the surviving 328-seed artifact and reproducing it
exactly:

  * scan the flat image byte-granular for $20 (JSR abs) and $4C (JMP abs);
  * the target is the next two bytes, little-endian;
  * keep a target only if it lands inside a *loaded* segment.

That last clause is what separates a real scan from a plausible one. The image is
zero-filled in the gaps between segments, and `$00 $00` after a stray $20 in a data run
yields a target in a gap — which is not code, it is nothing. Filtering on the loaded ranges
from `tools/apple2_image.py` (imported, not copied) drops exactly the seven targets that
made the naive scan report 335 instead of 328.

**The vector table is excluded by default.** `$4000..$403E` in `STD.SUBS.OBJ` is 21 `JMP`
vectors, not code, and the scan is byte-granular: it puts a seed on every third byte of the
table, so Ghidra decompiles the vectors and reports their *targets*' bodies under the
vectors' addresses. That is how the first Ghidra run presented the monster step vector as
`FUN_4000`. See `docs/archive/handoff-apple2-crosswalk.md` T0. Pass `--include-vectors` to get the
historical list back — that is the gate this tool's `--selftest` checks against.

Usage:
    python3 tools/apple2_seeds.py -o /var/tmp/u2_6502_seeds.txt
    python3 tools/apple2_seeds.py --include-vectors -o disasm/apple2-seeds.txt
    python3 tools/apple2_seeds.py --selftest
    python3 tools/apple2_seeds.py --summary
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apple2_image  # noqa: E402  (RESIDENT / OVERLAYS / BASE / LIMIT — one source of truth)

# STD.SUBS.OBJ opens with a JMP vector table; seeding inside it makes Ghidra report a
# vector as a routine whose body is its target's code. Verified by hand: $4000 = JMP $4054,
# $4003 = JMP $406F, $4006 = JMP $4080 (the string printer), $4036 = JMP $44D5 (sign).
VECTOR_TABLE = (0x4000, 0x403E)

# The documented § 8 result, measured on the 1989 re-release image. A scan that returns 40
# or 4000 has a bug, not a new discovery.
HISTORICAL_COUNT = 328

JSR_ABS = 0x20
JMP_ABS = 0x4C


def loaded_ranges(exdir, overlays=()):
    """(start, end) half-open for every segment apple2_image.py would lay down."""
    plan = list(apple2_image.RESIDENT)
    for name in overlays:
        plan.append(apple2_image.OVERLAYS[name])
    out = []
    for addr, fn in plan:
        path = os.path.join(exdir, fn)
        if not os.path.exists(path):
            sys.exit(f"missing {path}; run cp2 extract first (docs/apple2-reference.md § 2)")
        out.append((addr, addr + os.path.getsize(path)))
    return sorted(out)


def scan(img, base, ranges, include_vectors=False):
    inside = lambda a: any(lo <= a < hi for lo, hi in ranges)
    vlo, vhi = VECTOR_TABLE
    seeds = set()
    for i, b in enumerate(img):
        if b not in (JSR_ABS, JMP_ABS) or i + 2 >= len(img):
            continue
        t = img[i + 1] | (img[i + 2] << 8)
        if not (base <= t < base + len(img)) or not inside(t):
            continue
        if not include_vectors and vlo <= t <= vhi:
            continue
        seeds.add(t)
    return sorted(seeds)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-i", "--image", default="/var/tmp/u2_6502.bin",
                    help="flat image from tools/apple2_image.py")
    ap.add_argument("-d", "--dir", default="decompiled/apple2",
                    help="extracted Apple files, for the loaded-range filter")
    ap.add_argument("-o", "--out", help="write hex addresses one per line (default: stdout)")
    ap.add_argument("--include-vectors", action="store_true",
                    help="do not exclude $4000..$403E (reproduces the historical 328)")
    ap.add_argument("--with", dest="overlay", action="append", default=[],
                    help=f"same overlays as apple2_image.py: {', '.join(apple2_image.OVERLAYS)}")
    ap.add_argument("--summary", action="store_true", help="print counts instead of addresses")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.image):
        sys.exit(f"no such image {args.image}; build it: python3 tools/apple2_image.py -o {args.image}")
    img = open(args.image, "rb").read()
    ranges = loaded_ranges(args.dir, args.overlay)
    if len(img) != apple2_image.LIMIT - apple2_image.BASE:
        sys.exit(f"{args.image} is {len(img)} bytes, expected {apple2_image.LIMIT - apple2_image.BASE}; "
                 f"rebuild it with tools/apple2_image.py")

    seeds = scan(img, apple2_image.BASE, ranges, args.include_vectors)

    if args.selftest:
        fails = []
        raw = scan(img, apple2_image.BASE, ranges, include_vectors=True)
        if len(raw) != HISTORICAL_COUNT:
            fails.append(f"vector-inclusive scan gives {len(raw)} seeds, "
                         f"documented result is {HISTORICAL_COUNT}")
        vec = [s for s in raw if VECTOR_TABLE[0] <= s <= VECTOR_TABLE[1]]
        if len(vec) != 19:
            fails.append(f"expected 19 seeds inside the vector table, got {len(vec)}")
        if any(VECTOR_TABLE[0] <= s <= VECTOR_TABLE[1] for s in seeds):
            fails.append("default output still contains vector-table addresses")
        if len(seeds) != len(raw) - len(vec):
            fails.append("default output is not the raw scan minus the vector table")
        inside = lambda a: any(lo <= a < hi for lo, hi in ranges)
        if not all(inside(s) for s in seeds):
            fails.append("a seed lands in a zero-filled gap")
        # negative control for the loaded-range filter: without it the scan over-counts,
        # and the extras are all gap addresses.
        naive = set()
        for i, b in enumerate(img):
            if b in (JSR_ABS, JMP_ABS) and i + 2 < len(img):
                t = img[i + 1] | (img[i + 2] << 8)
                if apple2_image.BASE <= t < apple2_image.BASE + len(img):
                    naive.add(t)
        extras = naive - set(raw)
        if not extras or any(inside(t) for t in extras):
            fails.append("loaded-range filter removed nothing, or removed a real address — "
                         "check the segment table")
        if fails:
            print("SELFTEST FAILED")
            for f in fails:
                print("  -", f)
            sys.exit(1)
        print(f"selftest ok: {len(raw)} raw seeds ({len(vec)} in the vector table, "
              f"excluded by default), {len(seeds)} emitted; every seed inside a loaded segment")
        return

    if args.summary:
        print(f"image        : {args.image} ({len(img)} bytes, base ${apple2_image.BASE:04X})")
        print(f"loaded       : {len(ranges)} segments")
        print(f"seeds        : {len(seeds)}"
              + ("" if args.include_vectors else f" (vector table ${VECTOR_TABLE[0]:04X}"
                                                    f"..${VECTOR_TABLE[1]:04X} excluded)"))
        per = {f"{a:04X}": 0 for a, _ in ranges}
        for s in seeds:
            for lo, hi in ranges:
                if lo <= s < hi:
                    per[f"{lo:04X}"] += 1
        for a, fn in sorted((a, f) for a, f in apple2_image.RESIDENT):
            print(f"  ${a:04X} {fn:<20} {per.get(f'{a:04X}', 0)}")
        return

    text = "\n".join(f"{s:04X}" for s in seeds) + "\n"
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(text)
        print(f"# wrote {args.out}: {len(seeds)} seeds")
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
