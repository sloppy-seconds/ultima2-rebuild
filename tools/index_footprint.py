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
import csv
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
COLS = ("segment", "offset", "span", "access", "file", "line", "indexed", "es_as", "note",
        "entity", "entity_basis")

# ------------------------------------------------------------------ entity names
# Two columns join an access to the THING it touches, and they are populated only where
# the evidence really names it. The basis string carries the grade in its own name, so a
# reader cannot lift the entity out of the table without also lifting how it was known:
#   slot-address        -- the absolute DS address IS the entity. Measured.
#   file-literal-inferred -- the row is a creature-array base, which names no creature;
#                            the only evidence is a type literal elsewhere in the same
#                            file, and only when the file holds exactly one. Inferred,
#                            and blank rather than guessed whenever a file holds several.
# Names come from generated, gated sources only. Nothing here is hand-copied: a hand-typed
# creature table already drifted from the census once and produced a wrong count.

ITEM_BASE, SPELL_BASE = 0xD6, 0xB6
MON_TYPE_BASE = 0x197

# Populated by build() before any file is parsed. emit_rows reads them, which is why they
# are module-level rather than threaded through every call.
ENTITY_BY_OFFSET: dict[int, str] = {}
ENTITY_BY_FILE: dict[str, str] = {}


def _read_generated_tsv(path: pathlib.Path) -> list[dict]:
    """Read a committed generated table, skipping the '#' preamble every census table has.

    Feeding one of these straight to DictReader makes a comment line the header and yields
    zero usable rows, which looks like an empty table instead of a parsing mistake.
    """
    body = [ln for ln in path.read_text(encoding="utf-8").splitlines()
            if not ln.lstrip().startswith("#")]
    return list(csv.DictReader(body, delimiter="\t"))


def load_entity_names() -> tuple[dict[int, str], dict[int, str], dict[str, str]]:
    """Return (ds_offset -> entity, file -> entity, diagnostics).

    Items and spells come from the arrays the extractor generates from the executable, so
    the slot order is the executable's and not this tool's opinion. Creature types come
    from the census, which is the entity authority; the type byte is art << 2.
    """
    diag: dict[str, str] = {}
    by_offset: dict[int, str] = {}

    content = REPO / "engine" / "content" / "gen_content.h"
    if not content.is_file():
        raise SystemExit("index_footprint: engine/content/gen_content.h is missing -- it is "
                         "committed and generated, so its absence is a tree fault, not an "
                         "optional input")
    text = content.read_text(encoding="utf-8", errors="replace")
    for array, base, kind in (("U2_T_ITEMS", ITEM_BASE, "item"),
                              ("U2_T_SPELLS", SPELL_BASE, "spell")):
        m = re.search(rf"{array}\[[^]]*\]\s*=\s*\{{(.*?)\}};", text, re.S)
        if not m:
            raise SystemExit(f"index_footprint: could not find {array} in gen_content.h -- "
                             "the extractor changed shape, so entity names would be silently "
                             "incomplete")
        names = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
        for i, nm in enumerate(names):
            if nm.upper() == "NONE":
                continue          # slot 0 of the spell list is a placeholder, not a spell
            by_offset[base + i] = f"{kind}:{nm.lower().replace(' ', '-').replace('.', '')}"
        diag[f"{kind}_slots"] = str(len(names))

    creature_tsv = REPO / "disasm" / "census" / "creature.tsv"
    if not creature_tsv.is_file():
        raise SystemExit("index_footprint: disasm/census/creature.tsv is missing -- run "
                         "tools/census.py; creature entities would otherwise be silently empty")
    types = {}
    for r in _read_generated_tsv(creature_tsv):
        art = (r.get("art") or "").strip()
        if art:
            types[int(art, 0) << 2] = r["name"]     # art is 0x-prefixed hex
    diag["creature_types"] = str(len(types))
    return by_offset, types, diag


def creature_file_literals(paths: list[pathlib.Path], types: dict[int, str]
                           ) -> tuple[dict[str, str], int]:
    """Map a file to the one creature its type literals name, and only when there is one.

    Returns (file -> entity, count of files left blank for holding several types). A file
    holding two or more type literals is unattributed on purpose: spreading its
    creature-array rows over every type mentioned in it would invent edges that look
    exactly as good as the real ones.
    """
    pats = {t: re.compile(rf"\$0x{t:02x}\b") for t in types}
    out: dict[str, str] = {}
    ambiguous = 0
    for path in paths:
        found: set[str] = set()
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.lstrip().startswith("#"):
                continue
            for t, pat in pats.items():
                if pat.search(line):
                    found.add(types[t])
        if len(found) == 1:
            out[path.name] = "creature:" + next(iter(found)).lower().replace(" ", "-")
        elif len(found) > 1:
            ambiguous += 1
    return out, ambiguous

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


# --------------------------------------------------------------- forced-keep bytes
# A forced-keep line holds raw bytes because gas re-encodes the same instruction a
# different way. The trailing comment names the instruction, but a comment is prose and
# prose can be stale, and the body is authoritative over prose without exception. So
# these tables decode the BYTES; the comment is used only as a cross-check whose
# disagreements are printed. An opcode not listed here is reported as undecoded, never
# guessed at -- the same rule the mnemonic path applies to an unknown mnemonic with a
# memory operand.
#
# Coverage is the 8086 mr/rm/modrm family plus the four direct-displacement movs, which
# is where the memory accesses actually live. ss-relative bases are emitted without a
# segment prefix because classify_operand already collapses ss onto ds; that is the
# pre-existing behaviour of this tool, not a new claim.

REG8 = ["al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"]
REG16 = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"]
# 8086 r/m encodings for mod != 3, in encoding order, as (base, index-or-None).
# Written as pairs and rendered AT&T-style `(%bx,%si)` because that is both what objdump
# prints and what classify_operand's MEM_RE parses as base+idx. The order here was got
# wrong once (bx at index 0, bx+di at index 7) and a wrong row silently attributes an
# access to the wrong base register, so --selftest pins every eight entries against
# objdump rather than trusting anyone's memory including this file's.
RM16 = [("bx", "si"), ("bx", "di"), ("bp", "si"), ("bp", "di"),
        ("si", None), ("di", None), ("bp", None), ("bx", None)]

# opcode -> (mnemonic, width_bits, shape). Shape is named for the AT&T text this tool
# emits, not for the Intel operand order, because AT&T reverses Intel: opcode 8b is
# `mov r, r/m` in Intel and `mov mem, %reg` here, while opcode 89 is the mirror.
#   "mr"  modrm, memory is the first (source) operand   (mov 0x197(%di),%al)
#   "rm"  modrm, memory is the last (dest) operand      (mov %al,0x197(%di))
#   "rmi" modrm, memory dest, immediate source         (c6 /0 ib)
#   "grp" modrm, memory dest, mnemonic from reg field  (80/81/83)
#   "ms"  no modrm, disp16 follows, memory is source   (a0/a1)
#   "md"  no modrm, disp16 follows, memory is dest     (a2/a3)
BYTE_OPS: dict[int, tuple[str, int, str]] = {
    0x00: ("add", 8, "rm"),   0x01: ("add", 16, "rm"),
    0x02: ("add", 8, "mr"),   0x03: ("add", 16, "mr"),
    0x08: ("or", 8, "rm"),    0x09: ("or", 16, "rm"),
    0x0a: ("or", 8, "mr"),    0x0b: ("or", 16, "mr"),
    0x10: ("adc", 8, "rm"),   0x11: ("adc", 16, "rm"),
    0x12: ("adc", 8, "mr"),   0x13: ("adc", 16, "mr"),
    0x18: ("sbb", 8, "rm"),   0x19: ("sbb", 16, "rm"),
    0x1a: ("sbb", 8, "mr"),   0x1b: ("sbb", 16, "mr"),
    0x20: ("and", 8, "rm"),   0x21: ("and", 16, "rm"),
    0x22: ("and", 8, "mr"),   0x23: ("and", 16, "mr"),
    0x28: ("sub", 8, "rm"),   0x29: ("sub", 16, "rm"),
    0x2a: ("sub", 8, "mr"),   0x2b: ("sub", 16, "mr"),
    0x30: ("xor", 8, "rm"),   0x31: ("xor", 16, "rm"),
    0x32: ("xor", 8, "mr"),   0x33: ("xor", 16, "mr"),
    0x38: ("cmp", 8, "rm"),   0x39: ("cmp", 16, "rm"),
    0x3a: ("cmp", 8, "mr"),   0x3b: ("cmp", 16, "mr"),
    0x80: ("grp", 8, "grp"),  0x81: ("grp", 16, "grp"),  0x83: ("grp", 16, "grp"),
    0x84: ("test", 8, "rm"),  0x85: ("test", 16, "rm"),
    0x86: ("xchg", 8, "rm"),  0x87: ("xchg", 16, "rm"),
    0x88: ("mov", 8, "rm"),   0x89: ("mov", 16, "rm"),
    0x8a: ("mov", 8, "mr"),   0x8b: ("mov", 16, "mr"),
    0xa0: ("mov", 8, "ms"),   0xa1: ("mov", 16, "ms"),
    0xa2: ("mov", 8, "md"),   0xa3: ("mov", 16, "md"),
    0xc6: ("mov", 8, "rmi"),  0xc7: ("mov", 16, "rmi"),
}
GRP_MN = ["add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"]

BYTE_RE = re.compile(r"^\.byte\b\s*(.*?)(?:\s*#\s*(.*))?$")
HEXBYTE = re.compile(r"0x([0-9a-fA-F]{1,2})")
# A forced-keep run can be the tail of an instruction that began in a DIFFERENT file, and
# then its first byte is not an opcode at all. Decoding such a run invents a memory access
# that does not exist: 49 runs say they are a tail, and 7 of them decode into something
# plausible, which is the worst possible failure because nothing downstream would notice.
# The comment is the only evidence of a fragment that exists, so it is load-bearing here
# even though the bytes outrank it everywhere else.
TAIL_RE = re.compile(r"\b(tail|continuation|rest of|remainder)\b", re.I)


def _le(buf: list[int], i: int, n: int):
    """Little-endian unsigned read of n bytes, or None if the run is too short."""
    if i + n > len(buf):
        return None
    return sum(buf[i + k] << (8 * k) for k in range(n))


def _modrm(buf: list[int], i: int, width: int):
    """Decode one modrm byte.

    Returns (mem_or_None, reg_name, rm_reg_or_None, next_index, reg_index); the numeric
    reg_index is what the group opcodes need to pick their mnemonic.
    """
    if i >= len(buf):
        return None
    m = buf[i]
    i += 1
    mode, reg, rm = (m >> 6) & 3, (m >> 3) & 7, m & 7
    rn = (REG8 if width == 8 else REG16)[reg]
    if mode == 3:
        return None, rn, (REG8 if width == 8 else REG16)[rm], i, reg
    if mode == 0 and rm == 6:
        d = _le(buf, i, 2)
        if d is None:
            return None
        return f"0x{d:x}", rn, None, i + 2, reg
    if mode == 1:
        d = _le(buf, i, 1)
        if d is None:
            return None
        if d > 0x7F:
            d -= 0x100
        i += 1
    elif mode == 2:
        d = _le(buf, i, 2)
        if d is None:
            return None
        if d > 0x7FFF:
            d -= 0x10000
        i += 2
    else:
        d = 0
    base, idx = RM16[rm]
    # The % on the base register is not decoration: classify_operand's MEM_RE only matches
    # a base written %di, so emitting (di) would decode cleanly here and then produce no
    # row at all -- the worst kind of failure, a decoder that looks like it worked.
    mem = f"%{base}" + (f",%{idx}" if idx else "")
    mem = f"({mem})" if d == 0 else f"{d:#x}({mem})"
    return mem, rn, None, i, reg


def decode_run(buf: list[int]) -> tuple[str, str] | None:
    """Decode a forced-keep byte run to (mnemonic, AT&T operands), or None if uncovered.

    The operand text is deliberately well-formed AT&T: an immediate is written `$0` and
    not `$imm`, because classify_operand skips anything starting with `$` and a
    malformed source would silently swallow the memory operand beside it.
    """
    if not buf:
        return None
    ent = BYTE_OPS.get(buf[0])
    if ent is None:
        return None
    mn, width, shape = ent
    acc = "al" if width == 8 else "ax"
    if shape == "ms":
        d = _le(buf, 1, 2)
        return None if d is None else (mn, f"0x{d:x},%{acc}")
    if shape == "md":
        d = _le(buf, 1, 2)
        return None if d is None else (mn, f"%{acc},0x{d:x}")
    r = _modrm(buf, 1, width)
    if r is None:
        return None
    mem, rn, rmreg, nxt, reg_idx = r
    if shape == "grp":
        mn = GRP_MN[reg_idx]
    if mem is None:                       # register-to-register: touches no memory
        if shape in ("rmi", "grp"):
            imm = _imm(buf, nxt, buf[0])
            return mn, f"{imm},%{rmreg}"
        return mn, f"%{rmreg},%{rn}"
    if shape == "mr":
        return mn, f"{mem},%{rn}"
    if shape in ("rmi", "grp"):
        # The immediate is decoded rather than stubbed. It cannot change a footprint row,
        # because an immediate is never a memory operand, but a placeholder in emitted
        # text is a small lie that the next reader would have to re-check.
        imm = _imm(buf, nxt, buf[0])
        return mn, f"{imm},{mem}"
    return mn, f"%{rn},{mem}"


def _imm(buf: list[int], i: int, opcode: int) -> str:
    """Read the immediate an r/m-plus-immediate opcode carries, or $0 if the run is short."""
    n = 2 if opcode in (0x81, 0xc7) else 1
    v = _le(buf, i, n)
    if v is None:
        return "$0"
    if n == 2:
        return f"$0x{v:x}"
    return f"$0x{v:x}"


def byte_runs(lines: list[str]) -> tuple[dict, set, int, int, int]:
    """Pre-pass: join consecutive .byte lines into runs and decode each one.

    Returns (start_line -> (mn, ops, comment), lines consumed as data, decoded, undecoded,
    fragments). Consecutive joining matters because a forced-keep line can be the tail of
    an instruction that began on the previous line, so a single line is not always a whole
    instruction. A run that says it is the tail of something in another file is never
    decoded -- see TAIL_RE.
    """
    out: dict[int, tuple[str, str, str]] = {}
    consumed: set[int] = set()
    decoded = undecoded = fragments = 0
    i = 0
    while i < len(lines):
        m = BYTE_RE.match(lines[i].strip())
        if not m:
            i += 1
            continue
        buf: list[int] = []
        comment = ""
        start = i
        while i < len(lines):
            m2 = BYTE_RE.match(lines[i].strip())
            if not m2:
                break
            buf += [int(h, 16) for h in HEXBYTE.findall(m2.group(1))]
            if m2.group(2):
                comment = m2.group(2)
            consumed.add(i + 1)
            i += 1
        if TAIL_RE.search(comment):
            fragments += 1
            continue
        d = decode_run(buf)
        if d:
            out[start + 1] = (d[0], d[1], comment)
            decoded += 1
        else:
            undecoded += 1
    return out, consumed, decoded, undecoded, fragments


def comment_disagrees(mn: str, ops: str, comment: str) -> bool:
    """True when the comment names an instruction whose mnemonic is not what we decoded.

    Only a mnemonic comparison is made. A comment that names no mnemonic at all is not a
    disagreement, because plenty of forced-keep comments describe something else.
    """
    if not comment:
        return False
    named = re.findall(r"\b(mov|add|or|adc|sbb|and|sub|xor|cmp|test|xchg)\b", comment)
    if not named:
        return False
    return mn not in named


def emit_rows(mn: str, ops: str, lineno: int, fname: str, es_as: str,
              note_extra: str = "") -> tuple[list[dict], list[str]]:
    """One instruction's footprint rows. Shared by the mnemonic path and the byte path."""
    rows: list[dict] = []
    errors: list[str] = []
    if mn.startswith(CONTROL):
        return rows, errors
    acc = access_for(mn)
    operands = split_operands(ops) if ops else []
    if acc is None:
        if any(classify_operand(o) for o in operands):
            if mn in IMPOSSIBLE:
                errors.append(f"{fname}:{lineno}: {mn!r} cannot exist on this target -- "
                              "misdecode candidate, needs a ruling (see IMPOSSIBLE)")
            else:
                errors.append(f"{fname}:{lineno}: mnemonic {mn!r} with a memory operand is "
                              "not in the access table -- refusing to guess")
        return rows, errors
    reads, writes = acc[1], acc[2]
    dst_role, src_role = roles_for(acc[0], reads, writes)
    last = len(operands) - 1
    for pos, op in enumerate(operands):
        cm = classify_operand(op)
        if cm is None:
            continue
        role = dst_role if pos == last else src_role
        if not role:
            continue
        access = {"read": "read", "write": "write", "rmw": "read-modify-write"}[role]
        seg, offset = cm["seg"], cm["offset"]
        indexed = bool(cm["base"]) or bool(cm["idx"])
        note = "absolute operand, no base register" if cm["bare"] else ""
        if note_extra:
            note = (note + "; " if note else "") + note_extra
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
        entity, basis = "", ""
        if seg == "ds":
            if offset in ENTITY_BY_OFFSET:
                entity, basis = ENTITY_BY_OFFSET[offset], "slot-address"
            elif offset == MON_TYPE_BASE and fname in ENTITY_BY_FILE:
                # The row itself names only the creature array; the file is the evidence.
                entity, basis = ENTITY_BY_FILE[fname], "file-literal-inferred"
        rows.append({"segment": seg, "offset": offset, "span": span, "access": access,
                     "file": fname, "line": str(lineno),
                     "indexed": "yes" if indexed else "no",
                     "es_as": es_as if seg == "es" else "", "note": note,
                     "entity": entity, "entity_basis": basis})
    return rows, errors


def parse_file(path: pathlib.Path) -> tuple[list[dict], list[str], dict]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    rows: list[dict] = []
    errors: list[str] = []
    stats = {"parsed": 0, "labels": 0, "data": 0, "comments": 0, "blank": 0, "unparsed": 0}
    es_as = parse_es_setup(lines)
    runs, consumed, byte_decoded, byte_undecoded, byte_fragments = byte_runs(lines)
    forced_keep = len(consumed)
    disagreements: list[str] = []
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
            # A forced-keep run that decoded is an instruction like any other, so it goes
            # through the same emitter. The line number reported is the run's first line,
            # because that is where the opcode lives.
            if i in runs:
                bmn, bops, bcomment = runs[i]
                r, e = emit_rows(bmn, bops, i, path.name, es_as,
                                 note_extra="decoded from forced-keep bytes")
                rows += r
                errors += e
                if comment_disagrees(bmn, bops, bcomment):
                    disagreements.append(
                        f"{path.name}:{i}: bytes decode to {bmn} but comment says "
                        f"{bcomment.strip()[:60]!r}")
            continue
        m = INSTR_RE.match(line)
        if not m:
            stats["unparsed"] += 1
            errors.append(f"{path.name}:{i}: unparsed instruction-shaped line {line[:40]!r}")
            continue
        mn, ops = m.group(1), (m.group(2) or "").strip()
        stats["parsed"] += 1
        r, e = emit_rows(mn, ops, i, path.name, es_as)
        rows += r
        errors += e
    return rows, errors, {**stats, "forced_keep_lines": forced_keep,
                          "byte_decoded": byte_decoded, "byte_undecoded": byte_undecoded,
                          "byte_fragments": byte_fragments,
                          "byte_comment_disagree": len(disagreements),
                          "byte_disagreements": disagreements,
                          "es_as": es_as}


def build() -> tuple[list[dict], list[str], dict]:
    if not ASM.is_dir():
        raise SystemExit("index_footprint: no asm/ -- refusing to report success on a tree it "
                         "cannot see")
    global ENTITY_BY_OFFSET, ENTITY_BY_FILE
    paths = sorted(ASM.glob("*.asm"))
    ENTITY_BY_OFFSET, creature_types, ent_diag = load_entity_names()
    ENTITY_BY_FILE, ent_ambiguous = creature_file_literals(paths, creature_types)
    rows: list[dict] = []
    errors: list[str] = []
    tot = {"files": 0, "parsed": 0, "labels": 0, "data": 0, "comments": 0, "blank": 0,
           "unparsed": 0, "forced_keep_lines": 0, "byte_decoded": 0, "byte_undecoded": 0,
           "byte_fragments": 0, "byte_comment_disagree": 0}
    byte_disagree: list[str] = []
    es_unresolved: list[str] = []
    for path in paths:
        r, e, st = parse_file(path)
        rows += r
        errors += e
        tot["files"] += 1
        for k in ("parsed", "labels", "data", "comments", "blank", "unparsed",
                  "forced_keep_lines", "byte_decoded", "byte_undecoded",
                  "byte_fragments", "byte_comment_disagree"):
            tot[k] += st[k]
        byte_disagree += st["byte_disagreements"]
        if st["es_as"] == "none" and any(x["segment"] == "es" for x in r):
            es_unresolved.append(path.name)
    tot["es_unresolved"] = es_unresolved
    tot["entity_diag"] = ent_diag
    tot["entity_rows"] = sum(1 for r in rows if r["entity"])
    tot["entity_distinct"] = len({r["entity"] for r in rows if r["entity"]})
    tot["entity_ambiguous_files"] = ent_ambiguous
    tot["byte_disagreements"] = byte_disagree
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
    # The forced-keep tally is printed because a number that is never shown is a number
    # that quietly grows. Undecoded runs are not a failure today -- they are the known
    # hole -- but the count must move in public.
    print(f"index_footprint: forced-keep byte runs: decoded {tot['byte_decoded']}, "
          f"undecoded {tot['byte_undecoded']}, refused as cross-file fragments "
          f"{tot['byte_fragments']}, comment-disagreements "
          f"{tot['byte_comment_disagree']}")
    for d in tot["byte_disagreements"][:10]:
        print(f"  WARN  {d} -- the bytes are the authority, the comment is a claim")
    print(f"index_footprint: cs writes (selfmod) = {len(tot['selfmod'])}")
    d = tot["entity_diag"]
    print(f"index_footprint: entity column filled on {tot['entity_rows']} rows, "
          f"{tot['entity_distinct']} distinct entities "
          f"(item slots {d.get('item_slots')}, spell slots {d.get('spell_slots')}, "
          f"creature types {d.get('creature_types')}; "
          f"{tot['entity_ambiguous_files']} files left blank for naming several types)")
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

    # ---- forced-keep byte decoding ------------------------------------------------
    # Every expected string below was confirmed against objdump -mi8086 before it was
    # written down, because hand-computed modrm expectations were wrong three times in a
    # row while this decoder was being built. A wrong r/m row attributes an access to the
    # wrong base register and looks completely healthy doing it.
    def dcase(label: str, seq: list[int], want: str | None):
        nonlocal ok
        got = decode_run(seq)
        s = f"{got[0]} {got[1]}" if got else None
        if s != want:
            ok = False
            print(f"FAIL  decode {label}: got {s!r} want {want!r}")
        else:
            print(f"ok    decode {label}")

    dcase("load from absolute address", [0xa0, 0xdf, 0x00], "mov 0xdf,%al")
    dcase("store to absolute address", [0xa2, 0xe5, 0x00], "mov %al,0xe5")
    dcase("indexed read via %di", [0x8b, 0x85, 0x97, 0x01], "mov 0x197(%di),%ax")
    dcase("indexed write with disp8", [0x88, 0x45, 0x02], "mov %al,0x2(%di)")
    dcase("rm=0 is bx+si", [0x8a, 0x00], "mov (%bx,%si),%al")
    dcase("rm=1 is bx+di", [0x8a, 0x01], "mov (%bx,%di),%al")
    dcase("rm=2 is bp+si", [0x8a, 0x02], "mov (%bp,%si),%al")
    dcase("rm=3 is bp+di", [0x8a, 0x03], "mov (%bp,%di),%al")
    dcase("rm=4 is si", [0x8a, 0x04], "mov (%si),%al")
    dcase("rm=5 is di", [0x8a, 0x05], "mov (%di),%al")
    dcase("rm=6 at mod=0 is disp16 not bp", [0x8a, 0x06, 0x34, 0x12], "mov 0x1234,%al")
    dcase("rm=6 at mod=1 is bp", [0x8a, 0x46, 0x7f], "mov 0x7f(%bp),%al")
    dcase("rm=7 is bx", [0x8a, 0x07], "mov (%bx),%al")
    dcase("group opcode takes mnemonic from reg field", [0x80, 0xc9, 0x20], "or $0x20,%cl")
    dcase("mov immediate to memory", [0xc6, 0x85, 0x97, 0x01, 0x05], "mov $0x5,0x197(%di)")
    dcase("register-to-register decodes but touches no memory", [0x8a, 0xc1], "mov %cl,%al")
    dcase("an opcode outside the table is refused, not guessed", [0xf6, 0xc0], None)

    case("a decoded forced-keep produces a real row",
         "# s\nsub_0001:\n        .byte 0xa0,0xdf,0x00  # forced-kept: gas re-encodes "
         "'mov    0xdf,%al' differently\n        ret\n", False,
         lambda r: any(x["offset"] == 0xdf and x["access"] == "read"
                       and "forced-keep" in x["note"] for x in r))
    case("a decoded forced-keep store is a write",
         "# s\nsub_0001:\n        .byte 0xa2,0xe5,0x00  # forced-kept\n        ret\n", False,
         lambda r: any(x["offset"] == 0xe5 and x["access"] == "write" for x in r))
    case("an indexed forced-keep gets its span",
         "# s\nsub_0001:\n        .byte 0x8b,0x85,0x97,0x01  # forced-kept\n        ret\n", False,
         lambda r: any(x["offset"] == 0x197 and x["span"] == "0x0197+32" for x in r))
    # The control that matters most: a fragment is not an instruction, and a decoder that
    # does not know that invents accesses out of nothing. This must produce no rows.
    case("a cross-file fragment is refused, never decoded",
         "# s\nsub_0001:\n        .byte 0xa0,0x7b,0x04  # tail of `mov 0x76(%di),%al` "
         "started at asm/0000_sub_0000.asm\n        ret\n", False, lambda r: not r)

    # ---- entity column -----------------------------------------------------------
    # The refusal controls matter more than the positive ones: an entity column that
    # guesses is worse than one that leaves a cell blank.
    case("an item slot names its item",
         "# s\nsub_0001:\n        mov    0xe5,%al\n        ret\n", False,
         lambda r: any(x["entity"] == "item:tri-lithium" and x["entity_basis"] == "slot-address"
                       for x in r))
    case("the ring slot names the ring",
         "# s\nsub_0001:\n        mov    0xd6,%al\n        ret\n", False,
         lambda r: any(x["entity"] == "item:ring" for x in r))
    case("a spell slot names its spell",
         "# s\nsub_0001:\n        mov    0xb7,%al\n        ret\n", False,
         lambda r: any(x["entity"].startswith("spell:") for x in r))
    case("slot 0 of the spell list is a placeholder, not an entity",
         "# s\nsub_0001:\n        mov    0xb6,%al\n        ret\n", False,
         lambda r: all(x["entity"] == "" for x in r))
    case("a creature array read is attributed when the file names one type",
         "# s\nsub_0001:\n        mov    0x197(%di),%al\n        cmp    $0x34,%al\n        ret\n",
         False,
         lambda r: any(x["entity"] == "creature:daemon"
                       and x["entity_basis"] == "file-literal-inferred" for x in r))
    # The most important control in this tool: two types in one file means the array rows
    # cannot be attributed to either, and inventing one would look exactly as good.
    case("a file naming two creature types is left blank, not guessed",
         "# s\nsub_0001:\n        mov    0x197(%di),%al\n        cmp    $0x34,%al\n"
         "        cmp    $0x38,%al\n        ret\n", False,
         lambda r: all(x["entity"] == "" for x in r if x["offset"] == 0x197))
    case("an unrelated offset carries no entity",
         "# s\nsub_0001:\n        mov    0x4e,%al\n        ret\n", False,
         lambda r: all(x["entity"] == "" for x in r))

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
