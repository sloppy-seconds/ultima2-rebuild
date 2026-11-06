#!/usr/bin/env python3
"""demo_key_gate.py — the availability proof for the control bytes the engine claims.

docs/demo-record-playback.md D10 refuses to let Ctrl-R rest on a belief. The claim
is "nothing in the original compares against 0x12, so the byte cannot displace a
game shortcut", and the same reasoning is what lets --dev claim Ctrl-Q (0x11). A
claim written in prose goes stale the moment an `asm/` correction lands, so this is
a gate: it reads the byte-proven tree and fails when the claim stops being true.

What it checks, one line per control byte:

  0x12  Ctrl-R, the demo toggle.   MUST NOT be compared anywhere.
  0x11  Ctrl-Q, --dev quit.        MUST NOT be compared anywhere.
  0x13  Ctrl-S, the screenshot.    MUST be compared at asm/4F76, which is the site
                                    seam.c cites for it. A screenshot key that
                                    collided with play would be invisible in a
                                    scripted capture, because scripts send the
                                    shifted byte.

Only instruction operands count. Comments are skipped, and a DS displacement is not
an immediate: `cmpb $0xff,0x12` in asm/5AB1 compares the view-mode byte at DS:12 and
has nothing to do with a keystroke, so a naive substring search for "0x12" would
report a collision that does not exist. That false positive is the reason this is a
script with a rule rather than a grep in a shell line.

Run it with --selftest and it proves it can go red, by copying the tree, planting a
`cmp $0x12,%al`, and checking that the planted file is the one it names.

Usage:
    python3 tools/demo_key_gate.py [--asm-dir DIR] [--selftest]
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import sys
import tempfile

# `cmp`, `cmpb`, `cmpw`, `cmpi...` with an immediate first operand. AT&T order, which
# is what the tree is written in: `cmp $0x13,%al`. The second operand must be a
# register for this to be a keystroke test; a bare displacement is a DS byte.
IMM_CMP = re.compile(r"^\s*cmp[bwl]?\s+\$0x([0-9a-fA-F]{1,4})\s*,\s*%(a?[a-z]{2}|[a-z]x)\b")

FREE = {
    0x11: "Ctrl-Q (--dev quit)",
    0x12: "Ctrl-R (demo record/playback toggle)",
}
CLAIMED = {
    0x13: ("Ctrl-S (screenshot)", "4F76_sub_4f76.asm"),
}


def scan(asm_dir: str) -> dict[int, list[str]]:
    """Every immediate comparison against a register, keyed by the byte compared."""
    hits: dict[int, list[str]] = {}
    for name in sorted(os.listdir(asm_dir)):
        if not name.endswith(".asm"):
            continue
        path = os.path.join(asm_dir, name)
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for lineno, line in enumerate(f, 1):
                stripped = line.strip()
                if stripped.startswith("#") or stripped.startswith(";"):
                    continue
                m = IMM_CMP.match(line)
                if not m:
                    continue
                val = int(m.group(1), 16)
                hits.setdefault(val, []).append(f"{name}:{lineno}")
    return hits


def check(asm_dir: str) -> list[str]:
    hits = scan(asm_dir)
    problems: list[str] = []

    for byte, what in sorted(FREE.items()):
        where = hits.get(byte, [])
        if where:
            problems.append(
                f"0x{byte:02X} {what} is compared in the original at "
                + ", ".join(where)
                + f"; the engine must not claim it. {what.split(' ')[0]} is taken."
            )

    for byte, (what, site) in sorted(CLAIMED.items()):
        where = hits.get(byte, [])
        if not where:
            problems.append(
                f"0x{byte:02X} {what}: no comparison found anywhere in asm/. "
                "The engine's claim that this byte is free for the engine is now "
                "unsupported, and the site it cites has stopped existing."
            )
        elif not any(site in w for w in where):
            problems.append(
                f"0x{byte:02X} {what}: cited site {site} no longer compares it "
                f"(found: {', '.join(where)}). Update seam.c's citation or move the key."
            )
    return problems


def selftest(asm_dir: str) -> int:
    """The control. A gate that cannot go red where it is enforced is the defect
    AGENTS.md § 3 says this project has shipped three times, so the perturbation
    runs against a copy and the gate is pointed at the copy, exactly as CI would."""
    tmp = tempfile.mkdtemp(prefix="demo-key-gate-", dir=os.environ.get("TMPDIR", "/var/tmp"))
    try:
        victim = "4F76_sub_4f76.asm"
        src = os.path.join(asm_dir, victim)
        if not os.path.exists(src):
            print(f"SELFTEST FAIL  {victim} is not in {asm_dir}; the control cannot run")
            return 1
        shutil.copytree(asm_dir, os.path.join(tmp, "asm"), symlinks=True)
        planted = os.path.join(tmp, "asm", victim)
        with open(planted, "a", encoding="utf-8") as f:
            f.write("        cmp    $0x12,%al\n")

        problems = check(os.path.join(tmp, "asm"))
        named = [p for p in problems if p.startswith("0x12")]
        if not named:
            print("SELFTEST FAIL  a planted `cmp $0x12,%al` was not reported")
            return 1
        if victim not in named[0]:
            print(f"SELFTEST FAIL  the report did not name the planted file: {named[0]}")
            return 1
        print(f"SELFTEST PASS  a planted `cmp $0x12,%al` in {victim} is reported by name")

        # And the second shape: the cited screenshot site disappearing must be
        # reported too, not merely the byte becoming free.
        shutil.rmtree(os.path.join(tmp, "asm"))
        shutil.copytree(asm_dir, os.path.join(tmp, "asm"), symlinks=True)
        stripped = os.path.join(tmp, "asm", victim)
        lines = open(stripped, encoding="utf-8").read().splitlines(True)
        with open(stripped, "w", encoding="utf-8") as f:
            f.writelines(l for l in lines if "cmp    $0x13,%al" not in l)
        problems = check(os.path.join(tmp, "asm"))
        if not any(p.startswith("0x13") for p in problems):
            print("SELFTEST FAIL  deleting the cited Ctrl-S site was not reported")
            return 1
        print("SELFTEST PASS  removing the cited Ctrl-S site is reported")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--asm-dir", default=os.path.join(os.path.dirname(__file__), "..", "asm"))
    ap.add_argument("--selftest", action="store_true", help="prove the gate can go red")
    args = ap.parse_args()

    asm_dir = os.path.abspath(args.asm_dir)
    if not os.path.isdir(asm_dir):
        print(f"demo_key_gate: no asm directory at {asm_dir}", file=sys.stderr)
        return 2

    if args.selftest:
        return selftest(asm_dir)

    problems = check(asm_dir)
    if problems:
        for p in problems:
            print(f"FAIL  {p}")
        return 1
    print("PASS  engine control bytes are free: "
          + ", ".join(f"0x{b:02X}" for b in sorted(FREE))
          + "; cited sites present: "
          + ", ".join(f"0x{b:02X}" for b in sorted(CLAIMED)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
