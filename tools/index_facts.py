#!/usr/bin/env python3
"""index_facts.py -- the `# fact:` line: schema, extractor, and the gate that keeps it honest.

WHY. The game bible is generated from a fact store, and the store's authoring surface is one line
in an `asm/` header (docs/plan-game-bible.md § 3). Behavioural rules live in headers because that
is where the reading that produced them lives; the extractor is the only thing that moves them
into `disasm/facts/`. An LLM was refused as the extractor on purpose: it would put a model between
the bytes and the fact store, and that is where folklore enters.

THE LINE

    # fact: <id> | <kind> | <grade> | <entities> | <statement> [| tools]

Five fields, or six when the evidence is a tool rather than the routine the line sits in.
Pipe-delimited, one line, no continuation, and **a malformed line is a hard failure** -- a skipped
line is a fact that silently stopped existing.

  id         unique across the whole tree. `^[a-z0-9][a-z0-9._-]{2,}$`.
  kind       closed enum: rule | arithmetic | condition | relation | identity.
  grade      the evidence grade from AGENTS.md § 5: observed | measured | inferred | recalled.
             This is the *evidence* grade, not the coverage grade; the glossary keeps them apart.
  entities   one or more `ns:value` tokens, comma separated. See NAMESPACES. A fact with no entity
             is a fact nobody can query, so at least one is required.
  statement  one sentence, capped, ending in a full stop. The cap is enforced because a statement
             that grows into a paragraph is a page pretending to be a row.
  tools      optional sixth field: comma-separated repo paths of the tools that derived the fact.
             Evidence is otherwise implicit -- the header the line sits in -- and a fact never
             restates its own filename.

NAMESPACES. `disasm/census/` is the entity authority and facts reference entities instead of
copying census columns, but census tables have no id column, so the reference is namespaced and
resolved against the column that actually names the thing:

    state:<field>   an engine/src/core/state.h field name (via tools/state_layout.py)
    asm:<prefix>    an asm/ file, by its four-hex address prefix
    place:<name> creature:<name> sector:<layer> npc:<name> vehicle:<what>
    gate:<era_index> tile:<art> world:<name> site:<layer> vendor:<shop>

An unresolvable entity is a hard failure. That is the whole reason the tokens exist: a fact about a
place that got renamed is a fact that should stop resolving, loudly.

SHIPS EMPTY. No `# fact:` line exists in the tree yet, and that is the ruling: the thing that can
fail comes before the thing that fills it. The controls below are what prove it can fail.

USAGE
  tools/index_facts.py                 # the extracted table on stdout
  tools/index_facts.py --verify        # schema + entity resolution; exit 1 on any failure
  tools/index_facts.py --write         # write disasm/facts/asm.tsv (this tool is its only writer)
  tools/index_facts.py --check-fresh   # regenerate and diff against the committed table
  tools/index_facts.py --selftest      # the controls; all must behave as named
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from state_layout import fields as state_fields  # noqa: E402

ASM_DIR = REPO / "asm"
CENSUS = REPO / "disasm" / "census"
OUT = REPO / "disasm" / "facts" / "asm.tsv"

FACT_RE = re.compile(r"^#\s*fact:\s*(.*)$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{2,}$")
KINDS = ("rule", "arithmetic", "condition", "relation", "identity")
GRADES = ("observed", "measured", "inferred", "recalled")
STATEMENT_MAX = 240
COLS = ("id", "kind", "grade", "entities", "statement", "evidence_tool", "source_file",
        "source_line")

# The column that names each census entity. Census has no id column, so this map IS the id rule,
# and it lives here rather than in census.py because census.py emits tables, not references.
CENSUS_ID_COL = {
    "place": "guide_name", "creature": "name", "sector": "layer", "npc": "name",
    "vehicle": "what", "gate": "era_index", "tile": "art", "world": "name",
    "site": "layer", "vendor": "shop",
}


def _census_values() -> dict[str, set[str]]:
    out: dict[str, set[str]] = {}
    for ns, col in CENSUS_ID_COL.items():
        path = CENSUS / f"{ns}.tsv"
        if not path.exists():
            out[ns] = set()
            continue
        lines = path.read_text(encoding="utf-8").splitlines()
        header = next((i for i, l in enumerate(lines)
                       if not l.startswith("#") and "\t" in l), None)
        if header is None or col not in lines[header].split("\t"):
            out[ns] = set()
            continue
        idx = lines[header].split("\t").index(col)
        vals = set()
        for line in lines[header + 1:]:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.split("\t")
            if len(parts) > idx and parts[idx]:
                vals.add(parts[idx])
        out[ns] = vals
    return out


def entity_namespaces() -> dict[str, set[str]]:
    ns = _census_values()
    ns["state"] = {f["name"] for f in state_fields()}
    ns["asm"] = {p.name[:4].lower() for p in ASM_DIR.glob("*.asm")}
    return ns


def parse_line(raw: str, where: str, ns: dict[str, set[str]]) -> tuple[dict | None, list[str]]:
    """Return (row, errors). errors is non-empty for any malformed line -- never a silent skip."""
    body = FACT_RE.match(raw)
    if not body:
        return None, [f"{where}: not a fact line"]
    text = body.group(1)
    if "\t" in text:
        return None, [f"{where}: a fact line may not contain a tab"]
    parts = [p.strip() for p in text.split("|")]
    if len(parts) not in (5, 6):
        return None, [f"{where}: expected 5 or 6 pipe-delimited fields, found {len(parts)}"]
    fid, kind, grade, entities, statement = parts[:5]
    tools = parts[5] if len(parts) == 6 else ""
    errs: list[str] = []
    if not ID_RE.match(fid):
        errs.append(f"{where}: id {fid!r} is not {ID_RE.pattern}")
    if kind not in KINDS:
        errs.append(f"{where}: kind {kind!r} not in {', '.join(KINDS)}")
    if grade not in GRADES:
        errs.append(f"{where}: grade {grade!r} not in {', '.join(GRADES)}")
    toks = [t.strip() for t in entities.split(",") if t.strip()]
    if not toks:
        errs.append(f"{where}: a fact needs at least one entity or it cannot be queried")
    for tok in toks:
        if ":" not in tok:
            errs.append(f"{where}: entity {tok!r} is not ns:value")
            continue
        name, _, value = tok.partition(":")
        if name not in ns:
            errs.append(f"{where}: entity namespace {name!r} is unknown")
        elif value not in ns[name]:
            errs.append(f"{where}: entity {tok!r} does not resolve in {name}")
    if not statement.endswith("."):
        errs.append(f"{where}: statement must end in a full stop")
    if len(statement) > STATEMENT_MAX:
        errs.append(f"{where}: statement is {len(statement)} chars, cap is {STATEMENT_MAX}")
    if tools:
        for t in (x.strip() for x in tools.split(",")):
            if not (REPO / t).exists():
                errs.append(f"{where}: cited tool {t!r} is not in the repo")
    if errs:
        return None, errs
    return dict(id=fid, kind=kind, grade=grade, entities=",".join(toks), statement=statement,
                evidence_tool=tools), []


def extract(ns: dict[str, set[str]] | None = None,
            asm_dir: pathlib.Path | None = None) -> tuple[list[dict], list[str]]:
    asm_dir = asm_dir or ASM_DIR
    if not asm_dir.is_dir():
        raise SystemExit(f"index_facts: no {asm_dir} -- refusing to report success on a tree "
                         "it cannot see")
    ns = ns or entity_namespaces()
    rows: list[dict] = []
    errors: list[str] = []
    seen: dict[str, str] = {}
    for path in sorted(asm_dir.glob("*.asm")):
        for i, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(),
                                 start=1):
            if not FACT_RE.match(line):
                continue
            where = f"{path.name}:{i}"
            row, errs = parse_line(line, where, ns)
            if errs:
                errors.extend(errs)
                continue
            if row["id"] in seen:
                errors.append(f"{where}: id {row['id']} already claimed at {seen[row['id']]}")
                continue
            seen[row["id"]] = where
            row["source_file"] = path.name
            row["source_line"] = str(i)
            rows.append(row)
    return rows, errors


def table(rows: list[dict]) -> str:
    return "\t".join(COLS) + "\n" + "\n".join(
        "\t".join(r.get(c, "") for c in COLS) for r in rows) + ("\n" if rows else "")


def verify() -> int:
    rows, errors = extract()
    print(f"index_facts: {len(rows)} fact(s) extracted from asm/ headers")
    for e in errors:
        print(f"  FAIL  {e}")
    print("index_facts: " + ("FAIL" if errors else "ok"))
    return 1 if errors else 0


GOOD = "# fact: stat.charisma-is-4e | identity | observed | state:stat_cha | " \
       "Charisma lives at DS:4E and nowhere else."


def _selftest() -> int:
    """Controls, both polarities. Each names the mistake it exists to catch."""
    ok = True
    ns = entity_namespaces()

    def expect(label: str, line: str, want_ok: bool):
        nonlocal ok
        row, errs = parse_line(line, "ctl", ns)
        if bool(errs) == want_ok:
            ok = False
            print(f"FAIL  control {label}: expected {'clean' if want_ok else 'red'}, "
                  f"got {errs or 'clean'}")
        else:
            print(f"ok    control {label}")

    expect("a well-formed fact", GOOD, True)
    expect("wrong field count", GOOD + " | extra | more", False)
    expect("unknown kind", GOOD.replace("| identity |", "| vibes |"), False)
    expect("unknown grade", GOOD.replace("| observed |", "| probably |"), False)
    expect("no entity", GOOD.replace("state:stat_cha", ""), False)
    expect("entity that does not resolve", GOOD.replace("state:stat_cha", "state:not_a_field"),
           False)
    expect("unknown namespace", GOOD.replace("state:stat_cha", "planet:mars"), False)
    expect("statement with no full stop", GOOD.rstrip("."), False)
    expect("statement over the cap", GOOD.replace("Charisma lives at DS:4E and nowhere else.",
                                                 "x" * (STATEMENT_MAX + 1) + "."), False)
    expect("a tab smuggled into the line", GOOD.replace(" | ", "\t|"), False)
    expect("a cited tool that is not in the repo", GOOD + " | tools/nope.py", False)
    expect("a cited tool that is", GOOD + " | tools/bcd_census.py", True)

    # End-to-end over a scratch directory: the whole extract path, not just the line parser.
    tmp = pathlib.Path("/var/tmp/u2facts-selftest")
    tmp.mkdir(parents=True, exist_ok=True)
    (tmp / "0001_sub_0001.asm").write_text(
        f"# sub_0001\n{GOOD}\n# fact: dup.id.here | rule | inferred | state:stat_cha | "
        "Second fact.\nsub_0001:\n        ret\n", encoding="utf-8")
    (tmp / "0002_sub_0002.asm").write_text(
        f"# sub_0002\n# fact: dup.id.here | rule | inferred | state:stat_cha | "
        "Same id again.\nsub_0002:\n        ret\n", encoding="utf-8")
    rows, errs = extract(ns, tmp)
    if len(rows) != 2 or not any("already claimed" in e for e in errs):
        ok = False
        print(f"FAIL  control end-to-end extract with a duplicate id: got {len(rows)} rows, "
              f"{errs}")
    else:
        print("ok    control end-to-end extract with a duplicate id")
    for p in tmp.glob("*.asm"):
        p.unlink()
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check-fresh", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return _selftest()
    if args.verify:
        return verify()
    rows, errors = extract()
    if errors:
        for e in errors:
            print(f"  FAIL  {e}", file=sys.stderr)
        return 1
    if args.write:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(table(rows), encoding="utf-8")
        print(f"wrote {OUT.relative_to(REPO)} ({len(rows)} rows)")
        return 0
    if args.check_fresh:
        if not OUT.exists():
            print(f"index_facts: FAIL  {OUT.relative_to(REPO)} does not exist")
            return 1
        if OUT.read_text(encoding="utf-8") != table(rows):
            print(f"index_facts: FAIL  {OUT.relative_to(REPO)} is STALE; run "
                  "tools/index_facts.py --write")
            return 1
        print(f"index_facts: ok  {OUT.relative_to(REPO)} matches the headers")
        return 0
    sys.stdout.write(table(rows) + ("\n" if rows and not table(rows).endswith("\n") else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
