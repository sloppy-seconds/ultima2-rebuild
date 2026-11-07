#!/usr/bin/env python3
"""demo_span_diff.py — diff the DS mirror a demo recorded against the one a playback ran.

A per-record digest mismatch names a record. This names offsets, which is the difference
between a hypothesis and a finding. Every real divergence this project has root-caused came
from running it: the missing roll counter, the creature layer § 13.2 said was unnecessary,
and the surviving slot-24 fault at record 37.

usage:
    demo_span_diff.py DEMO LIVE_LIVE [RECORD]
    demo_span_diff.py DEMO LIVE_LIVE --all

DEMO      the .demo file. It must have been recorded with --demo-snapshots, which writes
          the full DS span after every record.
LIVE_LIVE the <demo>.live sidecar written by a playback run that was also given
          --demo-snapshots. One @snap block per DIVERGED record, so records that verified
          have no entry and will be reported as such rather than as a zero diff.
RECORD    the 1-based record number to compare. Omit to list what is comparable.

The payload is 51 lines of 32 bytes, not one line. A parser that reads a single line
compares 32 bytes and reports "0 differing" for a span that differs in thirteen -- which
is a real mistake this tool exists to prevent, and the reason the reader below consumes
consecutive hex lines until one is not hex.
"""
import re
import sys

U2_STATE_BYTES = 1616          # U2_DEMO_SPAN: DS:0000..064F

# Creature arrays inside the span, from engine/src/core/state.h. Named so the output says
# "mon_type[24]" and not "DS:01AF"; a slot is a thing a person can reason about, an offset
# is a thing you have to go and look up.
ARRAYS = [
    ("mon_x",    0x137), ("mon_y",    0x157), ("mon_hp",    0x177),
    ("mon_type", 0x197), ("mon_kind", 0x1B7), ("mon_role", 0x1D7),
]
NAMED = {
    0x00: "sector_x", 0x01: "sector_y", 0x13: "player_stamp", 0x12: "view_mode",
    0x23F: "rng_state[0]", 0x240: "rng_state[1]", 0x241: "rng_state[2]",
    0x242: "rng_state[3]", 0x243: "rng_state[4]", 0x244: "rng_state[5]",
}


def label(off: int) -> str:
    if off in NAMED:
        return NAMED[off]
    for name, base in ARRAYS:
        if base <= off < base + 32:
            return f"{name}[{off - base}]"
    return ""


def spans(path: str) -> dict:
    """Every `@snap N` block in a file, keyed by record number, as one hex string."""
    lines = [l.rstrip("\n") for l in open(path, errors="replace")]
    out = {}
    for i, line in enumerate(lines):
        m = re.match(r"@snap (\d+)", line)
        if not m:
            continue
        buf = []
        j = i + 1
        while j < len(lines) and re.fullmatch(r"[0-9a-f]+", lines[j].strip()):
            buf.append(lines[j].strip())
            j += 1
        out[int(m.group(1))] = "".join(buf)
    return out


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    show_all = "--all" in sys.argv
    if len(args) < 2:
        print(__doc__)
        return 2
    demo, live_path = args[0], args[1]
    rec_n = int(args[2]) if len(args) > 2 else None

    rec, live = spans(demo), spans(live_path)
    if not rec:
        print(f"{demo}: no @snap blocks. Re-record with --demo-snapshots.")
        return 1
    if not live:
        print(f"{live_path}: no @snap blocks. That playback either verified every record "
              f"(good news) or did not run with --demo-snapshots.")
        return 1

    common = sorted(set(rec) & set(live))
    if rec_n is None:
        print(f"recorded snaps: {len(rec)}   diverged snaps: {len(live)}")
        print(f"comparable: {common if common else 'none — nothing diverged'}")
        return 0

    if rec_n not in rec:
        print(f"record {rec_n}: not in the recorded snapshots")
        return 1
    if rec_n not in live:
        print(f"record {rec_n}: no live dump, which means it VERIFIED")
        return 0

    a, b = rec[rec_n], live[rec_n]
    n = min(len(a), len(b)) // 2
    if n * 2 != len(a) or n * 2 != len(b):
        print(f"record {rec_n}: span lengths differ ({len(a)//2} vs {len(b)//2} bytes);"
              f" comparing only the first {n}")
    diffs = [i for i in range(n) if a[2 * i:2 * i + 2] != b[2 * i:2 * i + 2]]
    print(f"record {rec_n}: {len(diffs)} differing bytes of {n}")
    for i in diffs[: (200 if show_all else 30)]:
        tag = label(i)
        print(f"  DS:{i:04X}  recorded {a[2*i:2*i+2]}  live {b[2*i:2*i+2]}"
              + (f"   {tag}" if tag else ""))
    if len(diffs) > 30 and not show_all:
        print(f"  ... {len(diffs) - 30} more (--all)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
