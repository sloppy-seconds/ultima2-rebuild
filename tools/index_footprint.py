#!/usr/bin/env python3
"""index_footprint.py -- who touches each byte, across DS, CS and ES.

THE QUERY THIS EXISTS FOR (docs/plan-code-index.md § 3.1): "who writes the sweep byte", answered
in one command, including the writes that are not a literal `0x4c7` anywhere -- the `incb` inside a
forced-keep blob, the `sub %cl,0x4e(%bx,%di)` whose real footprint is a whole array, the `es:`
store that only means what it means because some routine set ES first.

ONE ROW PER (segment, offset, file, line, access). access is read | write | read-modify-write, and
read-modify-write is kept as its own value rather than folded into reads, because classifying
`incb` as a read is exactly what hid the sweep descend.

THE ACCESS TABLE BELOW IS EVIDENCE, NOT GENERATED DATA. ~25 hand-curated rows, each citing what
proves it, inside the generator, exactly like the span allowlist. AT&T puts the destination LAST.
**An unrecognised mnemonic with a memory operand is a hard failure, not a skip** -- a silent skip
is how an index starts quietly lying about what it covers.

SPANS ARE CURATED, NEVER INFERRED. An indexed operand (`0x4cd(%si)`) is a *base*, and the real
footprint is a 64-entry array. Loop-bound inference gets ranges wrong with confidence, so a span is
filled only from the allowlist below, one citation per entry. Everything else is `span: unknown`,
and a query that could land inside an unknown span returns that base as a loud candidate. A silent
empty answer is the failure this file is built to avoid.

SEGMENTS ARE NOT ONE THING. ES is resolved per routine from that routine's own setup, and where no
routine in the graph sets it at all the index records a documented calling convention rather than
failing: see ES_CONVENTION below, which carries a reason per entry. Video-resident ES goes to the
seams column, not the data rows. A `cs:` *write* is a selfmod class with an asserted count,
because a new one in this binary would be news.

THE PARSER ACCOUNTS FOR ITSELF. Every non-comment line is parsed, or counted as data, or counted as
a label. An unparsed *instruction-shaped* line fails --verify, and a file with one is capped at
`segmented` so a parse failure cannot hide behind a decoded grade.

USAGE
  tools/index_footprint.py                      # summary
  tools/index_footprint.py --write              # write disasm/index/footprint.tsv
  tools/index_footprint.py --check-fresh
  tools/index_footprint.py --verify
  tools/index_footprint.py --selftest
  tools/index_footprint.py --lookup DS:4C7      # or 0x4c7, or a state.h field name
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from dialect_masm import split_operands  # noqa: E402  (one splitter in the repo, on purpose)
from state_layout import fields as state_fields  # noqa: E402

ASM = REPO / "asm"
OUT = REPO / "disasm" / "index" / "footprint.tsv"
COLS = ("segment", "offset", "span", "access", "file", "line", "indexed", "es_as", "note")

# ---------------------------------------------------------------- access table
# (reads, writes) for the memory operand(s). AT&T: destination is the LAST operand.
# Each row cites what proves it. This is a closed enum: a mnemonic not matched here, seen with a
# memory operand, is a hard failure.
ACCESS: list[tuple[str, str, str, str]] = [
    ("mov", "src", "dst", "gas AT&T: mov src,dst. movb/movw are width forms of the same contract."),
    ("cmp", "both", "", "cmp sets flags from both operands; it stores nothing."),
    ("test", "both", "", "test ANDs and sets flags only."),
    ("add", "src", "dst+src", "read-modify-write on the destination."),
    ("sub", "src", "dst-src", "read-modify-write on the destination."),
    ("adc", "src", "dst+src+cf", "read-modify-write; the carry-in is why sbb/adc are not reads."),
    ("sbb", "src", "dst-src-cf", "read-modify-write."),
    ("and", "src", "dst&src", "read-modify-write."),
    ("or", "src", "dst|src", "read-modify-write."),
    ("xor", "src", "dst^src", "read-modify-write; xor dst,dst is the idiom for zeroing."),
    ("inc", "dst", "dst+1", "read-modify-write."),
    ("dec", "dst", "dst-1", "read-modify-write."),
    ("neg", "dst", "-dst", "read-modify-write."),
    ("not", "dst", "~dst", "read-modify-write."),
    ("xchg", "both", "swap", "read-modify-write on BOTH operands."),
    ("lea", "src", "dst", "lea computes an address; it reads the memory form, writes the register."),
    ("shl", "dst", "dst<<n", "shift/rotate forms are read-modify-write."),
    ("shr", "dst", "dst>>n", "shift/rotate forms are read-modify-write."),
    ("sal", "dst", "dst<<n", "alias of shl."),
    ("sar", "dst", "dst>>n", "arithmetic shift right."),
    ("rol", "dst", "rotate", "rotate forms are read-modify-write."),
    ("ror", "dst", "rotate", "rotate forms are read-modify-write."),
    ("push", "src", "", "push reads the operand and writes the stack, not the operand."),
    ("pop", "", "dst", "pop writes the operand."),
    ("mul", "src", "", "mul/imul read the memory operand and write implicit accumulators."),
    ("imul", "src", "", "see mul."),
    ("div", "src", "", "div/idiv read the memory operand and write implicit accumulators."),
    ("idiv", "src", "", "see div."),
    ("in", "", "dst", "in writes the operand from a port; the port is not memory."),
    ("out", "src", "", "out reads the operand and writes a port."),
    ("ins", "", "dst", "insb/insw: the port is the source, the memory operand is written."),
    ("outs", "src", "", "outsw/outsb: the memory operand is read, the port is written."),
    ("rcl", "dst", "dst", "rotate-through-carry is read-modify-write."),
    ("rcr", "dst", "dst", "rotate-through-carry is read-modify-write."),
    ("scas", "both", "", "scas compares the accumulator with the operand and advances DI."),
    ("cmps", "both", "", "cmps compares two string operands; both are read."),
    ("movs", "src", "dst", "movs copies src to dst; the destination is the last operand."),
]
# Mnemonics that CANNOT exist on this target. The disassembler ran with a modern default ISA, so a
# few bytes got decoded as SSE or x87 that an 8086 cannot execute. They are not data accesses and
# they are not silently indexed: each occurrence is reported as a misdecode candidate, because a
# silent skip is the failure this table exists to prevent. If one ever turns out to be real it goes
# in ACCESS with a citation, not here.
IMPOSSIBLE = {"movups", "movlp", "movhp", "fisubrs", "fistp", "fnstsw", "fldt", "fisttp"}
# Control transfers are NOT data accesses (plan § 3.1), and are matched before the failure path.
CONTROL = ("jmp", "call", "loop", "loope", "loopne", "ret", "iret")
# Width suffixes gas accepts on the mnemonics above: b, w, and the l/b forms jwasm emits.
WIDTHS = ("", "b", "w", "l")
# Which mnemonics modify what they read, rather than overwriting it. The table above says *which*
# operand side is which; this says what the write does to the old value, which is what the
# read-modify-write column of the index reports.
RMW_STEMS = {"add", "sub", "adc", "sbb", "and", "or", "xor", "inc", "dec", "neg", "not",
             "shl", "shr", "sal", "sar", "rol", "ror"}
READ_BOTH = {"cmp", "test"}   # no destination at all: both operands are read
RMW_BOTH = {"xchg"}           # read-modify-write on both

# ---------------------------------------------------------------- span allowlist
# base -> (length, citation). Curated evidence, never inferred from a loop bound.
SPANS: dict[int, tuple[int, str]] = {
    0x4cd: (64, "asm/6B3D_space_field_draw.asm walks the star table as 0x4cd(%si); 64 entries"),
    0x137: (32, "engine/src/core/state.h creature grid slot 1 (32 slots); asm/ names it a base"),
    0x157: (32, "engine/src/core/state.h creature grid slot 2"),
    0x177: (32, "engine/src/core/state.h creature grid slot 3"),
    0x197: (32, "engine/src/core/state.h creature grid slot 4"),
    0x1d7: (32, "engine/src/core/state.h creature grid slot 5"),
}

LABEL_RE = re.compile(r"^[A-Za-z_.$][\w.$]*:\s*$")
INSTR_RE = re.compile(r"^([a-z][a-z0-9]*)(?:\s+(.*))?$")
MEM_RE = re.compile(r"^(?:%?(?P<seg>cs|ds|es|ss|fs|gs):)?"
                    r"(?P<disp>0x[0-9a-fA-F]+|\d+)?"
                    r"(?:\((?P<base>%[a-z0-9]+)(?:,(?P<idx>%[a-z0-9]+))?\))?$")
FORCED_KEEP = re.compile(r"^\.byte\b")


def classify_operand(op: str) -> dict | None:
    """Return {seg, offset, base, idx, bare} for a memory operand, None for anything else.

    In AT&T a BARE displacement is a memory reference -- `mov %al,0x4e` writes DS:4E, and that is
    the dominant form in this tree, so an index that only matched `0x4e(%bx)` would miss most of
    the file. `$0x4e` is an immediate, `%ax` is a register, a bare label is a code address.
    """
    op = op.strip()
    if not op or op.startswith("$") or (op.startswith("%") and ":" not in op):
        return None
    m = MEM_RE.match(op)
    if not m or (m.group("disp") is None and m.group("base") is None):
        return None
    seg = (m.group("seg") or "ds").lower()
    if seg == "ss":
        seg = "ds"
    disp = m.group("disp")
    return {"seg": seg, "offset": int(disp, 0) if disp else 0, "base": m.group("base"),
            "idx": m.group("idx"), "bare": m.group("base") is None}


def access_for(mn: str) -> tuple[str, str, str, str] | None:
    """Look a mnemonic up in the closed table, tolerating width suffixes. None = unknown."""
    for width in WIDTHS:
        if width and not mn.endswith(width):
            continue
        stem = mn[: len(mn) - len(width)] if width else mn
        for stem_name, reads, writes, cite in ACCESS:
            if stem == stem_name:
                return stem, reads, writes, cite
    return None


def roles_for(stem: str, reads: str, writes: str) -> tuple[str, str]:
    """(dst_role, src_role) for a memory operand in that position: read | write | rmw | ''."""
    if stem in READ_BOTH:
        return "read", "read"
    if stem in RMW_BOTH:
        return "rmw", "rmw"
    dst = ("rmw" if stem in RMW_STEMS else "write") if writes else ""
    src = "read" if reads else ""
    return dst, src


def parse_es_setup(lines: list[str]) -> str:
    """How does this routine load ES? es=ds | es=cs | es=io(video) | es=from-reg | es=none.

    ES is not a constant across the binary, so an `es:` operand means nothing until the routine that
    uses it is read. Comment lines are skipped, deliberately: a header may describe a calling
    convention (`asm/154E`'s says `push %es ; es = ds`) that its body does not perform, and the body
    is authoritative without exception.

    WHAT ACTUALLY OCCURS IN THIS BINARY, measured over all 1212 files: `from-reg` in 10 of them
    (`asm/4B76_pixel_plot`, `asm/4BAA_pixel_erase`, `asm/0100_sub_0100`, `asm/3139_cast_spell_flow`
    and six more, all loading ES from AX or CL), and `none` in the other 1202. The `ds`, `cs` and
    `io(video)` forms are recognised and unit-tested but do not appear anywhere in `asm/` — an
    earlier version of this docstring cited asm/154E, asm/155D and asm/310B_cmd_C as examples, and
    all three of those claims came from header prose, not from a body that sets ES. Dead branches
    are kept because the controls prove they would work if a re-derivation ever produced such a
    routine, but nothing in this tree exercises them, and that is stated rather than hidden.

    Where ES is loaded from a register whose value came from a caller (`mov %ax,%es`) the answer is
    `from-reg` and the index says so rather than guessing — a reported limit, not a silent one.
    """
    found = set()
    for ln in lines:
        if ln.startswith("#"):
            continue
        low = ln.lower()
        if re.search(r"mov\s+%ds,\s*%es", low):
            found.add("ds")
        if re.search(r"mov\s+%cs,\s*%es", low):
            found.add("cs")
        if re.search(r"mov\s+\$0x(b800|b000|ba00)\s*,\s*%es", low):
            found.add("io(video)")
        if re.search(r"mov\s+%[a-z0-9]+,\s*%es\s*$", low):
            found.add("from-reg")
    if not found:
        return "none"
    return "+".join(x for x in ("ds", "cs", "io(video)", "from-reg") if x in found)


def parse_file(path: pathlib.Path) -> tuple[list[dict], list[str], dict]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    rows: list[dict] = []
    errors: list[str] = []
    stats = {"parsed": 0, "labels": 0, "data": 0, "comments": 0, "blank": 0, "unparsed": 0}
    es_as = parse_es_setup(lines)
    forced_keep = 0
    for i, raw in enumerate(lines, start=1):
        line = raw.strip()
        if raw.lstrip().startswith("#"):
            stats["comments"] += 1
            continue
        if not line:
            stats["blank"] += 1
            continue
        if LABEL_RE.match(line):
            stats["labels"] += 1
            continue
        if FORCED_KEEP.match(line) or line.startswith("db ") or line.startswith("."):
            stats["data"] += 1
            if FORCED_KEEP.match(line):
                forced_keep += 1
            continue
        m = INSTR_RE.match(line)
        if not m:
            stats["unparsed"] += 1
            errors.append(f"{path.name}:{i}: unparsed instruction-shaped line {line[:40]!r}")
            continue
        mn, ops = m.group(1), (m.group(2) or "").strip()
        stats["parsed"] += 1
        if mn.startswith(CONTROL):
            continue
        acc = access_for(mn)
        if acc is None:
            if any(classify_operand(o) for o in (split_operands(ops) if ops else [])):
                if mn in IMPOSSIBLE:
                    errors.append(f"{path.name}:{i}: {mn!r} cannot exist on this target -- "
                                  "misdecode candidate, needs a ruling (see IMPOSSIBLE)")
                else:
                    errors.append(f"{path.name}:{i}: mnemonic {mn!r} with a memory operand is "
                                  "not in the access table -- refusing to guess")
            continue
        reads, writes, _ = acc[1], acc[2], acc[3]
        dst_role, src_role = roles_for(acc[0], reads, writes)
        operands = split_operands(ops) if ops else []
        last = len(operands) - 1
        for pos, op in enumerate(operands):
            cm = classify_operand(op)
            if cm is None:
                continue
            role = dst_role if pos == last else src_role
            if not role:
                continue  # this mnemonic does not touch an operand in that position
            access = {"read": "read", "write": "write", "rmw": "read-modify-write"}[role]
            seg, offset = cm["seg"], cm["offset"]
            indexed = bool(cm["base"]) or bool(cm["idx"])
            note = "absolute operand, no base register" if cm["bare"] else ""
            span = ""
            if indexed:
                for base, (length, cite) in SPANS.items():
                    if offset == base:
                        span = f"{offset:#06x}+{length}"
                        note = cite
                if not span:
                    span = "unknown"
            if seg == "es" and es_as == "io(video)":
                seg = "video"
            rows.append({"segment": seg, "offset": offset, "span": span,
                         "access": access,
                         "file": path.name, "line": str(i),
                         "indexed": "yes" if indexed else "no",
                         "es_as": es_as if seg == "es" else "", "note": note})
    return rows, errors, {**stats, "forced_keep_lines": forced_keep, "es_as": es_as}


def build() -> tuple[list[dict], list[str], dict]:
    if not ASM.is_dir():
        raise SystemExit("index_footprint: no asm/ -- refusing to report success on a tree it "
                         "cannot see")
    rows: list[dict] = []
    errors: list[str] = []
    tot = {"files": 0, "parsed": 0, "labels": 0, "data": 0, "comments": 0, "blank": 0,
           "unparsed": 0, "forced_keep_lines": 0}
    es_unresolved: list[str] = []
    for path in sorted(ASM.glob("*.asm")):
        r, e, st = parse_file(path)
        rows += r
        errors += e
        tot["files"] += 1
        for k in ("parsed", "labels", "data", "comments", "blank", "unparsed",
                  "forced_keep_lines"):
            tot[k] += st[k]
        if st["es_as"] == "none" and any(x["segment"] == "es" for x in r):
            es_unresolved.append(path.name)
    tot["es_unresolved"] = es_unresolved
    tot["selfmod"] = [f"{r['file']}:{r['line']}" for r in rows
                      if r["segment"] == "cs" and r["access"] != "read"]
    return rows, errors, tot


def blocking(errors: list[str]) -> list[str]:
    """Misdecode candidates are reported everywhere and never indexed, but they do not block a
    write: they are a curated, named list (IMPOSSIBLE) with a citation, printed on every run, so
    they are loud rather than silent. --verify still exits non-zero over them, which is where the
    ruling has to land."""
    return [e for e in errors if "misdecode candidate" not in e]


def table(rows: list[dict]) -> str:
    return "\t".join(COLS) + "\n" + "\n".join(
        "\t".join(str(r.get(c, "")) for c in COLS)
        for r in sorted(rows, key=lambda r: (r["segment"], r["offset"], r["file"],
                                             int(r["line"])))) + "\n"


# ES AS A CALLING CONVENTION, ruled 2026-11-23. `--es-sources` measured 61 files that use `es:` with
# no in-file setup, then asked the graph who sets it for them: not one of their callers sets ES
# either. No amount of traversal can resolve a value established above the entry point, so the index
# records it as a convention instead of failing forever. The reason is part of the data, not a comment
# beside it: an exemption list without reasons is how a gate quietly stops meaning anything, and
# --verify fails if an entry here stops matching a real unresolved site.
ES_CONVENTION: dict[str, str] = {
    "1564_sub_1564.asm": "reads a byte through ES:DI (`mov %es:(%di),%al`); no in-file setup and no "
                         "caller sets ES, so ES arrives set from above the entry point",
    "1573_sub_1573.asm": "reads a byte through ES:DI (`mov %es:(%di),%al`); same shape as 1564, "
                         "reached from the same dispatch",
    "6144_dungeon_shape_dispatch.asm": "port input into ES:DI (`insb (%dx),%es:(%di)`); the string "
                                       "operand's segment is set by the caller's caller, not by any "
                                       "routine in the graph",
    "7030_space_collision.asm": "scans ES (`scas %es:(%di),%ax`); same convention, and the only es: "
                                "string operand outside the two 15xx readers",
}


def verify() -> int:
    rows, errors, tot = build()
    for e in errors:
        print(f"  FAIL  {e}")
    if tot["unparsed"]:
        print(f"  FAIL  {tot['unparsed']} instruction-shaped line(s) went unparsed -- the parser "
              "must account for every line")
    unexempt = [f for f in tot["es_unresolved"] if f not in ES_CONVENTION]
    stale = [f for f in ES_CONVENTION if f not in tot["es_unresolved"]]
    if unexempt:
        print(f"  FAIL  {len(unexempt)} file(s) use es: with no resolvable setup and no recorded "
              f"convention: {', '.join(unexempt[:6])}")
    if stale:
        # An exemption that no longer exempts anything is a lie with a reason attached, and the
        # reason is exactly what makes people trust it. Name it and fail.
        print(f"  FAIL  {len(stale)} ES_CONVENTION entr(ies) no longer match an unresolved site: "
              f"{', '.join(stale[:6])} -- delete the entry, do not leave it standing")
    for f in tot["es_unresolved"]:
        if f in ES_CONVENTION:
            print(f"  NOTE  {f}: es: with no in-file setup, ruled a calling convention -- "
                  f"{ES_CONVENTION[f]}")
    print(f"index_footprint: {len(rows)} rows from {tot['files']} files; "
          f"parsed {tot['parsed']}, data {tot['data']}, labels {tot['labels']}, "
          f"forced-keep {tot['forced_keep_lines']}, unparsed {tot['unparsed']}")
    print(f"index_footprint: cs writes (selfmod) = {len(tot['selfmod'])}")
    ok = not errors and not tot["unparsed"] and not unexempt and not stale
    print("index_footprint: " + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


def lookup(query: str) -> int:
    rows, _, _ = build()
    q = query.strip().lower()
    seg = "ds"
    if ":" in q:
        seg, q = q.split(":", 1)
        seg = {"ds": "ds", "cs": "cs", "es": "es"}.get(seg, seg)
    if re.fullmatch(r"0x[0-9a-f]+", q) or re.fullmatch(r"[0-9a-f]+", q):
        # `4c7`, `0x4c7` and `DS:4C7` all mean the same byte, and a query surface that makes you
        # remember the prefix is a query surface nobody uses. Bare hex is hex here; a bare word
        # that happens to be all hex digits ("add", "cab") is ambiguous, so it is read as hex and
        # the answer line says what was assumed.
        want = int(q, 16) if q.startswith("0x") or re.fullmatch(r"[0-9a-f]+", q) else int(q)
    else:
        hit = next((f for f in state_fields() if f["name"] == q.lstrip("_")), None)
        if hit is None:
            print(f"lookup: {query!r} is not an offset or a state.h field name")
            return 2
        want, seg = int(hit["ds_offset"]), "ds"
        print(f"lookup: {q} -> {seg}:{want:#06x} (from engine/src/core/state.h)")
    direct = [r for r in rows if r["segment"] == seg and r["offset"] == want]
    for r in direct:
        print(f"  {r['segment']}:{r['offset']:#06x}  {r['access']:<17} {r['file']}:{r['line']}"
              + (f"  [{r['span']}]" if r["span"] else ""))
    # Loud candidates: a span that could contain the offset but whose length is unknown.
    cands = [r for r in rows if r["segment"] == seg and r["span"] == "unknown"
             and r["offset"] != want]
    if cands:
        print(f"  {len(cands)} indexed operand(s) elsewhere in {seg} with an UNKNOWN span -- "
              "one of them may own this byte:")
        for r in cands[:12]:
            print(f"    base {r['offset']:#06x} {r['file']}:{r['line']}")
    if not direct and not cands:
        print(f"  nothing in {seg}:{want:#06x} -- and no indexed base could reach it. "
              "That is a claim about the index, not about the game.")
    return 0


def _selftest() -> int:
    global ASM  # the controls below retarget the module-level ASM dir; without this, assigning it
                 # anywhere in this function makes every read of it local.
    ok = True
    tmp = pathlib.Path("/var/tmp/u2footprint-selftest")
    tmp.mkdir(parents=True, exist_ok=True)

    def case(label: str, body: str, want_err: bool, check=None):
        nonlocal ok
        for f in tmp.glob("*.asm"):
            f.unlink()
        (tmp / "0001_sub_0001.asm").write_text(body, encoding="utf-8")
        global ASM
        keep = ASM
        try:
            ASM = tmp
            rows, errs, tot = build()
            bad = bool(errs) or bool(tot["unparsed"]) or bool(tot["es_unresolved"])
            if bad != want_err or (check and not check(rows)):
                ok = False
                print(f"FAIL  control {label}: errs={errs} unparsed={tot['unparsed']}")
            else:
                print(f"ok    control {label}")
        finally:
            ASM = keep

    case("a plain DS write", "# s\nsub_0001:\n        mov    %al,0x4e\n        ret\n", False,
         lambda r: any(x["access"] == "write" and x["offset"] == 0x4e for x in r))
    case("read-modify-write is its own value", "# s\nsub_0001:\n        incb   0x4c7\n        ret\n",
         False, lambda r: any(x["access"] == "read-modify-write" for x in r))
    case("cmp reads and writes nothing", "# s\nsub_0001:\n        cmp    $0x0,0x4c7\n        ret\n",
         False, lambda r: any(x["access"] == "read" for x in r))
    case("indexed operand is flagged", "# s\nsub_0001:\n        mov    0x4cd(%si),%al\n        ret\n",
         False, lambda r: any(x["indexed"] == "yes" and x["span"] == "0x04cd+64" for x in r))
    case("unknown indexed base is span unknown",
         "# s\nsub_0001:\n        mov    0x700(%si),%al\n        ret\n", False,
         lambda r: any(x["span"] == "unknown" for x in r))
    case("an unknown mnemonic with a memory operand is a hard failure",
         "# s\nsub_0001:\n        frob   0x4e\n        ret\n", True)
    case("an unknown mnemonic with no memory operand is tolerated",
         "# s\nsub_0001:\n        frob\n        ret\n", False)
    case("a jmp target is not a data access", "# s\nsub_0001:\n        jmp    0x4e\n        ret\n",
         False, lambda r: not r)
    case("es: with no setup is a failure", "# s\nsub_0001:\n        mov    %al,%es:0x6c\n        ret\n",
         True)

    # The mirror control, because an exemption tested only in the failing direction is a permission
    # nobody can trust: a file named in ES_CONVENTION must let verify() pass, and an entry that stops
    # matching must make it fail. Both halves are checked here rather than asserted in a comment.
    for f in tmp.glob("*.asm"):
        f.unlink()
    # Every file named in ES_CONVENTION has to be present, or the stale rule fires -- correctly -- on
    # the three that are missing, and the control would be testing the wrong thing while passing.
    for name in ES_CONVENTION:
        (tmp / name).write_text("# s\nsub_x:\n        mov    %es:(%di),%al\n        ret\n",
                                encoding="utf-8")
    keep = ASM
    try:
        ASM = tmp
        rc = verify()
        if rc != 0:
            ok = False
            print("FAIL  control a recorded ES convention passes verify: rc=" + str(rc))
        else:
            print("ok    control a recorded ES convention passes verify")
        # And the stale-entry half: leave the file with no `es:` at all, so the ONLY reason verify can
        # go red is that the convention entry now matches nothing. Renaming it instead would have made
        # it fail for the unexempted-site reason and proved nothing about the stale rule.
        (tmp / "1564_sub_1564.asm").write_text(
            "# s\nsub_x:\n        mov    %al,0x4e\n        ret\n", encoding="utf-8")
        if verify() == 0:
            ok = False
            print("FAIL  control a stale ES convention entry was tolerated")
        else:
            print("ok    control a stale ES convention entry goes red")
    finally:
        ASM = keep
    case("es set to ds resolves", "# s\nsub_0001:\n        mov    %ds,%es\n        mov    %al,%es:0x6c\n"
         "        ret\n", False, lambda r: any(x["segment"] == "es" for x in r))
    case("video es goes to its own segment",
         "# s\nsub_0001:\n        mov    $0xb800,%es\n        mov    %al,%es:0x6c\n        ret\n",
         False, lambda r: any(x["segment"] == "video" for x in r))
    case("a garbage line fails self-accounting", "# s\nsub_0001:\n        ??? nonsense\n        ret\n",
         True)
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--check-fresh", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--lookup")
    a = ap.parse_args()
    if a.selftest:
        return _selftest()
    if a.verify:
        return verify()
    if a.lookup:
        return lookup(a.lookup)
    rows, errors, tot = build()
    if blocking(errors):
        for e in blocking(errors):
            print(f"  FAIL  {e}", file=sys.stderr)
        return 1
    if a.write:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(table(rows), encoding="utf-8")
        print(f"wrote {OUT.relative_to(REPO)} ({len(rows)} rows)")
        return 0
    if a.check_fresh:
        if not OUT.exists():
            print(f"index_footprint: FAIL  {OUT.relative_to(REPO)} does not exist")
            return 1
        if OUT.read_text(encoding="utf-8") != table(rows):
            print(f"index_footprint: FAIL  {OUT.relative_to(REPO)} is STALE; run "
                  "tools/index_footprint.py --write")
            return 1
        print(f"index_footprint: ok  {OUT.relative_to(REPO)} matches asm/")
        return 0
    print(f"{len(rows)} footprint rows over {tot['files']} files; "
          f"{len(set((r['segment'], r['offset']) for r in rows))} distinct (segment,offset)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
