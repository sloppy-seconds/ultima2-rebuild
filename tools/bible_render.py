#!/usr/bin/env python3
"""bible_render.py -- the two skins over the fact store. Last, optional, and deterministic.

WHY. The reader is an agent first and the fact store is the product (docs/plan-game-bible.md § 6),
so this is the one deliverable in the epic that may be skipped without anything downstream going
wrong. It renders committed artefacts and nothing else: no model runs here, so the output is a pure
function of what is in the tree, and the freshness gates stay on the fact and prose stores rather
than on HTML.

TWO SKINS, ONE STORE
  site/bible/       judged prose. Only sentences whose verdict passes all four questions; each
                    carries its evidence grade inline and a fold listing the files behind it.
  site/internals/   tables only, no drafted prose, no judge. Coverage, the unnamed-offset queue,
                    and the disagreement rows. A second prose surface here would drift from
                    docs/phase4-state.md inside a week, which is why the plan forbids it.

PAGE STATES, per page, because staleness is per page:
  fresh  the verdicts still hash to the prose on disk
  stale  the prose moved under the verdicts -- re-judge before trusting the page
  void   no verdicts, or a cited fact is gone from the store

THE DISAGREEMENT TABLE is the artefact § 6 calls the most valuable output, and it is prose-free on
purpose: their claim, our verdict, our grade, and the direction of the disagreement. Softening it
into paragraphs would weaken the one thing here that is genuinely a control.

NOT PUBLIC. v1 is internal-only. No verbatim game string may appear in a public skin until the
owner rules otherwise (§ 7), so `--public` refuses rather than quietly rendering something that
looks published.

USAGE
  tools/bible_render.py                 # render into site/
  tools/bible_render.py --serve         # render, then serve on :8098
  tools/bible_render.py --out DIR
  tools/bible_render.py --public        # refuses, and says why
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import html
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
BIBLE = REPO / "disasm" / "bible"
FACTS = REPO / "disasm" / "facts" / "asm.tsv"
CENSUS = REPO / "disasm" / "census"
INDEX = REPO / "disasm" / "index"
SPELLS = REPO / "disasm" / "source-third-party-spells.tsv"

GRADE_CLASS = {"observed": "g-obs", "measured": "g-meas", "inferred": "g-infer",
               "recalled": "g-recall"}

CSS = """
:root{--bg:#14161a;--fg:#dfe3e8;--dim:#8b95a3;--line:#2a2f38;--acc:#e0b341}
body{background:var(--bg);color:var(--fg);font:15px/1.55 ui-monospace,SFMono-Regular,Menlo,monospace;
margin:0;padding:2rem 1.25rem;max-width:60rem}
a{color:var(--acc)}h1{font-size:1.3rem;margin:0 0 .3rem}h2{font-size:1rem;margin:2rem 0 .5rem;
color:var(--dim);text-transform:uppercase;letter-spacing:.06em}
.sub{color:var(--dim);margin:0 0 1.5rem}.sent{padding:.5rem 0;border-bottom:1px solid var(--line)}
.badge{font-size:.7rem;padding:.1rem .4rem;border:1px solid var(--line);border-radius:3px;
margin-right:.5rem;vertical-align:middle}
.g-obs{color:#7fd18b;border-color:#3f5c44}.g-meas{color:#79b8ff;border-color:#33506e}
.g-infer{color:#e0b341;border-color:#6b5622}.g-recall{color:#c586c0;border-color:#5c3a5a}
.st-fresh{color:#7fd18b}.st-stale{color:#e0b341}.st-void{color:#f47c7c}
details{margin:.2rem 0 .3rem;color:var(--dim);font-size:.8rem}
summary{cursor:pointer;color:var(--dim)}
table{border-collapse:collapse;width:100%;font-size:.82rem;margin:.5rem 0 1.5rem}
th,td{border-bottom:1px solid var(--line);padding:.35rem .5rem;text-align:left;vertical-align:top}
th{color:var(--dim);text-transform:uppercase;font-size:.7rem;letter-spacing:.05em}
.claim{max-width:22rem}.verdict{max-width:30rem}.mono{font-family:inherit;color:var(--dim)}
nav{margin-bottom:1.5rem;font-size:.85rem}
"""


def esc(s: str) -> str:
    return html.escape(s, quote=False)


def tsv(path: pathlib.Path) -> list[dict]:
    if not path.exists():
        return []
    lines = path.read_text(encoding="utf-8").splitlines()
    head = next((i for i, l in enumerate(lines) if not l.startswith("#") and "\t" in l), None)
    if head is None:
        return []
    return list(csv.DictReader(lines[head:], delimiter="\t"))


def page_state(page: str, facts: dict[str, dict]) -> tuple[str, str]:
    """fresh / stale / void, derived from the artefacts, never asserted."""
    import yaml
    prose_f = BIBLE / f"{page}.prose.yaml"
    ver_f = BIBLE / f"{page}.verdicts.tsv"
    if not prose_f.exists() or not ver_f.exists():
        return "void", "no prose or no verdicts for this page"
    try:
        draft = yaml.safe_load(prose_f.read_text(encoding="utf-8")) or []
    except Exception:
        return "void", "prose does not parse"
    ph = hashlib.sha256(prose_f.read_text(encoding="utf-8").encode()).hexdigest()[:12]
    rows = [l.split("\t") for l in ver_f.read_text(encoding="utf-8").splitlines()[1:] if l.strip()]
    if len(rows) != len(draft):
        return "stale", f"{len(rows)} verdict rows for {len(draft)} sentences"
    for r in rows:
        if len(r) > 8 and r[8] != ph:
            return "stale", "prose hash moved under the verdicts"
    for item in draft:
        for fid in item.get("facts", []):
            if fid not in facts:
                return "void", f"cited fact {fid} is gone from the store"
    return "fresh", "verdicts match the prose on disk"


def page_html(page: str, facts: dict[str, dict]) -> str:
    import yaml
    prose_f = BIBLE / f"{page}.prose.yaml"
    ver_f = BIBLE / f"{page}.verdicts.tsv"
    state, why = page_state(page, facts)
    dropped = set()
    df = BIBLE / f"{page}.dropped"
    if df.exists():
        dropped = {l.split("\t")[0] for l in df.read_text(encoding="utf-8").splitlines() if l}
    body: list[str] = []
    if prose_f.exists() and ver_f.exists():
        draft = yaml.safe_load(prose_f.read_text(encoding="utf-8")) or []
        verdicts = {r[0]: r for r in (l.split("\t") for l in
                                      ver_f.read_text(encoding="utf-8").splitlines()[1:]
                                      if l.strip())}
        for i, item in enumerate(draft, start=1):
            sid = f"s{i}"
            v = verdicts.get(sid, [])
            if sid in dropped or any(x != "pass" for x in v[1:5]):
                continue  # a failed sentence is not rendered; it is in the drop list
            grades = sorted({facts[f]["grade"] for f in item.get("facts", []) if f in facts})
            srcs = sorted({facts[f]["source_file"] for f in item.get("facts", []) if f in facts})
            tools = sorted({t for f in item.get("facts", []) if f in facts
                            for t in facts[f].get("evidence_tool", "").split(",") if t})
            badges = "".join(f'<span class="badge {GRADE_CLASS.get(g, "")}">{esc(g)}</span>'
                             for g in grades)
            # Not a link: the served root is site/, and asm/ is not inside it. A link here would
            # 404, and a filename that lies about being clickable is worse than plain text.
            ev = "".join(f'<span class="mono">{esc(s)}</span> ' for s in srcs)
            ev += "".join(f' <span class="mono">{esc(t)}</span>' for t in tools)
            body.append(f'<div class="sent">{badges}{esc(item["text"])}'
                        f"<details><summary>evidence</summary>{ev}</details></div>")
        if dropped:
            body.append(f'<p class="sub">{len(dropped)} sentence(s) dropped by the judge and '
                        "not rendered.</p>")
    else:
        body.append('<p class="sub">Nothing rendered: this page has no committed prose yet.</p>')
    return (f"<!doctype html><meta charset=utf-8><title>{esc(page)} · bible</title>"
            f"<style>{CSS}</style><nav><a href=\"index.html\">bible</a> · "
            f"<a href=\"../internals/index.html\">internals</a></nav>"
            f"<h1>{esc(page)}</h1><p class=\"sub\">state: "
            f"<span class=\"st-{state}\">{state}</span> — {esc(why)}</p>"
            + "".join(body))


def disagreement_rows() -> list[dict]:
    """Their claim, our verdict, our grade, and the direction. Two joins, no prose."""
    out: list[dict] = []
    for r in tsv(SPELLS):
        if not r.get("verdict"):
            continue
        v = r["verdict"]
        head = v.split(".")[0].upper()
        direction = ("agrees" if v.upper().startswith("MATCHES") else
                     "partial" if ("PARTLY" in head or "SHAPE" in head) else "disagrees")
        out.append({"source": "third-party spell table", "claim": f"{r['name']} ({r['class']}): "
                    f"{r['claim']}", "ours": v, "grade": r.get("grade", ""),
                    "direction": direction})
    sectors = {s["layer"]: s for s in tsv(CENSUS / "sector.tsv")}
    for p in tsv(CENSUS / "place.tsv"):
        sec = sectors.get(p.get("sectors", "").split("/")[-1], {})
        ours, src = sec.get("name", ""), sec.get("name_source", "")
        if ours and src == "bytes" and ours.strip().upper() != p["guide_name"].strip().upper():
            out.append({"source": "vendored location table", "claim": p["guide_name"],
                        "ours": f"{ours} (read off the map tiles, sector {sec.get('layer','')})",
                        "grade": p.get("grade", ""), "direction": "spelling"})
    return out


def table(cols: list[str], rows: list[dict], cls: str = "") -> str:
    head = "".join(f"<th>{esc(c)}</th>" for c in cols)
    body = "".join("<tr>" + "".join(f'<td class="{cls}">{esc(str(r.get(c, "")))}</td>'
                                    for c in cols) + "</tr>" for r in rows)
    return f"<table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>"


def internals_html() -> str:
    cov = tsv(INDEX / "coverage.tsv")
    n = len(cov)
    by = {}
    for r in cov:
        by[r["grade"]] = by.get(r["grade"], 0) + 1
    summary = [{"grade": g, "files": c, "share": f"{100.0 * c / n:.1f}%" if n else "—"}
               for g, c in sorted(by.items(), key=lambda kv: -kv[1])]
    dis = disagreement_rows()
    order = {"disagrees": 0, "partial": 1, "spelling": 2, "agrees": 3}
    dis.sort(key=lambda r: (order.get(r["direction"], 9), r["claim"]))
    bad = sum(1 for r in dis if r["direction"] != "agrees")
    offs = tsv(INDEX / "unnamed-offsets.tsv")
    return (f"<!doctype html><meta charset=utf-8><title>internals</title><style>{CSS}</style>"
            "<nav><a href=\"../bible/index.html\">bible</a> · internals</nav>"
            "<h1>internals</h1><p class=\"sub\">Tables only, by ruling. Intent prose lives in "
            "<span class=\"mono\">docs/phase4-state.md</span>; a second prose surface here would "
            "drift from it inside a week.</p>"
            f"<h2>coverage · {n} files</h2>"
            + (table(["grade", "files", "share"], summary) if summary else
               "<p class=\"sub\">no coverage table</p>")
            + f"<h2>against the third-party tables · {len(dis)} judged, {bad} not agreeing</h2>"
            + (table(["direction", "claim", "ours", "grade", "source"], dis, "claim") if dis
               else "<p class=\"sub\">no disagreement rows</p>")
            + f"<h2>unnamed state offsets · {len(offs)} rows</h2>"
            + (table(list(offs[0].keys()), offs[:60]) if offs
               else "<p class=\"sub\">every offset is named</p>"))


def render(out: pathlib.Path) -> int:
    facts = {r["id"]: r for r in tsv(FACTS)}
    pages = sorted(p.name[: -len(".prose.yaml")] for p in BIBLE.glob("*.prose.yaml")) \
        if BIBLE.is_dir() else []
    (out / "bible").mkdir(parents=True, exist_ok=True)
    (out / "internals").mkdir(parents=True, exist_ok=True)
    for page in pages:
        (out / "bible" / f"{page}.html").write_text(page_html(page, facts), encoding="utf-8")
    lis = "".join(f'<li><a href="{esc(p)}.html">{esc(p)}</a> '
                  f'<span class="st-{page_state(p, facts)[0]}">{page_state(p, facts)[0]}</span>'
                  "</li>" for p in pages)
    (out / "bible" / "index.html").write_text(
        f"<!doctype html><meta charset=utf-8><title>bible</title><style>{CSS}</style>"
        "<nav>bible · <a href=\"../internals/index.html\">internals</a></nav>"
        f"<h1>the bible</h1><p class=\"sub\">Generated from <span class=\"mono\">disasm/facts/"
        f"asm.tsv</span> and <span class=\"mono\">disasm/bible/</span>. {len(pages)} page(s). "
        "Sentences the judge failed are not rendered.</p>"
        f"<ul>{lis or '<li class=\"sub\">no pages yet</li>'}</ul>", encoding="utf-8")
    (out / "internals" / "index.html").write_text(internals_html(), encoding="utf-8")
    dis_n = len(disagreement_rows())
    print(f"bible_render: {len(pages)} bible page(s), internals tables, {dis_n} disagreement "
          f"row(s) -> {out.relative_to(REPO) if out.is_relative_to(REPO) else out}")
    return 0 if pages else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=str(REPO / "site"))
    ap.add_argument("--serve", action="store_true")
    ap.add_argument("--port", type=int, default=8098)
    ap.add_argument("--public", action="store_true")
    a = ap.parse_args()
    if a.public:
        print("bible_render: REFUSED. v1 is internal-only. No verbatim game string may appear in "
              "a public skin until the owner rules otherwise (docs/plan-game-bible.md § 7), and "
              "the fact store quotes game text today. Render internally, or get the ruling.")
        return 2
    out = pathlib.Path(a.out)
    rc = render(out)
    if a.serve:
        import functools
        import http.server
        import socketserver
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(out))
        with socketserver.TCPServer(("", a.port), handler) as httpd:
            print(f"serving {out} on http://0.0.0.0:{a.port} (ctrl-c to stop)")
            httpd.serve_forever()
    return rc


if __name__ == "__main__":
    sys.exit(main())
