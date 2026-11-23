#!/usr/bin/env python3
"""calls_gate.py -- prove each `# calls:` header line matches what its body actually does.

WHY. A `# calls:` line is hand-editable prose duplicating a fact that lives in the body, so it
can drift silently, and 601 files carry one. This is the gate the code-index epic ships in place
of the deferred `xref` index: docs/plan-code-index.md section 3.4. The body is authoritative
without exception, and the gate is report-only -- no --fix, because editing an asm/ header
touches the CP0-frozen surface and deserves a human.

WHAT COUNTS AS A TRANSFER. The header records *control transfers*, not literal `call` text, and
three shapes matter:

  1. `call L_xxxxxx` and `jmp L_xxxxxx`. asm/017D is a three-byte stub whose whole body is
     `jmp L_000442`, and its header claims exactly that.
  2. Forced-keep swallowed calls: `.byte 0xe8  # forced-kept: stop-trunc call 0x514d`. The
     transfer is real and only the comment names its target.
  3. Fragment splits. A routine can be cut across files and only the last fragment holds the
     `ret`. asm/507A_print_player_name claims `L_005203`, called two fragments later in
     asm/5085; the claim is true, so a file whose body has no `ret` may satisfy a claim
     anywhere in the chain of fragments it falls through to.

HOW THE HEADER IS WRITTEN, which took three attempts to get right:

  * The list wraps. A continuation line is `#` plus deep indentation and no field name, and a
    claim can sit there (asm/6B3D lists four targets on the first line and four more below).
  * A long list is elided with `(+N more)`, written by tools/clean_wave.py. That N is a
    *checkable assertion*, not a shrug: this gate fails when the body has a different number of
    unlisted transfers than the line claims. Ignoring the marker would have hidden five files'
    worth of real drift behind a truncation that was actually accurate.

A gate that gets any of this wrong is worse than none. The first rule, `call` text only, called
212 of 601 files drift; the second missed continuations and elisions and called 17 files drift.
Both would have made the gate noise on the day it landed.

USAGE
  tools/calls_gate.py              # exit 1 if any header disagrees with its body
  tools/calls_gate.py --quiet      # verdict line only
  tools/calls_gate.py --selftest   # the controls; all must behave as named
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ASM_DIR = pathlib.Path(__file__).resolve().parent.parent / "asm"
MAX_FRAGMENTS = 8  # a fall-through chain longer than this is a bug in the chain, not a routine

CALLS_RE = re.compile(r"^#\s*calls:\s*(.*)$", re.MULTILINE)
CONT_RE = re.compile(r"^#\s{2,}\S")                       # wrapped header line, no field name
FIELD_RE = re.compile(r"^#\s*[a-z][a-z -]*:")             # a line that starts a new field
ELIDE_RE = re.compile(r"\(\+(\d+) more\)")
L_TOKEN = re.compile(r"L_[0-9a-f]{6}")
XFER_RE = re.compile(r"^\s*(?:call|jmp)\s+(L_[0-9a-f]{6})\b")
FORCED_CALL_RE = re.compile(r"\.byte\s+0xe8\b.*?\b(?:call|jmp)\s+0x([0-9a-f]{3,6})")
RET_RE = re.compile(r"^\s*ret\b")
START_RE = re.compile(r"^([0-9A-Fa-f]{4})_")


def norm(token: str) -> str:
    """Normalise any target spelling to the file's own `L_%06x` form."""
    return "L_%06x" % int(token.replace("L_", ""), 16)


def parse(text: str) -> tuple[set[str], int | None, set[str], bool]:
    """Return (claimed, elision_count_or_None, transfers, has_ret) for one file."""
    claimed: set[str] = set()
    elide: int | None = None
    transfers: set[str] = set()
    has_ret = False
    in_calls = False
    for line in text.splitlines():
        m = CALLS_RE.match(line)
        if m:
            in_calls = True
            e = ELIDE_RE.search(line)
            if e:
                elide = int(e.group(1))
            claimed.update(norm(t) for t in L_TOKEN.findall(line))
            continue
        if in_calls and line.startswith("#") and CONT_RE.match(line) and not FIELD_RE.match(line):
            e = ELIDE_RE.search(line)
            if e:
                elide = int(e.group(1))
            claimed.update(norm(t) for t in L_TOKEN.findall(line))
            continue
        in_calls = False
        if line.startswith("#"):
            continue
        m = XFER_RE.match(line)
        if m:
            transfers.add(norm(m.group(1)))
        for hexv in FORCED_CALL_RE.findall(line):
            transfers.add(norm(hexv))
        if RET_RE.match(line):
            has_ret = True
    return claimed, elide, transfers, has_ret


def check(files: list[tuple[str, str]]) -> list[str]:
    """files is an address-ordered list of (name, text). Returns human-readable failures."""
    parsed = [parse(text) for _, text in files]
    fails: list[str] = []
    for i, (name, _) in enumerate(files):
        claimed, elide, transfers, has_ret = parsed[i]
        if not claimed:
            continue  # no claim, nothing to check; absence is not drift
        satisfied = set(transfers)
        if not has_ret:  # falls through: a claim may be satisfied in a later fragment
            for j in range(i + 1, min(i + 1 + MAX_FRAGMENTS, len(parsed))):
                satisfied |= parsed[j][2]
                if parsed[j][3]:
                    break
        for miss in sorted(claimed - satisfied):
            fails.append(f"{name}: claims {miss}, no transfer reaches it (body or fragments)")
        unlisted = sorted(transfers - claimed)
        if elide is not None:
            if elide != len(unlisted):
                fails.append(f"{name}: says (+{elide} more) but the body has {len(unlisted)} "
                             f"unlisted transfer(s): {', '.join(unlisted) or 'none'}")
        else:
            for extra in unlisted:
                fails.append(f"{name}: transfers to {extra}, not claimed and not elided")
    return fails


def _selftest() -> int:
    """Controls. Each names the mistake it exists to catch."""
    ok = True

    def expect(label: str, files, want_fail: bool):
        nonlocal ok
        fails = check(files)
        if bool(fails) != want_fail:
            ok = False
            print(f"FAIL  control {label}: expected {'red' if want_fail else 'green'}, got "
                  f"{'red' if fails else 'green'} {fails}")
        else:
            print(f"ok    control {label}")

    expect("header matches body",
           [("a_x.asm", "# calls: L_005203\nsub_a:\n        call L_005203\n        ret\n")], False)
    expect("phantom claim",
           [("a_x.asm", "# calls: L_005203\nsub_a:\n        nop\n        ret\n")], True)
    expect("unclaimed transfer",
           [("a_x.asm", "# calls: L_000442\nsub_a:\n        call L_000442\n        jmp L_005203\n        ret\n")],
           True)
    expect("fragment-satisfied claim",
           [("1_507a.asm", "# calls: L_005203\nsub_a:\n        mov $0x0,%bx\n"),
            ("2_507d.asm", "sub_d:\n        cmp $0x0,%al\n"),
            ("3_5085.asm", "sub_e:\n        call L_005203\n"),
            ("4_508b.asm", "sub_b:\n        ret\n")],
           False)
    expect("forced-keep swallowed call",
           [("a_x.asm", "# calls: L_0051df\nsub_a:\n        .byte 0xe8  # forced-kept: call 0x51df\n        ret\n")],
           False)
    expect("claim on a wrapped continuation line",
           [("a_x.asm", "# calls: L_000c33 (cmd_done),\n#           L_0051df (str_out)\n"
                        "sub_a:\n        call L_000c33\n        call L_0051df\n        ret\n")], False)
    expect("elision count that matches",
           [("a_x.asm", "# calls: L_000c33 (+1 more)\nsub_a:\n        call L_000c33\n"
                        "        call L_0051df\n        ret\n")], False)
    expect("elision count that lies",
           [("a_x.asm", "# calls: L_000c33 (+2 more)\nsub_a:\n        call L_000c33\n"
                        "        call L_0051df\n        ret\n")], True)
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--quiet", action="store_true", help="verdict line only")
    ap.add_argument("--selftest", action="store_true", help="run the controls and exit")
    args = ap.parse_args()

    if args.selftest:
        return _selftest()

    files = sorted((p for p in ASM_DIR.glob("*.asm")),
                   key=lambda p: (START_RE.match(p.name).group(1).upper()
                                  if START_RE.match(p.name) else p.name, p.name))
    pairs = [(p.name, p.read_text(encoding="utf-8", errors="replace")) for p in files]
    fails = check(pairs)
    with_claim = sum(1 for _, t in pairs if CALLS_RE.search(t))
    if fails and not args.quiet:
        for f in fails:
            print(f"  DRIFT  {f}")
    print(f"{'FAIL' if fails else 'PASS'}  {len(fails)} drift(s) across "
          f"{with_claim} files carrying a # calls: line ({len(pairs)} scanned)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
