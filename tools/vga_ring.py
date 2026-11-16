#!/usr/bin/env python3
"""Continuous VGA frame ring.

Why this exists and turn_recorder.py does not solve it: combat messages are
drawn and overwritten inside a single turn. A capture taken at turn boundaries
was measured over 151 turns of a real fight and contained zero readable text —
only map art — even though memory proved a kill happened. So the frame must be
sampled continuously and every distinct frame kept, rather than sampled on
state change.

The memory API sustains ~250 req/s at 0.3 ms (measured 2026-10-21), so a few
hundred Hz is available and a transient message has a real chance of being
caught.

Each distinct frame is written raw so any later tool can re-render it, and the
index carries the rendered text plus the turn state at that instant, so a
message can be tied to the turn that produced it.

Read-only against the emulator. Writes only under --out.

Usage: tools/vga_ring.py [port] [--out DIR] [--hz 120] [--max-frames N]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import time
import urllib.request

DS = 0x08BE
VGA_BASE = 0xB8000
VGA_LEN = 0x1900
ROWS, COLS = 25, 80


def rd(port, phys, n):
    url = f"http://127.0.0.1:{port}/api/memory/{phys}/range/{n}"
    with urllib.request.urlopen(url, timeout=5) as r:
        return bytes(json.load(r)["values"])


def bcd(b):
    """Packed decimal: render bytes as hex digits, read the result as decimal."""
    return int("".join(f"{x:02X}" for x in b) or "0")


def render(vga):
    out = []
    for r in range(ROWS):
        line = ""
        for c in range(COLS):
            i = (r * COLS + c) * 2
            ch = vga[i] if i < len(vga) else 0
            line += chr(ch) if 32 <= ch < 127 else " "
        out.append(line.rstrip())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", type=int, default=None,
                    help="default: the live session's recorded port (see tools/s86_ports.py)")
    ap.add_argument("--out", default="/var/tmp/u2-vga")
    ap.add_argument("--hz", type=float, default=120.0)
    ap.add_argument("--max-frames", type=int, default=6000)
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="stop after this many seconds (0 = run until Ctrl-C)")
    args = ap.parse_args()
    if args.port is None:
        import s86_ports          # sys.path carries tools/ when run as tools/<name>.py
        args.port = s86_ports.resolve("http")

    out = pathlib.Path(args.out)
    (out / "frames").mkdir(parents=True, exist_ok=True)
    idx = open(out / "frames.jsonl", "a", buffering=1)
    period = 1.0 / args.hz
    ds_phys = DS * 16

    last_hash = None
    kept = 0
    sampled = 0
    t_start = time.time()
    print(f"VGA ring on port {args.port} at {args.hz:g} Hz -> {out}")
    print("keeps every DISTINCT frame; dedupes identical ones")
    try:
        while kept < args.max_frames:
            t0 = time.time()
            if args.seconds and time.time() - t_start > args.seconds:
                break
            vga = rd(args.port, VGA_BASE, VGA_LEN)
            sampled += 1
            h = hashlib.sha1(vga).hexdigest()
            if h != last_hash:
                last_hash = h
                head = rd(args.port, ds_phys, 0x60)
                lines = [l for l in render(vga) if l.strip()]
                (out / "frames" / f"{kept:05d}.vga").write_bytes(vga)
                idx.write(json.dumps({
                    "n": kept, "t": round(time.time(), 4), "sha": h[:12],
                    "pos": [head[0], head[1]],
                    "hp": bcd(head[0x51:0x53]),
                    "food": bcd(head[0x53:0x56]),
                    "gold": bcd(head[0x58:0x5A]),
                    "exp": bcd(head[0x56:0x58]),
                    "lines": lines,
                }) + "\n")
                text = " | ".join(l.strip() for l in lines
                                  if any(ch.isalpha() for ch in l))
                note = text[:80] if text else ""
                print(f"  frame {kept:5d} pos=({head[0]:2d},{head[1]:2d}) "
                      f"hp={bcd(head[0x51:0x53]):5d} {note}")
                kept += 1
            sleep = period - (time.time() - t0)
            if sleep > 0:
                time.sleep(sleep)
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        idx.close()
        print(f"sampled {sampled} frames, kept {kept} distinct -> "
              f"{out / 'frames.jsonl'}")


if __name__ == "__main__":
    main()
