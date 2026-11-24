#!/usr/bin/env python3
"""record_spans: which DS span holds which file's records, at what stride.

Why this exists (docs/plan-record-index.md § 7b, § 7c). An agent querying "which code
touches items" asked DS:0xD6, found only indexed access, and concluded nothing names an
item. The truth is that DS:0xD6 is a 16-record array at stride 1, so DS:0xD9 *is* the boots
slot and the code says so by name. The mistake was not carelessness, it was having no place
to look up that a span is a record array at all. This is that place.

Two halves, and the split is deliberate:

  * The SHAPE is derived. Parallel arrays are found mechanically in disasm/index/state.tsv,
    which is itself generated from engine/src/core/state.h and pinned by _Static_assert.
    Nothing here hand-types an offset.
  * The MEANING is declared, with evidence per row. Which data file backs a span, and what
    kind of entity its records are, is not readable from bytes by anything this tool can
    run, so it is stated in BACKING below and the gate checks the statement still matches
    the derived shape. A declared entry whose group no longer exists is an error, not a skip.

Detection finds every candidate and only the ruled ones become rows; anything left
unclassified is printed, so a new array cannot arrive and go unnoticed. `--strict-unknown`
makes that fatal, which is what a reviewer wants and not what a busy commit wants.

The stride of the inventory array is not assumed. state.h cross-references three items by
name and address -- items_held[BOOTS] (DS:D9), [CLOAK] (DS:DA), [GREEN IDOL] (DS:E4) -- and
those three agree with stride 1 and the extractor's item order only if both are right. That
is a check, so it is run as one: see _prove_item_stride.

Usage:
  tools/record_spans.py                 print the table
  tools/record_spans.py --at 0xD9       what lives at this DS address
  tools/record_spans.py --what item     every span for an entity kind
  tools/record_spans.py --write         write disasm/index/record-spans.tsv
  tools/record_spans.py --check-fresh   regenerate and diff against the committed table
  tools/record_spans.py --selftest      run the controls
"""
from __future__ import annotations

import argparse
import csv
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
STATE = REPO / "disasm" / "index" / "state.tsv"
CONTENT = REPO / "engine" / "content" / "gen_content.h"
OUT = REPO / "disasm" / "index" / "record-spans.tsv"

COLS = ("group", "ds_start", "ds_end", "shape", "n_records", "record_stride",
        "member_fields", "entity_kind", "backing_file", "evidence", "grade")

# The meaning of each derived group, with the evidence that says so. A stale entry -- a
# group that no longer derives -- is an error below, because a declaration that can outlive
# the thing it describes is how a table quietly becomes fiction.
#
# grade is `measured` where a load site or a live capture fixed the shape, and `inferred`
# where only the C declaration and prose do.
BACKING: dict[str, dict[str, str]] = {
    "mon_x": {
        "group": "mon",
        "entity_kind": "creature",
        "backing_file": "monx / MONG",
        "evidence": "asm/22D5 loads CX=0x100 over the six arrays; field order and stride "
                    "matched live against four sectors in docs/monster-spawn.md § 6",
        "grade": "measured",
    },
    "items_held": {
        "group": "items",
        "entity_kind": "item",
        "backing_file": "player",
        "evidence": "state.h names items_held[BOOTS] at DS:D9, [CLOAK] at DS:DA and "
                    "[GREEN IDOL] at DS:E4; all three agree with stride 1 and the "
                    "extractor's U2_T_ITEMS order, and that agreement is gated by "
                    "_prove_item_stride",
        "grade": "measured",
    },
    "spell_counts": {
        "group": "spells",
        "entity_kind": "spell",
        "backing_file": "player",
        "evidence": "state.h: the SPELLS: list counts, `mov al,[di+0xB6]`, bounds 1..9. "
                    "Slot 0 is the NONE placeholder in U2_T_SPELLS, not a spell",
        "grade": "measured",
    },
    "weapons_owned": {
        "group": "weapons",
        "entity_kind": "weapon",
        "backing_file": "player",
        "evidence": "state.h names the array and cmd_W's read of it; the menu index is one "
                    "lower than the asm index there, so the C index is not the digit",
        "grade": "inferred",
    },
    "armour_owned": {
        "group": "armour",
        "entity_kind": "armour",
        "backing_file": "player",
        "evidence": "state.h: buying CLOTH took DS:97 from 00 to 01, measured in "
                    "captures/spice86/armour-buy/",
        "grade": "measured",
    },
}

# Candidates the detection finds and the owner has not ruled on. Pinned exactly by the
# selftest, so a new multi-byte array in state.h turns the suite red and cannot arrive as a
# silent gap in this table.
KNOWN_UNRULED = {"map_base", "name", "rng_state", "speed", "speed_mult", "stage_a",
                 "tick_low"}

ITEM_BASE, SPELL_BASE = 0xD6, 0xB6


def read_generated(path: pathlib.Path) -> list[dict]:
    """Read a committed generated table, skipping any '#' preamble."""
    if not path.is_file():
        raise SystemExit(f"record_spans: {path.relative_to(REPO)} is missing -- it is "
                         "generated and committed, so its absence is a tree fault")
    body = [ln for ln in path.read_text(encoding="utf-8").splitlines()
            if not ln.lstrip().startswith("#")]
    return list(csv.DictReader(body, delimiter="\t"))


def content_array(name: str) -> list[str]:
    text = CONTENT.read_text(encoding="utf-8", errors="replace")
    m = re.search(rf"{name}\[[^]]*\]\s*=\s*\{{(.*?)\}};", text, re.S)
    if not m:
        raise SystemExit(f"record_spans: {name} not found in gen_content.h")
    return re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))


def derive_groups() -> tuple[list[dict], list[str]]:
    """Find runs of equal-sized, contiguous, named fields.

    A run of one field is a flat array of one-byte records (items_held). A run of several is
    a struct-of-arrays: N parallel arrays, each N bytes, so record i sits at base + i*stride
    in every member (the mon_* block). Reserved fields are excluded because they are the
    absence of a fact, not a record.
    """
    fields = []
    for r in read_generated(STATE):
        if r["kind"] != "named":
            continue
        size = int(r["size"])
        if size < 2:
            continue
        fields.append({"name": r["name"], "off": int(r["ds_offset"], 16), "size": size})
    fields.sort(key=lambda f: f["off"])

    groups, i = [], 0
    while i < len(fields):
        run = [fields[i]]
        while (i + len(run) < len(fields)
               and fields[i + len(run)]["off"] == run[-1]["off"] + run[-1]["size"]
               and fields[i + len(run)]["size"] == run[0]["size"]):
            run.append(fields[i + len(run)])
        off0 = run[0]["off"]
        size = run[0]["size"]
        if len(run) == 1:
            shape, n_rec, stride = "flat", size, 1
        else:
            shape, n_rec, stride = "struct-of-arrays", size, size
        groups.append({"first": run[0]["name"], "members": [f["name"] for f in run],
                       "ds_start": off0, "ds_end": off0 + size * len(run) - 1,
                       "shape": shape, "n_records": n_rec, "record_stride": stride})
        i += len(run)

    ruled = {g["first"] for g in groups if g["first"] in BACKING}
    unknown = [g["first"] for g in groups if g["first"] not in BACKING]
    stale = [k for k in BACKING if k not in {g["first"] for g in groups}]
    if stale:
        raise SystemExit("record_spans: BACKING declares groups that no longer derive: "
                         + ", ".join(stale) + " -- state.h moved, so the declaration is now "
                         "a claim about nothing. Update or delete it.")
    return groups, unknown


def _prove_item_stride(groups: list[dict]) -> list[str]:
    """Gate the inventory stride instead of assuming it.

    state.h cross-references three items by name and address. Those references agree with
    stride 1 and the extractor's item order only if both are correct, so checking them
    catches a renumbered array or a reordered extractor -- either of which would otherwise
    show up as an entity column quietly pointing at the wrong item.
    """
    problems: list[str] = []
    items = [g for g in groups if g["first"] == "items_held"]
    if not items:
        return ["items_held group missing, so its stride is unproven"]
    g = items[0]
    names = [n.upper() for n in content_array("U2_T_ITEMS")]
    for want_name, want_addr in (("BOOTS", 0xD9), ("CLOAK", 0xDA), ("GREEN IDOL", 0xE4)):
        if want_name not in names:
            problems.append(f"{want_name} is not in U2_T_ITEMS, so the stride proof is void")
            continue
        idx = names.index(want_name)
        got = g["ds_start"] + idx * g["record_stride"]
        if got != want_addr:
            problems.append(f"items_held[{want_name}] derives DS:{got:04X} but state.h "
                            f"names DS:{want_addr:04X} -- stride or item order moved")
    return problems


def build() -> tuple[list[dict], list[str], list[str]]:
    groups, unknown = derive_groups()
    problems = _prove_item_stride(groups)
    rows: list[dict] = []
    for g in groups:
        b = BACKING.get(g["first"])
        if not b:
            continue
        rows.append({"group": b["group"], "ds_start": f"0x{g['ds_start']:04X}",
                     "ds_end": f"0x{g['ds_end']:04X}", "shape": g["shape"],
                     "n_records": g["n_records"], "record_stride": g["record_stride"],
                     "member_fields": ",".join(g["members"]),
                     "entity_kind": b["entity_kind"], "backing_file": b["backing_file"],
                     "evidence": b["evidence"], "grade": b["grade"]})
    rows.sort(key=lambda r: int(r["ds_start"], 16))
    return rows, problems, unknown


def render(rows: list[dict]) -> str:
    head = ("# generated by tools/record_spans.py -- do not hand-edit\n"
            "# which DS span holds which file's records, at what stride.\n"
            f"# {{\"rows\": {len(rows)}}}\n")
    return head + "\t".join(COLS) + "\n" + "".join(
        "\t".join(str(r[c]) for c in COLS) + "\n" for r in rows)


def resolve(rows: list[dict], addr: int) -> str:
    """Answer 'what lives at this DS address' -- the lookup whose absence caused the bug."""
    for r in rows:
        lo, hi = int(r["ds_start"], 16), int(r["ds_end"], 16)
        if not (lo <= addr <= hi):
            continue
        if r["shape"] == "flat":
            rec = (addr - lo) // int(r["record_stride"])
            names = []
            if r["entity_kind"] == "item":
                names = content_array("U2_T_ITEMS")
            elif r["entity_kind"] == "spell":
                names = content_array("U2_T_SPELLS")
            label = names[rec] if rec < len(names) else ""
            return (f"DS:{addr:04X} is record {rec} of {r['n_records']} in {r['group']} "
                    f"({r['member_fields']}), stride {r['record_stride']}"
                    + (f", named {label!r}" if label else "")
                    + f"; entity {r['entity_kind']}, grade {r['grade']}")
        # struct-of-arrays: N parallel arrays of N bytes, so the array index is the
        # quotient and the record number is the remainder. Getting these the other way
        # round reads mon_type as record 3 of mon_x, which is a confident wrong answer.
        fields = r["member_fields"].split(",")
        idx = (addr - lo) // int(r["record_stride"])
        rec = (addr - lo) % int(r["record_stride"])
        member = fields[idx] if idx < len(fields) else f"array {idx}"
        return (f"DS:{addr:04X} is {member}[{rec}] -- record {rec} of {r['n_records']} in "
                f"{r['group']} ({r['shape']}, {len(fields)} parallel arrays, stride "
                f"{r['record_stride']}); entity {r['entity_kind']}, grade {r['grade']}")
    return f"DS:{addr:04X} is not inside any ruled record span"


def selftest() -> int:
    fails: list[str] = []

    def check(label: str, ok: bool) -> None:
        print(("ok      " if ok else "FAIL    ") + label)
        if not ok:
            fails.append(label)

    groups, unknown = derive_groups()
    by_first = {g["first"]: g for g in groups}

    check("inventory array is flat with 16 one-byte records",
          by_first["items_held"]["shape"] == "flat"
          and by_first["items_held"]["n_records"] == 16
          and by_first["items_held"]["record_stride"] == 1)
    check("the monster block is a struct-of-arrays of 32 records",
          by_first["mon_x"]["shape"] == "struct-of-arrays"
          and by_first["mon_x"]["n_records"] == 32
          and len(by_first["mon_x"]["members"]) == 8)
    check("the stride proof passes on the real tree", not _prove_item_stride(groups))

    # The control that makes the proof worth having: a stride of 2 must be caught, not
    # silently published. Without this the proof is a comment with a print statement.
    wrong = [dict(g) for g in groups]
    for g in wrong:
        if g["first"] == "items_held":
            g["record_stride"] = 2
    check("CONTROL a wrong inventory stride goes red",
          bool(_prove_item_stride(wrong)))

    check("a declared group that no longer derives is fatal",
          _stale_raises({"nonexistent_field_xyz": {}}))

    rows, problems, _ = build()
    check("build reports no evidence problems", not problems)
    check("every ruled group became a row",
          len(rows) == len(BACKING))
    check("every row names its evidence and its grade",
          all(r["evidence"] and r["grade"] in ("measured", "inferred") for r in rows))

    # The lookup is the deliverable, so it is controlled directly.
    check("--at 0xD9 resolves to the boots slot",
          "record 3" in resolve(rows, 0xD9) and "BOOTS" in resolve(rows, 0xD9))
    check("--at 0x197 resolves to mon_type record 0",
          "mon_type[0]" in resolve(rows, 0x197) and "creature" in resolve(rows, 0x197))
    check("--at one past a struct-of-arrays base is the next record, not the next array",
          "mon_type[1]" in resolve(rows, 0x198))
    check("--at the first array's second slot stays in that array",
          "mon_x[1]" in resolve(rows, 0x138))
    check("--at outside every span says so rather than guessing",
          "not inside" in resolve(rows, 0x004))
    check("spell slot 0 is the placeholder, not a spell",
          "NONE" in resolve(rows, SPELL_BASE))

    check("the unruled set is exactly what is on disk, so a new array goes red",
          set(unknown) == KNOWN_UNRULED)

    print()
    if fails:
        print(f"selftest: FAIL ({len(fails)})")
        return 1
    print("selftest: PASS")
    return 0


def _stale_raises(override: dict) -> bool:
    """True if BACKING naming a group that does not derive raises SystemExit."""
    saved = dict(BACKING)
    try:
        BACKING.clear()
        BACKING.update(saved)
        BACKING.update(override)
        try:
            derive_groups()
        except SystemExit:
            return True
        return False
    finally:
        BACKING.clear()
        BACKING.update(saved)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check-fresh", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--at", help="resolve a DS address, e.g. 0xD9")
    ap.add_argument("--what", help="list spans for an entity kind")
    ap.add_argument("--strict-unknown", action="store_true",
                    help="fail when a derived group has no ruling")
    a = ap.parse_args(argv)

    if a.selftest:
        return selftest()

    rows, problems, unknown = build()
    for p in problems:
        print(f"record_spans: EVIDENCE PROBLEM: {p}", file=sys.stderr)
    if problems:
        return 1
    # The unruled notice is for whoever is maintaining this table, not for someone asking
    # what lives at an address; printing it on every lookup trains people to ignore it.
    if unknown and not (a.at or a.what):
        print(f"record_spans: NOTE {len(unknown)} derived group(s) have no ruling and are "
              f"not published: {', '.join(sorted(unknown))}")
    if a.strict_unknown and unknown:
        print("record_spans: --strict-unknown and unruled groups present", file=sys.stderr)
        return 1

    if a.at:
        print(resolve(rows, int(a.at, 0)))
        return 0
    if a.what:
        hits = [r for r in rows if r["entity_kind"] == a.what]
        for r in hits:
            print(f"{r['group']:<10} {r['ds_start']}..{r['ds_end']} "
                  f"{r['n_records']} records, stride {r['record_stride']} "
                  f"({r['shape']}) <- {r['backing_file']} [{r['grade']}]")
        if not hits:
            print(f"record_spans: no span ruled for entity kind {a.what!r}")
        return 0

    text = render(rows)
    if a.write:
        OUT.write_text(text, encoding="utf-8")
        print(f"wrote {OUT.relative_to(REPO)} ({len(rows)} rows)")
        return 0
    if a.check_fresh:
        if not OUT.is_file():
            print(f"record_spans: {OUT.relative_to(REPO)} does not exist", file=sys.stderr)
            return 1
        if OUT.read_text(encoding="utf-8") == text:
            print("record_spans: fresh")
            return 0
        print(f"record_spans: {OUT.relative_to(REPO)} is STALE -- regenerate with --write",
              file=sys.stderr)
        return 1
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
