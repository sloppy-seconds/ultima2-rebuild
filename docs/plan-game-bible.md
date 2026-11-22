# Epic: the bible — a generated, graded, self-correcting reference to the game

**Status: proposal, not built. Not a live queue.** Depends on
`docs/plan-code-index.md`; see § 9 for the ordering argument.

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

Behavioural rules that cannot be mechanically derived are authored **in `asm/` headers**, where
the evidence lives and where corrections land, and extracted into the fact store. Not authored
in the fact store. That is the difference between a generated view and a second copy.

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

Plus a fourth, because true-and-empty compiles: **vacuity.** A sentence that cites facts and
says nothing about them is rejected.

Prefer a different model family for the judge where available. The judge's output is per
sentence with a reason, never a page-level verdict.

**The judge must be seen red before it is trusted**, per this repo's standing rule about
gates. Three controls ship with it and run in CI: a deliberately false sentence, a
grade-laundered one, and an unsourced-but-true one. All three must fail. A judge that cannot
fail is worse than no judge.

---

## 6. Rendering

Static site, generated, no server, regeneratable in CI with a freshness gate. Two skins over
one fact store:

* **`bible/`** — the game. Game systems, places, planets, creatures, items, spells, time and
  gates. Grades visible inline; clicking a sentence shows its evidence files.
* **`internals/`** — our build. asm ↔ C, classic vs enhanced, coverage numbers, what is still
  inferred. Contributor-facing, and kept out of the player's book so our caveats do not
  pollute prose someone reads to learn the game.

Enhanced mode can only ever be rendered as `derived from X, diverges at Y`. An unqualified
enhanced row would claim evidence that does not exist.

**The most valuable page is free: "where we disagree with the guide."** We already vendor their
table with a fetch date, so diffing our derived facts against theirs in both directions is a
join, and it turns a reference into a control.

---

## 7. The text and licence line, which needs ruling

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
* **Decide what game text the public site may contain.** See D6.

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
| **"charisma doesn't have any other effect"** | **not establishable without the code index** |

That last row is the argument for the ordering. "Charisma has no other effect" is a *negative*
claim, and observation can never establish it — no amount of playing proves absence. A
data-flow index answers it in one query: every routine that reads the charisma byte. Their
guide's most actionable advice ("keep charisma below 66") rests entirely on a claim their
method cannot verify and ours can. That is the pitch for both epics in one line.

The spike is: generate that one page end to end — facts, closed-book draft, judged, rendered —
with every sentence anchored, and let it be wrong in public on one page rather than right in
principle on none.

---

## 9. Ordering across both epics

The index epic is a prerequisite, but only two of its five indexes gate the bible:

| step | what | why here |
| --- | --- | --- |
| A | `dataflow` + `engine-map` + `tools/lookup` | the substrate, and the spike needs exactly this |
| B | fact schema + extraction + **Statistics spike** | proves the whole loop for one page |
| C | `coverage`, `xref`, census folded into `lookup` | finishes the index epic |
| D | full bible pages, `internals/` skin, disagreement page | volume work, only after B proves it |

The argument against finishing the index first: the spike will tell us what the index is
missing, and it costs a page rather than a quarter. The Statistics page needs a charisma
data-flow query, so step A is genuinely upstream — but nothing in B needs `xref`.

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

---

## 11. Decisions to settle

**D1 — Where do facts live?** Recommend: generated `disasm/facts/` (YAML, committed), with
behavioural rules authored in `asm/` headers and extracted. Alternative: a database file,
which is less reviewable and diffs worse.

**D2 — Anchor syntax.** Recommend the two-field YAML above (`text` + `facts[]`), one entry per
sentence. Alternatives: inline markers in markdown (nicer to read, easier to write unanchored
prose by accident).

**D3 — Same model for writer and judge?** Recommend: different families where available, and
the controls in § 5 run against whichever pair is chosen so the choice is evidenced rather than
assumed.

**D4 — What happens to a sentence that fails the judge?** Recommend: it is dropped and the
failure is listed on the page under "could not be supported". Hiding judge failures would make
the site look more certain than it is.

**D5 — Do we publish at all, and if so what game text?** Recommend: build the generator first,
decide publication later, and default to no verbatim game strings in the public skin until
ruled. The internal site is as useful for our purposes and has no exposure.

**D6 — Does the public skin quote game strings?** Open, needs the owner. Interacts with the
existing committed-text ruling, which was about reconstruction files, not a website.

**D7 — Third-party names in a public site.** Recommend: keep `name_source` visible in the
rendered output, not just the data, so attribution survives into what a reader sees.

**D8 — How many pages, and which first?** Recommend: Statistics (spike), then planets and
places (census already covers them), then creatures, then items and spells, then time and
gates. Ordered by how much of the fact base already exists.

**D9 — Does the bible cite captures?** Recommend: yes, and a claim about visible behaviour
must name its frame, per the existing "a behavioural claim needs a frame" rule. Frames are
gitignored, so the citation names the capture directory rather than embedding the image.

**D10 — Regeneration cost.** A full rebuild drafts every page through two models. Recommend
incremental: only pages whose facts changed get re-drafted, and the freshness gate checks
staleness rather than rebuilding everything on every commit.
