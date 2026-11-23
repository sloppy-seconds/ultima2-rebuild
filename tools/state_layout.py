#!/usr/bin/env python3
"""state_layout.py -- the DS offset table, generated from engine/src/core/state.h.

WHY. `state.h` declares the DS working segment as a struct and says so in its own header: "THIS
STRUCT IS THE ONE TABLE FOR DS OFFSETS". A claim like that is only true if something reads it.
Until now nothing did -- `tools/u2_edit.py` hand-copies `BASE = 0x36` and a field dict that cites
`state.h` in comments, which is the second hand-maintained copy the glossary's Seed module ruling
forbids. This is the parser the code index epic owes (`docs/plan-code-index.md` § 6 item 2, #32):
`coverage` needs it for the unnamed-offset queue, `footprint` needs it for names as query keys
and the `save_offset` column, and it is the only thing that makes the `u2_edit.py` copy fixable.

WHAT IT PROVES, and every one of these caught something on the first run:

  * the `/* 0xNN */` comment on each field agrees with the offset computed from the running sizes,
    and the fields are contiguous with no gap;
  * every `U2_OFF(name) == 0xNNN` assert agrees with the same computation, and the computed
    `sizeof` equals the struct's own size assert;
  * **self-accounting**: a line inside the struct that looks field-shaped and does not parse is a
    hard failure. This is not ceremony. The first version of this parser silently skipped
    `uint8_t reserved_2A[4]` because the name pattern was lowercase-only, which broke contiguity
    from 0x2A onward and would have made every offset after it wrong in a table that looked fine.

WHAT IT REPORTS AND DOES NOT YET FAIL. The header claims every named field is pinned by a
`_Static_assert`; 18 named fields are not (`sector_x`, `stat_agi`, `space_ship_x`, ...). The
`asserted` column says so per row, and `--verify` ratchets: it fails if the count rises above
`--max-unasserted`, which is now 0 — every named field is pinned, and it can only stay that way.
and its own commit, not this tool's.

USAGE
  tools/state_layout.py                      # TSV to stdout
  tools/state_layout.py --json               # same rows as JSON
  tools/state_layout.py --verify             # the gate; exit 1 on any disagreement
  tools/state_layout.py --write              # write disasm/index/state.tsv (this tool is its only writer)
  tools/state_layout.py --check-fresh        # regenerate and diff against the committed table
  python3 -c "import sys; sys.path.insert(0,'tools'); from state_layout import fields"

COLUMNS
  ds_offset   the byte's address in the DS segment, as the asm writes it.
  save_offset the same byte's address inside the 256-byte save record, i.e. ds - 0x36, or empty
              when the byte is not part of the saved record. 0x36 is not a guess here: it is the
              offset the struct itself gives the `name` field, which is the first saved byte.
  name        the C field name. `reserved_*` means nobody has proved what the byte does.
  c_type      uint8_t or uint16_t.
  size        bytes, array length folded in.
  kind        named | reserved.
  asserted    yes if a U2_OFF assert pins this exact field.
  cited_in    the first asm/ file the declaration cites, empty if it cites none.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
STATE_H = REPO / "engine" / "src" / "core" / "state.h"
OUT = REPO / "disasm" / "index" / "state.tsv"

FIELD_RE = re.compile(
    r"^\s*/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*(uint8_t|uint16_t)\s+([A-Za-z0-9_]+)"
    r"(\[(0x[0-9A-Fa-f]+|\d+)\])?\s*;")
# Anything that looks like a declaration but is not one FIELD_RE accepts. Deliberately loose:
# a false positive here is a loud failure we want, a false negative is the silent kind.
FIELDISH_RE = re.compile(r"^\s*(?:/\*[^*]*\*/\s*)?uint(8|16)_t\s+[A-Za-z0-9_]+")
# Not anchored to `_Static_assert(` on purpose: the compound form
# `_Static_assert(U2_OFF(a) == 0x272 && U2_OFF(b) == 0x372, ...)` pins two fields on one line, and
# an anchored pattern counted only the first — which reported stage_b as unasserted while the header
# asserted it. A parser that silently under-counts is the defect this tool exists to prevent.
ASSERT_RE = re.compile(r"U2_OFF\(([A-Za-z0-9_]+)\)\s*==\s*(0x[0-9A-Fa-f]+)")
SIZEOF_RE = re.compile(r"_Static_assert\(sizeof\(u2_state\)\s*==\s*(0x[0-9A-Fa-f]+)")
ASM_CITE_RE = re.compile(r"asm/([0-9A-Fa-f]{4})")


class Field(dict):
    """A parsed field. Dict subclass so callers can treat rows as plain records."""


def parse(text: str) -> tuple[list[Field], list[str]]:
    """Return (fields, unparsed_lines). Offsets are computed, never read from the comment."""
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("typedef struct u2_state"))
        end = next(i for i, l in enumerate(lines) if l.startswith("} u2_state"))
    except StopIteration:
        raise SystemExit("state_layout: no `typedef struct u2_state` / `} u2_state` in "
                         f"{STATE_H}. The parser's assumption about the file is stale.")

    fields: list[Field] = []
    unparsed: list[str] = []
    in_comment = False
    offset = 0
    for i in range(start + 1, end):
        line = lines[i]
        if in_comment:
            if "*/" in line:
                in_comment = False
            continue
        m = FIELD_RE.match(line)
        if m:
            count = int(m.group(5), 0) if m.group(5) else 1
            width = 1 if m.group(2) == "uint8_t" else 2
            cites = ASM_CITE_RE.findall(line)
            fields.append(Field(
                ds_offset=offset, comment_offset=int(m.group(1), 16), name=m.group(3),
                c_type=m.group(2), size=width * count, count=count,
                kind="reserved" if m.group(3).startswith("reserved") else "named",
                cited_in=cites[0].lower() if cites else ""))
            offset += width * count
            if line.count("/*") > line.count("*/"):
                in_comment = True
            continue
        if FIELDISH_RE.match(line):
            unparsed.append(f"state.h:{i + 1}: {line.strip()[:70]}")
            continue
        if "/*" in line and "*/" not in line:
            in_comment = True
    return fields, unparsed


def asserts(text: str) -> tuple[dict[str, int], int | None]:
    offs = {m.group(1): int(m.group(2), 16) for m in ASSERT_RE.finditer(text)}
    sz = SIZEOF_RE.search(text)
    return offs, (int(sz.group(1), 16) if sz else None)


def annotate(fields: list[Field], aoffs: dict[str, int]) -> list[Field]:
    save_base = next((f["ds_offset"] for f in fields if f["name"] == "name"), None)
    for f in fields:
        f["asserted"] = "yes" if f["name"] in aoffs else "no"
        rel = None if save_base is None else f["ds_offset"] - save_base
        f["save_offset"] = (f"0x{rel:03x}" if rel is not None and 0 <= rel < 0x100 else "")
    return fields


COLS = ("ds_offset", "save_offset", "name", "c_type", "size", "kind", "asserted", "cited_in")


def rows(fields: list[Field]) -> list[dict[str, str]]:
    out = []
    for f in fields:
        r = {c: (f"0x{f['ds_offset']:03x}" if c == "ds_offset" else str(f.get(c, "")))
             for c in COLS}
        out.append(r)
    return out


def table() -> str:
    rs = rows(FIELDS)
    return "\t".join(COLS) + "\n" + "\n".join(
        "\t".join(r[c] for c in COLS) for r in rs) + "\n"


def _verify_text(text: str, max_unasserted: int) -> tuple[list[str], str]:
    """All the checks, on arbitrary text, so the controls can feed it known-bad input."""
    fields, unparsed = parse(text)
    aoffs, sizeof_assert = asserts(text)
    annotate(fields, aoffs)

    fails: list[str] = []
    for line in unparsed:
        fails.append(f"unparsed field-shaped line: {line}")
    off = 0
    for f in fields:
        if f["comment_offset"] != off:
            fails.append(f"{f['name']}: comment says 0x{f['comment_offset']:x}, sizes say "
                         f"0x{off:x}")
        if f["name"] in aoffs and aoffs[f["name"]] != off:
            fails.append(f"{f['name']}: _Static_assert says 0x{aoffs[f['name']]:x}, sizes say "
                         f"0x{off:x}")
        off += f["size"]
    if sizeof_assert is None:
        fails.append("no `sizeof(u2_state)` assert to check the total against")
    elif sizeof_assert != off:
        fails.append(f"struct size: assert says 0x{sizeof_assert:x}, sizes say 0x{off:x}")
    unasserted = [f["name"] for f in fields if f["kind"] == "named" and f["asserted"] == "no"]
    if len(unasserted) > max_unasserted:
        fails.append(f"{len(unasserted)} named fields have no U2_OFF assert (ratchet allows "
                     f"{max_unasserted}): {', '.join(unasserted[:8])}")
    named = sum(1 for f in fields if f["kind"] == "named")
    summary = (f"{len(fields)} fields, {named} named, {len(unasserted)} named without an "
               f"assert, struct 0x{off:x}")
    return fails, summary


def verify(max_unasserted: int) -> int:
    text = STATE_H.read_text(encoding="utf-8")
    FIELDS.clear()
    FIELDS.extend(parse(text)[0])
    annotate(FIELDS, asserts(text)[0])
    fails, summary = _verify_text(text, max_unasserted)
    print(f"state_layout: {summary}")
    for f in fails:
        print(f"  FAIL  {f}")
    print("state_layout: " + ("FAIL" if fails else "ok"))
    return 1 if fails else 0


_GOOD = """typedef struct u2_state {
    /* 0x00 */ uint8_t sector_x;      /* asm/5AE1 */
    /* 0x01 */ uint8_t reserved_01[2];
    /* 0x03 */ uint8_t name[16];
} u2_state;
_Static_assert(U2_OFF(sector_x) == 0x000, "x");
_Static_assert(U2_OFF(name)     == 0x003, "n");
_Static_assert(sizeof(u2_state) == 0x13, "size");
"""


def selftest() -> int:
    """Controls. Each is a mistake the gate must catch, plus the clean case."""
    ok = True

    def expect(label: str, text: str, want_fail: bool, cap: int = 1):
        nonlocal ok
        fails, _ = _verify_text(text, cap)
        if bool(fails) != want_fail:
            ok = False
            print(f"FAIL  control {label}: expected {'red' if want_fail else 'green'}, "
                  f"got {fails or 'clean'}")
        else:
            print(f"ok    control {label}")

    expect("clean struct", _GOOD, False, cap=1)
    expect("comment offset disagrees with the sizes",
           _GOOD.replace("/* 0x03 */", "/* 0x04 */"), True)
    expect("_Static_assert disagrees with the sizes",
           _GOOD.replace("U2_OFF(name)     == 0x003", "U2_OFF(name)     == 0x004"), True)
    expect("struct size assert disagrees",
           _GOOD.replace("sizeof(u2_state) == 0x13", "sizeof(u2_state) == 0x14"), True)
    expect("field-shaped line the parser cannot read",
           _GOOD.replace("    /* 0x01 */ uint8_t reserved_01[2];",
                         "    uint8_t sneaky[2];"), True)
    expect("ratchet: unasserted named fields above the cap",
           _GOOD.replace("uint8_t reserved_01[2];", "uint8_t stat_agi;"), True, cap=0)
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


FIELDS: list[Field] = []
FIELDS, _UNPARSED = parse(STATE_H.read_text(encoding="utf-8"))
annotate(FIELDS, asserts(STATE_H.read_text(encoding="utf-8"))[0])


def fields() -> list[Field]:
    """The parsed struct, offsets computed and `asserted`/`save_offset` annotated.

    The importable entry point: `coverage` and `footprint` both read the DS layout through this
    rather than re-parsing state.h, which is the whole reason the parser exists.
    """
    return FIELDS


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check-fresh", action="store_true")
    ap.add_argument("--selftest", action="store_true", help="run the controls and exit")
    ap.add_argument("--max-unasserted", type=int, default=0,
                    help="ratchet: fail above this many named fields without a U2_OFF assert")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if args.verify:
        return verify(args.max_unasserted)
    if args.write:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(table(), encoding="utf-8")
        print(f"wrote {OUT.relative_to(REPO)} ({len(FIELDS)} rows)")
        return 0
    if args.check_fresh:
        if not OUT.exists():
            print(f"state_layout: FAIL  {OUT.relative_to(REPO)} does not exist")
            return 1
        fresh = table()
        if OUT.read_text(encoding="utf-8") != fresh:
            print(f"state_layout: FAIL  {OUT.relative_to(REPO)} is STALE; run "
                  "tools/state_layout.py --write")
            return 1
        print(f"state_layout: ok  {OUT.relative_to(REPO)} matches the struct")
        return 0
    if args.json:
        print(json.dumps(rows(FIELDS), indent=1))
    else:
        sys.stdout.write(table())
    return 0


if __name__ == "__main__":
    sys.exit(main())
