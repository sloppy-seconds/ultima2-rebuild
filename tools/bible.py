#!/usr/bin/env python3
"""bible.py -- the deterministic half of the bible loop: packets, pre-check, verdicts, gate.

WHY. The writer and the critic are models, and no model runs in CI (ADR 0003). Everything that is
NOT a model lives here: selecting the fact rows for a page, composing the exact task message each
model sees, the mechanical vacuity pre-check that runs *before* the judge, the artefact schema, the
hashes, and the gate that asserts the committed artefacts without running anything.

THE LOOP (run by an agent, never by CI):

    tools/bible.py --packet statistics > /var/tmp/p.txt      # style block + fact rows
        -> bible-writer subagent, given /var/tmp/p.txt verbatim
    tools/bible.py --precheck statistics draft.yaml           # hard failures + numbered sentences
        -> bible-critic subagent, given the same rows plus the numbered sentences
    tools/bible.py --record statistics draft.yaml verdicts.tsv

WHAT GETS COMMITTED, under disasm/bible/:

    <page>.prose.yaml      the writer's output, verbatim
    <page>.verdicts.tsv    one row per sentence: four questions, reason, judge model, hashes
    <page>.dropped         sentences that failed, with the failing question (page omits them)
    pair.json              the writer/critic model ids and both prompt hashes
    controls.tsv           the three controls that must all fail, with the pair hash

CI then asserts, without a model: the pair still matches the prompts, every sentence has a verdict,
every verdict passes or the sentence is in the drop list, the prose hash still matches, and the
controls file is present, current, and red. **Changing a prompt or a model id invalidates every
verdict in the tree**, which is the point of hashing them into the artefacts -- re-judging never
touches the writer, so that is cheap.

USAGE
  tools/bible.py --page statistics --packet
  tools/bible.py --page statistics --precheck DRAFT.yaml
  tools/bible.py --page statistics --judge-in DRAFT.yaml
  tools/bible.py --page statistics --record DRAFT.yaml VERDICTS.tsv
  tools/bible.py --verify
  tools/bible.py --selftest
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys

import yaml

REPO = pathlib.Path(__file__).resolve().parent.parent
FACTS = REPO / "disasm" / "facts" / "asm.tsv"
PROMPTS = REPO / "tools" / "prompts"
OUT = REPO / "disasm" / "bible"

WRITER_MODEL = "local/qwen3.8-flash-next"
CRITIC_MODEL = "local/deepseek-flash"

# A page is a curated selector over entity namespaces, not an inferred cluster: grouping facts by
# what they mention is exactly the kind of plausible-wrong automatic call this plan refuses.
# Add a page by naming the entity values its facts must touch.
PAGES = {
    "statistics": {"state": ["stat_str", "stat_agi", "stat_sta", "stat_cha", "stat_wis",
                             "stat_int"]},
}

STOP = set("""the a an and or but of to in on at is are was were be been it its this that these
those with from as by for not no than then so such can may will would has have had do does did
there here which who whom whose when where all any each other more most some only just very
""".split())


def sha(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:12]


def load_facts() -> dict[str, dict]:
    if not FACTS.exists():
        raise SystemExit(f"bible: no {FACTS} -- run tools/index_facts.py --write first")
    rows: dict[str, dict] = {}
    lines = FACTS.read_text(encoding="utf-8").splitlines()
    if not lines:
        return rows
    head = lines[0].split("\t")
    for line in lines[1:]:
        if not line.strip():
            continue
        r = dict(zip(head, line.split("\t")))
        rows[r["id"]] = r
    return rows


def page_rows(page: str) -> list[dict]:
    if page not in PAGES:
        raise SystemExit(f"bible: unknown page {page!r}; known: {', '.join(sorted(PAGES))}")
    want = PAGES[page]
    out = []
    for r in load_facts().values():
        ents = [e.split(":", 1) for e in r["entities"].split(",") if ":" in e]
        for ns, val in ents:
            if ns in want and val in want[ns]:
                out.append(r)
                break
    return sorted(out, key=lambda r: r["id"])


def pair() -> dict:
    w = (PROMPTS / "bible-writer.md").read_text(encoding="utf-8")
    c = (PROMPTS / "bible-critic.md").read_text(encoding="utf-8")
    return {"writer_model": WRITER_MODEL, "critic_model": CRITIC_MODEL,
            "writer_prompt_sha": sha(w), "critic_prompt_sha": sha(c)}


def packet(page: str) -> str:
    """The writer's whole task message: style block, then the rows. Nothing else, ever."""
    style = (PROMPTS / "bible-writer.md").read_text(encoding="utf-8")
    rows = page_rows(page)
    if not rows:
        raise SystemExit(f"bible: page {page!r} has no facts -- nothing to draft")
    lines = [style, "\n---\n\n## Fact rows\n"]
    for r in rows:
        lines.append(f"- id: {r['id']}\n  grade: {r['grade']}\n  entities: {r['entities']}\n"
                     f"  statement: {r['statement']}\n")
    return "\n".join(lines)


def read_draft(path: pathlib.Path) -> list[dict]:
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(data, list) or not data:
        raise SystemExit(f"bible: {path} is not a non-empty YAML list")
    out = []
    for i, item in enumerate(data, start=1):
        if not isinstance(item, dict) or "text" not in item or "facts" not in item:
            raise SystemExit(f"bible: {path} entry {i} needs text and facts")
        out.append({"sid": f"s{i}", "text": str(item["text"]).strip(),
                    "facts": [str(f).strip() for f in item["facts"]]})
    return out


def tokens(s: str) -> set[str]:
    return {w.rstrip("s") for w in re.findall(r"[a-z0-9]+", s.lower())
            if len(w) >= 4 and w not in STOP}


def precheck(page: str, draft: pathlib.Path) -> tuple[list[dict], list[str]]:
    """The mechanical half of vacuity, run before the judge. Hard failures, not opinions."""
    rows = {r["id"]: r for r in page_rows(page)}
    sents = read_draft(draft)
    errs: list[str] = []
    for s in sents:
        if not s["facts"]:
            errs.append(f"{s['sid']}: cites no fact -- a sentence must cite something")
            continue
        for fid in s["facts"]:
            if fid not in rows:
                errs.append(f"{s['sid']}: cites {fid!r}, which is not a fact on this page")
        cited = [rows[f] for f in s["facts"] if f in rows]
        if cited:
            pool = set().union(*(tokens(r["statement"]) for r in cited))
            if not (tokens(s["text"]) & pool):
                errs.append(f"{s['sid']}: vacuous -- shares no content word with the facts it "
                            "cites")
        if len(s["text"]) < 25:
            errs.append(f"{s['sid']}: too short to carry a claim ({len(s['text'])} chars)")
    return sents, errs


def judge_in(page: str, draft: pathlib.Path) -> str:
    """The critic's whole task message: critic block, the rows, the numbered sentences."""
    sents, errs = precheck(page, draft)
    if errs:
        for e in errs:
            print(f"  FAIL  {e}", file=sys.stderr)
        raise SystemExit("bible: fix the pre-check failures before judging -- the judge does not "
                         "get a page that already failed a mechanical check")
    block = (PROMPTS / "bible-critic.md").read_text(encoding="utf-8")
    rows = page_rows(page)
    out = [block, "\n---\n\n## Fact rows\n"]
    for r in rows:
        out.append(f"- id: {r['id']} | grade: {r['grade']} | entities: {r['entities']} | "
                   f"statement: {r['statement']}\n")
    out.append("\n## Sentences\n")
    for s in sents:
        out.append(f"- {s['sid']}: {s['text']}\n  cites: {', '.join(s['facts'])}\n")
    return "".join(out)


VCOLS = ("sentence_id", "resolves", "supported", "grade_survived", "non_vacuous", "reason",
         "judge_model", "prompt_sha", "prose_sha")

# The judge's own controls, recorded when a human or an agent runs the model. Same four
# questions as VCOLS so a verdict and the control that broke it can be read the same way.
# It had no header row at all, which made every reader count fields by hand and made the
# first data row the header for anything that tried to parse it.
CCOLS = ("control_id", "resolves", "supported", "grade_survived", "non_vacuous", "reason",
         "judge_model", "critic_prompt_sha", "control_note")
QUESTIONS = ("resolves", "supported", "grade_survived", "non_vacuous")


def record(page: str, draft: pathlib.Path, verdicts: pathlib.Path) -> int:
    """Commit the artefacts. The critic's TSV gets the hashes appended here, never by the model."""
    sents, errs = precheck(page, draft)
    if errs:
        for e in errs:
            print(f"  FAIL  {e}", file=sys.stderr)
        return 1
    prose = draft.read_text(encoding="utf-8")
    ph = sha(prose)
    p = pair()
    given = {}
    for line in verdicts.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) == 5:
            parts.append("")  # an all-pass row legitimately carries an empty reason
        if len(parts) < 6:
            print(f"  FAIL  verdict row {parts[:1]} has {len(parts)} fields, need 6",
                  file=sys.stderr)
            return 1
        given[parts[0]] = parts
    missing = [s["sid"] for s in sents if s["sid"] not in given]
    if missing:
        print(f"  FAIL  no verdict for {', '.join(missing)} -- every sentence needs one",
              file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    dropped, rows = [], []
    for s in sents:
        sid, res, sup, grd, vac, reason = given[s["sid"]][:6]
        rows.append("\t".join((sid, res, sup, grd, vac, reason, p["critic_model"],
                               p["critic_prompt_sha"], ph)))
        if any(v != "pass" for v in (res, sup, grd, vac)):
            dropped.append(f"{sid}\t{','.join(n for n, v in (('resolves', res), ('supported', sup), ('grade', grd), ('vacuous', vac)) if v != 'pass')}\t{reason}")
    (OUT / f"{page}.prose.yaml").write_text(prose, encoding="utf-8")
    (OUT / f"{page}.verdicts.tsv").write_text("\t".join(VCOLS) + "\n" + "\n".join(rows) + "\n",
                                              encoding="utf-8")
    (OUT / f"{page}.dropped").write_text("\n".join(dropped) + ("\n" if dropped else ""),
                                         encoding="utf-8")
    (OUT / "pair.json").write_text(json.dumps(p, indent=2, sort_keys=True) + "\n",
                                   encoding="utf-8")
    print(f"bible: recorded {page} -- {len(sents)} sentence(s), {len(dropped)} dropped")
    return 0


def verify() -> int:
    """The CI gate. No model runs here; this asserts what a generation step left behind."""
    fails: list[str] = []
    if not OUT.is_dir():
        print("bible: FAIL  no disasm/bible/ -- nothing has been generated")
        return 1
    p = pair()
    pf = OUT / "pair.json"
    if not pf.exists():
        fails.append("no pair.json -- the writer/critic pair is unrecorded")
        pair_now = None
    else:
        pair_now = json.loads(pf.read_text(encoding="utf-8"))
        if pair_now != p:
            fails.append("pair.json does not match the current prompts or model ids -- every "
                         "verdict in the tree is void; re-judge")
    for prose in sorted(OUT.glob("*.prose.yaml")):
        page = prose.name[: -len(".prose.yaml")]
        vf = OUT / f"{page}.verdicts.tsv"
        if not vf.exists():
            fails.append(f"{page}: no verdicts file")
            continue
        ph = sha(prose.read_text(encoding="utf-8"))
        lines = vf.read_text(encoding="utf-8").splitlines()
        head = lines[0].split("\t") if lines else []
        if head != list(VCOLS):
            fails.append(f"{page}: verdict header is {head}, expected {list(VCOLS)}")
            continue
        n_sent = len(yaml.safe_load(prose.read_text(encoding="utf-8")) or [])
        rows = [l.split("\t") for l in lines[1:] if l.strip()]
        if len(rows) != n_sent:
            fails.append(f"{page}: {len(rows)} verdict row(s) for {n_sent} sentence(s)")
        for r in rows:
            if r[8] != ph:
                fails.append(f"{page}/{r[0]}: prose hash {r[8]} != current {ph} -- prose moved "
                             "under the verdict")
            if pair_now and r[7] != pair_now["critic_prompt_sha"]:
                fails.append(f"{page}/{r[0]}: judged under a different critic prompt")
            if any(v != "pass" for v in r[1:5]):
                df = OUT / f"{page}.dropped"
                txt = df.read_text(encoding="utf-8") if df.exists() else ""
                if r[0] not in txt:
                    fails.append(f"{page}/{r[0]}: failed but not listed in {page}.dropped")
                if not r[5].strip():
                    fails.append(f"{page}/{r[0]}: failed with an empty reason")
    cf = OUT / "controls.tsv"
    if not cf.exists():
        fails.append("no controls.tsv -- the judge has never been seen red")
    else:
        clines = [l for l in cf.read_text(encoding="utf-8").splitlines() if l.strip()]
        if not clines or clines[0].split("\t") != list(CCOLS):
            fails.append("controls.tsv is missing its header row -- expected "
                         + "\t".join(CCOLS))
            clines = clines[1:] if clines else clines
        else:
            clines = clines[1:]
        rows = [dict(zip(CCOLS, l.split("\t"))) for l in clines]
        if len(rows) < 3:
            fails.append(f"controls.tsv has {len(rows)} row(s); three controls are required")
        for r in rows:
            cid = r.get("control_id", "?")
            if len(r) < 6 or not r.get("reason", "").strip():
                fails.append(f"control {cid} row too short or has no reason")
                continue
            if pair_now and r.get("critic_prompt_sha") != pair_now["critic_prompt_sha"]:
                fails.append(f"control {cid} was judged under a stale critic prompt")
            if all(r.get(q) == "pass" for q in QUESTIONS):
                fails.append(f"control {cid} PASSED -- a control that cannot fail is worse "
                             "than no control")
    for f in fails:
        print(f"  FAIL  {f}")
    print(f"bible: {'FAIL' if fails else 'ok'}")
    return 1 if fails else 0


def _selftest() -> int:
    """Controls for the deterministic half. The judge's own controls are a model run, not this."""
    ok = True
    tmp = pathlib.Path("/var/tmp/u2bible-selftest")
    tmp.mkdir(parents=True, exist_ok=True)
    rows = page_rows("statistics")
    if not rows:
        print("FAIL  control page has facts -- statistics should have rows")
        ok = False
    else:
        print("ok    control page has facts")
    fid = rows[0]["id"] if rows else "no.such.fact"

    def case(label: str, draft: str, want_ok: bool):
        nonlocal ok
        p = tmp / "d.yaml"
        p.write_text(draft, encoding="utf-8")
        try:
            _, errs = precheck("statistics", p)
        except SystemExit as exc:  # a malformed draft must be a red control, not a crash
            errs = [str(exc)]
        if bool(errs) == want_ok:
            ok = False
            print(f"FAIL  control {label}: got {errs or 'clean'}")
        else:
            print(f"ok    control {label}")

    good = f"- text: \"Charisma is the byte at DS:4E, which is where the routine adds it.\"\n  facts: [{fid}]\n"
    case("a cited, non-vacuous sentence", good, True)
    case("a sentence citing nothing", '- text: "Charisma lives somewhere in the block."\n  facts: []\n', False)
    case("a sentence citing a fact that is not on the page",
         '- text: "Charisma is the byte at DS:4E, which is where it is added."\n  facts: [not.a.fact]\n',
         False)
    case("a vacuous sentence", '- text: "This one matters a great deal."\n  facts: [' + fid + ']\n',
         False)
    case("a sentence too short to carry a claim", f'- text: "Stats matter."\n  facts: [{fid}]\n',
         False)
    case("a draft that is not a list", "text: nope\n", False)
    for f in tmp.glob("*"):
        f.unlink()
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--page")
    ap.add_argument("--packet", action="store_true")
    ap.add_argument("--precheck", metavar="DRAFT")
    ap.add_argument("--judge-in", metavar="DRAFT")
    ap.add_argument("--record", nargs=2, metavar=("DRAFT", "VERDICTS"))
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--pair", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return _selftest()
    if a.verify:
        return verify()
    if a.pair:
        print(json.dumps(pair(), indent=2, sort_keys=True))
        return 0
    if not a.page:
        ap.error("--page is required for packet/precheck/judge-in/record")
    d = pathlib.Path(a.precheck or a.judge_in or (a.record[0] if a.record else ""))
    if a.packet:
        sys.stdout.write(packet(a.page))
        return 0
    if a.precheck:
        _, errs = precheck(a.page, d)
        for e in errs:
            print(f"  FAIL  {e}")
        print(f"bible: {a.page} pre-check {'FAIL' if errs else 'ok'}")
        return 1 if errs else 0
    if a.judge_in:
        sys.stdout.write(judge_in(a.page, d))
        return 0
    if a.record:
        return record(a.page, d, pathlib.Path(a.record[1]))
    ap.error("nothing to do")


if __name__ == "__main__":
    sys.exit(main())
