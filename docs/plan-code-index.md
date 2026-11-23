# Epic: the code index — a queryable map from bytes to behaviour

**Status: proposal, not built. Not a live queue.** The live queues remain
`docs/outstanding.md` and `docs/needs-gameplay.md`.

Opened 2026-11-23 after a session that made four corrections to decode that already existed
in `asm/`, and found the answer to a fifth question with a grep that should have been a
lookup. It was written to be argued with, and it was: § 7 is now the record of rulings from a
decision review on 2026-11-23, and nothing in it is open. Three terms the review settled are in
`GLOSSARY.md` (**Footprint**, **Coverage grade**, **Behavioural core**, plus the naming/access
split on **DS mirror**), and the two decisions a later reader would otherwise wonder at are
ADRs: `docs/adr/0001-asm-owns-access-the-mirror-owns-names.md` and
`docs/adr/0002-committed-generated-index-tables.md`.

---

## 1. The problem, with numbers

`asm/` holds 1212 files. **1058 of them carry `confidence: low`** — measured, not estimated.
So 87% of the binary is *segmented* (boundaries proven, bytes exact) and *not understood*.
That is not a failure of Phase 3; Phase 3's contract was byte-exactness and it delivered it.
It is the state a pull model produces: Phase 4 decodes a function when a feature needs it, so
semantic depth equals the depth of the last feature built, and nothing records where that is.

**Corrected during the review, and the correction matters.** The 1058 counts the `confidence:`
stamp, and that stamp was assigned in bulk by `tools/clean_wave.py`, which writes
`# confidence: {conf} (bytes proven by check_func.py)`. So the 87% measures a default a tool
wrote, not a fact about understanding. `coverage` therefore derives its own grade from
evidence-bearing header fields and treats `confidence:` as an input column, never as the
headline (§ 3.3). Measured against the headers instead: 271 files are named, 93 of those still
carry `confidence: low`, 109 carry a `provenance: live:` line, and 67 carry a
`body corrected:` marker.

The consequence is that returning to the original is usually not re-decoding. The four
corrections in one session:

| what was believed | how it died | class |
| --- | --- | --- |
| the star field staggers against a counter at `DS:4C3` | `0x4c3` is one star's own `old_x` in a scratch byte | misread a fragment |
| nothing decrements the sweep | it decrements in the `H` arm, past a `ret` | incomplete read |
| the ship sprite is a 6-pixel vertical stroke | the header said so; the body makes 7 calls | trusted prose over body |
| the sweep's descend needs a breakpoint to explain | `grep -rn 0x4c7 asm/` | missing lookup |

Three of four were questions about files already in the tree. All four were **data-flow or
completeness** questions, and the tree has no index for either.

The behavioural cause is not carelessness. Grepping one address takes ten seconds; reading a
506-line function takes ten minutes. (Measured: the median `asm/` file is 11 lines and the mean
31, so the slow-read cost is a long tail, not the norm. What is actually expensive is *finding*
which of 1212 files matters — the same problem, and the one the index removes.) The fast path and the correct path diverge, and nothing
made the fast path correct. **The goal of this epic is to make the fast path correct.**

---

## 2. Non-goals, and they are load-bearing

* **The index is never where a fact lives.** It is a generated view over sources that stay
  authoritative. The moment a human can type a conclusion into it, it becomes a second
  hand-maintained copy, which is the failure mode `AGENTS.md` exists to prevent. G90's
  "nothing re-seeds" and the sprite's `-3..+2` were both conclusions duplicated outside their
  source, and both then cited as authority.
* **It is not a decompiler output, a pseudocode layer, or a symbol table.** A translation
  layer would be a third copy of the binary. The reconstruction *is* the specification.
* **It does not replace `asm/` headers.** Headers stay the record of evidence, corrections and
  confidence. The index points at them.
* **It does not hold engine design.** `docs/phase4-state.md` and the `contract-*.md` specs own
  intent. The index reports what is, mechanically.
* **No prose verification.** A header claim like "a 6-pixel vertical stroke" cannot be checked
  mechanically, and pretending otherwise produces a gate that passes vacuously. The one narrow
  exception is `# calls:`: where a header enumerates callees, the enumeration must match the
  body (§ 3.4). Everywhere else prose is checked by reading, and the index's job is to make the
  right file obvious, not to read it for you.
* **The Apple II tree is out of scope.** `decompiled/apple2/` is a different address space with
  different semantics, and the cross-ISA anchors are a string-level mapping. This supersedes the
  draft's D7, which asked the same question about `xref`.
* **`xref` is not in this epic.** It is a different problem wearing this epic's clothes: the
  static half partly exists as `# calls:` headers, and the runtime half has to decide which of
  292 `ExecutionFlow.json` harvests count as evidence. It gets its own epic; what ships here
  instead is the `# calls:` gate (§ 3.4).

---

## 3. Proposed shape

Three generated indexes, one query command, one output directory. The fourth index the draft
proposed, `xref`, is cut (§ 3.4), and the first is renamed: it is a static map of byte
*ownership*, not a data-flow analysis, and a name that overpromises invites the misuse this epic
exists to prevent.

```
tools/index_footprint.py    -> disasm/index/footprint.tsv
tools/index_coverage.py     -> disasm/index/coverage.tsv
tools/index_engine_map.py   -> disasm/index/engine-map.tsv
tools/lookup                -> query surface over all three
tools/index_fixtures.py     -> the acceptance fixtures, run with --selftest
```

`tools/census.py` keeps its own entry point and is **not** folded under `lookup`. The census
needs `original/`; these three read only committed inputs and are therefore declared
CI-runnable, and folding census in would spend that property to buy an alias.

`disasm/index/` is committed and generated, exactly like `disasm/census/`, with the same two
gates per index: `--verify` (asserts the data's own invariants) and `--check-fresh`
(regenerate to a temp dir and diff against the committed copy). The census already proved
this pattern, and it also proved why the second gate is mandatory: `place-index.tsv` sat
stale with unjoined rows while its own gate reported green.

### 3.1 `footprint` — the one that would have saved this week

One row per `(segment, offset, asm_file, access)` where access is
`read | write | read-modify-write`, plus a `sites` column of line numbers so a query result is
directly jumpable. Queries: `lookup footprint 0x4c7`, `lookup footprint DS:4C7` and
`lookup footprint sweep_counter` all answer "who touches the sweep byte" in one command.

The `access` column comes from a **closed table of about 25 mnemonic rows**, hand-curated inside
the generator with a citation per row, exactly like the span allowlist: it is evidence, not
generated data. Ruled 2026-11-23. `mov`/`movb`/`movw` write the destination and read the source;
`cmp`/`cmpb`/`cmpw` and `test` read both and write neither; `add`, `sub`, `adc`, `sbb`, `and`,
`or`, `xor` and their `b`/`w` variants are read-modify-write on the destination; `inc`/`dec` and
their width variants are read-modify-write; `lea` reads; `xchg` is read-modify-write on both;
`jmp` and `call` targets are control transfers and not data accesses, so they are excluded; and
`.byte` is forced-keep opacity. AT&T puts the destination last. **An unrecognised mnemonic with a
memory operand is a hard failure, not a skip**, because a silent skip is how an index starts
quietly lying about what it covers. The operand splitter is imported from
`tools/dialect_masm.py`'s `split_operands()`, which already handles quotes and paren depth; its
`att_operand()` returns `None` rather than guessing, which is the failure shape this index wants
as well. One splitter in the repo, and the dependency direction is harmless: the index importing
a parser does not make the assembler's correctness depend on the index.

Five hard cases, and they are the design, not the footnotes:

1. **Dynamic offsets.** `asm/6B3D` touches the star tables as `0x4cd(%si)`, so the literal
   operand is a *base*, and the real footprint is a 64-entry array. A naive index reports
   `0x4cd` and confidently implies nothing else reads the tables. The row records `base`, flags
   `indexed` when a base register is present, and a query for an offset inside a flagged range
   returns the range's owner. Measured: 398 indexed operand occurrences over **99 distinct
   bases**, so the problem is bounded. `span` is filled **only** from a curated allowlist literal
   inside the generator, one citation per entry (the 64-star tables, the five creature grids at
   `0x137/0x157/0x177/0x197/0x1d7`, the `0x76/0x96/0xb6/0xd6` stride) — never from loop-bound
   inference, which is a separate analysis that gets ranges wrong with confidence. Everything
   else is `span: unknown`, and an unknown span must return its base as a loud candidate on any
   query that could land inside it. A silent empty answer is the failure.
2. **Read-modify-write.** `incb`, `decb`, `sbb`, `adc`, `or`, `and` against a DS operand are
   both. Classifying them as reads only would have hidden the sweep descend again.
3. **Forced-keep blobs.** Where the disassembler gave up, `asm/` carries `.byte` lines with the
   raw bytes and a comment — 326 files, 1171 lines today. A write inside one is invisible to any
   parser, so opacity is reported **per file**, a query whose answer includes a file with
   undecoded bytes in range is flagged, and such a file cannot hold a `decoded` grade. A silent
   gap is worse than a loud one.
4. **Segments are not one thing.** `asm/` holds 308 `cs:` data references (`cs:0x6cac` delta
   table, `cs:0x730` jump table, the `cs:0x4F04` DOS wrapper parameter block) and 102 `es:`
   references including real writes such as `mov %cl,%es:0x50(%di)`. A DS-only index answers
   "who touches this byte" and silently implies nothing reads those tables, which is this
   project's worst failure shape, so the table carries a `segment` column and all three are
   indexed. ES is not constant: `asm/154E` sets `es = ds`, `asm/155D` sets `ES = CS`, and
   `asm/310B_cmd_C` sets `ES = 0xB800/0xBA00`, video memory. So each `es:` site is resolved from
   its own routine's setup to `es=ds | es=cs | es=io(video) | es=unknown`, `es=io(video)` goes to
   the seams column rather than the data rows, and a routine using `es:` with no resolvable setup
   fails `--verify`. `cs:` uses file offsets, one convention with `asm/` itself, base + span for
   tables, and a `cs:` *write* is reported as a `selfmod` class with its expected count asserted,
   because a new one in this binary would be news.
5. **The parser accounts for itself.** The index's claim is that the fast path is correct, and the
   way a parser breaks is by skipping a line shape it did not recognise. Every non-comment line is
   parsed or counted, the unparsed count is printed, and any unparsed *instruction-shaped* line
   fails `--verify` — tolerance only for directives like `.byte` and `.p2align`, which count as
   forced-keep instead. A file with unparsed instruction lines is capped at `segmented`, so a
   parse failure cannot hide behind a decoded grade.

### 3.2 `engine-map` — asm ↔ C

One row per `(asm_address, c_file, c_symbol)`. The raw material already exists: the C tree cites
addresses in comments as a habit (`flight.c` carries `/* asm/7030's orbit determination
(entry L_007230) ... */`), and the generator scans C comments for `asm/XXXX` and `L_XXXXXX`.

**The `mode` column is dropped, and enhanced mode with it (this reverses the draft's D5).**
Measured: 69 of 145 `.c`/`.h` files under `engine/src` cite no address, and that list is
`platform/renderer_sdl.c`, `sha256.c`, `ini.c`, `licenses_data.h` — code with no asm counterpart
by design. So "cites nothing" is not "has no evidence behind it"; the honest unit is the
**behavioural core** (glossary), and the exemption list lives in the generator and is printed in
the report header. Enhanced mode would need a `diverges:` note per feature, nobody has written
them, and an empty column that looks filled is the "looks finished" defect, so it is deferred
until the notes exist.

**Citation validation is not this tool's rule to own.** `tools/xref_provenance.py` already gates
docs→asm citations and owns the `# provenance:` vocabulary. It gains a `--c` mode pointed at the
C tree, and `engine-map` reports a dangling citation as an input error rather than re-implementing
the check. Two gates that both resolve `asm/XXXX` would drift, and the rename hazard `AGENTS.md`
names is exactly the failure they would catch inconsistently.

### 3.3 `coverage` — how much is understood, and by what measure

Per `asm/` file, the derived **coverage grade** (glossary) rather than the bulk `confidence:`
stamp:

* **segmented** — the file exists and `check_func.py` passes. All 1212.
* **decoded** — carries `name-evidence:`, *or* a non-`sub_` filename, *or* a `body corrected:`
  marker, because a correction is proof somebody read the body.
* **observed** — `provenance:` contains `live:`.

`confidence:` is reported as an input column and never moves the grade. The report header prints
the predicates beside the counts, so the number cannot be quoted away from the rule that made it.

What makes it actionable:

* **Rollup by engine module, with `unmapped` as a first-class row.** "Per subsystem" was
  undefined in the draft and nothing in the tree defines a subsystem; rolling up through
  `engine-map` makes the rollup answer *which feature is still guesswork*, and it shrinks when a
  feature is built. Address blocks and DS-ownership rollups answer different questions.
* **`cited_by_engine` is orthogonal to the grade.** A C citation proves linkage, not correctness,
  and the interesting rows are exactly the ones where the two disagree.
* **The unnamed-offset queue.** `state.h` names 113 DS offsets; the index publishes every offset
  the code touches that has no name, ordered by touch count. That is how the mirror grows: the
  index proposes, the mirror decides.

### 3.4 `xref` — cut, and what ships in its place

The draft's fourth index merged the static call graph with the runtime CFG corpus, because the
command handlers are reached by `jmp *%bx` through a jump table and are statically
undiscoverable. That is still true and still worth doing, but it is a separate problem: the
static half partly exists already as generated `# calls:` lines in 601 headers, and the runtime
half has to decide which of 292 `ExecutionFlow.json` harvests count as evidence, under the rule
that CFG *blocks* are a seed list and `ExecutedInstructions` is the proof. It gets its own epic.

What ships here instead is the gate the cut exposes: `tools/calls_gate.py`. A `# calls:` line is
hand-editable prose duplicating a fact that lives in the body, so it can drift silently; this
section claimed the two agree, and the gate found five transfers across three files that did not
claim them. The gate reports disagreement, **the body is authoritative without exception**, and it
is report-only — no `--fix`, because auto-editing an `asm/` header touches the CP0-frozen surface
and deserves a human.

The rule is the load-bearing part, and two versions of it were wrong before one was right. A
transfer is `call` *and* `jmp` — `asm/017D` is a three-byte stub whose whole body is
`jmp L_000442` — plus forced-keep `.byte 0xe8` lines whose comment names the swallowed call, plus
the fall-through chain of a fragment split, since only the last fragment holds the `ret` and
`asm/507A`'s claim is satisfied two fragments later in `asm/5085`. A header's list wraps onto
indented continuation lines, and a long list is elided with `(+N more)`, which is a checkable
assertion and not a shrug: the gate fails when the body has a different number of unlisted
transfers than the line claims. `call`-text-only called 212 of 601 files drift; ignoring
continuations and elisions called 17. Either would have made the gate noise on the day it landed,
so the eight controls in `--selftest` are what keep the rule honest, and the gate runs in
`tools/check_all.sh` behind its own selftest.

---

## 4. Query surface

`tools/lookup <index> <key>`, plus `tools/lookup --what all <key>` to fan out over the three
indexes. The point is that one short command beats one correct grep, so the fast path and the
correct path are the same path.

The contract, because a tool that is only sometimes scriptable never gets scripted:

* TSV on stdout by default, `--json` on request.
* Keys accepted as a bare hex offset (`0x4c7`, `4c7`), a `DS:` form (`DS:4C7`), a `state.h`
  field name (`sweep_counter`), or a save-relative offset.
* **Exit 1 when nothing matches.** An empty result that looks like success is the same failure as
  a silent gap.

`AGENTS.md`'s "Look up before you derive" table gets a row per index, in the same commit as the
index it names — a rule that names a tool is followed, a rule that advises an attitude is not.

---

## 5. Gates

Every index ships with a control that makes it red, run from the same directory and ref
position CI will use. This project has twice built a gate that could not fail where it
counted, so:

* Every index is **declared CI-runnable**: it reads only committed inputs (`asm/`, `engine/src`,
  `state.h`), so a gate that needs `original/` is a bug in the gate. All three run in the
  always-run set, not under `check_all.sh --fast`.
* `footprint --verify` must fail if any offset appears in zero files (impossible), if the
  indexed-range flag is dropped, if forced-keep coverage is reported as 100%, if an `es:` site
  has no resolvable segment, if the `selfmod` count differs from its asserted expectation, or if
  the parser's unparsed instruction-line count is above zero.
* `coverage --verify` must fail if a grade is reported higher than the header markers support,
  and if any file with unparsed instruction lines or in-range `.byte` lines holds a `decoded`
  grade.
* `engine-map --verify` must fail if a cited address does not exist in `asm/` — by consuming
  `xref_provenance.py --c`, not by re-deriving the rule.
* Sole writer is the generator (D6, ADR 0002). Freshness is wired through the existing
  `tools/generated_fresh.sh` `check` function and `tools/check_all.sh`; no new gate framework.
* `tools/index_fixtures.py --selftest` is the acceptance gate (§ 8).

---

## 6. Sequencing inside this epic

1. The `# calls:` gate (§ 3.4) — **shipped 2026-11-23** as `tools/calls_gate.py`.
2. **The `state.h` parser, as its own deliverable, landing before `coverage`.** Ruled 2026-11-23:
   `coverage`'s most actionable output is the unnamed-offset queue, and that queue is the
   parser's offset list minus the named ones. Shipping `coverage` without it would ship the boring
   half first and advertise a queue that cannot exist yet. The parser is small, `footprint` needs
   it anyway, and it is the only thing that makes `tools/u2_edit.py`'s hand-copied offsets
   fixable (`docs/outstanding.md`). Filed as its own issue for the same reason.
3. `coverage` — nearly free once the parser exists, delivers the headline number, and proves the
   gate pattern before anything hard is attempted. **Its rollup column defers to `engine-map`**
   and says so in the table header rather than shipping a stub column.
4. `footprint` + `lookup` — highest value, carries the hard cases, and consumes the parser that
   item 2 built rather than growing its own.
5. `engine-map` — cheap once the citation gate has a `--c` mode.
6. `xref` — its own epic, not this one's item 4.

This file stays a plan and `docs/outstanding.md` stays the only queue. One GitHub issue per
deliverable (footprint, coverage, the `state.h` parser, engine-map, the `# calls:` gate, the
deferred `xref` epic), each linking here and never restating it.

---

## 7. Rulings

Reviewed 2026-11-23, decision by decision. The draft's D-numbers are kept so nothing is lost.
Where a ruling reverses the draft's recommendation, it says so.

| # | decision | ruling |
| --- | --- | --- |
| D1 | where generated tables live | `disasm/index/`, committed, mirroring `disasm/census/`. Reviewable generated diffs beat a gitignored build dir. ADR 0002. |
| D2 | indexed DS accesses | one row per base with `indexed` and a `span`. `span` comes only from a curated allowlist literal inside the generator, one citation per entry, never from loop-bound inference. Everything else is `span: unknown`, and an unknown span must return its base as a candidate on any query that could land inside it. |
| D3 | forced-keep opacity | per file, and it bites: a file with undecoded bytes in range is flagged on the query and cannot hold a `decoded` grade. |
| D4 | C citation marker | loose regex, but the gate is `xref_provenance.py --c`, not a second citation checker inside `engine-map`. |
| D5 | enhanced mode in `engine-map` | **reverses the draft.** Out of v1. A `diverges:` column nobody has filled is a field that looks finished. |
| D6 | hand edits to `disasm/index/` | nobody. The generator is the sole writer and the freshness gate enforces it via `tools/generated_fresh.sh`. ADR 0002. |
| D7 | Apple II | moot as asked, since `xref` is cut, and re-ruled as a non-goal: the footprint index is DOS-only. |
| D8 | freshness contract | `--verify` and `--check-fresh` both wired into `check_all.sh`, always-run, not `--fast`-skipped. |
| D9 | does this block Phase 4 | no. Leverage, not a gate. Reciprocal added: a new decode claim about byte ownership is checked against the index, and when the index is wrong the fix is the generator, never the table. |
| new | name of the first index | `footprint`, not `dataflow`. It maps ownership, not value-flow, and the draft's own fear is a plausible wrong answer. In the glossary. |
| new | what "understood" means | the derived **coverage grade** (segmented / decoded / observed) from header evidence; `confidence:` demoted to an input column. |
| new | segments | a `segment` column; DS, CS and ES all indexed. ES resolved per routine (`es=ds`, `es=cs`, `es=io(video)`, `es=unknown`); video goes to seams, not data rows; `cs:` writes are a `selfmod` class with an asserted count. |
| new | rollup unit | engine module via `engine-map`, with `unmapped` as a first-class row. |
| new | engine citation and grade | orthogonal; a citation never raises a grade. |
| new | `state.h` | parsed for real, for the first time, inside this epic; the `u2_edit.py` migration is deferred (§ 9). |
| new | `lookup` and census | census is not folded under `lookup`. It needs `original/`; the indexes do not. |
| new | parser accounting | every non-comment line parsed or counted; unparsed instruction lines fail `--verify` and cap the grade. |
| new | acceptance | `tools/index_fixtures.py --selftest`, built from the § 1 corrections, wired into `check_all.sh`. |
| new | prose verification | out of scope, stated as a non-goal. `# calls:` is the one narrow mechanical exception. |
| new | rollup of the naming question | the DS mirror names a byte, `asm/` owns what touches it. ADR 0001. |

---

## 8. What success looks like

* `tools/lookup footprint 0x4c7` returns the sweep's writers, including the `H` arm, in under a
  second.
* A number is available, publicly in the repo, for how much of the binary has been read, it is
  derived from header evidence rather than a bulk stamp, and it moves when decode lands.
* Behavioural-core engine code with no asm citation is a list, and the list shrinks.
* Nobody in a later session says "I grepped and couldn't find it" about a byte-ownership
  question, because there is a command for it.

**The criteria are enforced, not asserted.** `tools/index_fixtures.py` encodes the § 1 mistakes
as regression cases: the `H`-arm writer of `0x4c7` must appear; the star-table bases must own
`0x4cd..0x50d`; a query landing inside an unknown span must return the base as a candidate rather
than nothing. Three of the four corrections in § 1 are footprint questions and become fixtures.
The fourth — prose read as fact — is not mechanically checkable, and the answer to that class is
the `# calls:` gate plus reading. That limitation is stated here rather than papered over.

---

## 9. Found while ruling, and not this epic's to fix

`tools/state_layout.py` parses `engine/src/core/state.h` now (#32, 2026-11-23): 113 fields, with
the `/* 0xNN */` comments and the `U2_OFF` asserts both checked against offsets computed from the
declared types, `sizeof` checked against the struct's own size assert, and any field-shaped line
the parser cannot read treated as a hard failure rather than a skip. Its output is
`disasm/index/state.tsv`, committed, with the tool as its only writer.
`tools/u2_edit.py:95` still hand-maintains `BASE = 0x36` and a field dict that cites `state.h` in
comments, so the glossary's **Seed module** ruling — "offsets are generated from `state.h`, never
copied from it" — remains false in the code.

Found while building it: the header says every named field is pinned by a `_Static_assert`, and 18
named fields are not (`sector_x`, `stat_agi`, `space_ship_x`, …). The table carries an `asserted`
column and `--verify` ratchets the count, so the number can only fall. Adding the missing asserts
is engine source and its own commit.
`footprint` needs the struct read for real (names as query keys, the `save_offset` column), so
this epic builds that parser and exposes it as an importable module.

Migrating `tools/u2_edit.py` onto it is a behaviour change to a working save tool and is **not**
part of this epic: its own commit, its own test, and a run of `tools/save_cross_load.py`. The
glossary line is marked not-yet-true until it lands.
