# The Record Index — Plan (third epic)

**Status: VERDICT NOT RATIFIED, 2026-11-25. The 2026-11-24 cut was measured with a broken
instrument and is withdrawn.** § 7a reported 12 attributable entities against the pre-registered
kill threshold of 25. Two defects made that number too low: items were queried at the item array's
base address instead of their per-slot absolute addresses, and the footprint index could not see
memory access written as forced-keep `.byte` lines (142 such accesses, now decoded -- see
`tools/index_footprint.py`). Re-measured in § 7b: **25 entities, exactly on the threshold**, and
the count moves 24/25/26 depending on which creature rule is used, so the threshold is coarser
than the decision it was set to make and cannot decide it. What survives unchanged is the shape:
creatures and items work (25 entities), spells/NPCs/gates are structurally unreachable. That is a
two-kind index, not the five-kind index G10 was written to approve or refuse, and that is the
question for the owner. S0b (the column contract) landed and stands on its own merits.

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

Three new generated tables, one writer each, committed, freshness-gated. **Format is ruled: TSV,
per `docs/adr/0005-generated-tables-stay-tsv-jsonl-for-nested-rows.md`** — the split is by row
shape, not by age, so a table goes JSONL only if its rows are genuinely nested and ships a sidecar
for the do-not-hand-edit marker and column contract that a TSV header carries inline. Of the three
below, `records` and `entity-code` are flat by construction (an edge is a row, and a multi-citation
edge is several rows, which is also what makes citations diffable). `entities` is the one with a
real case for nesting, because entity rows want alias lists and several evidence citations; if it
goes JSONL it must ship its sidecar in the same commit.

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

**Who writes what (ruled 2026-11-23).** `census.py` mints the entity ids, because it is already
the entity authority, already owns the ten tables, and already carries `--verify` and
`--check-fresh`. A second tool emitting `entities.tsv` would have been a second writer over the
same facts — the exact drift ADR 0002 exists to stop. The new tool, `tools/entity_index.py`,
writes only `records.tsv` and `entity-code.tsv` and references ids it did not create. Both tools
follow `census.py`'s discipline: invent nothing, name a source on every row, and let `--verify`
assert the invariants while `--check-fresh` proves the committed copy is current.

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
7. **Three name grades, ruled 2026-11-23, because public is now the goal.** Attribution is not a
   licence, and `creature.tsv`'s names come from the Ultima Codex and the vendored Gamer Corner
   table. **(1)** names the binary itself contains — Origin's copyright, gated behind the bible
   plan's game-text ruling; **(2)** generic words nobody owns (`GUARD`, `HORSE`, or a name we chose);
   **(3)** third-party wiki names, **internal-only until someone establishes the source's licence**.
   A public site therefore ships with fewer proper nouns than the internal one, and that is an
   honest outcome rather than a defect. `name_source` and `grade` stay visible in any render, as the
   bible plan's D7 already requires.

---

## 7. Slices, in order, each with its gate

S0 exists because a plan built on assumed edges is worth less than an afternoon.

| # | work | gate |
| --- | --- | --- |
| ~~**S-1**~~ | **DONE 2026-11-23.** `bestiary.tsv` carried byte-identical duplicate rows and `census.py` dropped the columns that distinguished its art-matching rows, so `creature.tsv` had **no unique key**: 120 rows, 16 distinct `art` values, no combination of up to three columns unique, `source` and `grade` constant across every row. Now: `creature.tsv` is 15 rows keyed by `art`, `art-variant.tsv` is 118 rows keyed by (name, section, source_variant, variant, image), the generator collapses duplicates and refuses to write an unkeyable table, and `census.py --selftest` asserts uniqueness for all eleven tables. Full finding and two extra defects it exposed: `docs/bestiary.md` § 8. | done — `census --selftest` (6 controls) and `census --verify` both green, wired into `check_all` |
| **S0** | **DONE 2026-11-24, spike only, no store written. Result: 12 named entities with at least one `measured` edge, against the pre-registered threshold of 25 — see § 7a below for the numbers, the method, and the three things the plan got wrong.** Join `records` × `footprint` for creature, item and spell spans; join sector/world immediates. Report how many entities get at least one code edge, by leg and grade. No store is written. | the spike's own numbers, written into this doc. **Kill threshold pre-registered 2026-11-23: fewer than 25 distinct entities of the v1 kinds (Q11) with at least one `measured` edge, and this epic is cut to bible slices** — the number is fixed before the measurement so the measurement can actually change the plan. **Breached: 12 < 25.** |
| **S0b** | **First increment landed 2026-11-24, and it survived the cut precisely because it was never part of the bargain.** `tools/table_contract.py` declares the base for **217 columns across 21 tables** and enforces two rules: a numeric-looking column of a covered table must be declared, and every declared value must satisfy its declaration. Two more traps of the same shape turned up while writing it — `engine-map.tsv`'s `asm_addr` is decimal (256 is 0x100) and `census/sector.tsv`'s `sector` holds `00`,`03`,`10` as decimal, so `sector=10` means ten, not sixteen. **21 tables remain uncovered** (`function-map.tsv`, the bytecheck pair, and others) and are reported as uncovered rather than silently passing. Full finding in the commit. Every generated table declares each column's type and base, and `--verify` asserts every value against it. The motivating case is real and recent: `footprint.tsv`'s offset column holds **decimal**, and filtering it as hex returns zero rows, which reads as absence of evidence rather than a bad query. | a control per declared column: feed a value of the wrong type or base and the gate goes red; a table with no contract is a `--verify` failure, not a warning. **13 controls, five of them driving `verify()` end to end against a throwaway tree, wired into `check_all`** |
| S1 | `records.tsv` — the record-address model, with stride, count and file provenance | `--verify` asserts every span is inside the DS block, strides tile without overlap, and every row names an evidence site; `--check-fresh` |
| S2 | `entities.tsv` — census joined under stable ids | `--verify` asserts every census row appears exactly once, no id is minted for a row census does not have, and `name_source` is never blank where census had one |
| S3 | `entity-code.tsv` from `record-touch` and `immediate` legs | a control per leg: a known-true edge must appear, and a thing with no record must get no edge — plus the negative-edge grade ceiling enforced in code, not in prose |
| S4 | `c-citation` leg: extend the `engine-map` discipline so C code can cite an entity, not only an address | `xref_provenance --c` extended; a citation that resolves to nothing fails the build |
| S5 | facts keyed to entity ids; one bible page becomes a query; **retire `docs/status-screen-z.md`** as the proof | the retired doc's claims must all be present in the generated page, checked by a script that diffs claim-for-claim, not by reading |
| S6 | docs, `docs/outstanding.md`, the issue, and a line in `docs/harness-portability.md` § 6 for whatever generic lesson S0 teaches | `tools/doc_command_gate.py` |

### 7a. S0 result, 2026-11-24: the threshold was breached

> **CORRECTION, 2026-11-25. The number below is wrong and finding 2 is refuted.** The
> measurement was taken against a footprint index that could not see memory access written as
> raw bytes, and it looked at the item array's base address instead of its slots. Re-measured
> after `tools/index_footprint.py` learned to decode forced-keep `.byte` lines: **25 named
> entities, not 12**, and items are the 13 of them. Finding 2 below is false -- items were
> always attributable. See § 7b. The G10 verdict is therefore **not ratified**: a threshold
> breached by a broken instrument is not a measurement, and the re-measured number sits exactly
> on the line it was supposed to decide, which means the line cannot decide it either.

**12 distinct named entities of the v1 kinds carry at least one `measured` code edge. The
pre-registered kill threshold is 25. Per ruling G10 the epic is therefore cut to bible
slices.** The number was fixed before the measurement precisely so that this sentence could
be written without renegotiating anything, and it is written.

The measurement, and the three ways this plan was wrong about its own inputs:

**1. No same-instruction form exists at these bases.** The plan's § 3 said creature
attribution is derivable "because the base is the displacement: `mov 0x137(%di),%al`". That is
true of *which field* a routine touches and false of *which entity*. Across all 193 footprint
rows at the ten DS bases, exactly **one** instruction carried an immediate
(`cmpb $0x4,0x4a` in `asm/3611_monster_damage_apply.asm`). This codebase always mediates
through a register, in both directions: `mov $0x2c,%al` then `mov %al,0x197(%di)` to write, and
`mov 0x197(%di),%al` then `cmp $0x34,%al` to test. `grep` for the same-instruction form
`cmp $LIT,0x197(%di)` returns **zero across the whole tree**. An index built on the assumption
that a literal sits in the instruction that touches the base would have reported nothing at all
and looked like an absence of evidence — which is the § 3 failure mode this plan warned about
for `footprint`'s decimal column, arriving in a new shape.

So the spike added a bounded slice: walk back from a store to the load that filled the register,
and forward from a load to the compare that consumed it, giving up if the register is written by
anything unnamed in between. Conservative by construction — it can under-report, it cannot
invent. That resolved **23 of 193** rows on the backward leg and **17 of 193** on the forward
leg.

**2. ~~The item and spell legs are structurally empty, not under-mined.~~ REFUTED 2026-11-25,
see § 7b.** The claim below was produced by looking at `0xD6`, the base of the item array, and
finding only relative indexing there. It is false. Each item has its **own absolute address**
(`DS:D7` is the wand, `DS:E5` is tri-lithium) and the code touches those directly; the engine's
own `overworld.h` documents seven of them with the asm file each came from, which is the
"look it up before you derive" rule broken inside the session that was supposed to be measuring
carefully. The spell half survives: `0xB6..0xBF` really does yield nothing, and it still yields
nothing after the index was repaired, which makes it a stronger claim than it was.

Every one of the six
`0xD6` (items held) and six `0xB6` (spell counts) sites addresses through a computed `%di`
(`mov 0xd6(%di),%al`), and no literal anywhere in the tree names which item or which spell a
site means. The slot index is a loop counter or a cursor, never an immediate. This was checked
by direct `grep` and not inferred from the tool returning nothing, because a tool returning
nothing is exactly what a broken tool looks like. **An item or spell page therefore cannot be
derived from code at all** — it has to come from the content files, which is the census's job
and already done.

**3. Two of the five v1 kinds were never reachable from `footprint`.** `npc` lives in `tlkxNN`
record rows and `gate` in `mapx` marker tiles; neither has a DS address, so no footprint row
can touch either. Q11's five-kind list treated them as if they were the same kind of thing as
a creature slot. They are not, and no amount of slicing changes that.

What did come out, per leg:

| leg | named entities with ≥1 `measured` edge | note |
| --- | --- | --- |
| creature | **12** — Balron, Daemon, Devil, Fighter, Frigate, Guard, King, Minax, Orc, Sea Monster, Thief, Wizard | literals at `mon_type` only; mapping is `art = type >> 2` |
| item | **0** | no literal names an item, at any site, anywhere |
| spell | **0** | same |
| npc | **0** | not addressable from DS at all |
| gate | **0** | not addressable from DS at all |
| **counted total** | **12** | threshold 25 |

Reported but deliberately **not** counted, because `world` and `sector` are not among Q11's five
v1 kinds: the **world leg works completely** — the absolute form `mov 0x6d,%al` then
`cmp $0x9,%al` yields all ten worlds including Planet X, which is how `asm/4223_planet_x_gate`
was named in the first place. The sector leg yielded literals `0x00`–`0x05` and `0x7C` against
`DS:4A`, which are era/world digits rather than sector identities, so it names nothing.

**The honest reading.** Twelve creatures with a real code edge is a true and useful fact, and it
is also the whole of what the code can say about entities: the engine already knows those twelve
from the spawn arms and the special-attack dispatch, and both are decoded. The record index
would have spent S1 through S4 building a store to re-derive what four `asm/` files already
state inline. That is what the threshold was for.

**What survives the cut.** S0b (the column contract) is not part of the bargain — it is a
standing ADR 0005 obligation on every generated table including the ones already shipped, and
`footprint.tsv`'s decimal column is a live trap for the next reader. S1–S4 are cut. S5 was the
proof of value and it cannot be built on a 12-entity store.

For the record, the reason S0 came first: **a bible page that can retire a hand-maintained doc
was the only evidence that this epic earned its existence.** S0 found that the evidence was not
there, which is the outcome the threshold existed to allow.

---

### 7b. Re-measured, 2026-11-25: the instrument was broken, and the number now sits on the line

Two independent defects made the § 7a count too low. Both were found by a question from the owner,
not by the spike auditing itself, which is the part of this worth recording.

**Defect 1 -- items were read at the wrong address.** The spike queried `0xD6`, the base of the
item array, found only register-relative access there, and concluded no literal names an item.
But `items_held` is 16 slots at absolute addresses and the code touches each slot directly:
`DS:D7` is the wand, `DS:DD` the ankh, `DS:E5` tri-lithium. `engine/src/game/overworld.h`
documents seven of them with the asm file each was derived from, so the answer was in the tree
and was not looked up before deriving.

**Defect 2 -- the footprint index could not see access written as raw bytes.** A forced-keep
`.byte` line holds a real instruction that gas re-encodes differently, with the instruction named
only in a comment. The index parses mnemonics, so it recorded nothing, and its self-accounting
could not flag it because a `.byte` is not instruction-shaped and so was never counted as a line
it failed to parse. **142 memory accesses were invisible**, including three creature-HP sites and
five spell-count sites. `tools/index_footprint.py` now decodes them from the bytes (cross-checked
60-for-60 against objdump, with 17 committed controls) and refuses cross-file fragments; the
index went 2079 rows to 2299, pure insertion.

**Re-measured:**

| leg | § 7a | re-measured | note |
| --- | --- | --- | --- |
| creature | 12 | **12** | unchanged; the strict rule is a type literal in a file that reads `mon_type` |
| item | 0 | **13** | per-slot absolute addresses; RING 10 sites, TRI-LITHIUM 9, HELM 5 |
| spell | 0 | **0** | still nothing, and now nothing in a repaired index |
| npc, gate | 0 | **0** | no DS address; unchanged and not fixable this way |
| **total** | **12** | **25** | threshold is 25 |

**Why the threshold no longer decides anything.** The re-measured total lands exactly on the line,
and it moves by ±2 depending on which creature attribution rule is used: literal-anywhere gives
26 (and is wrong, because `$0x20` and `$0x14` are just the numbers 32 and 20 and appear all over
the tree in files that have nothing to do with monsters), strict-file-co-occurrence gives 25, and
the original spike's narrower rule gives 24. A pre-registered threshold is a promise to lose an
argument, and that promise is only worth keeping if the measurement resolves finer than the
decision boundary. This one does not. So the honest report is not "met" or "not met" but **the
instrument is coarser than the threshold**, and the decision has to be made on the merits.

**What survives the correction unchanged.** The shape of the answer, which is what the threshold
was really testing. Three of the five v1 kinds are not address-shaped things and no repair reaches
them: spells have no literal naming a slot even now, and NPCs and gates have no DS address at all.
What works is creatures and items, 25 entities between them. That is a two-kind index, not the
five-kind index G10 was written to approve or refuse.

## 8. Decisions — all ruled 2026-11-23

| # | decision | recommendation |
| --- | --- | --- |
| ~~**D1**~~ | ~~Store format~~ **RULED: TSV, JSONL only for nested rows** — `docs/adr/0005-...md` | done |
| ~~**D2**~~ | ~~Does ADR 0004 stand — prose generator-only, no hand-written MDX?~~ | **RULED: it stands, amended.** Quirks become facts via an observation-mining pass, and voice gets a separate visibly-ungraded editorial lane. Recorded as D9/D10/D11 of `docs/plan-game-bible.md` and as an amendment to `docs/adr/0004-prose-is-a-generated-view.md`. The draft's hand-written MDX layer is still rejected — it is the drift this project has been eliminating all year |
| ~~**D3**~~ | **RULED — entity id scheme:** | Namespaced **composite** natural keys, corrected after measurement: `sector:<layer>:<n>` works because `(layer, sector)` is unique across all 54 rows; the bare sector number does not (33 distinct across 54). `tile` is keyable on `art`, `gate` on `(era, gate_index)`, `npc` on `(layer, sector, record_index)`, `vehicle` and `vendor` need the full four-column cell key. **`creature` is not keyable at all until S-1 lands.** Namespaced because census has no id column today |
| **D4** | ~~Is any public site ever in scope?~~ **RULED 2026-11-23: yes, public is the goal** — with the owner's condition that copyright is handled carefully when we get there. That *raises* the bar rather than lowering it: the wall has to be built now, not at publishing time, because retrofitting a leak is not possible. `--public` still refuses until the text ruling (D6 of the bible plan) lands, and no extracted dialogue or art may ever enter a committed generated table. | done — see § 6, and the open follow-on below |
| ~~**Q14**~~ | ~~Third-party names are not ours to publish either.~~ | **RULED — the three name grades are in § 6 item 7.** Attribution is not a licence: binary names are gated behind the game-text ruling, generic words are free, and third-party wiki names stay internal-only until someone establishes the source's licence. A public site ships with fewer proper nouns than the internal one, which is an honest outcome rather than a defect |
| ~~**D5**~~ | ~~How far to go on map-cell attribution, given § 3's limit~~ | **RULED: fence it.** Attribute sectors through the `DS:49`/`4A`/`6D` immediates a routine already tests, and record the limit in the table's own notes. Chasing address computations is a dataflow problem this index deliberately is not |
| **G1** | Epic, or bible slices? | **Ruled: own epic, conditionally.** If S0 falls below its pre-registered threshold, it is cut to bible slices and this file says so. The condition is the point — the answer is not settled by preference but by the measurement |
| **G6** | Composite or surrogate ids? | **Ruled: composite**, with a gate that refuses to mint an id when a disambiguator is blank. Meaningless ids that never break are worse than verbose ones that fail loudly |
| **G7** | Who mints the ids? | **Ruled: `census.py`**, because it already owns the ten tables and their gates. `entity_index.py` writes only `records` and `entity-code` and references ids it did not create (§ 4) |
| **G8** | Where do record strides come from? | **Ruled: asm load site = `measured`; agreement with our own C parser = `inferred`, and the row names which.** The engine is our code and can be wrong in exactly the way the asm proves it isn't |
| **G9** | Grade inheritance for an edge | **Ruled: the edge inherits the weakest grade in its chain, and the row names which leg was weakest.** Showing `measured` beside a claim resting on an inferred span is grade laundering, which the bible's own critic already caught once |
| **G10** | S0's kill threshold | **Ruled in advance: fewer than 25 distinct entities of the v1 kinds with at least one `measured` edge cuts the epic to bible slices.** Set before the measurement so the measurement can change the plan |
| **G11** | Which entity kinds in v1? | **Ruled: creature, item, spell, npc, gate.** Sector and place via immediates, explicitly `inferred`. Tile and vendor deferred — tile touches the cleanroom wall, vendor is derived from tile |
| **G12** | Commit `entity-code.tsv` or generate on demand? | **Ruled: committed, with `--check-fresh`,** like every other generated table. An edge nobody can diff is an edge nobody checks |
| **D6** | Is a dungeon orc the same entity as an overworld orc? **RULED 2026-11-23: yes — an orc is an orc, and stats are a function of context, not of the creature.** Owner's formulation, adopted over this plan's earlier `domain` column, which is rejected: a prefix or a domain column makes the *name* carry information that belongs in a key, and it only fits because two names happen to collide. The program has no shared identifier across the two encodings — different type byte, different art, different HP source — so "Orc" was always our entity; the ruling is that a stat column describes a **population**, keyed by how the creature arose, and never a property of the entity alone. **Context vocabulary is closed at three:** `placed` (in `monx`), `spawned` (the wandering-spawn arms), `dungeon` (a stamp). Nothing else produces a stat population. **This retroactively dissolves two disputes that were filed as data problems:** the sea serpent's `255 \| 64` (placed vs spawned, both overworld) and the Guard's row — the question "which HP is right" was malformed in the first case. Three conditions come with it, and they are the parts that stop the model from hiding errors: **(a) confidence is per creature, not per model** — Balron is the strong case because it carries the *same special in both domains* (overworld sleep, resist GREEN IDOL; dungeon `YOU FEEL A STRONG MAGIC!` writing `paral_sleep`), a shared behavioural signature, whereas Orc has no special in either domain and its pairing rests on name plus list position alone; **(b) source disagreement stays a separate column from population** — Guard's `255 \| 500` is two sources where one is probably wrong, not two true contexts, and merging them lets a real error hide behind a plausible label; **(c) a population's value may be a formula, not a constant** — dungeon HP is `((2*type + floor)*8) \| 0x10`, so Orc's dungeon population is `48–152` with `floor` named as the variable. **Schema consequence:** one entity with N populations is a nested row, but at five columns it ships as a second *flat* table `creature-populations.tsv` keyed `(entity, context)` carrying `value_or_formula, source, grade` — ADR 0005's JSONL clause stays unused, and `creature.tsv` keeps one row per creature with no fake single HP. Its `conflicts` column is empty for Orc and Balron today, not because there is no variation but because the dungeon population was never in that table's scope |

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
