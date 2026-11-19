#!/usr/bin/env python3
"""CP5 sound gate: the step thud and the refusal buzz, in the order the original plays them.

Evidence: captures/spice86/step-sound-20261117. The original, under Spice86, was probed with
two independent instruments — the register dump at asm/5CD2_speaker_freq while a tone played,
and polling Spice86's own PC-speaker state for the span the gate stayed up. Twelve step
attempts, six accepted and four refused:

    accepted step   two tones of divisor 0x4000 (73 Hz), 65,000 cycles each, 642,175 cycles
                    between their starts
    refused step    the same two tones, then one tone of divisor 0x500 (932 Hz), 835,000
                    cycles, printed with --INVALID MOVE!

Why this is a separate checker and not a grep: the naive assertion — "did the engine sound the
speaker during a walk" — is INERT. The warp tone and the wound tone already call seam_tone, so
it goes green with the step wiring deleted. What has to be asserted is the divisor, the count,
the gap and the ORDER, and a refused step has to be shown to actually have been refused, or
the refusal half is testing nothing at all.

Controls, all of which were run and did go red:
  - delete the u2_sound_step call in u2_overworld_step_food -> the accepted-step case fails
    with zero tones, not with a skip;
  - move u2_sound_refusal above the thumps -> the refused-step case fails on order;
  - remove the mute guard from seam_tone -> both mute halves fail with six tones instead
    of four and one swallowed emission instead of three;
  - remove the fidelity check from u2_feedback -> only the classic mute half fails, for
    inventing a notice the original's five-byte handler does not print.

Usage: sound_check.py <engine-binary> <work-dir>
Exit 0 = both cases matched. Exit 1 = a divergence. Exit 2 = the scenario did not happen as
claimed, which is never allowed to read as a pass.
"""
import os
import re
import shutil
import subprocess
import sys

# The engine's own key bytes (core/keys.h): U2_KEY_EAST, U2_KEY_WEST.
EAST = "0xf3"
WEST = "0xf2"
# Ctrl-S: the original's sound key (asm/4F76 compares it, asm/4F7B toggles DS:54D).
MUTE = "0x13"

WATER_COLUMN = 10     # mapx column filled with open water, which refuses a step on foot
START_X = 11          # one cell east of it: WEST is refused, EAST is grass
START_Y = 5

TONE = re.compile(r"seam=speaker_freq asm=5CD2 divisor=(\d+) ms=(\d+)")
SILENCE = re.compile(r"seam=speaker_silence asm=5CE4 ms=(\d+)")
TEXT = re.compile(r"seam=str_out asm=4FDF text=(.*)")


TONE = re.compile(r"seam=speaker_freq asm=5CD2 divisor=(\d+) ms=(\d+)")
SILENCE = re.compile(r"seam=speaker_silence asm=5CE4 ms=(\d+)")
MUTED = re.compile(r"seam=speaker_mute asm=(5CB2|5CE4)")
TEXT = re.compile(r"seam=str_out asm=4FDF text=(.*)")


def run(engine, work, case, keys=None, mode=None):
    keys = keys or (EAST if case == "east" else WEST) + "\n"
    with open(os.path.join(work, f"{case}.keys"), "w") as f:
        f.write(keys)
    env = dict(os.environ)
    if mode:
        env["U2_MODE"] = mode
    # --config into the work dir, not the default config.ini in the cwd. This gate
    # presses Ctrl-S, and Ctrl-S now writes its setting back so the player does not
    # have to set it again next session; without this the gate would leave a
    # config.ini in whatever directory CI happened to invoke it from.
    with open(os.path.join(work, "config.ini"), "w") as f:
        f.write("[game]\nsound = on\n")
    r = subprocess.run([engine, "--headless", "--play-map", "--cleanroom",
                        "--data-dir", os.path.join(work, "content"),
                        "--save-dir", os.path.join(work, "save"),
                        "--config", os.path.join(work, "config.ini"),
                        "--script", os.path.join(work, f"{case}.keys"), "--trace"],
                       capture_output=True, text=True, timeout=120, env=env)
    return (r.stdout or "") + (r.stderr or "")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    engine, work = (os.path.abspath(p) for p in sys.argv[1:3])
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))

    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    seed = subprocess.run([sys.executable, os.path.join(here, "cp5_fight_seed.py"), work],
                          capture_output=True, text=True)
    if seed.returncode != 0:
        print("sound-check: FAIL (the synthetic content did not build)")
        print(seed.stderr)
        return 1

    # A map the refusal is certain on: all grass, one column of open water. Grass is the
    # measured passable byte (docs/passability-findings.md F10) and open water is one of
    # the two bytes the passability gate is proven to refuse on foot.
    grid = bytearray([0x08]) * 4096
    for y in range(64):
        grid[y * 64 + WATER_COLUMN] = 0x00
    with open(os.path.join(work, "content", "mapx00"), "wb") as f:
        f.write(bytes(grid))

    rec = os.path.join(work, "save", "PLAYER", "PLAYER")
    subprocess.run([sys.executable, os.path.join(root, "tools", "u2_edit.py"), "setpos",
                    rec, str(START_X), str(START_Y)], capture_output=True, check=False)

    fails = []

    # --- accepted step -------------------------------------------------------
    out = run(engine, work, "east")
    tones = TONE.findall(out)
    gaps = SILENCE.findall(out)
    if "--INVALID MOVE!" in "".join(TEXT.findall(out)):
        print("sound-check: FAIL (the east step was refused too; the scenario proves nothing)")
        return 2
    if tones != [("16384", "14"), ("16384", "14")]:
        fails.append(f"accepted step: expected two 0x4000/14 ms thumps, got {tones}")
    if gaps != ["121"]:
        fails.append(f"accepted step: expected one 121 ms gap between the thumps, got {gaps}")
    if any(int(d) == 1280 for d, _ in tones):
        fails.append("accepted step: a refused-step buzz appeared on a step that was accepted")

    # --- refused step --------------------------------------------------------
    out = run(engine, work, "west")
    seq = [(m.group(1), m.group(2)) for m in TONE.finditer(out)]
    texts = TEXT.findall(out)
    if not any("--INVALID MOVE!" in t for t in texts):
        print("sound-check: FAIL (the west step was not refused; the buzz half proves nothing)")
        return 2
    if seq != [("16384", "14"), ("16384", "14"), ("1280", "175")]:
        fails.append(f"refused step: expected thud, thud, buzz; got {seq}")
    # Order, because the original's buzz belongs to the refusal and not to the step:
    # the buzz must come after the complaint reached the screen, not before it. The
    # comparison is positions in one trace stream, not indices into two separate lists.
    buzz_at = out.find("divisor=1280")
    complaint_pos = out.index("--INVALID MOVE!")
    if buzz_at < 0:
        fails.append("refused step: no buzz at all")
    elif buzz_at < complaint_pos:
        fails.append("refused step: the buzz sounded before the complaint was printed")

    if fails:
        for f in fails:
            print(f"sound-check: FAIL {f}")
        return 1

    # --- Ctrl-S mutes, and unmutes ---------------------------------------------
    # asm/4F76 is the key poll comparing AL against 0x13 and asm/4F7B is the handler:
    # `xorb $0xff,0x54d` and back into the poll with AH=0. So this is the game's own
    # sound key, and three properties come with the bytes rather than with taste: the
    # byte is consumed and never reaches a command, the flag toggles (both directions,
    # which is why one press cannot prove the gate), and the handler prints nothing.
    #
    # Run in both modes because they are supposed to differ in exactly one of those:
    # classic stays silent, because asm/4F7B is five bytes and none of them is output;
    # enhanced announces through the one seam that invents text. A gate in one mode
    # only would let the other mode drift.
    seq = "\n".join([EAST, MUTE, EAST, MUTE, EAST]) + "\n"
    for mode, want_notice in (("classic", False), ("enhanced", True)):
        out = run(engine, work, f"mute-{mode}", keys=seq, mode=mode)
        tones = TONE.findall(out)
        hush = MUTED.findall(out)
        # Two thuds, then one muted step (three swallowed emissions: tone, gap, tone),
        # then two thuds again. Anything else and the toggle is a one-way switch or the
        # flag is not read at all.
        if tones != [("16384", "14")] * 4:
            fails.append(f"mute [{mode}]: expected two thuds, a silent step, two thuds; got {tones}")
        if len(hush) != 3:
            fails.append(f"mute [{mode}]: expected three swallowed emissions during the muted step, got {len(hush)}")
        if "ILLEGAL" in out:
            fails.append(f"mute [{mode}]: Ctrl-S reached the command loop; the original swallows it in the poll")
        notices = ("[sound off]" in out, "[sound on]" in out)
        if want_notice and notices != (True, True):
            fails.append(f"mute [{mode}]: enhanced must announce the toggle, saw {notices}")
        if not want_notice and any(notices):
            fails.append(f"mute [{mode}]: classic invented a notice; asm/4F7B prints nothing")

    if fails:
        for f in fails:
            print(f"sound-check: FAIL {f}")
        return 1
    print("sound-check: ok -- thud-thud on a step, thud-thud-buzz on a refusal, in that order;"
          " Ctrl-S mutes and unmutes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
