"""Live capture for the overworld -> dungeon transition and torch ignition.

Why this exists as its own probe rather than reusing turn_recorder: the dungeon
transition is not a turn-boundary event, it is a mode change, and the things that
matter are not in the turn odometer. This watches for all of them at once:

  * DS:49/4A sector digits  -- did entering load a different mapxNN (as the
                               castle did, 00 -> 03), or is the same grid being
                               reinterpreted?
  * the live 4 KB grid       -- matched against every original/mapx* file each
                               time it changes, so a new map is named, not guessed
  * video mode (BIOS 0x449)  -- a first-person 3D view may switch text/graphics
                               mode; this is the only place that shows it
  * VGA ink                  -- non-zero pixel bytes in the video span. "dark
                               until you light a torch" should read as ~0 ink
                               while dark and non-zero after ignition.
  * DS:64 torches, DS:15/18  -- the mode-chain pair from the passability work,
                               plus the torch count so ignition is visible

Full DS + VGA snapshots are written on every detected event so a later tool can
re-render history instead of re-running the game.

Read-only against the emulator. Writes only under --out.

Usage: python3 tools/dungeon_capture.py [port] [--out DIR] [--hz 20] [--seconds N]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import pathlib
import sys
import time
import urllib.request

DS = 0x08BE
DS_LEN = 0x2800
VGA_BASE = 0xB8000
VGA_LEN = 0x1900
# BIOS video mode lives at 0040:0049. Physical = seg*16 + off = 0x400 + 0x49 = 0x449.
# Multiplying the whole address by 16 reads 0x4490 instead, which is unrelated RAM.
BDA_MODE_PHYS = 0x40 * 16 + 0x49


def rd(phys, n, port):
    url = f"http://127.0.0.1:{port}/api/memory/{phys}/range/{n}"
    return bytes(json.load(urllib.request.urlopen(url, timeout=5))["values"])


def sector_match(grid, maps):
    best, name = -1, "?"
    for nm, data in maps.items():
        same = sum(1 for a, b in zip(grid, data) if a == b)
        if same > best:
            best, name = same, nm
    return name, best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", type=int, default=None,
                    help="default: the live session's recorded port (see tools/s86_ports.py)")
    ap.add_argument("--out", default="/var/tmp/dungeon-cap")
    ap.add_argument("--hz", type=float, default=20.0)
    ap.add_argument("--seconds", type=float, default=600.0)
    a = ap.parse_args()
    if a.port is None:
        import s86_ports          # sys.path carries tools/ when run as tools/<name>.py
        a.port = s86_ports.resolve("http")

    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    idx = open(out / "events.jsonl", "a", buffering=1)

    maps = {os.path.basename(p): open(p, "rb").read()[:4096]
            for p in glob.glob("original/mapx*")}

    prev = None
    n = 0
    end = time.time() + a.seconds
    period = 1.0 / a.hz
    print(f"capturing on port {a.port} at {a.hz:g} Hz -> {out}")
    while time.time() < end:
        t0 = time.time()
        try:
            # Read a block wide enough for every field used below. The fields
            # reach DS:66, so a 0x60-byte read indexes past the end; this bit me
            # once, hence the explicit size rather than an exact span.
            h = rd(DS * 16, 0x100, a.port)
            mode = rd(BDA_MODE_PHYS, 1, a.port)[0]
            vga = rd(VGA_BASE, VGA_LEN, a.port)
            grid = rd(DS * 16 + 0x1800, 4096, a.port)
        except Exception as exc:                    # emulator gone or busy
            sys.stdout.write(f"read failed: {exc}\n")
            sys.stdout.flush()
            time.sleep(1.0)
            continue

        ink = sum(1 for b in vga if b)
        sig = (h[0x49], h[0x4A], mode, h[0x13], h[0x15], h[0x18], h[0x64],
               h[0x51], h[0x52])
        if sig != prev:
            name, same = sector_match(grid, maps)
            rec = {
                "n": n, "t": round(time.time(), 3),
                "pos": [h[0], h[1]],
                "sector_digits": [h[0x49], h[0x4A]],
                "grid_best_match": name, "grid_match_cells": same,
                "video_mode": mode,
                "vga_ink": ink,
                "riding": h[0x13], "DS15": h[0x15], "DS18": h[0x18],
                "torches_DS64": h[0x64],
                "hp_bcd": h[0x51:0x53].hex(),
            }
            (out / f"ev{n:03d}.ds").write_bytes(rd(DS * 16, DS_LEN, a.port))
            (out / f"ev{n:03d}.vga").write_bytes(vga)
            idx.write(json.dumps(rec) + "\n")
            print(f"  ev{n:3d} pos=({h[0]:2d},{h[1]:2d}) sector={h[0x49]}{h[0x4A]} "
                  f"map={name}:{same}/4096 mode={mode} ink={ink:5d} "
                  f"ride={h[0x13]:02x} DS15={h[0x15]:02x} DS18={h[0x18]:02x} "
                  f"torches={h[0x64]:02x} hp={h[0x51:0x53].hex()}")
            prev = sig
            n += 1
        sleep = period - (time.time() - t0)
        if sleep > 0:
            time.sleep(sleep)
    idx.close()
    print(f"done: {n} events -> {out / 'events.jsonl'}")


if __name__ == "__main__":
    main()
