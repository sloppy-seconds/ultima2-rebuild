# The Record Index — Plan (third epic)

**Status: planned, not built. Five decisions at § 8 need the owner before S1 starts.**

This is the join layer between the two finished epics. The code index produced four
generated tables about *addresses*; the census produced ten about *things*; the bible
produced three sentences because nothing connects them. The connection is the work here.

The owner's framing, 2026-11-23: *"what we're missing is the database the bible would be
constructed from — one that cross-references both the ASM and the C engines for each
thing ... and we'd need to cross reference with data files, maps, etc too."*

A third-party draft (Gemini, 2026-11-23) proposed a pipeline for this. It is useful as a
statement of the destination and wrong about most of the starting line, so § 1 corrects it
before anyone builds something that already exists.

---

## 1. What already exists — read this before building anything

The draft's premise was "instead of maintaining markdown tables by hand, which drift from
the code". **This repository has no hand-maintained index tables.** Every table below is
generated, has exactly one writer, and is freshness-gated (`--check-fresh` regenerates to a
temp dir and diffs against the committed copy).

| the draft called for | this repo already has | gap |
| --- | --- | --- |
| Layer A: code symbol crosswalk, asm ↔ C | `disasm/index/engine-map.tsv`, 1911 rows, queryable both directions (`--asm`, `--c`), with `xref_provenance --c` validating every C-side citation against real `asm/` ranges | none — and no tree-sitter or Clang AST is needed, because the C tree *cites* the asm it implements and a gate proves the citation resolves |
| Layer C: game entities with names and grades | `disasm/census/`, ten generated tables (`world sector place npc tile creature gate vehicle site vendor`), each row naming its source, with `name_source` and `grade` columns | no stable id column, and no link to code |
| Layer B: binary file formats | `tools/tlkx_decode.py`, `tools/monx_layout_scan.py`, `tools/map_render.py`, `tools/state_layout.py`, plus format prose spread across `docs/` | the format knowledge is in prose and in tools, not in one queryable table |
| Layer D: relational edges between things | nothing | **this is the actual gap** |
| "a normalised DB built at build time" | committed generated TSV under `disasm/index/` and `disasm/census/`, per ADR 0002 | see D1 |

Two factual corrections to the draft, both measured here:

- **Sector maps are 64 × 64, not 64 × 66.** `U2_SECTOR_EDGE` is 64, `U2_SECTOR_TILES` is
  4096, proven at `asm/50B3` (`shl $6`) and `asm/2290` (CX = 4096).
- **Tile art index and map byte are related by ×4** — `census/tile.tsv` opens
  `art 0x00 / map_byte 0x00`, `0x01 / 0x04`, `0x02 / 0x08`. The draft had the direction of
  that relationship right.
- The `tlkx` high-bit encoding the draft describes as "ASCII rotated by +128" is real and
  already decoded by `tools/tlkx_decode.py`; a `tlkxNN` record's **position is its
  identity**, which is a fact about the game, not an implementation detail.

---

## 2. The actual gap, named

The four-way join the owner wants is:

> **thing ↔ data record ↔ asm routine ↔ C symbol**

Three legs exist. The missing ones are:

1. **Entity ids.** Census rows have natural keys (`creature` by art index, `sector` by
   number + world + era, `npc` by sector + record index, `gate` by era + index, `tile` by
   art) but no id column, so nothing can reference a thing. This is why `# fact:` lines
   namespace their entity tokens by hand.
2. **A record-address model.** `state.tsv` names the bases — `mon_x` at `DS:137`,
   `mon_hp` at `DS:177`, `map_base` at `DS:472`, `items_held` at `DS:D6` — but not the
   *stride*, the *count*, or *which file and offset the bytes came from*. Without that, a
   footprint row is an address, not a statement about a thing.
3. **Entity ↔ code edges**, derived from the two above.

**Why the bible has three facts, in one line:** facts are keyed by *file* (`asm:20ab`),
pages are keyed by *thing*. Every page must therefore be hand-assembled from a hand-picked
file, so volume cannot become mechanical. This epic is what makes a bible page a query.

---

## 3. The measured limit, which decides the design

Data files load into the same DS block `footprint` already indexes, so a lot is derivable.
Re-measured for this plan (`footprint`'s offset column is **decimal** — filtering it on hex
returns zero rows and looks like an absence of evidence; that trap cost a wrong claim in
this session and is written here so it is not repeated):

| DS base | what lives there | footprint rows |
| --- | --- | --- |
| `137` `157` `177` `197` `1D7` | creature x / y / hp / kind / role | 25 / 23 / 20 / 32 / 9 |
| `D6` | items held | 9 |
| `B6` | spell counts | 5 |
| `472` | `map_base` | 5 |

Creature slots are attributable because **the base is the displacement**: `mov
0x137(%di),%al`. Map cells are **not**, because the asm loads the base into a register and
indexes off it — `add 0x472,%bx` then a subscript — so no displacement survives to be
indexed. Recovering cell-level attribution means following an address computation, which
is a dataflow problem this index deliberately is not.

Consequences, stated as ceilings rather than hopes:

- **creature, item, spell, npc, gate attribution: derivable and gradeable `measured`.**
- **sector / map-cell attribution: not derivable from footprint.** Attribute it instead
  through what a routine *knows* — the sector bytes `DS:49`/`DS:4A` and the world byte
  `DS:6D`. That is how `asm/4223_planet_x_gate` was identified: it compares the world byte
  to 9, and 9 is Planet X.
- **A negative edge may never be graded above `inferred`** (standing ruling). "No routine
  touches this thing" is a claim about an index's coverage, not about the game.

---

## 4. Proposed store

Three new generated tables, one writer each, committed, freshness-gated. TSV, per ADR 0002
— see D1.

**`disasm/index/records.tsv`** — the record-address model. One row per DS span that holds
loaded file data.

| column | meaning |
| --- | --- |
| `ds_base` | base offset |
| `stride` | bytes per record |
| `count` | records in the span |
| `entity_kind` | what a record *is* (`creature`, `item`, `spell`, ...) |
| `source_file` | `monxNN`, `mapxNN`, the exe, or `—` for runtime-only |
| `source_offset` | where in the file, or the rule that computes it |
| `evidence` | the asm or C site that proves the span, or the tool that measured it |
| `grade` | `observed` / `measured` / `inferred` |

**`disasm/index/entities.tsv`** — one row per thing, census joined to an id.

| column | meaning |
| --- | --- |
| `id` | namespaced natural key, see D3 |
| `kind` | `creature` `sector` `place` `npc` `gate` `tile` `vehicle` `item` `spell` |
| `name`, `name_source`, `grade` | carried from census unchanged, attribution intact |
| `record_ref` | FK into `records.tsv` |
| `census_row` | which census table and row it came from |

**`disasm/index/entity-code.tsv`** — the edges, one row per (entity, code site) pair.

| column | meaning |
| --- | --- |
| `entity_id` | FK |
| `asm_file` | the routine |
| `c_symbol` | via `engine-map`, or `—` |
| `leg` | how the edge was derived: `record-touch`, `immediate`, `c-citation` |
| `grade` | per leg, per § 3 ceilings |

`tools/entity_index.py` would be the single writer for all three, extending `census.py`'s
discipline: it invents nothing, every row names its source, and `--verify` asserts the
invariants while `--check-fresh` proves the committed copy is current.

---

## 5. Not adopted from the third-party draft, and why

These are rejections with reasons, so nobody proposes them again in a session that has not
read this page.

- **SQLite / DuckDB / JSON graph.** At 19k rows the joins are a Python dictionary, and the
  store's whole job is to be diffable in a commit and greppable by an agent. A binary DB
  file is not reviewable in a diff and would be a second representation of facts already in
  TSV. Ruled as D1 so it stays a decision and not a drift.
- **Tree-sitter / Clang AST extraction.** The C tree already cites the asm it implements and
  `xref_provenance --c` fails the build when a citation does not resolve. Parsing the C
  again would produce a second, ungated answer to a question that already has a gated one.
- **`asm_snippet` and `c_source_snippet` columns.** Copying bodies into a store creates a
  second hand-drift-prone copy of a CP0-frozen surface, and asm bodies contain game text
  (§ 6). The store holds *references* — file, line, symbol — and a reader opens the file.
- **Human-written `editorial_prose` (MDX).** This directly contradicts ADR 0004: prose is a
  generated view and humans do not edit it. The draft's instinct that pages need editorial
  voice is right; the answer is more facts and a working judge, not a second hand-maintained
  store. See D2.
- **Public web features** — auto-linker, hover cards, hex inspector, live map atlas. The
  renderer is optional by ruling, `site/` is gitignored, and `bible_render --public`
  refuses until the owner rules on publishing game text. See D4 and § 6.

---

## 6. Copyright and licensing — hard rules

The owner raised this explicitly, and the draft would have walked into it: a browser tool
that decodes `tlkxNN` dialogue and shows slack bytes is a text-republication machine.

1. **Game text and art are not ours.** Text is committed only where it is load-bearing for
   the byte-exact reconstruction (`asm/`), per the ruling of 2026-10-28. That permission
   does not extend to a *second* copy in a generated store, and a copy in a public site is
   a different act again.
2. **No extracted dialogue, place text, or art in any committed generated table.** Entity
   rows may carry a *name* only where census already carries one, with its `name_source`
   and `grade` intact.
3. **Third-party names stay attributed and stay out of `asm/`.** `bestiary/` names come from
   the Ultima Codex and the Gamer Corner table, vendored with URL and fetch date. Bytes
   decide identity; the wiki decides names; a wiki name is never promoted into an `asm/`
   filename.
4. **Any inspector that reads the player's own files is local-only, forever.** A hex or
   dialogue viewer may run against a player-supplied directory on their machine. It may not
   ship extracted content, and it may not be part of anything public while D4 stands.
5. **Art and sound stay behind the cleanroom wall.** Substitute or generated art may not
   cite `asm/`, and `docs/tile-art-spec.md` remains the only document a redraw artist may
   read.
6. **No GPL or AGPL-derived data in committed tables.** Apache-2.0 compatibility is enforced
   by `.gitignore` and is not to be worked around.

---

## 7. Slices, in order, each with its gate

S0 exists because a plan built on assumed edges is worth less than an afternoon.

| # | work | gate |
| --- | --- | --- |
| **S0** | **Spike: do the derivable legs actually yield edges?** Join `records` × `footprint` for creature, item and spell spans; join sector/world immediates. Report how many entities get at least one code edge, by leg and grade. No store is written. | the spike's own numbers, written into this doc; if creature attribution yields fewer than a hand-picked dozen edges the design is wrong and says so here first |
| S1 | `records.tsv` — the record-address model, with stride, count and file provenance | `--verify` asserts every span is inside the DS block, strides tile without overlap, and every row names an evidence site; `--check-fresh` |
| S2 | `entities.tsv` — census joined under stable ids | `--verify` asserts every census row appears exactly once, no id is minted for a row census does not have, and `name_source` is never blank where census had one |
| S3 | `entity-code.tsv` from `record-touch` and `immediate` legs | a control per leg: a known-true edge must appear, and a thing with no record must get no edge — plus the negative-edge grade ceiling enforced in code, not in prose |
| S4 | `c-citation` leg: extend the `engine-map` discipline so C code can cite an entity, not only an address | `xref_provenance --c` extended; a citation that resolves to nothing fails the build |
| S5 | facts keyed to entity ids; one bible page becomes a query; **retire `docs/status-screen-z.md`** as the proof | the retired doc's claims must all be present in the generated page, checked by a script that diffs claim-for-claim, not by reading |
| S6 | docs, `docs/outstanding.md`, the issue, and a line in `docs/harness-portability.md` § 6 for whatever generic lesson S0 teaches | `tools/doc_command_gate.py` |

S5 is the whole point and the reason S0 comes first: **a bible page that can retire a
hand-maintained doc is the only evidence that this epic earned its existence.**

---

## 8. Decisions needed from the owner

| # | decision | recommendation |
| --- | --- | --- |
| **D1** | Store format: committed generated TSV, or SQLite/DuckDB? | **TSV.** Diffable, greppable by an agent, matches ADR 0002, and 19k rows is a dictionary lookup |
| **D2** | Does ADR 0004 stand — prose generator-only, no hand-written MDX? | **Yes, keep it.** The draft's human-prose layer is the drift this project has been eliminating all year |
| **D3** | Entity id scheme | Namespaced natural keys: `creature:0x0c`, `sector:21`, `npc:21:3`, `gate:3:2`, `tile:0x1f`, `item:helm`. Natural because the game addresses things by index; namespaced because census has no id column today |
| **D4** | Is any public site ever in scope, and if so what may it show? | **Not in this epic.** Keep `--public` refusing; revisit only with an explicit ruling on game text |
| **D5** | How far to go on map-cell attribution, given § 3's limit | **Don't chase it.** Attribute sectors through `DS:49`/`4A`/`6D` immediates and record the limit in the table's own notes |

---

## 9. Risks

- **The index becomes a fifth hand-maintained truth.** It will, the moment a generator stops
  being its only writer. Every table here ships with `--check-fresh` or it does not ship.
- **A plausible wrong edge is worse than a missing one.** This project has been bitten
  repeatedly by tools returning confident wrong answers. Hence the per-leg grade, the
  negative-edge ceiling, and a control per leg that is written before the edge is trusted.
- **Attribution effort concentrates where the code is least legible.** Creature and item
  spans are clean; sectors and places are not. The honest output is a table with holes and
  a grade in every cell, not a filled table built on inference.
- **Volume tempts before the judge works.** The bible's three judge controls all failed on
  their intended question. S5 must not start until those pass, or the epic manufactures
  fluent wrong prose at scale.
