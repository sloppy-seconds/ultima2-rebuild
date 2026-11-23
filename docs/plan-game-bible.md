# Epic: the bible — a generated, graded, self-correcting reference to the game

**Status: ruled, not built. Not a live queue.** Depends on `docs/plan-code-index.md`; § 9
carries the interleaved order, and § 11 is the record of rulings from the decision review on
2026-11-23 — nothing in this document is open. Terms live in `GLOSSARY.md` (**the bible**,
**negative fact**, **evidence grade** against **coverage grade**); the two decisions that need
to stand on their own are ADR 0003 (no model runs in CI) and ADR 0004 (prose is a generated
view).

The model is the Gamer Corner Ultima II guide
(`guides.gamercorner.net/ultimaii/`), which is excellent and which this project has already
used as a control — `disasm/source-gamercorner-locations.csv` is their location table,
vendored with URL and fetch date. The difference in kind, and the only reason this is worth
building: **their guide is a snapshot of someone's understanding; ours would be a projection
of the binary.**

---

## 1. The thesis

Facts are cheap to get right and boring to read. Prose is expensive, is where the value is,
and is what an LLM is good at. Neither can be trusted alone.

So: **facts are generated from the tree, prose is drafted from a closed fact set, and a second
agent judges the prose against those facts sentence by sentence.** The thing that makes that
checkable rather than a vibes test is that **sentences carry their evidence structurally** —
each drafted sentence names the fact IDs it asserts. Without anchors, the judge degrades into
an opinion and the result is a confident wiki with our logo on it.

The payoff no human-written guide can match: because prose is a *view* over facts, a corrected
decode invalidates the prose. When `asm/6B3D` is re-read and a claim dies, every sentence
citing it goes stale and is re-drafted on the next build. The G90 and G85 class of error stops
being something we must be careful about and becomes something that cannot survive a rebuild.

---

## 2. Non-goals

* **Not a wiki.** No human edits the site or the prose store. Humans maintain the fact base
  indirectly, by decoding.
* **Not a place facts live.** Facts are generated from `asm/`, the census and the code index.
  A hand-authored fact is the G90 failure at scale.
* **Never written from third-party prose.** The guide is a *control* — diffed against, never
  used as a source. See § 7.
* **Not the internals doc.** Engine mapping (asm ↔ C, classic vs enhanced) is a second skin
  over the same index for contributors, not content in the player-facing book.
* **No tile-art page in v1.** A bible page about tile art becomes a trace document, and a
  public page is exactly the hand-off vector the cleanroom wall exists to close. The art story
  keeps its own documents; the exclusion is named here rather than left to be discovered.
* **No enhanced mode in v1.** The index epic ruled enhanced out of `engine-map` because the
  divergence notes do not exist, and a `diverges:` field nobody has filled reads as finished.
  Same defect one layer up, same answer: excluded, and writing the notes is its own item rather
  than a dependency smuggled in through a rendering rule.
* **A fact's evidence is never a `docs/*.md` file.** Evidence is an `asm/` file or a tool.
  The 100 files in `docs/` are working notes, and a fact citing prose about prose inherits
  every drift in it.
* **The reader is an agent first.** The fact store and its queries are the product; the rendered
  page is proof that the facts render. That is why the renderer is last and optional, and it is
  the reason this epic exists: returning agents re-decode things and cite prose that has gone
  stale.

---

## 3. The fact schema

A fact is one assertion, machine-readable, with provenance. Sketch:

```yaml
- id: stat-encoding-hex
  kind: rule
  entities: [stat/strength, stat/intelligence, stat/charisma]
  statement: "Displayed as two decimal digits; interpreted as hex, so the value is
              tens*16 + ones. 56 displayed is 86 actual."
  grade: measured
  evidence: [asm/<file>, tools/bcd_census.py]
  name_source: ours
```

Fields that matter and why:

* **`grade`** — `observed | measured | inferred | recalled`, the existing § 5 scale. Rendered
  visibly. This is the whole differentiator: the guide cannot mark a guess, and we can.
* **`evidence`** — file paths, not prose. A correction to the file invalidates the fact.
* **`entities`** — the join keys into the census (`world, sector, place, npc, tile, creature,
  gate, vehicle, site, vendor`). Cross-linking is then mechanical: match entity references in
  prose against those tables. That half is a weekend.
* **`name_source`** — `ours | wiki`, because we ruled that bytes decide identity and the wiki
  decides names. The game spells `TOMMERSVILLE` and `PIRATES COVE` itself where their guide
  says Tommersville and Pirates Harbour. Every row says where its name came from.
* **`kind`** — a closed enum, not free text: `rule | arithmetic | condition | relation |
  identity`. A closed set is what makes "does this fact support that sentence" decidable. An
  unknown `kind` fails `--verify`.
* **`scope`** — required on a **negative fact** (glossary), naming the method that established
  the absence and its blind spots. A negative fact may not be graded above `inferred` until the
  method is exhaustive.
* **`disputed`** — propagated by the extractor from a header carrying `NAME DISPUTED:` or
  `body corrected:`. A disputed fact renders inline as disputed and may not be graded above
  `inferred` until the header settles. Dropping it would hide the most interesting rows.

Entities are ids owned by the census (`sector/21`, `creature/…`, `place/…`), and the census is
**entity authority, not a fact source**: facts reference entity ids and never duplicate census
columns, so there is no cycle between the two. A fact citing an unknown entity id fails
`--verify`.

Behavioural rules that cannot be mechanically derived are authored **in `asm/` headers**, where
the evidence lives and where corrections land, and extracted into the fact store. Not authored
in the fact store. That is the difference between a generated view and a second copy.

The authoring surface is a strict header line, because 236 headers carry `name-evidence:` *prose*
and prose extraction is itself a model task — which would put a model between the bytes and the
fact store, and that is where folklore enters:

```
# fact: <id> | <kind> | <grade> | <entities> | <statement>
```

One line per fact, pipe-delimited, fixed field order, statement capped at one sentence, no
continuation lines, and a malformed line is a hard failure rather than a skipped one. Adding
these lines is legal under CP0: the body guard md5s only non-`#` lines and the assembler ignores
comments. Headers are retrofitted only for pages being drafted, starting with statistics.

**Evidence is implicit, and that is the point.** Ruled 2026-11-23. A `# fact:` line's evidence
*is* the header it sits in, so the line never repeats its own filename, and staleness hashes the
containing file. A fact whose evidence is a tool rather than the routine it describes — the stat
encoding comes from `tools/bcd_census.py`, the place names from `tools/census.py` — carries an
optional sixth field naming the tool, and staleness hashes the tool as well. Making the field
mandatory would have every line in the tree restate where it lives, and a required field that is
always derivable is a field that drifts.

**Shipped 2026-11-23 as `tools/index_facts.py` (#27), with zero facts in the tree**, which is the
ruling rather than an oversight: the thing that can fail comes before the thing that fills it.
Thirteen controls, including the end-to-end duplicate-id case. Two details settled while building
it:

* **The store is TSV, not YAML.** D1 said YAML. Every other generated table in this repo is TSV
  (`disasm/census/`, `disasm/index/`), the freshness gate is a line diff, and a tab inside a
  statement is now a hard failure, which is the only hazard the format has.
* **Entity references are namespaced, because census has no id column.** The ruling said facts
  reference census entity ids; census tables have `guide_name`, `name`, `layer`, `art`, `what`
  and `era_index` instead. So a reference is `ns:value` — `place:`, `creature:`, `sector:`,
  `npc:`, `vehicle:`, `gate:`, `tile:`, `world:`, `site:`, `vendor:`, plus `state:` for a
  `state.h` field and `asm:` for a routine — and `--verify` resolves every one against the column
  that names it. An unresolvable entity is a hard failure, which is the point: a fact about a
  place that got renamed should stop resolving, loudly.

---

## 4. The writer: closed-book

The writer receives only the fact rows for one page, and nothing else. No model knowledge
about Ultima II is permitted, because every model has read about this game on the internet and
will produce a fluent sentence that is third-party folklore or popular-and-wrong. Closed-book
is the single most important constraint in this document.

Output is structured, not free prose:

```yaml
- text: "Statistics look like decimal 0-99 but are read as hex, so 56 displayed means 86."
  facts: [stat-encoding-hex]
- text: "Because of that, going from 49 to 50 gains 7, not 1."
  facts: [stat-encoding-hex, stat-step-arithmetic]
```

A sentence with no `facts` is a compile error. A sentence citing a fact that does not support
it is a judge failure.

The writer also carries a committed style block, because style rules here are grade rules in
disguise: present tense, no hedges on `observed` or `measured` facts, a hedge required for an
`inferred` one, and no third-party folklore vocabulary. An unversioned prompt is a second
hand-maintained copy of the judge's standard, so the block lives in `tools/prompts/` and is
covered by the prompt hash in § 5.

---

## 5. The judge: three mechanical questions

Not "is this true" — that is the question a same-family critic waves through, because
plausibility is what it is tuned to reward. Per sentence:

1. **Does it resolve?** Every cited fact exists and is about the same entities.
2. **Is it supported?** Nothing in the sentence asserts what the cited facts do not state.
   This is where outside knowledge gets caught: a true-sounding clause with no fact behind it
   fails even if it happens to be correct.
3. **Did the grade survive?** An `inferred` fact rendered as flat assertion fails. Prose
   naturally smooths uncertainty away, so this must be checked, not hoped for.

Plus a fourth, because true-and-empty compiles: **vacuity**, and it is made mechanical or it is
the vibes test this section refuses. A deterministic pre-check runs before the judge: a sentence
must name at least one entity from its cited facts and must contain a token drawn from the
fact's `statement`, or it is rejected as vacuous. The judge then carries the same four questions
as a second pass and must give a non-empty reason for any failure. The two checks together are
what make "supported" testable: the pre-check catches the empty sentence, question 2 catches the
sentence that names the right entity and asserts something no fact states.

The judge's output is per sentence with a reason, never a page-level verdict, and it is a
committed artefact. One row per sentence: the sentence id, the four questions as separate
pass/fail fields, a reason per failure, the judge model id, the prompt hash, and the hash of the
prose it judged. CI asserts the artefact without running a model — every sentence has a verdict,
every verdict passes or the sentence is absent from the page and present in the drop list, and
the recorded prose hash matches the store. A consequence worth naming now: **changing the judge
model or prompt invalidates every verdict**, which means re-judging all pages. That is cheap,
because re-judging never touches the writer.

**The judge must be seen red before it is trusted**, per this repo's standing rule about gates.
Three controls ship with it: a deliberately false sentence, a grade-laundered one, and an
unsourced-but-true one. All three must fail. A judge that cannot fail is worse than no judge.

**Controls do not run in CI, because no model runs in CI** (ADR 0003). They run as part of the
generation step, and their output is committed alongside a hash of the writer/judge pair in use;
CI asserts the control file exists, matches the current pair hash, and shows all three controls
failing. "The judge was seen red" is therefore a checkable artefact, not a claim in prose. The
writer and judge must be different model families, both ids recorded per page, and the pair is
chosen by whoever runs the build rather than fixed here — naming a pair now would be a decision
made without the evidence the controls produce.

---

## 6. Rendering

Generated, no server, and **rendering is last and optional**: the generator writes HTML into a
gitignored `site/`, viewable locally the way `map_viewer.py --serve` already works, and the
freshness gate operates on the fact and prose stores rather than on HTML. Hosting is a
publication question and publication is deferred (§ 7), so no hosting decision is made here.
Two skins over one fact store:

* **`bible/`** — the game. Game systems, places, planets, creatures, items, spells, time and
  gates. Evidence grades visible inline; clicking a sentence shows its evidence files.
* **`internals/`** — **tables only, no drafted prose.** The coverage table, the engine-map table,
  the disagreement rows, and links out to the owning doc. Its content is exactly what the index
  epic's three tables already generate, and `docs/phase4-state.md` already owns intent prose, so
  a second prose surface there would drift from the docs inside a week. The two skins are honest
  in different ways: the player's book is judged prose, the internals skin is tables.

**Page granularity.** Staleness, cost and the drop list are all per page, so the unit has to be
named: one page per system (`statistics`, `time`, `gates`, `hyperjump`) plus one page per
lookup-shaped entity (each place, each creature), because a reader asking about sector 21 should
get one page, and the census already has the row to build it from. Behavioural rules live on the
system pages. A corrected charisma byte then dirties `statistics`, not the whole book.

**The most valuable output is free, and it is not prose: "where we disagree with the guide," as
per-claim rows.** Measured: their vendored table is 28 rows with four columns
(`Name,Type,World,Treasure`), `disasm/census/place.tsv` already joins it with `grade` and
`source`, and `disasm/source-third-party-spells.tsv` already carries `verdict` and `grade` per
spell claim. So the content is half-built and it is two joins, not a page. Render it as their
claim, our verdict, our evidence grade and the direction of the disagreement — prose would only
soften the one artefact that is genuinely a control.

---

## 7. The text and licence line, ruled for v1

`AGENTS.md` already rules that game text is committed because `asm/` cannot exist without it,
and that art is never committed and is substituted under the cleanroom. A public website is a
different exposure from a repository of reconstruction files, and it will want to quote game
strings at length.

Three things this epic must not do:

* **Do not generate prose from the guide's prose.** Facts may be checked against their table;
  sentences may not be derived from their sentences. Their location table is vendored as data
  with attribution; their prose is not an input.
* **Do not publish substitute-art-adjacent evidence.** The cleanroom wall says
  `docs/tile-art-spec.md` may carry intent and never a trace. A bible page about tile art must
  not become a trace document, or it hands the wall's forbidden content to whoever draws next.
* **No verbatim game string in any public skin until the owner rules otherwise.** Ruled
  2026-11-23: v1 is internal-only, and the public skin — if it is ever built — renders no
  verbatim game string, paraphrase only, with `name_source` visible in the rendered page and
  third-party table rows quoted as data with attribution in the footer. The existing
  committed-text ruling was about reconstruction files, and a website is a different exposure.

---

## 8. Worked spike: the Statistics page

The guide's statistics passage is the right first deliverable because it is small, it is the
kind of page people actually want, and grading our own decode of it exposes exactly how much
work this is.

| claim in their passage | our current state |
| --- | --- |
| stats display decimal, read as hex (`tens*16 + ones`) | decoded — packed decimal, `tools/bcd_census.py` |
| values above 99 wrap to 0 | decoded — no ceiling guard on any add site, owner recollection confirmed by bytes |
| stat increases come in steps of 4 | not established here; needs the increase site read |
| discount is `charisma + intelligence`, wrapping at 255 | partial — `asm/20AB_bcd_price_from_stats` exists; G23 records the static half "did not come out clean" |
| three price tiers, middle at internal 64, cheapest at 128 | not decoded |
| **"charisma doesn't have any other effect"** | **answerable now, with a scope** — charisma is `DS:4E` (proven 2026-10-29) and `grep -rn 0x4e asm/` returns 9 files, 6 non-comment hits |

That last row is still the pitch and it is **no longer a blocker**. "Charisma has no other
effect" is a *negative* fact (glossary), and observation can never establish it — no amount of
playing proves absence. But the fast path already answers it: read the nine files that mention
`0x4e`, and grade the claim `measured (grep; indexed bases unresolved)`, which is the honest
scope rather than a caveat. `footprint` does not create the claim, it upgrades the scope from
"grep, indexed bases unresolved" to "every site that can reach the byte". Their guide's most
actionable advice ("keep charisma below 66") rests on a claim their method cannot verify and
ours can, and that is still the argument for both epics — it just does not gate this one.

The spike is: generate that one page end to end — facts, closed-book draft, judged, rendered —
with every sentence anchored, and let it be wrong in public on one page rather than right in
principle on none. Three of the seven claims above are things we have not decoded, and the page
must show that: see the `open` section in § 12.

---

## 9. Ordering across both epics

Re-ruled 2026-11-23 against `docs/plan-code-index.md` § 6 and § 7, which moved after this section
was written: `dataflow` is now `footprint`, `xref` is cut to its own epic, the census is **not**
folded under `lookup`, and `coverage` ships *before* `footprint`. The interleaved order:

| step | what | why here |
| --- | --- | --- |
| 1 | the `# calls:` gate (index #22) | an hour, independent, can go red the day it lands |
| 2 | `coverage` (index #23) | nearly free, gives the headline number, proves the gate pattern |
| 3 | fact schema + extractor + **Statistics spike** (bible) | de-risks the schema, the writer and the judge for one page, and needs no index deliverable at all |
| 4 | `footprint` + `lookup` (index #24) | the big one, arriving with the spike's requirements already known |
| 5 | bible volume, `engine-map`, disagreement rows, internals skin | volume work, only after 3 proves the loop |

The index is a *producer* of cheap truth and should not wait; the bible's risky part was never
the facts, it is the schema, the closed-book writer and a judge nobody has seen red, and none of
that needs the index now that the spike is unblocked. Two things would change this order: wanting
the charisma claim proven rather than scoped moves `footprint` ahead of the spike, and a bad
spike — a schema that cannot express a negative claim, a judge that waves everything through — is
a cheap failure worth having at step 3 rather than after an epic.

---

## 10. Failure modes, with the fix named

| failure | why it happens | fix |
| --- | --- | --- |
| judge agrees too easily | same model family, plausibility-tuned | three mechanical questions, per sentence, different family where possible, controls seen red |
| outside knowledge smuggled in | the writer has read about the game | closed-book generation, unsourced sentence is a hard error |
| grade laundering | fluent prose flattens uncertainty | grade is a field, judge checks it, renderer displays it |
| vacuity | true sentences that say nothing | judge rejects; page length is not a metric |
| facts drift from `asm/` | hand-maintained copies | facts generated or extracted only; `--check-fresh` gate |
| the site becomes the authority | people cite the rendering | every page names its generator and commit; the site is explicitly a view |
| stale prose after a correction | prose authored, not derived | anchors make staleness detectable; unanchored prose is rejected |
| the freshness gate cannot byte-compare prose | LLM output is non-deterministic, so the census precedent does not transfer | staleness and input-equality instead of `cmp` (§ 12) |
| a claim is in scope but has no fact | silence reads as completeness | the `open` section is generated, and a claim row with no fact and no `open` entry fails `--verify` |
| a fact vanishes and the page quietly shrinks | deletion is not correction | `void` state: the page is not rendered at all until re-drafted (§ 12) |

---

## 11. Rulings

Reviewed 2026-11-23. The draft's D-numbers are kept so nothing is lost; reversals say so.

| # | decision | ruling |
| --- | --- | --- |
| D1 | where facts live | generated `disasm/facts/`, committed, YAML. Behavioural rules are authored as `# fact:` lines in `asm/` headers and extracted; **one writer per file**, so the extractor owns `asm.tsv` and each tool that generates facts owns its own file (§ 12). |
| D2 | anchor syntax | the two-field YAML (`text` + `facts[]`), one entry per sentence. An unanchored sentence is a compile error. |
| D3 | writer and judge families | different families, mandatory, both ids recorded per page; the pair is chosen by whoever runs the build and the controls must have been run against the pair in use. |
| D4 | a sentence that fails | dropped, and listed on the page under "could not be supported". A page that silently dropped three sentences looks more certain than the truth. |
| D5 | publish at all | internal-only v1. The public skin is deferred behind an owner ruling, and renders no verbatim game string when it comes. |
| D6 | verbatim game strings | none in any public skin until the owner rules otherwise (§ 7). |
| D7 | third-party names | `name_source` is visible in the rendered page, not just the data, so attribution survives into what a reader sees. |
| D8 | page order | Statistics spike, then the disagreement rows, then planets and places (the census already holds 781 rows), then creatures, then items and spells, then time and gates. |
| D9 | capture citations | yes, and a claim about visible behaviour names its capture directory; frames stay gitignored. |
| D10 | regeneration cost | incremental, and the gate is staleness rather than a rebuild — because byte-comparing LLM prose is impossible (§ 12). |
| new | does the spike need the index | **reverses § 9 as drafted.** No. Charisma is `DS:4E`, nine files mention it; the spike runs grep-first now with a scoped grade and `footprint` upgrades the scope later. |
| new | where models run | never in CI (ADR 0003). Generation is a human-triggered agent step that commits prose plus per-sentence verdicts; CI gates the artefacts. This makes the draft's "controls run in CI" false. |
| new | how a fact leaves a prose header | a strict one-line `# fact:` field, retrofit only for drafted pages, malformed line is a hard failure. An LLM extracting facts from headers is refused: it puts a model between the bytes and the fact store. |
| new | who owns the prose store | the generator (ADR 0004). A human who wants different wording changes the fact or the prompt, never the prose. |
| new | facts against `docs/` | a fact's evidence is an `asm/` file or a tool, never a `docs/*.md`. Docs stay working notes; retiring the generated index docs is deferred. |
| new | grade vocabulary | facts carry an **evidence grade**; the index's is always **coverage grade**; "grade" unqualified is banned. `recalled` renders in the internal skin and is excluded from any public page. |
| new | tile art | excluded from v1 (§ 2). |
| new | enhanced mode | excluded from v1 (§ 2), for the same reason the index epic cut it from `engine-map`. |
| new | vacuity | a deterministic pre-check (name an entity from the cited facts, contain a token from the `statement`) plus the judge's second pass with a mandatory reason. |
| new | census vs facts | census is entity authority, not a fact source. Facts reference entity ids and never copy census columns; unknown entity id or unknown `kind` fails `--verify`. |
| new | negative facts | a named kind carrying `scope`; may not be graded above `inferred` until the method is exhaustive. |
| new | disputed headers | `NAME DISPUTED:` and `body corrected:` propagate to a `disputed` flag, rendered inline, capped at `inferred`. |
| new | page granularity | one page per system plus one per lookup-shaped entity. |
| new | `internals/` skin | tables only, no drafted prose, no judge. |
| new | rendering and hosting | generator writes to a gitignored `site/`; no hosting decision until publication is ruled. |
| new | the disagreement output | per-claim rows, not a prose page. |
| new | who reads it | an agent first; the fact store is the product and the renderer is last and optional. |
| new | "the bible" | kept as the name, defined in `GLOSSARY.md` against the docs and the index. |
| new | the `open` section | a report, not a queue. Work gets added to `docs/outstanding.md` or `docs/needs-gameplay.md` by hand, in the commit that exposed it. |

---

## 12. Layout, build order, and page states

**Layout.** `disasm/facts/` generated fact rows; `bible/prose/` the anchored prose store;
`bible/verdicts/` per-sentence judge verdicts; `site/` gitignored rendered HTML;
`tools/prompts/bible_writer.md` and `tools/prompts/bible_judge.md` committed and hashed, because
an unversioned prompt is a second hand-maintained copy of the judge's standard. Tools follow the
index epic's naming: `tools/index_facts.py`, `tools/bible_draft.py`, `tools/bible_judge.py`,
`tools/bible_render.py`.

**One writer per file, not one writer per directory.** Ruled 2026-11-23, and ADR 0002 carries it.
The header extractor writes `disasm/facts/asm.tsv`. A tool-derived fact is written by the tool
that knows its own evidence, so `tools/bcd_census.py` and `tools/census.py` each grow a `--facts`
mode writing `disasm/facts/bcd.tsv` and `disasm/facts/census.tsv`. Sole-writer and freshness are
per-file properties, and that is the only way both can hold when three generators feed one
directory. A second extractor that re-emitted another tool's output was rejected: it is a copy
with extra steps, and it drops the grade the originating tool already carries.

**Build order.** (1) schema, extractor and `--verify`, with controls that go red on a malformed
fact line and on a missing evidence file — committed with **zero facts in it**, because the thing
that can fail comes before the thing that fills it; (2) retrofit `# fact:` lines in the
statistics headers and generate the first rows; (3) the writer, anchors enforced as a hard error;
(4) the judge, its four questions, the vacuity pre-check and the three controls; (5) the renderer,
last, because nothing else depends on it.

**Staleness, since a byte-compare is impossible.** Two deterministic checks. *Input equality:*
the artefact records the fact-set hash, the prompt hashes and the model ids, and the gate asserts
those still match the tree, so a changed input makes every dependent page stale without
regenerating anything. *Evidence movement:* each fact records a hash of its evidence files, and a
page citing a fact whose evidence moved is stale. Together they are what "a corrected decode
invalidates the prose" means mechanically.

**Page states.** `fresh`; `stale`, where evidence moved and sentences will be re-drafted; and
`void`, where a cited fact no longer exists — a deletion, not a correction, and the page is not
rendered at all until re-drafted, because a quietly shorter page is worse than a visible absence.

**The `open` section.** Every page carries a generated list of the claims in scope that have no
supporting fact, and a claim row with no fact and no `open` entry fails `--verify`. Silence is
not an option, which is the bible's version of the index epic's unknown-span rule. It is a report
on the page, not a queue: work goes to `docs/outstanding.md` or `docs/needs-gameplay.md` by hand,
in the commit that exposed it.
