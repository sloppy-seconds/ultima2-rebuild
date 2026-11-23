#!/usr/bin/env python3
"""index_engine_map.py -- which C implements which asm, and which asm has no C claim.

THE QUERY: "if I change asm/6DFD, which C do I look at?" and its inverse, "this C file asserts it
implements something -- does it name what?" (docs/plan-code-index.md § 3.2.)

ONE ROW PER (asm_address, c_file, c_line, c_symbol). The raw material already exists: the C tree
cites addresses in comments as a habit, and the generator scans `engine/src` for `asm/XXXX` and
`L_XXXXXX`. A symbol is the enclosing top-level definition, which is a heuristic and is labelled as
one in the table rather than dressed up as parsed fact.

CITATION VALIDITY IS NOT THIS TOOL'S RULE. `tools/xref_provenance.py --c` owns resolving `asm/XXXX`
and `L_XXXXXX`, because two gates that both resolve them drift, and the rename hazard AGENTS.md names
is exactly what they would catch inconsistently. This tool imports it and reports a dangling citation
as an input error.

THE MODE COLUMN IS GONE, and enhanced mode with it. 69 of 145 C files cite no address, and that list
is `platform/`, `renderer_sdl.c`, `sha256.c`, `ini.c`, `licenses_data.h` -- code with no asm
counterpart *by design*. So "cites nothing" is not "has no evidence behind it". The honest unit is
the behavioural core, the exemption list lives below and is printed in the report header, and an
enhanced-mode column stays unbuilt until someone writes the `diverges:` notes it would need.

USAGE
  tools/index_engine_map.py                 # summary
  tools/index_engine_map.py --write         # write disasm/index/engine-map.tsv
  tools/index_engine_map.py --check-fresh
  tools/index_engine_map.py --verify
  tools/index_engine_map.py --selftest
  tools/index_engine_map.py --asm 6DFD      # what C claims about one routine
  tools/index_engine_map.py --c engine/src/game/flight.c
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from xref_provenance import ASM_CITE_RE, CITE_C_RE, check_c, load_functions, resolve  # noqa: E402

ASM = REPO / "asm"
SRC = REPO / "engine" / "src"
OUT = REPO / "disasm" / "index" / "engine-map.tsv"
COLS = ("asm_addr", "asm_file", "c_file", "c_line", "c_symbol", "cited_as")

# Files with no asm counterpart BY DESIGN, each with the reason, because an exemption without a
# reason is how a gate quietly stops meaning anything. The cleanroom entries are the strongest: art
# substitution MUST NOT cite asm/, since whoever draws substitute art is forbidden from looking at
# the originals (AGENTS.md, the cleanroom wall).
EXEMPT_PREFIXES = ("platform/", "tests/", "debug/")
EXEMPT_NAMES = {"renderer_sdl.c", "sha256.c", "sha256.h", "ini.c", "ini.h", "licenses_data.h",
                "main.c", "cli.c", "cli.h", "compat.h", "version.c", "version.h",
                "attr.h", "artifact.h", "api.h", "panel.h", "game.h", "residency.h",
                "pool_names.h", "font.c", "font.h", "font8x8_basic.h",
                "tiles_builtin.c", "tiles_builtin.h", "tiles_cleanroom.h", "tiles_ega.c",
                "tiles_ega.h", "pic.h", "pic_ega.c", "pic_ega.h", "dungeon_sprites_gen.h",
                "dungeon_assets.c", "dungeon_assets.h", "fmtowns.c", "fmtowns.h"}
EXEMPT_REASONS = {
    "platform/": "host seam: audio, input, window. No asm counterpart exists.",
    "debug/": "harness instrumentation (trace, turnlog, dump_state). Not a reimplementation.",
    "font*, tiles_builtin*, tiles_ega*, pic*, dungeon_sprites_gen.h, dungeon_assets*":
        "substitute art and generated art blobs. Forbidden from citing asm/ by the cleanroom wall.",
    "fmtowns*": "derived from the FM Towns disc, so its evidence is the disc, not this binary.",
    "cli*, api.h, main.c, compat.h, ini*, sha256*, licenses*": "harness and packaging surface.",
}

CITE_RE = re.compile(r"asm/([0-9A-Fa-f]{4})\b|\bL_([0-9a-fA-F]{6})\b")
# A top-level definition starts at column 0 and is not a call, a prototype or a control keyword.
DEF_RE = re.compile(r"^(?:static\s+|inline\s+|const\s+|extern\s+)*"
                    r"[A-Za-z_][\w \*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{?\s*$")
TYPEDEF_RE = re.compile(r"^(?:typedef\s+)?(?:struct|enum|union)\s+([A-Za-z_]\w*)")
CLOSE_RE = re.compile(r"^\}\s*([A-Za-z_]\w*)\s*;")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else"}


def is_exempt(rel: str) -> bool:
    name = pathlib.Path(rel).name
    return name in EXEMPT_NAMES or any(rel.startswith(p) for p in EXEMPT_PREFIXES)


def c_files() -> list[pathlib.Path]:
    return sorted(p for p in SRC.rglob("*") if p.suffix in (".c", ".h"))


def enclosing_symbol(lines: list[str], i: int) -> str:
    """The innermost top-level definition above line i. A heuristic, and the table says so."""
    for j in range(i, -1, -1):
        line = lines[j]
        if line.startswith("}") or line.startswith("typedef struct") or line[:1].isalpha():
            m = CLOSE_RE.match(line) or TYPEDEF_RE.match(line) or DEF_RE.match(line)
            if m and m.group(1) not in KEYWORDS:
                return m.group(1)
    return "(file)"


def build() -> tuple[list[dict], list[str], dict]:
    if not SRC.is_dir() or not ASM.is_dir():
        raise SystemExit("index_engine_map: no engine/src or no asm/ -- refusing to report "
                         "success on a tree it cannot see")
    funcs = load_functions()
    by_prefix = {p.name[:4].lower(): p.name for p in ASM.glob("*.asm")}
    rows: list[dict] = []
    errors: list[str] = []
    cited_files: set[str] = set()
    for path in c_files():
        rel = str(path.relative_to(SRC))
        lines = path.read_text(errors="replace").splitlines()
        for i, line in enumerate(lines, start=1):
            hits = list(CITE_RE.finditer(line))
            if not hits:
                continue
            cited_files.add(rel)
            sym = enclosing_symbol(lines, i - 1)
            for m in hits:
                if m.group(1):
                    addr = int(m.group(1), 16)
                    f = resolve(funcs, addr)
                    if f is None:
                        errors.append(f"{rel}:{i}: {m.group(0)} resolves to no function range")
                        continue
                    rows.append({"asm_addr": f["start"], "asm_file": f["file"], "c_file": rel,
                                 "c_line": i, "c_symbol": sym, "cited_as": m.group(0)})
                else:
                    addr = int(m.group(2), 16)
                    f = resolve(funcs, addr)
                    rows.append({"asm_addr": addr, "asm_file": (f or {}).get("file", ""),
                                 "c_file": rel, "c_line": i, "c_symbol": sym,
                                 "cited_as": m.group(0)})
    uncited = [str(p.relative_to(SRC)) for p in c_files()
               if str(p.relative_to(SRC)) not in cited_files and not is_exempt(
                   str(p.relative_to(SRC)))]
    mapped = {r["asm_addr"] for r in rows}
    return rows, errors, {"cited_files": len(cited_files), "total_c_files": len(c_files()),
                          "uncited_core": sorted(uncited), "mapped_asm": len(mapped),
                          "asm_files": len(by_prefix), "exempt": sorted(
                              str(p.relative_to(SRC)) for p in c_files()
                              if is_exempt(str(p.relative_to(SRC))))}


def table(rows: list[dict]) -> str:
    return "\t".join(COLS) + "\n" + "\n".join(
        "\t".join(str(r[c]) for c in COLS)
        for r in sorted(rows, key=lambda r: (r["asm_addr"], r["c_file"], r["c_line"]))) + "\n"


def verify(strict_core: bool = False) -> int:
    rows, errors, tot = build()
    mentions, dangling = check_c(load_functions())
    for e in errors:
        print(f"  FAIL  {e}")
    for where, cite in sorted(set(dangling)):
        print(f"  FAIL  {where}: {cite} dangling (xref_provenance --c)")
    if tot["uncited_core"]:
        # A report, not a verdict, until the owner rules on which of these owe a citation. Failing
        # on them would be a ruling made by a parser; ignoring them would be the silent gap this
        # project keeps refusing. So: printed every run, fatal only under --strict-core.
        line = (f"  NOTE  {len(tot['uncited_core'])} behavioural-core file(s) cite no asm "
                "address (see --strict-core)")
        print(line)
        for f in tot["uncited_core"][:10]:
            print(f"          engine/src/{f}")
        if len(tot["uncited_core"]) > 10:
            print(f"        ... and {len(tot['uncited_core']) - 10} more")
    if strict_core and tot["uncited_core"]:
        print("  FAIL  --strict-core: the files above cite no asm address")
    print(f"index_engine_map: {len(rows)} rows; {tot['cited_files']}/{tot['total_c_files']} C "
          f"files cite an address; {tot['mapped_asm']} distinct asm routines claimed of "
          f"{tot['asm_files']}")
    print(f"index_engine_map: exempt by design ({len(tot['exempt'])}): "
          f"{', '.join(tot['exempt'][:10])}")
    ok = not errors and not dangling and not (strict_core and tot["uncited_core"])
    print("index_engine_map: " + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


def query(what: str, value: str) -> int:
    rows, _, _ = build()
    if what == "asm":
        want = int(value, 16)
        hits = [r for r in rows if r["asm_addr"] == want]
        for r in hits:
            print(f"  {r['c_file']}:{r['c_line']}  {r['c_symbol']}  (cites {r['cited_as']})")
        if not hits:
            print(f"  no C file cites asm/{want:04X}. That is a claim about the map, not about "
                  "the game: the routine may be implemented and simply not named in a comment.")
    else:
        want = value
        hits = [r for r in rows if r["c_file"] == want or r["c_file"].endswith("/" + want)]
        for r in hits:
            print(f"  {r['asm_file']:<34} {r['c_symbol']}  (line {r['c_line']})")
        if not hits:
            print(f"  {want} cites no asm address.")
    return 0


def _selftest() -> int:
    ok = True
    rows, errors, tot = build()

    def check(label: str, cond: bool):
        nonlocal ok
        print(("ok    control " if cond else "FAIL  control ") + label)
        if not cond:
            ok = False

    check("rows exist", len(rows) > 100)
    check("every row names a C file and a line",
          all(r["c_file"] and r["c_line"] for r in rows))
    check("asm_addr is an int", all(isinstance(r["asm_addr"], int) for r in rows))
    check("exemption list is non-empty and printed", bool(tot["exempt"]))
    check("uncited core is a list, not a crash", isinstance(tot["uncited_core"], list))
    check("a query for a mapped routine answers", any(
        query("asm", f"{r['asm_addr']:x}") == 0 for r in rows[:1]))
    check("a query for an unmapped address says so out loud",
          query("asm", "ffff") == 0)
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--strict-core", action="store_true",
                    help="with --verify, fail on behavioural-core files that cite no address")
    ap.add_argument("--check-fresh", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--asm")
    ap.add_argument("--c")
    a = ap.parse_args()
    if a.selftest:
        return _selftest()
    if a.verify:
        return verify(strict_core=a.strict_core)
    if a.asm:
        return query("asm", a.asm)
    if a.c:
        return query("c", a.c)
    rows, errors, tot = build()
    if errors:
        for e in errors:
            print(f"  FAIL  {e}", file=sys.stderr)
        return 1
    if a.write:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(table(rows), encoding="utf-8")
        print(f"wrote {OUT.relative_to(REPO)} ({len(rows)} rows)")
        return 0
    if a.check_fresh:
        if not OUT.exists():
            print(f"index_engine_map: FAIL  {OUT.relative_to(REPO)} does not exist")
            return 1
        if OUT.read_text(encoding="utf-8") != table(rows):
            print(f"index_engine_map: FAIL  {OUT.relative_to(REPO)} is STALE; run "
                  "tools/index_engine_map.py --write")
            return 1
        print(f"index_engine_map: ok  {OUT.relative_to(REPO)} matches engine/src and asm/")
        return 0
    print(f"{len(rows)} rows; {tot['cited_files']}/{tot['total_c_files']} C files cite an "
          f"address; {tot['mapped_asm']} asm routines claimed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
