#!/usr/bin/env python3
"""Measure the plane's real march rate, properly.

The 12 cells/s figure came from `pos()` before and after a time.sleep(2.0) and a Manhattan
distance. That is wrong in three ways, and the owner's instinct that 12 cells/s is too fast for
a human is probably the tell:

  * the flight loop masks both coordinates & 0x3F, so a plane crossing 0 or 63 produces a
    bogus distance -- it can look like 25 cells when it wrapped, or 1 when it crossed the seam;
  * a sector change reloads the position, so a jump can be a map transition and not flight at
    all;
  * the two reads bracket more than the sleep, so the elapsed time is not really 2 s.

So: poll fast, record every sample with its own timestamp and the sector, and count only
transitions that are single-cell steps within one sector. That is a march, and anything else is
labelled. This also happens to be G77, the plane's march-step cost, which has been open as
"borrowed 103 ms floor" since it was filed.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import time
import urllib.request

ROOT = pathlib.Path("/var/home/paul/Projects/decompile-u2-v2")
DS = 0x08BE * 16
SAVE = pathlib.Path("/var/tmp/u2/plane/player_rate")
PAD = (3, 10)
samples = []


def sh(*a):
    return subprocess.run(a, cwd=ROOT, capture_output=True, text=True)


def port():
    sys.path.insert(0, str(ROOT / "tools"))
    import s86_ports
    return s86_ports.resolve("http")


def ds(off, n=1):
    with urllib.request.urlopen(
            f"http://127.0.0.1:{port()}/api/memory/{DS + off}/range/{n}", timeout=5) as r:
        return json.load(r)["values"]


def state():
    v = ds(0x00, 2)
    s = ds(0x49, 2)
    return v[0], v[1], s[0] * 10 + s[1], ds(0x13)[0]


def key(k):
    sh("python3", "tools/spice86_type.py", "--keys", k)


def main():
    pathlib.Path(SAVE).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "references" / "ultima2" / "player", SAVE)
    sh("python3", "tools/u2_edit.py", "maxout", str(SAVE))
    for a in (("name", "PILOT"), ("sector", "41"), ("item", "BRASS BUTTON", "1"),
              ("entry_x", str(PAD[0])), ("entry_y", str(PAD[1]))):
        sh("python3", "tools/u2_edit.py", "set", str(SAVE), *a)
    sh("bash", "tools/spice86_start.sh", "--headless", "--patch-arrows",
       "--player", str(SAVE), "--auto-play", "--name", "bot-rate")
    for _ in range(6):
        time.sleep(3)
        if state()[3] != 0:
            break
        key("Escape"), time.sleep(0.4), key("p")
    x, y, sec, md = state()
    print(f"loaded pos ({x},{y}) sector {sec} mode {md:#04x}")
    if (x, y) != PAD:
        print("not on pad; stopping")
        sh("bash", "tools/spice86_stop.sh")
        return
    key("b")
    time.sleep(1.2)
    print(f"boarded mode {state()[3]:#04x}")
    for i in range(4):
        key("l")
        time.sleep(0.6)
        a = state()
        time.sleep(0.25)
        if state()[:2] != a[:2]:
            print(f"airborne after {i+1} L")
            break

    print("\n-- sampling at max practical rate for 4 s --")
    t0 = time.time()
    while time.time() - t0 < 4.0:
        s = state()
        samples.append((time.time() - t0, *s))
    n = len(samples)
    span = samples[-1][0] - samples[0][0]
    print(f"{n} samples in {span:.2f}s = {n/span:.0f} Hz")

    steps = wraps = jumps = same = 0
    prev = samples[0]
    for s in samples[1:]:
        if (s[1], s[2]) == (prev[1], prev[2]):
            same += 1
        elif s[3] != prev[3]:
            jumps += 1
        elif abs(s[1] - prev[1]) + abs(s[2] - prev[2]) == 1:
            steps += 1
        elif (abs(s[1] - prev[1]) == 1 and prev[1] in (0, 63)) or \
             (abs(s[2] - prev[2]) == 1 and prev[2] in (0, 63)):
            wraps += 1
        else:
            jumps += 1
        prev = s
    moved = steps + wraps
    print(f"\ntransitions: {steps} single-cell steps, {wraps} seam crossings, "
          f"{jumps} jumps (sector change / teleport), {same} unchanged")
    if moved:
        rate = moved / span
        print(f"\nRATE: {rate:.1f} cells/s  ->  {1000/rate:.0f} ms per cell")
        print("compare: U2_PLANE_STEP_MS is a borrowed 103 ms floor (G77)")
    print("\nfirst 12 samples:")
    for s in samples[:12]:
        print("  t=%.3f pos=(%d,%d) sec=%d mode=%#04x" % s)
    sh("bash", "tools/spice86_stop.sh")


main()
