#!/usr/bin/env python3
"""index_coverage.py -- how much of the binary is understood, and by what measure.

WHY. The headline this project carried for weeks -- "87% of the binary is not understood" -- was a
count of the `confidence:` stamp, and that stamp was written in bulk by `tools/clean_wave.py`. It
was a default, not a finding, so the number was reproducible and meaningless (docs/plan-code-index.md
§ 7, and the generic version in `docs/harness-portability.md` § 6). This tool derives a **coverage
grade** from markers that only change when somebody reads a body, and prints the predicates beside
the counts so a figure cannot be quoted away from the rule that produced it.

THE GRADES, highest applies:

  observed   `# provenance:` contains `live:` -- a capture saw it run.
  decoded    carries `# name-evidence:`, *or* a non-`sub_` filename, *or* a `# body corrected:`
             marker, because a correction is proof somebody read the body.
  segmented  the file exists and tiles the code stream. All of them.

`confidence:` is an input column and never moves a grade.

D3, CORRECTED WHILE BUILDING THIS. The ruling said a file with undecoded bytes in range cannot hold
a `decoded` grade, and read literally that means "has a `.byte` line", which caps 146 files -- for
the wrong reason. A forced-keep is not ignorance: `.byte 0xc6  # forced-kept: gas re-encodes
'movb $0x3,0x2f' differently` is a fully decoded instruction that gas cannot emit identically.
The honest test is whether the line says anything at all. **A `.byte` with no comment is
undecoded**, and exactly one file in the tree has one (`asm/6144_dungeon_shape_dispatch.asm`), so
the cap bites once today. It is still a real rule: it goes red the day someone deletes a comment.

THE UNNAMED-OFFSET QUEUE. `state.h` names 113 DS offsets and marks the rest `reserved_*`. Every
reserved byte is published with a count of how many `asm/` files mention its literal, which is how
the mirror grows: the index proposes, the mirror decides. The count is a *literal mention*, not
proof of access -- `0x4c7` can be an immediate -- and the real access column arrives with
`footprint` (#24). Ordering by true touch count is deferred with it.

DEFERRED, NAMED RATHER THAN STUBBED. `cited_by_engine` needs `engine-map` (#26) and the rollup by
engine module needs it too. Neither ships as an empty column, because a column that looks finished
is worse than a missing one (D5).

USAGE
  tools/index_coverage.py                 # the report, predicates beside counts
  tools/index_coverage.py --verify        # the gate; exit 1 on any disagreement
  tools/index_coverage.py --write         # write both tables (this tool is their only writer)
  tools/index_coverage.py --check-fresh   # regenerate and diff against the committed tables
  tools/index_coverage.py --selftest      # the controls; all must behave as named
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from state_layout import fields as _sl_fields, annotate as _sl_annotate, asserts as _sl_asserts  # noqa: E402

ASM_DIR = REPO / "asm"
COV_OUT = REPO / "disasm" / "index" / "coverage.tsv"
OFF_OUT = REPO / "disasm" / "index" / "unnamed-offsets.tsv"

NAME_EVIDENCE_RE = re.compile(r"^#\s*name-evidence:", re.M)
CORRECTED_RE = re.compile(r"^#\s*body corrected:", re.M)
LIVE_RE = re.compile(r"^#\s*provenance:.*live:", re.M)
CONF_RE = re.compile(r"^#\s*confidence:\s*(\w+)", re.M)
SUB_NAME_RE = re.compile(r"_sub_[0-9a-f]{4}\.asm$")
BYTE_RE = re.compile(r"^\s*\.byte\b")
HEX_RE = re.compile(r"0x([0-9a-fA-F]{1,4})\b")

COV_COLS = ("file", "grade", "name_evidence", "body_corrected", "live", "confidence",
            "undecoded_byte_lines")
OFF_COLS = ("ds_offset", "size", "state_field", "mentioned_in_files")


def file_record(path: pathlib.Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    body = [l for l in text.splitlines() if not l.startswith("#")]
    undecoded = [l for l in body if BYTE_RE.match(l) and "#" not in l]
    name_ev = bool(NAME_EVIDENCE_RE.search(text))
    corrected = bool(CORRECTED_RE.search(text))
    live = bool(LIVE_RE.search(text))
    nonsub = not SUB_NAME_RE.search(path.name)
    grade = "segmented"
    if name_ev or corrected or nonsub:
        grade = "decoded"
    if live:
        grade = "observed"
    # D3, corrected: undecoded bytes in range cap the grade at segmented.
    if undecoded and grade != "segmented":
        grade = "segmented"
    m = CONF_RE.search(text)
    return dict(file=path.name, grade=grade, name_evidence="yes" if name_ev else "",
                body_corrected="yes" if corrected else "", live="yes" if live else "",
                confidence=(m.group(1) if m else ""), undecoded_byte_lines=len(undecoded),
                body=body)


def records() -> list[dict]:
    return [file_record(p) for p in sorted(ASM_DIR.glob("*.asm"))]


def mention_counts(recs: list[dict]) -> dict[int, int]:
    """Files mentioning each offset literal. A mention is not an access; see the docstring."""
    counts: dict[int, int] = {}
    for r in recs:
        seen = {int(h, 16) for line in r["body"] for h in HEX_RE.findall(line)}
        for off in seen:
            counts[off] = counts.get(off, 0) + 1
    return counts


def offset_rows(recs: list[dict]) -> list[dict]:
    counts = mention_counts(recs)
    state = _sl_annotate(_sl_fields(), _sl_asserts(
        (REPO / "engine" / "src" / "core" / "state.h").read_text())[0])
    return [dict(ds_offset=f"0x{f['ds_offset']:03x}", size=f["size"], state_field=f["name"],
                 mentioned_in_files=counts.get(f["ds_offset"], 0))
            for f in state if f["kind"] == "reserved"]


def table(rows: list[dict], cols) -> str:
    return "\t".join(cols) + "\n" + "\n".join(
        "\t".join(str(r.get(c, "")) for c in cols) for r in rows) + "\n"


def cov_table(recs: list[dict]) -> str:
    return table([{k: r[k] for k in COV_COLS} for r in recs], COV_COLS)


def off_table(recs: list[dict]) -> str:
    rows = sorted(offset_rows(recs), key=lambda r: (-int(r["mentioned_in_files"]), r["ds_offset"]))
    return table(rows, OFF_COLS)


def report(recs: list[dict]) -> str:
    n = len(recs)
    seg = n
    dec = sum(1 for r in recs if r["grade"] in ("decoded", "observed"))
    obs = sum(1 for r in recs if r["grade"] == "observed")
    capped = [r["file"] for r in recs if r["undecoded_byte_lines"] and r["name_evidence"]]
    offs = sorted(offset_rows(recs), key=lambda o: (-int(o["mentioned_in_files"]), o["ds_offset"]))
    pct = lambda x: f"{100.0 * x / n:5.1f}%"  # noqa: E731
    out = [
        f"coverage grade over {n} asm files, derived from markers only:",
        f"  segmented  {seg:5}  {pct(seg)}   every file: it exists and tiles the stream",
        f"  decoded    {dec:5}  {pct(dec)}   name-evidence: OR non-sub_ filename OR body corrected:",
        f"  observed   {obs:5}  {pct(obs)}   provenance: contains live:",
        "",
        f"  `confidence:` is reported per file and moved nothing. It was bulk-assigned by",
        f"  tools/clean_wave.py, which is why it is an input column and not the headline.",
        f"  D3 cap (a .byte with no comment = undecoded bytes in range) bites on "
        f"{len([r for r in recs if r['undecoded_byte_lines']])} file(s): "
        f"{', '.join(r['file'] for r in recs if r['undecoded_byte_lines']) or 'none'}",
        "",
        f"unnamed-offset queue: {len(offs)} reserved bytes in state.h, "
        f"{sum(1 for o in offs if int(o['mentioned_in_files']))} mentioned by at least one asm file.",
        "  top of the queue (files mentioning the literal; access counts arrive with footprint):",
    ]
    for o in offs[:5]:
        out.append(f"    {o['ds_offset']}  {o['state_field']:16} mentioned in "
                   f"{o['mentioned_in_files']} files")
    out.append("")
    out.append("deferred, named: cited_by_engine and the per-module rollup need engine-map (#26).")
    return "\n".join(out)


def verify() -> int:
    recs = records()
    fails: list[str] = []
    names = [r["file"] for r in recs]
    if len(names) != len(set(names)):
        fails.append("a file appears twice in the table")
    for r in recs:
        if r["undecoded_byte_lines"] and r["grade"] != "segmented":
            fails.append(f"{r['file']}: holds {r['grade']} while carrying "
                         f"{r['undecoded_byte_lines']} undecoded .byte line(s)")
        if r["grade"] == "observed" and r["live"] != "yes":
            fails.append(f"{r['file']}: graded observed with no live provenance")
        if r["grade"] == "segmented" and not r["undecoded_byte_lines"] and (
                r["name_evidence"] or r["live"]):
            fails.append(f"{r['file']}: graded segmented while carrying decoded markers")
    offs = offset_rows(recs)
    if any(o["state_field"].startswith("reserved_") is False for o in offs):
        fails.append("the unnamed-offset queue contains a named state.h field")
    print(report(recs))
    for f in fails:
        print(f"  FAIL  {f}")
    print("index_coverage: " + ("FAIL" if fails else "ok"))
    return 1 if fails else 0


def _selftest() -> int:
    """Controls over the grade derivation. Each names the mistake it catches."""
    ok = True
    tmp = pathlib.Path("/var/tmp/u2cov-selftest")
    tmp.mkdir(parents=True, exist_ok=True)

    def expect(label: str, content: str, want: str, name: str = "0001_sub_0001.asm"):
        nonlocal ok
        p = tmp / name
        p.write_text(content, encoding="utf-8")
        rec = file_record(p)
        if rec["grade"] != want:
            ok = False
            print(f"FAIL  control {label}: got {rec['grade']}, wanted {want}")
        else:
            print(f"ok    control {label}")

    body = "sub_0001:\n        mov    $0x0,%ax\n        ret\n"
    expect("bare sub_ stub is segmented", f"# sub_0001\n{body}", "segmented")
    expect("a filename that is not sub_ counts as named", f"# thing_doer\n{body}", "decoded",
           name="0001_thing_doer.asm")
    expect("name-evidence makes it decoded", f"# x\n# name-evidence: read the body\n{body}",
           "decoded")
    expect("a body correction makes it decoded", f"# x\n# body corrected: 2026-01-01 -- evidence: x\n{body}",
           "decoded")
    expect("live provenance makes it observed",
           f"# x\n# name-evidence: y\n# provenance: live:g60, static\n{body}", "observed")
    expect("undecoded .byte caps the grade at segmented",
           f"# x\n# name-evidence: y\n# provenance: live:g60, static\n"
           "sub_0001:\n        .byte 0x7f\n        ret\n", "segmented")
    expect("a commented forced-keep does NOT cap the grade",
           f"# x\n# name-evidence: y\n"
           "sub_0001:\n        .byte 0xc6  # forced-kept: gas re-encodes 'movb $0x3,0x2f'\n"
           "        ret\n", "decoded")
    for n in ("0001_sub_0001.asm", "0001_thing_doer.asm"):
        (tmp / n).unlink(missing_ok=True)
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
    recs = records()
    if args.verify:
        return verify()
    if args.write:
        COV_OUT.parent.mkdir(parents=True, exist_ok=True)
        COV_OUT.write_text(cov_table(recs), encoding="utf-8")
        OFF_OUT.write_text(off_table(recs), encoding="utf-8")
        print(f"wrote {COV_OUT.relative_to(REPO)} ({len(recs)} rows) and "
              f"{OFF_OUT.relative_to(REPO)} ({len(offset_rows(recs))} rows)")
        return 0
    if args.check_fresh:
        bad = [p for p, want in ((COV_OUT, cov_table(recs)), (OFF_OUT, off_table(recs)))
               if not p.exists() or p.read_text(encoding="utf-8") != want]
        for p in bad:
            print(f"index_coverage: FAIL  {p.relative_to(REPO)} is STALE; run "
                  "tools/index_coverage.py --write")
        if bad:
            return 1
        print("index_coverage: ok  both tables match the tree")
        return 0
    print(report(recs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
