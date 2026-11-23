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

# Files off the convention that a named entry label does not explain. An exception list without
# reasons is how a gate quietly stops meaning anything, and this one only ever shrinks. It is empty
# on purpose: the one apparent outlier, 4050_cmd_T.asm at +0x20d, turned out to be the gate's
# mistake, not the corpus's — its entry is labelled `cmd_T:` and the clean wave only stamped L_
# labels from the fourth instruction on. See NAMED_ENTRY below.
EXCEPTIONS: dict[str, str] = {}

# A file whose first address-bearing label is later than name+0x200 is legitimate when the entry is
# labelled by name instead — `cmd_T:`, `sub_405d:` and so on. The address is still the filename's;
# it is simply not spelled out on the entry line.
ENTRY_LABEL_RE = re.compile(r"^([A-Za-z_][\w]*):\s*$")


def scan():
    ok, bad, nolabel, named_entry = [], [], [], []
    for p in sorted((ROOT / "asm").glob("*.asm")):
        try:
            name = int(p.name[:4], 16)
        except ValueError:
            continue
        text = p.read_text(errors="replace")
        labels = LABEL_RE.findall(text)
        if not labels:
            nolabel.append(p.name)
            continue
        delta = int(labels[0], 16) - name
        if delta == DELTA:
            ok.append(p.name)
            continue
        # Explain the gap before calling it a failure: is the entry labelled by name?
        first_l = f"L_{labels[0]}:"
        entry_named = False
        for line in text.splitlines():
            if line.startswith("#") or not line.strip():
                continue
            if line.startswith(first_l):
                break
            if ENTRY_LABEL_RE.match(line):
                entry_named = True
                break
        if entry_named:
            named_entry.append((p.name, delta))
        else:
            bad.append((p.name, delta))
    return ok, bad, nolabel, named_entry


def report() -> int:
    ok, bad, nolabel, named_entry = scan()
    off = [b for b in bad if b[0] not in EXCEPTIONS]
    print(f"label_offset_gate: {len(ok)} files on the convention (first label = name + 0x200), "
          f"{len(named_entry)} with the entry labelled by name instead, {len(nolabel)} with no labels")
    for name, delta in named_entry:
        print(f"  named entry  {name:34} first L_ label {delta:+#x}")
    for name, delta in bad:
        tag = "exception" if name in EXCEPTIONS else "OFF CONVENTION"
        print(f"  {tag:14} {name:34} delta {delta:+#x}")
    if off:
        print(f"label_offset_gate: FAIL ({len(off)} file(s) off the convention)")
        return 1
    print("label_offset_gate: ok")
    return 0


def _mk(tmp, fname, label):
    # No named entry label: these fixtures exist to test the unexplained cases.
    (tmp / fname).write_text(f"# t\nL_{label:06x}:\n        ret\n")


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
        (tmp / "5000_e.asm").write_text(  # entry labelled by name, L_ later: legitimate
            "# t\ncmd_x:\n        call L_0051df\n        db \"X-\",0\nsub_520d:\n"
            "L_00520d:\n        ret\n")
        global ROOT
        keep = ROOT
        ROOT = pathlib.Path(td)
        try:
            ok, bad, nolabel, named_entry = scan()
            checks.append(("a file on the convention passes", ok == ["1000_a.asm"]))
            checks.append(("a label equal to the filename is caught, not skipped",
                           ("2000_b.asm", 0) in bad))
            checks.append(("a delta of one is caught", ("3000_c.asm", 0x201) in bad))
            checks.append(("a file with no labels is reported separately, not as a pass",
                           nolabel == ["4000_d.asm"]))
            checks.append(("a named entry label explains a later first L_ label",
                           [n for n, _ in named_entry] == ["5000_e.asm"]
                           and not any(n == "5000_e.asm" for n, _ in bad)))
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
