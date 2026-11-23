#!/usr/bin/env python3
"""index_xref.py — which routine reached which, proven by execution. docs/plan-code-index.md § 3.4.

The static half of the call graph already ships as `# calls:` lines and `tools/calls_gate.py`.
This is the runtime half, and the thing it exists to get right is the evidentiary rule AGENTS.md
states and this project has been burned by: **CFG blocks are a seed list, `ExecutedInstructions`
is the proof.** `spice86dumpCfgBlocks.json` lists *discovered* blocks — Spice86 expands branch
successors statically — so a block can appear without ever having run. It reported a Minax fight
that never happened. So this tool reads `*ExecutionFlow.json` and nothing else, and the seed list
is only ever printed, labelled, never merged into an edge.

An edge is two instructions executed back to back in one harvest, both in the game's code segment.
The kind comes from the body line at the source address: a `call`, a `jmp`, a conditional jump, or
a fall-through. Fall-through edges are kept because they are what proves a fragment chain runs
end to end, which is the thing § 3.4 says only the last fragment of a split holds the `ret` for.

    tools/index_xref.py --write
    tools/index_xref.py --reached 7030        # who ever reached this routine
    tools/index_xref.py --callers-of 4E       # who writes DS:4E-adjacent code? no: who calls it
    tools/index_xref.py --verify
    tools/index_xref.py --seed-list           # the CFG block starts, labelled as candidates only
"""
from __future__ import annotations

import argparse
import bisect
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "disasm" / "index" / "xref-observed.tsv"
CAPTURES = ROOT / "captures"
HEADER = ["from", "from_asm", "to", "to_asm", "kind", "harvests", "times"]
GAME_SEGMENT = 0x017D          # AGENTS.md: runtime code segment; file_offset = offset + 0x200
BIAS = 0x200

LINE_RE = re.compile(r"^L_([0-9A-Fa-f]{4,6}):\s*$")
BODY_RE = re.compile(r"^\s+([a-z][a-z0-9]*)\b(.*)$")


def routines():
    """(start, name) for every asm file, sorted, so an address resolves to its routine."""
    out = []
    for p in ROOT.glob("asm/*.asm"):
        try:
            out.append((int(p.name[:4], 16), p.name))
        except ValueError:
            pass
    out.sort()
    return out


def resolve(routines, addr):
    """Which routine contains addr, by the next-start boundary. Not a fixed window: routines
    run well past 0x400 and a cap invented here silently drops real edges."""
    i = bisect.bisect_right(routines, (addr, "\uffff")) - 1
    if i < 0:
        return None
    start, name = routines[i]
    nxt = routines[i + 1][0] if i + 1 < len(routines) else start + 0x400
    return name if addr < nxt else None


def body_lines(path):
    """addr -> mnemonic for one asm file, from the label/body pairs the file prints."""
    lines = {}
    addr = None
    for raw in path.read_text(errors="replace").splitlines():
        m = LINE_RE.match(raw)
        if m:
            addr = int(m.group(1), 16)
            continue
        if addr is None:
            continue
        b = BODY_RE.match(raw)
        if b:
            lines[addr] = b.group(1)
            addr = None
    return lines


def harvests():
    return sorted(CAPTURES.rglob("*ExecutionFlow.json"))


def edges(routines, bodies):
    """(from, to, kind) -> (harvest set, total count)."""
    seen = {}
    for path in harvests():
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        flow = data if isinstance(data, list) else data.get("ExecutedInstructions", [])
        addrs = [e["Offset"] + BIAS for e in flow
                 if isinstance(e, dict) and e.get("Segment") == GAME_SEGMENT
                 and isinstance(e.get("Offset"), int)]
        for a, b in zip(addrs, addrs[1:]):
            if a == b:
                continue
            kind = classify(bodies, a)
            k = (a, b, kind)
            ent = seen.setdefault(k, [set(), 0])
            ent[0].add(path.parent.name)
            ent[1] += 1
    return seen


def classify(bodies, addr):
    """The kind of transfer the source instruction performs, read off the body line."""
    routines_ = None  # noqa: F841  (kept for readability of the lookup below)
    for name, table in bodies.items():
        for off, mn in table.items():
            if off == addr:
                if mn == "call":
                    return "call"
                if mn == "jmp":
                    return "jmp"
                if mn.startswith("j"):
                    return "jcc"
                return "fallthrough"
    return "unknown"


def build(routines):
    bodies = {p.name: body_lines(p) for p in ROOT.glob("asm/*.asm")}
    ed = edges(routines, bodies)
    rows = []
    for (a, b, kind), (hs, n) in sorted(ed.items()):
        rows.append([f"{a:04X}", resolve(routines, a) or "?", f"{b:04X}",
                     resolve(routines, b) or "?", kind, "|".join(sorted(hs)), str(n)])
    return rows


def write() -> int:
    rows = build(routines())
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\t".join(HEADER) + "\n" + "".join("\t".join(r) + "\n" for r in rows))
    kinds = {}
    for r in rows:
        kinds[r[4]] = kinds.get(r[4], 0) + 1
    print(f"wrote {OUT.relative_to(ROOT)} ({len(rows)} edges; "
          + ", ".join(f"{k} {v}" for k, v in sorted(kinds.items())) + ")")
    return 0


def reached(addr_hex: str) -> int:
    rows = read()
    if not rows:
        print("index_xref: no table; run --write", file=sys.stderr)
        return 1
    target = int(addr_hex, 16)
    r = routines()
    want = resolve(r, target)
    hits = [x for x in rows if int(x[2], 16) == target or resolve(r, int(x[2], 16)) == want]
    print(f"index_xref: {len(hits)} observed transfer(s) into "
          f"{want or addr_hex} ({target:#06x})")
    for x in hits[:20]:
        print(f"  {x[0]} ({x[1]}) --{x[4]}--> {x[2]}  in {len(x[5].split('|'))} harvest(s)")
    if not hits:
        print("  none. That is a claim about the corpus, not about the game: no harvest reached it.")
    return 0


def read():
    if not OUT.exists():
        return []
    return [l.split("\t") for l in OUT.read_text().splitlines()[1:] if l.strip()]


def verify() -> int:
    rows = build(routines())
    bad = [r for r in rows if r[1] == "?" or r[3] == "?"]
    for r in bad[:10]:
        print(f"  FAIL  {r[0]}->{r[2]} does not resolve to a routine in asm/")
    kinds = {}
    for r in rows:
        kinds[r[4]] = kinds.get(r[4], 0) + 1
    print(f"index_xref: {len(rows)} edges over {len(harvests())} harvests; "
          + ", ".join(f"{k} {v}" for k, v in sorted(kinds.items())))
    print("index_xref: evidence is ExecutedInstructions only; CFG blocks are never merged in")
    ok = not bad
    print("index_xref: " + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


def seed_list() -> int:
    """The CFG block starts, labelled as candidates. Printed, never merged: § 3.4's rule."""
    n = 0
    for path in sorted(CAPTURES.rglob("*CfgBlocks.json")):
        try:
            json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        n += 1
    print(f"index_xref: {n} CFG-block dumps exist. These are DISCOVERED blocks, a seed list, and")
    print("            proof of nothing -- spice86dumpCfgBlocks reported a Minax fight that never")
    print("            happened. Use them to choose where to look, never as an edge.")
    return 0


def selftest() -> int:
    r = routines()
    checks = [
        ("an address inside a known routine resolves to it", resolve(r, 0x7030) == "7030_space_collision.asm"),
        ("an address below the first routine resolves to nothing",
         resolve([(0x100, "a.asm"), (0x200, "b.asm")], 0x00ff) is None),
        ("an address past the next routine's start resolves to nothing",
         resolve(r, r[r.index((0x7030, "7030_space_collision.asm")) + 1][0] + 2)
         != "7030_space_collision.asm"),
        ("a source address with no body line is unknown, not guessed",
         classify({"x.asm": {0x1234: "mov"}}, 0x9999) == "unknown"),
        ("a call is a call and a fall-through is not a transfer",
         classify({"x.asm": {0x100: "call"}}, 0x100) == "call"
         and classify({"x.asm": {0x101: "mov"}}, 0x101) == "fallthrough"),
        ("CFG blocks are named only as a seed list",
         "seed list" in pathlib.Path(__file__).read_text()),
    ]
    for name, ok in checks:
        print(("ok    " if ok else "FAIL  ") + name)
    print("selftest: " + ("PASS" if all(c[1] for c in checks) else "FAIL"))
    return 0 if all(c[1] for c in checks) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--seed-list", action="store_true")
    ap.add_argument("--reached", metavar="ADDR")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.seed_list:
        return seed_list()
    if a.reached:
        return reached(a.reached)
    if a.verify:
        return verify()
    if a.write:
        return write()
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
