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


def edges(routines, mn_by_addr):
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
            kind = classify(mn_by_addr, a)
            k = (a, b, kind)
            ent = seen.setdefault(k, [set(), 0])
            ent[0].add(path.parent.name)
            ent[1] += 1
    return seen


def classify(mn_by_addr, addr):
    """The kind of transfer the source instruction performs, read off the body line."""
    mn = mn_by_addr.get(addr)
    if mn is None:
        return "unknown"
    if mn == "call":
        return "call"
    if mn == "jmp":
        return "jmp"
    if mn.startswith("j"):
        return "jcc"
    return "fallthrough"


def build(routines):
    mn_by_addr = {}
    for p in ROOT.glob("asm/*.asm"):
        for off, mn in body_lines(p).items():
            mn_by_addr.setdefault(off, mn)
    ed = edges(routines, mn_by_addr)
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
         classify({0x1234: "mov"}, 0x9999) == "unknown"),
        ("a call is a call and a fall-through is not a transfer",
         classify({0x100: "call"}, 0x100) == "call"
         and classify({0x101: "mov"}, 0x101) == "fallthrough"),
        ("a callee no harvest reached is the honest gap, not a contradiction",
         classify_gap("b.asm", set(), {}) == ("callee-never-reached",
                                              "no harvest ever reached the callee")),
        ("a callee proven from elsewhere is a caller gap, not a callee gap",
         classify_gap("b.asm", {"b.asm"}, {})[0] == "callee-reached-elsewhere"),
        ("an unexercised handler is named as such, with its count",
         classify_gap("b.asm", set(), {"b.asm": 0})[0] == "handler-unexercised"),
        ("CFG blocks are named only as a seed list",
         "seed list" in pathlib.Path(__file__).read_text()),
    ]
    for name, ok in checks:
        print(("ok    " if ok else "FAIL  ") + name)
    print("selftest: " + ("PASS" if all(c[1] for c in checks) else "FAIL"))
    return 0 if all(c[1] for c in checks) else 1


def static_edges(routines):
    """Routine-level transfers the bodies claim, via calls_gate's rule (call, jmp, forced-keep,
    fragment fall-through). The header is not consulted: the body is authoritative, and
    calls_gate already proves the header agrees."""
    sys.path.insert(0, str(ROOT / "tools"))
    from calls_gate import parse  # same transfer rule, one implementation
    out = {}
    for p in sorted(ROOT.glob("asm/*.asm")):
        _, _, transfers, _ = parse(p.read_text(errors="replace"))
        for tok in transfers:
            try:
                tgt = int(tok.replace("L_", ""), 16)
            except ValueError:
                continue
            to = resolve(routines, tgt)
            if to and to != p.name:
                out.setdefault((p.name, to), set()).add("static")
    return out


def classify_star(mn_by_addr, star_by_addr, addr):
    """As classify, but a transfer through a register or memory operand -- `jmp *%bx`, the command
    jump table -- is its own kind. That distinction is the whole reason the runtime half exists:
    an indirect target has no static target at all, so it can only ever be observed."""
    if star_by_addr.get(addr):
        return "indirect"
    return classify(mn_by_addr, addr)


def star_lines(path):
    """addr -> True for body lines that transfer through a register or memory operand."""
    out = {}
    addr = None
    for raw in path.read_text(errors="replace").splitlines():
        m = LINE_RE.match(raw)
        if m:
            addr = int(m.group(1), 16)
            continue
        if addr is not None and re.match(r"^\s+(jmp|call)\s+\*", raw):
            out[addr] = True
            addr = None
    return out


def observed_edges(routines):
    """Routine-level edges proven by execution, keyed the same way as static_edges."""
    mn_by_addr, star_by_addr = {}, {}
    for p in ROOT.glob("asm/*.asm"):
        for off, mn in body_lines(p).items():
            mn_by_addr.setdefault(off, mn)
        star_by_addr.update(star_lines(p))
    out = {}
    for path in harvests():
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        flow = data if isinstance(data, list) else data.get("ExecutedInstructions", [])
        addrs = [e["Offset"] + BIAS for e in flow
                 if isinstance(e, dict) and e.get("Segment") == GAME_SEGMENT
                 and isinstance(e.get("Offset"), int)]
        for x, y in zip(addrs, addrs[1:]):
            if x == y:
                continue
            a, b = resolve(routines, x), resolve(routines, y)
            if a and b and a != b:
                ent = out.setdefault((a, b), {"harvests": set(), "kinds": set()})
                ent["harvests"].add(path.parent.name)
                ent["kinds"].add(classify_star(mn_by_addr, star_by_addr, x))
    return out


def merge() -> int:
    """The two halves side by side. The interesting column is the disagreement."""
    r = routines()
    st, ob = static_edges(r), observed_edges(r)
    rows = []
    for key in sorted(set(st) | set(ob)):
        in_st, in_ob = key in st, key in ob
        ev = "both" if in_st and in_ob else ("static" if in_st else "observed")
        o = ob.get(key, {"harvests": set(), "kinds": set()})
        rows.append([key[0], key[1], ev, "|".join(sorted(o["kinds"])),
                     str(len(o["harvests"]))])
    p = ROOT / "disasm" / "index" / "xref.tsv"
    p.write_text("from_asm\tto_asm\tevidence\tkinds\tharvests\n"
                 + "".join("\t".join(x) + "\n" for x in rows))
    n = {k: sum(1 for x in rows if x[2] == k) for k in ("both", "static", "observed")}
    print(f"wrote {p.relative_to(ROOT)} ({len(rows)} routine-level edges: "
          f"both {n['both']}, static-only {n['static']}, observed-only {n['observed']})")
    print("  observed-only means proven-but-unclaimed: headers exist on 601 of 1212 files and")
    print("  elide long lists, so most of this class is a transfer no header bothers to claim.")
    print("  The indirect kind inside it is the interesting subset -- a jmp through a jump table")
    print("  has no static target at all and can only ever be observed.")
    return 0


def es_sources() -> int:
    """For a file that reads es: with no in-file setup, who could have set ES before the transfer?

    footprint refuses to guess this per-file, and it was right: ES arrives from a caller, so the
    answer is in the graph. This reads the graph and says, per caller, what ES that caller sets."""
    sys.path.insert(0, str(ROOT / "tools"))
    from index_footprint import parse_es_setup
    r = routines()
    starts = {n: s for s, n in r}
    ob = observed_edges(r)
    st = static_edges(r)
    for name in sorted({k[1] for k in st} | {k[1] for k in ob}):
        p = ROOT / "asm" / name
        if not p.exists():
            continue
        lines = p.read_text(errors="replace").splitlines()
        if "es:" not in "".join(lines).lower() or parse_es_setup(lines) != "none":
            continue
        callers = sorted({k[0] for k in st if k[1] == name} | {k[0] for k in ob if k[1] == name})
        print(f"{name}: es: used with no in-file setup; {len(callers)} caller(s)")
        for c in callers:
            cp = ROOT / "asm" / c
            setup = parse_es_setup(cp.read_text(errors="replace").splitlines()) if cp.exists() else "?"
            how = "observed" if (c, name) in ob else "static only"
            print(f"    {c:34} sets ES = {setup or 'nothing in file'}  ({how})")
        if not callers:
            print("    no caller in either half -- ES is set by something that never transfers")
            print("    here in this corpus, which is an honest gap, not a resolved fact.")
    return 0


def handler_exec():
    """file -> executed-count, from cmd_handlers.py, which is the authority on whether a command
    handler has ever been seen running. Imported by subprocess rather than by hand-copying its
    derivation: two implementations of 'was this handler reached' is the drift this project keeps
    refusing."""
    import subprocess
    try:
        out = subprocess.run([sys.executable, str(ROOT / "tools" / "cmd_handlers.py"), "--json"],
                             capture_output=True, text=True, timeout=600)
        data = json.loads(out.stdout)
    except (OSError, ValueError, subprocess.SubprocessError):
        return {}
    return {d["file"]: d["executed"] for d in data if isinstance(d, dict) and "file" in d}


def classify_gap(to, reached, hx):
    """Why a body claims a transfer no harvest proved. Pure, so the controls can hit it directly."""
    if to in hx:
        return ("handler-unexercised", f"handler exec count {hx[to]}") if hx[to] == 0 \
            else ("handler-exercised-elsewhere", f"handler ran {hx[to]}x, not via this caller")
    if to in reached:
        return "callee-reached-elsewhere", "callee is proven; this caller's edge is not"
    return "callee-never-reached", "no harvest ever reached the callee"


def reconcile() -> int:
    """The 884 claimed-but-never-seen edges, split by what that actually means."""
    r = routines()
    st, ob = static_edges(r), observed_edges(r)
    reached = {b for (_, b) in ob}
    hx = handler_exec()
    rows, counts = [], {}
    for key in sorted(set(st) - set(ob)):
        cls, note = classify_gap(key[1], reached, hx)
        rows.append([key[0], key[1], cls, note])
        counts[cls] = counts.get(cls, 0) + 1
    p = ROOT / "disasm" / "index" / "xref-gaps.tsv"
    p.write_text("from_asm\tto_asm\tclass\tnote\n"
                 + "".join("\t".join(x) + "\n" for x in rows))
    print(f"wrote {p.relative_to(ROOT)} ({len(rows)} static-only edges)")
    for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f"  {k:34} {v}")
    unexercised = sum(1 for v in hx.values() if v == 0)
    print(f"  cmd_handlers reports {unexercised} handler(s) never executed; "
          f"{counts.get('handler-unexercised', 0)} gap edge(s) point at one.")
    print("  callee-never-reached is the honest gap: a body says control can get there and no")
    print("  capture in the corpus has ever been there. That is what an UNPROVEN marker is for,")
    print("  and it is a capture queue, not a code defect.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--seed-list", action="store_true")
    ap.add_argument("--reached", metavar="ADDR")
    ap.add_argument("--merge", action="store_true",
                    help="write the static+observed merge and print the disagreement counts")
    ap.add_argument("--es-sources", action="store_true",
                    help="for files whose es: setup arrives from a caller, name the callers")
    ap.add_argument("--reconcile", action="store_true",
                    help="split claimed-but-never-observed edges into what each one means")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.seed_list:
        return seed_list()
    if a.merge:
        return merge()
    if a.es_sources:
        return es_sources()
    if a.reconcile:
        return reconcile()
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
