#!/usr/bin/env python3
"""Bidirectional provenance cross-reference between docs and asm/.

Why this exists: a claim read out of the static listing looked completely
reasonable until a live capture disproved it, and nothing on the function
recorded that it had only ever been read, never observed. Prose confidence did
not help, because prose is not checked. This makes the link machine-verifiable
in both directions.

Two directions:
  docs -> asm   every address cited in the findings docs must resolve to a real
                function range, so a citation cannot dangle.
  asm  -> docs  every named function reports whether its behaviour is static
                inference or live observation, via a `# provenance:` header line.

Values for `# provenance:` (comma separated, at least one required):
  static            read from the listing only
  live:<harvest>    observed in a named Spice86 harvest
  owner             reported by the owner, not yet seen in bytes
  retracted         disproven; the file must say by what

Usage: tools/xref_provenance.py [--strict]
Exit 1 under --strict when a doc citation dangles or a named function has no
provenance line. Warnings alone do not fail.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASM = ROOT / "asm"
DOCS = ROOT / "docs"

FUNC_RE = re.compile(r"^#\s*func\s+([0-9A-Fa-f]{4})\.\.([0-9A-Fa-f]{4})")
CONF_RE = re.compile(r"^#\s*confidence:\s*(\w+)")
PROV_RE = re.compile(r"^#\s*provenance:\s*(.+?)\s*$")
NAME_RE = re.compile(r"^#\s*(\S.*?)\s+—")

# Only the `asm/XXXX` form is an explicit function citation and therefore
# checked for resolution. A bare `0xXXXX` may be a data address, a segment, a
# register value or a constant, and treating those as dangling produced 47
# false positives on the first run (0x9000, 0x00FF, 0x0002 and so on).
ASM_CITE_RE = re.compile(r"asm/([0-9A-Fa-f]{4})\b")
ADDR_MENTION_RE = re.compile(r"\b0x([0-9A-Fa-f]{4})\b")


def load_functions() -> list[dict]:
    out = []
    for path in sorted(ASM.glob("*.asm")):
        start = end = None
        conf = prov = name = None
        for line in path.read_text(errors="replace").splitlines():
            if m := FUNC_RE.match(line):
                start, end = int(m.group(1), 16), int(m.group(2), 16)
            elif m := CONF_RE.match(line):
                conf = m.group(1)
            elif m := PROV_RE.match(line):
                prov = m.group(1)
            elif m := NAME_RE.match(line):
                name = m.group(1)
            if line and not line.startswith("#"):
                break
        if start is None:
            continue
        out.append({
            "file": path.name, "start": start, "end": end,
            "conf": conf, "prov": prov, "name": name,
            "named": not path.name.split("_")[0].endswith("_sub_")
                     and "_sub_" not in path.name and "L_" not in path.name,
        })
    return out


def resolve(funcs: list[dict], addr: int):
    # Function ranges are INCLUSIVE of the end byte. This is the project-wide
    # convention, proven by check_func.py (length = end - start + 1) and by every
    # `# func START..END len NNNN` header and function-map.tsv row, where
    # end - start + 1 == len for all 1298 functions. An earlier exclusive test
    # (`addr < end`) made the last byte of every function dangle, which is what
    # reported asm/0964 (last byte of sub_0962, 0962..0964) and asm/5002 (last
    # byte of sub_5000, 5000..5002) as resolving to no range. Ranges do not
    # overlap under the inclusive reading, so at most one function matches.
    for f in funcs:
        if f["start"] <= addr <= f["end"]:
            return f
    return None


CITE_C_RE = re.compile(r"asm/([0-9A-Fa-f]{4})\b|\bL_([0-9a-fA-F]{6})\b")


def check_c(funcs: list[dict]) -> tuple[int, list[tuple[str, str]]]:
    """The `--c` mode: does the C tree's asm citations resolve?

    This lives here and not in engine-map on purpose (docs/plan-code-index.md § 3.2): two gates that
    both resolve `asm/XXXX` drift, and the rename hazard AGENTS.md names is exactly the failure they
    would catch inconsistently. engine-map reports a dangling citation as an input error.
    """
    by_addr = {f["start"]: f for f in funcs}  # noqa: F841  (kept: resolve() walks funcs)
    labels: set[str] = set()
    for path in ASM.glob("*.asm"):
        for line in path.read_text(errors="replace").splitlines():
            for lm in re.finditer(r"^(L_[0-9a-fA-F]{6}):", line):
                labels.add(lm.group(1).lower())
    dangling: list[tuple[str, str]] = []
    mentions = 0
    for path in sorted((ROOT / "engine" / "src").rglob("*")):
        if path.suffix not in (".c", ".h"):
            continue
        for i, line in enumerate(path.read_text(errors="replace").splitlines(), start=1):
            for m in CITE_C_RE.finditer(line):
                mentions += 1
                if m.group(1):
                    if resolve(funcs, int(m.group(1), 16)) is None:
                        dangling.append((f"{path.name}:{i}", m.group(0)))
                elif m.group(2):
                    # `L_004c1c` names the instruction at offset 0x4c1c, not necessarily a label
                    # that asm/ actually prints -- most of these are call targets inside another
                    # routine's range. Resolve it as an address first; a defined label also counts.
                    addr = int(m.group(2), 16)
                    if resolve(funcs, addr) is None and m.group(2).lower() not in labels:
                        dangling.append((f"{path.name}:{i}", m.group(0)))
    return mentions, dangling


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--c", dest="c_mode", action="store_true",
                    help="check the C tree's asm/ and L_ citations instead of docs/")
    args = ap.parse_args()

    funcs = load_functions()
    if args.c_mode:
        mentions, dangling = check_c(funcs)
        print(f"xref --c: {mentions} citation(s) from engine/src resolve against asm/")
        for where, cite in sorted(set(dangling)):
            print(f"  FAIL  {where}: {cite} resolves to no function range or label")
        print(f"xref --c: {'FAIL' if dangling else 'ok'}")
        return 1 if dangling else 0

    funcs = load_functions()
    by_addr = {f["start"]: f for f in funcs}

    dangling: list[tuple[str, str]] = []
    hits: dict[str, set[str]] = {}
    mentions = 0
    for doc in sorted(DOCS.rglob("*.md")):
        text = doc.read_text(errors="replace")
        mentions += len(ADDR_MENTION_RE.findall(text))
        for m in ASM_CITE_RE.finditer(text):
            addr = int(m.group(1), 16)
            f = resolve(funcs, addr)
            if f is None:
                dangling.append((doc.name, m.group(0)))
            else:
                hits.setdefault(f["file"], set()).add(doc.name)

    named = [f for f in funcs if f["named"]]
    no_prov = [f for f in named if not f["prov"]]
    static_only = [f for f in named if f["prov"] and "live" not in f["prov"]
                   and "retracted" not in f["prov"]]
    cited = [f for f in named if f["file"] in hits]

    print(f"functions parsed            {len(funcs)}  (named: {len(named)})")
    print(f"bare address mentions in docs {mentions} (informational)")
    print(f"named functions cited in docs {len(cited)}")
    print(f"named functions with provenance "
          f"{len(named) - len(no_prov)}/{len(named)}")
    print(f"named functions static-only (never observed live) {len(static_only)}")

    uniq_dangling = sorted(set(dangling))
    print(f"\ndangling doc citations: {len(uniq_dangling)}")
    for doc, cite in uniq_dangling[:15]:
        print(f"  {doc}: {cite} resolves to no function range")
    if len(uniq_dangling) > 15:
        print(f"  ... and {len(uniq_dangling) - 15} more")

    if no_prov:
        print(f"\nnamed functions missing # provenance: {len(no_prov)}")
        for f in no_prov[:15]:
            print(f"  {f['file']}")
        if len(no_prov) > 15:
            print(f"  ... and {len(no_prov) - 15} more")

    if static_only:
        print(f"\nUNVERIFIED BY OBSERVATION - treat as provisional: "
              f"{len(static_only)}")
        for f in static_only[:10]:
            print(f"  {f['file']}  ({f['name']})")

    fail = args.strict and (dangling or no_prov)
    print("\n" + ("XREF: FAIL" if fail else "XREF: ok"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
