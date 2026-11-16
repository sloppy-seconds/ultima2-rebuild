#!/usr/bin/env python3
"""G43 sub-case, third attempt: does a GROUNDED plane refuse forest, and is that refusal
specific to the plane?

Attempts 1 and 2 each failed for a reason worth writing down. Attempt 1 mounted correctly at
sector 80's pad (9,40) and the plane refused to step west onto forest -- but every neighbour
of that pad is forest or mountain, so there was no positive control, and "refused forest"
without "accepted something" cannot be told apart from "a grounded plane cannot move at all".
Attempt 2 tried to add a control and destroyed the mount instead: the foot control step walked
the player OFF the pad onto forest, so the following `B` had nothing to board and every
"mounted" probe in that run was actually a foot probe.

So this run uses a synthetic sector (tools/u2_mkprobe.py, background 0x08 grass everywhere)
with one plane pad and one forest tile placed by hand, which is the only way to get the pad,
the forest and an open field of grass as three mutually adjacent cells. Real maps do not
offer that: sector 80's pads sit in a pocket of mountain and forest, and sector 41's sit
behind a 0x78 wall.

Five steps, and the order is the point:
  1 foot  E -> forest   expect ACCEPT   (the map is right; forest is walkable on foot)
  2 foot  W -> pad      expect ACCEPT   (back onto the pad so B can board)
  3 B                 -> DS:13 must become 0x26
  4 plane E -> forest   expect REFUSE   (the plane's own rule, asm/2604's forest test)
  5 plane W -> grass    expect ACCEPT   (the positive control: a grounded plane CAN move)

4 without 5 is not evidence. That is what attempt 1 was missing.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import time
import urllib.request

ROOT = pathlib.Path("/var/home/paul/Projects/decompile-u2-v2")
sys.path.insert(0, str(ROOT / "bot"))
from launch import build_save  # noqa: E402

DS_BASE = 0x08BE * 16
DIR = pathlib.Path("/var/tmp/u2/g43")
SAVE = DIR / "player3"
MAP = DIR / "mapx40"
PAD = (10, 10)
FOREST = (11, 10)
GRASS = (9, 10)
results = []


def sh(*a):
    return subprocess.run(a, cwd=ROOT, capture_output=True, text=True)


def port():
    sys.path.insert(0, str(ROOT / "tools"))
    import s86_ports
    return s86_ports.resolve("http")


def ds(off, n=1):
    with urllib.request.urlopen(
            f"http://127.0.0.1:{port()}/api/memory/{DS_BASE + off}/range/{n}",
            timeout=10) as r:
        return json.load(r)["values"]


def pos():
    return tuple(ds(0x00, 2))


def mode():
    return ds(0x13)[0]


def step(key, want, label):
    before = pos()
    sh("python3", "tools/spice86_type.py", "--keys", key)
    time.sleep(1.5)
    after = pos()
    moved = after != before
    ok = (moved == (want == "ACCEPT"))
    results.append((label, want, "moved" if moved else "refused", "OK" if ok else "MISMATCH"))
    print(f"  {label:28s} want {want:6s} -> {'moved' if moved else 'refused':8s} "
          f"{before}->{after}  {'ok' if ok else '** MISMATCH **'}")


def main():
    # Regenerate the synthetic sector rather than shipping a blob: a grass field from the
    # project's own generator, then two byte patches. Nothing here is copied from a real map,
    # so there is nothing copyrighted to commit.
    sh("python3", "tools/u2_mkprobe.py", "-o", str(MAP))
    b = bytearray(MAP.read_bytes())
    b[PAD[1] * 64 + PAD[0]] = 0x4C
    b[FOREST[1] * 64 + FOREST[0]] = 0x0C
    MAP.write_bytes(bytes(b))
    print(f"patched map: pad {PAD}=0x4c, forest {FOREST}=0x0c, rest grass")

    build_save(str(SAVE), 40, PAD[0], PAD[1] - 1, 10)
    # build_save stands one row below the pad; put it exactly on the pad instead
    sh("python3", "tools/u2_edit.py", "setpos", str(SAVE), str(PAD[0]), str(PAD[1]))

    sh("bash", "tools/spice86_start.sh", "--headless", "--patch-arrows",
       "--player", str(SAVE), "--seed-map", str(MAP), "--auto-play",
       "--name", "g43-plane3")
    time.sleep(3)
    print("loaded pos:", pos(), "mode:", f"{mode():#04x}")

    print("ON FOOT")
    step("Right", "ACCEPT", "1 foot east onto forest")
    step("Left", "ACCEPT", "2 foot west back onto pad")

    sh("python3", "tools/spice86_type.py", "--keys", "b")
    time.sleep(2)
    m = mode()
    print(f"3 B -> mode {m:#04x} (want 0x26)  {'ok' if m == 0x26 else '** MISMATCH **'}")
    results.append(("3 board the plane", "mode 0x26", f"{m:#04x}",
                    "OK" if m == 0x26 else "MISMATCH"))

    print("GROUNDED PLANE")
    step("Right", "REFUSE", "4 plane east onto forest")
    step("Left", "ACCEPT", "5 plane west onto grass")

    print("\n  %-30s %-10s %-9s %s" % ("step", "expected", "observed", "verdict"))
    for r in results:
        print("  %-30s %-10s %-9s %s" % r)
    sh("bash", "tools/spice86_stop.sh")


main()
