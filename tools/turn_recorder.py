#!/usr/bin/env python3
"""Turn-triggered recorder for a live Ultima II session.

Design, and why it is not a fixed-rate logger:

Ultima II is turn-based. The world advances only when the player acts or when
the idle auto-PASS fires, and every such turn changes food by exactly 19
(accepted move) or 10 (pass). Food is therefore a monotonic odometer of turns,
which gives two things a wall-clock logger cannot:

  * a change trigger, so snapshots land on turn boundaries rather than
    somewhere between them; and
  * a model check, because a food delta that is negative or cannot be written as
    19a + 10b means the model is wrong or a read was torn, and we say so instead
    of quietly recording a wrong history.

The odometer verdict is the FOOD MODEL only. It deliberately does NOT require the
per-sample move flag to agree with the decomposed move count: the game writes
player position and food non-atomically, so one accepted move can be sampled as
(delta=0, moved=True) on one poll and (delta=19, moved=False) on the next. That
split is a sampling artifact, not a lost turn or a model violation. Measured on
the 2026-10-21 castle-03 fight (3000 turns), re-derived 2026-10-22 directly from
the raw `turns/*.ds` blobs: of the 2999 consecutive-sample deltas, 2912 spent no
food at all (a rejected or idle step) and exactly 87 spent any, and every one of
those 87 spent exactly 19. Zero non-decomposable deltas.

Two accuracy notes, because the earlier wording invited a misreading:

  * "positive" here means food CONSUMED (previous minus current). Food itself
    falls; the raw readings are all decreases. A reader who took "positive" as
    "food went up" would conclude the opposite of what the bytes show.
  * No delta of 10 appears anywhere in this capture, so the 10/pass half of the
    model is UNEXERCISED by this data, not confirmed by it. The 19/move half is
    confirmed 87 times. Do not cite this capture as evidence for the pass cost.

All 76 prior "anomalies" were such split pairs or zero-cost rejects, i.e. the old
check was over-coupling food to a non-atomic position read.

Polling is fast on purpose: the memory API sustains 250 req/s at 0.3 ms with no
errors (measured 2026-10-21). The old 3 Hz limit belonged to the windowed CFG
view model, not to memory reads.

Each turn writes a raw snapshot (DS region + VGA) so any later tool can re-render
it, plus one JSONL index line with the derived facts.

Read-only against the emulator. Writes only under the output directory.

Usage: tools/turn_recorder.py [port] [--out DIR] [--hz 50] [--limit N]
"""
from __future__ import annotations

import argparse
import json
import pathlib
import time
import urllib.request

DS = 0x08BE
DS_LEN = 0x2800          # state block plus the 4 KB sector buffer
VGA_BASE = 0xB8000
VGA_LEN = 0x1900         # the span the game's own screen-invert routine touches
# BIOS video mode lives at 0040:0049. Physical = seg*16 + off = 0x400 + 0x49 =
# 0x449. Multiplying the whole address by 16 reads 0x4490 instead, unrelated RAM.
BDA_MODE_PHYS = 0x40 * 16 + 0x49

FOOD_PER_MOVE = 19
FOOD_PER_PASS = 10


def rd(port, phys, n):
    url = f"http://127.0.0.1:{port}/api/memory/{phys}/range/{n}"
    with urllib.request.urlopen(url, timeout=5) as r:
        return bytes(json.load(r)["values"])


def bcd3(b):
    """Three packed-decimal bytes, most significant pair first."""
    return int("".join(f"{x:02X}" for x in b))


def decompose(delta):
    """Can this food delta be made from a moves and b passes? Return one.

    delta == 0 returns (0, 0): a turn that spent no food (a rejected step) is
    model-consistent, not an anomaly.
    """
    for a in range(0, delta // FOOD_PER_MOVE + 1):
        rem = delta - a * FOOD_PER_MOVE
        if rem >= 0 and rem % FOOD_PER_PASS == 0:
            return a, rem // FOOD_PER_PASS
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", type=int, default=None,
                    help="default: the live session's recorded port (see tools/s86_ports.py)")
    ap.add_argument("--out", default="/var/tmp/u2-turns")
    ap.add_argument("--hz", type=float, default=50.0)
    ap.add_argument("--limit", type=int, default=4000,
                    help="stop after this many recorded turns")
    args = ap.parse_args()
    if args.port is None:
        import s86_ports          # sys.path carries tools/ when run as tools/<name>.py
        args.port = s86_ports.resolve("http")

    out = pathlib.Path(args.out)
    (out / "turns").mkdir(parents=True, exist_ok=True)
    idx_path = out / "index.jsonl"
    idx = open(idx_path, "a", buffering=1)

    period = 1.0 / args.hz
    ds_phys = DS * 16
    prev = None
    n = 0
    anomalies = 0
    print(f"recording on port {args.port} at {args.hz:g} Hz -> {out}")
    print("polls a 96-byte signature; full DS + VGA only on a real turn")
    try:
        while n < args.limit:
            t0 = time.time()
            head = rd(args.port, ds_phys, 0x60)
            sig = (head[0], head[1], head[0x53], head[0x54], head[0x55],
                   head[0x20])
            if prev is None:
                prev = sig
                print(f"baseline: pos=({head[0]},{head[1]}) "
                      f"food={bcd3(head[0x53:0x56])} hp={bcd3(head[0x51:0x53])}")
            elif sig != prev:
                ds = rd(args.port, ds_phys, DS_LEN)
                vga = rd(args.port, VGA_BASE, VGA_LEN)
                mode = rd(args.port, BDA_MODE_PHYS, 1)[0]
                px, py = head[0], head[1]
                food = bcd3(head[0x53:0x56])
                delta = bcd3(bytes(prev[2:5])) - food
                # Odometer verdict = the food model only: the delta must be a
                # non-negative combination of 19 (move) and 10 (pass). It is
                # NOT tied to the per-sample move flag, because position and
                # food update non-atomically and one move can split across two
                # polls (see module docstring). A negative or non-decomposable
                # delta is the real failure the odometer exists to catch.
                dec = decompose(delta) if delta >= 0 else None
                ok = dec is not None
                if not ok:
                    anomalies += 1
                moved = (px, py) != (prev[0], prev[1])
                # DS:20 is NOT a reject signal. engine/src/core/state.h says so
                # explicitly: "DISPUTED: docs called this an invalid-move counter,
                # but the harvest shows it decremented in the walk loop and
                # overwritten at runtime 0x2BC7. Treat as scratch; do not use as a
                # reject signal." An earlier version of this recorder labelled turns
                # "reject" from it, which asserted a mechanic off a scratch byte.
                # A turn with no move and no food spent is therefore UNRESOLVED,
                # not a rejection: it may be a reject, an idle auto-PASS, or a
                # dropped keystroke. Distinguishing them is item G0 in
                # docs/needs-gameplay.md and needs a live signal this recorder
                # does not have.
                scratch_changed = head[0x20] != prev[5]
                rec = {
                    "n": n, "t": round(time.time(), 3),
                    "pos": [px, py], "food": food, "delta": delta,
                    "moves": dec[0] if dec else None,
                    "passes": dec[1] if dec else None,
                    "moved": moved,
                    "stalled": (delta == 0 and not moved),
                    "scratch_changed": scratch_changed,
                    "odometer_ok": ok,
                    "scratch_20": head[0x20],
                    "hp": bcd3(head[0x51:0x53]),
                    "gold": bcd3(head[0x58:0x5A]),
                    "exp": bcd3(head[0x56:0x58]),
                    "monsters": [[ds[0x137 + i], ds[0x157 + i], ds[0x197 + i]]
                                 for i in range(32)
                                 if ds[0x137 + i] or ds[0x157 + i]],
                }
                (out / "turns" / f"{n:05d}.ds").write_bytes(ds)
                (out / "turns" / f"{n:05d}.vga").write_bytes(vga)
                # The game runs in CGA graphics mode (mode 4), so 0xB800 holds
                # pixels, not a text page -- an ASCII read of it is garbage. The
                # honest frame signal is the ink metric (non-zero pixel bytes)
                # that proved a blank frame was real darkness in dungeon_capture,
                # plus the BIOS video mode that shows why text would be junk.
                rec["video_mode"] = mode
                rec["vga_ink"] = sum(1 for b in vga if b)
                idx.write(json.dumps(rec) + "\n")
                flag = "" if ok else "  <-- ODOMETER ANOMALY"
                kind = ("stalled" if (delta == 0 and not moved)
                        else f"{rec['moves']}mv/{rec['passes']}pass")
                print(f"  turn {n:4d} pos=({px:2d},{py:2d}) d={delta:>5} "
                      f"[{kind}] hp={rec['hp']:5d} gold={rec['gold']:5d} "
                      f"mons={len(rec['monsters'])}{flag}")
                prev = sig
                n += 1
            sleep = period - (time.time() - t0)
            if sleep > 0:
                time.sleep(sleep)
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        idx.close()
        print(f"recorded {n} turns, {anomalies} odometer anomalies -> {idx_path}")


if __name__ == "__main__":
    main()
