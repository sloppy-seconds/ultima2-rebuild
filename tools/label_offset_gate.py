#!/usr/bin/env python3
"""label_offset_gate.py — the asm/ address convention, enforced. docs/plan-code-index.md § 7.

`asm/` filenames are **runtime offsets in segment 017D**. The `L_<addr>:` labels inside them are
**file offsets**. The two differ by the 0x200 MZ load offset, so for every file the first label must
equal the filename plus 0x200. That is not a style rule: it is the mapping AGENTS records as
`file_offset = offset + 0x200`, and getting it backwards produces a confident wrong answer about
which code lives where. It did exactly that on 2026-11-23, to a three-line epilogue, and the wrong
answer went into a header and a findings note before anyone measured.

    tools/label_offset_gate.py            # report
    tools/label_offset_gate.py --verify   # exit non-zero on any file off the convention
    tools/label_offset_gate.py --selftest
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DELTA = 0x200
LABEL_RE = re.compile(r"^L_([0-9A-Fa-f]{6}):", re.MULTILINE)

# Files off the convention, each with the reason. An exception list without reasons is how a gate
# quietly stops meaning anything, and this one only ever shrinks.
EXCEPTIONS = {
    "4050_cmd_T.asm": "first label is name+0x20d, not name+0x200; the clean wave cut this fragment "
                      "at a boundary 13 bytes off. Recorded, not fixed: moving a label is a body "
                      "change and CP0 freezes the body.",
}


def scan():
    ok, bad, nolabel = [], [], []
    for p in sorted((ROOT / "asm").glob("*.asm")):
        try:
            name = int(p.name[:4], 16)
        except ValueError:
            continue
        labels = LABEL_RE.findall(p.read_text(errors="replace"))
        if not labels:
            nolabel.append(p.name)
            continue
        if int(labels[0], 16) - name == DELTA:
            ok.append(p.name)
        else:
            bad.append((p.name, int(labels[0], 16) - name))
    return ok, bad, nolabel


def report() -> int:
    ok, bad, nolabel = scan()
    off = [b for b in bad if b[0] not in EXCEPTIONS]
    print(f"label_offset_gate: {len(ok)} files on the convention (first label = name + 0x200), "
          f"{len(nolabel)} with no labels")
    for name, delta in bad:
        tag = "exception" if name in EXCEPTIONS else "OFF CONVENTION"
        print(f"  {tag:14} {name:34} delta {delta:+#x}")
    if off:
        print(f"label_offset_gate: FAIL ({len(off)} file(s) off the convention)")
        return 1
    print("label_offset_gate: ok")
    return 0


def _mk(tmp, fname, label):
    (tmp / fname).write_text(f"# t\nsub_x:\nL_{label:06x}:\n        ret\n")


def selftest() -> int:
    import tempfile
    checks = []
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td) / "asm"
        tmp.mkdir()
        _mk(tmp, "1000_a.asm", 0x1200)      # on the convention
        _mk(tmp, "2000_b.asm", 0x2000)      # label == name: the mistake this catches
        _mk(tmp, "3000_c.asm", 0x3201)      # off by one
        (tmp / "4000_d.asm").write_text("# t\nsub_x:\n        ret\n")  # no labels
        global ROOT
        keep = ROOT
        ROOT = pathlib.Path(td)
        try:
            ok, bad, nolabel = scan()
            checks.append(("a file on the convention passes", ok == ["1000_a.asm"]))
            checks.append(("a label equal to the filename is caught, not skipped",
                           ("2000_b.asm", 0) in bad))
            checks.append(("a delta of one is caught", ("3000_c.asm", 0x201) in bad))
            checks.append(("a file with no labels is reported separately, not as a pass",
                           nolabel == ["4000_d.asm"]))
        finally:
            ROOT = keep
    checks.append(("every exception names a reason",
                   all(v for v in EXCEPTIONS.values())))
    for name, good in checks:
        print(("ok    " if good else "FAIL  ") + name)
    print("selftest: " + ("PASS" if all(c[1] for c in checks) else "FAIL"))
    return 0 if all(c[1] for c in checks) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    return report()


if __name__ == "__main__":
    sys.exit(main())
