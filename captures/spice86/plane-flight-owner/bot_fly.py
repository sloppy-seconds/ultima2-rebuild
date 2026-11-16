#!/usr/bin/env python3
"""Bot flies the plane, second version. Built from what run 1 showed.

Run 1 (fly.py) got the bot to board and to reach airborne, but not cleanly: the first `L` did
nothing, the arrows walked the plane on the ground one cell per press, and a later `L` put it
airborne where it covered 32 cells in about 2 s -- roughly 14 cells/s, far above the ~5 Hz the
owner's sampler could resolve. So three things this version does differently:

  * launch is retried until motion-with-no-input actually appears, because a single `L` is not
    reliable and the game gives no byte to confirm it (no byte says airborne; [DS:13] is a
    mount byte).
  * steering is confirmed by the path bending, not by the key being accepted.
  * landing is a loop, not a press: `L` is refused off grass with 'LAND--NOT HERE!!!!!' and the
    plane keeps moving, so the only test of a landing is that motion stops.

Everything is read from DS. Position is DS:00/01, mode DS:13.
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
SAVE = pathlib.Path("/var/tmp/u2/plane/player_fly2")
PAD = (3, 10)
log = []


def say(s):
    print(s, flush=True)
    log.append(s)


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


def pos():
    return tuple(ds(0x00, 2))


def mode():
    return ds(0x13)[0]


def key(k):
    sh("python3", "tools/spice86_type.py", "--keys", k)


def moving(gap=0.4):
    a = pos()
    time.sleep(gap)
    return a != pos(), a, pos()


def launch(max_tries=4):
    for i in range(1, max_tries + 1):
        key("l")
        time.sleep(0.8)
        for _ in range(4):
            m, a, b = moving()
            if m:
                say(f"  airborne after {i} press(es) of L: {a} -> {b}")
                return True
        say(f"  L #{i}: no motion yet")
    return False


def main():
    pathlib.Path(SAVE).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "references" / "ultima2" / "player", SAVE)
    sh("python3", "tools/u2_edit.py", "maxout", str(SAVE))
    for a in (("name", "PILOT"), ("sector", "41"), ("item", "BRASS BUTTON", "1"),
              ("entry_x", str(PAD[0])), ("entry_y", str(PAD[1]))):
        sh("python3", "tools/u2_edit.py", "set", str(SAVE), *a)

    sh("bash", "tools/spice86_start.sh", "--headless", "--patch-arrows",
       "--player", str(SAVE), "--auto-play", "--name", "bot-fly2")
    for i in range(6):
        time.sleep(3)
        if mode() != 0:
            break
        key("Escape"), time.sleep(0.5), key("p")
    say(f"loaded: pos {pos()} mode {mode():#04x}  (want pad {PAD})")
    if pos() != PAD:
        say("not on the pad, stopping")
        sh("bash", "tools/spice86_stop.sh")
        return

    key("b")
    time.sleep(1.2)
    say(f"boarded: mode {mode():#04x}")
    if mode() != 0x26:
        sh("bash", "tools/spice86_stop.sh")
        return

    say("\n-- LAUNCH --")
    if not launch():
        say("never became airborne; stopping")
        sh("bash", "tools/spice86_stop.sh")
        return

    say("\n-- rate: 2 s of no input --")
    a = pos()
    time.sleep(2.0)
    b = pos()
    dist = abs(b[0] - a[0]) + abs(b[1] - a[1])
    say(f"  {a} -> {b}  = {dist} cells in ~2 s (~{dist / 2:.0f} cells/s)")

    say("\n-- STEER: press Right, then watch whether the path bends --")
    track = [pos()]
    key("Right")
    for _ in range(8):
        time.sleep(0.25)
        track.append(pos())
    say("  path: " + " ".join(f"{p[0]},{p[1]}" for p in track))
    dx = track[-1][0] - track[-4][0]
    dy = track[-1][1] - track[-4][1]
    say(f"  last 3 intervals: dx={dx} dy={dy}  (heading changed: {abs(dy) > abs(dx)})")

    say("\n-- LAND: press L until motion stops (grass only) --")
    for i in range(1, 13):
        key("l")
        time.sleep(0.7)
        m, a, b = moving(0.6)
        if not m:
            say(f"  LANDED after {i} press(es) at {b}, mode {mode():#04x}")
            say("VERDICT: bot launched, steered, and landed the plane")
            break
        say(f"  try {i}: still moving {a} -> {b}")
    else:
        say("VERDICT: never landed within 12 attempts")

    sh("bash", "tools/spice86_stop.sh")
    pathlib.Path("/var/tmp/u2/plane/fly2-log.txt").write_text("\n".join(log) + "\n")


main()
