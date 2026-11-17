#!/usr/bin/env python3
"""
tests/prompt_gate.py — a question must be on the screen before the game waits for its answer.

The original writes text to 0xB8000 and the CRT is always displaying it, so it has no
present-before-read step at all. A port with a back buffer does, and the presenting loop sits
ABOVE the command handler: a handler that prints "READY WEAPON: ... WHICH?" and then blocks
puts the menu into a frame nobody has shown, so it reaches the screen only when the handler
returns — after the player has typed blind.

Owner-reported 2026-11-02 as a crash: buying a mace in Linda, pressing R, and getting what
looked like a frozen game. W did the same. cmd_M had the identical defect and nobody had
noticed. The shops hit it earlier still, which is why commerce.c grew show_and_wait.

The gate reads the engine's own trace, which records every seam call in order, and asserts
that between the prompt and the blocking read there is a present. That is the user-visible
property, not a proxy for it.

Run from engine/:
    python3 tests/prompt_gate.py            # gate
    python3 tests/prompt_gate.py --selftest # prove it can go red
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
ROOT = os.path.dirname(ENGINE)

# (label, seed case, keys, the token the prompt prints per mode, the digit the player answers)
#
# The token differs by mode in one case, and that is a real difference rather than a
# sloppy expectation: enhanced mode announces through feedback.h where the original was
# terse, so `W` prints `WEAR ARMOUR:` instead of the original's bare `WHICH?` (owner
# request, 2026-11-11). What this gate is about is the ORDER — menu on screen before the
# loop blocks for the digit — and that property has to hold in both modes, so both run
# and each is asked for the words that mode actually prints.
CASES = [
    ("R ready weapon", "armour", "R\n3\n",
     {"classic": "READY WEAPON:", "enhanced": "READY WEAPON:"}),
    ("W wear armour",  "armour", "W\n1\n",
     {"classic": "WHICH?", "enhanced": "WEAR ARMOUR:"}),
    ("M ready spell",  "armour", "M\n1\n",
     {"classic": "MAGIC SPELL READY #", "enhanced": "MAGIC SPELL READY #"}),
]

MODES = ("classic", "enhanced")


def check(trace_lines, token):
    """(ok, explanation) for one handler's trace."""
    prompt = None
    for i, line in enumerate(trace_lines):
        if "text=" in line and token in line:
            prompt = i
            break
    if prompt is None:
        return False, "the prompt %r was never printed" % token

    wait = None
    for i in range(prompt + 1, len(trace_lines)):
        if "seam=key_wait" in trace_lines[i]:
            wait = i
            break
    if wait is None:
        return False, "no blocking read followed the prompt at line %d" % (prompt + 1)

    for i in range(prompt + 1, wait):
        if "present=menu" in trace_lines[i]:
            return True, "presented at line %d, before the read at line %d" % (i + 1, wait + 1)

    return False, ("prompt at line %d, read at line %d, nothing presented between: "
                   "the player types blind" % (prompt + 1, wait + 1))


def run_case(label, seed, keys, token, workdir, mode):
    case_dir = os.path.join(workdir, "%s.%s" % (label.replace(" ", "_"), mode))
    if subprocess.run([sys.executable, os.path.join(HERE, "cp6_commerce_seed.py"),
                       case_dir, seed], capture_output=True).returncode != 0:
        return False, "seed failed"
    keyfile = os.path.join(case_dir, "k.keys")
    with open(keyfile, "w") as f:
        f.write(keys)
    trace = os.path.join(case_dir, "k.trace")
    # The mode is passed in the child's environment rather than as a flag so this gate
    # measures the same thing the player's default does, and so it keeps working when
    # the caller has pinned U2_MODE for a whole checkpoint.
    env = dict(os.environ, U2_MODE=mode)
    subprocess.run([os.path.join(ENGINE, "u2"), "--headless", "--play-map", "--cleanroom",
                    "--data-dir", os.path.join(case_dir, "content"),
                    "--save-dir", os.path.join(case_dir, "save"),
                    "--script", keyfile, "--trace=" + trace],
                   cwd=ENGINE, capture_output=True, env=env)
    if not os.path.isfile(trace):
        return False, "no trace written"
    with open(trace, "r", errors="replace") as f:
        return check(f.read().splitlines(), token)


def run_gate():
    binary = os.path.join(ENGINE, "u2")
    if not os.path.isfile(binary):
        print("SKIP  engine/u2 not built; run `make -C engine` first")
        return 0
    bad = 0
    with tempfile.TemporaryDirectory(prefix="u2-promptgate-") as tmp:
        for label, seed, keys, tokens in CASES:
            for mode in MODES:
                ok, why = run_case(label, seed, keys, tokens[mode], tmp, mode)
                print("%s  %s [%s]: %s"
                      % ("PASS" if ok else "FAIL", label, mode, why))
                if not ok:
                    bad += 1
    print("PROMPT GATE: %s" % ("PASS" if bad == 0 else "FAIL (%d)" % bad))
    return 0 if bad == 0 else 1


def synthetic(order):
    """A trace body from a list of event names, for the controls."""
    lines = []
    for ev in order:
        if ev == "prompt":
            lines.append("trace: seam=str_out asm=4FDF text=READY WEAPON:|9-QU, WHICH? ")
        elif ev == "present":
            lines.append("trace: seam=present asm=- present=menu before=key_wait")
        elif ev == "wait":
            lines.append("trace: seam=key_wait asm=4F4D got=1 ch=0x33")
        else:
            lines.append("trace: seam=tone asm=5CD2 divisor=1 ms=1")
    return lines


def run_selftest():
    controls = [
        ("prompt, present, read passes", synthetic(["prompt", "present", "wait"]), True),
        ("prompt, read with no present must go red",
         synthetic(["prompt", "wait"]), False),
        ("present AFTER the read must go red",
         synthetic(["prompt", "wait", "present"]), False),
        ("a present that is not the menu present must go red",
         synthetic(["prompt", "other", "wait"]), False),
        ("a missing prompt must go red", synthetic(["present", "wait"]), False),
    ]
    bad = 0
    for label, lines, want in controls:
        got, why = check(lines, "READY WEAPON:")
        ok = (got == want)
        if not ok:
            bad += 1
        print("  %s  %-46s -> %s (%s)" % ("ok  " if ok else "FAIL", label,
                                          "pass" if got else "fail", why))
    print("SELFTEST: %s" % ("PASS" if bad == 0 else "FAIL (%d)" % bad))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(run_selftest() if "--selftest" in sys.argv else run_gate())
