# Epic: the code index — a queryable map from bytes to behaviour

**Status: proposal, not built. Not a live queue.** The live queues remain
`docs/outstanding.md` and `docs/needs-gameplay.md`.

Opened 2026-11-23 after a session that made four corrections to decode that already existed
in `asm/`, and found the answer to a fifth question with a grep that should have been a
lookup. The purpose of this document is to be argued with: § 7 is a list of decisions with
recommended defaults, and every one of them is genuinely open.

---

## 1. The problem, with numbers

`asm/` holds 1212 files. **1058 of them carry `confidence: low`** — measured, not estimated.
So 87% of the binary is *segmented* (boundaries proven, bytes exact) and *not understood*.
That is not a failure of Phase 3; Phase 3's contract was byte-exactness and it delivered it.
It is the state a pull model produces: Phase 4 decodes a function when a feature needs it, so
semantic depth equals the depth of the last feature built, and nothing records where that is.

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
506-line function takes ten minutes. The fast path and the correct path diverge, and nothing
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

---

## 3. Proposed shape

Four generated indexes, one query command, one output directory.

```
tools/index_dataflow.py     -> disasm/index/dataflow.tsv
tools/index_enginemap.py    -> disasm/index/engine-map.tsv
tools/index_coverage.py     -> disasm/index/coverage.tsv
tools/index_xref.py         -> disasm/index/xref.tsv
tools/lookup                -> query surface over all four (+ census, which already exists)
```

`disasm/index/` is committed and generated, exactly like `disasm/census/`, with the same two
gates per index: `--verify` (asserts the data's own invariants) and `--check-fresh`
(regenerate to a temp dir and diff against the committed copy). The census already proved
this pattern, and it also proved why the second gate is mandatory: `place-index.tsv` sat
stale with unjoined rows while its own gate reported green.

### 3.1 `dataflow` — the one that would have saved this week

One row per `(ds_offset, asm_file, access)` where access is `read | write | read-modify-write`.
Queries: `lookup dataflow 0x4c7` answers "who touches the sweep byte" in one command.

Three hard cases, and they are the design, not the footnotes:

1. **Dynamic offsets.** `asm/6B3D` touches the star tables as `0x4cd(%si)`, so the literal
   operand is a *base*, and the real footprint is a 64-entry array. A naive index reports
   `0x4cd` and confidently implies nothing else reads the tables. The index must record
   `base`, and flag `indexed` when a base register is present, and a query for an offset
   inside a flagged range must return the range's owner. Getting this wrong produces a
   plausible wrong answer, which is the shape this project fears most.
2. **Read-modify-write.** `incb`, `decb`, `sbb`, `adc`, `or`, `and` against a DS operand are
   both. Classifying them as reads only would have hidden the sweep descend again.
3. **Forced-keep blobs.** Where the disassembler gave up, `asm/` carries `.byte` lines with
   the raw bytes and a comment. A DS write inside one of those is invisible to any parser.
   The index must count them and report coverage: "N bytes in `asm/` are undecoded
   instructions; a write could hide in these." A silent gap is worse than a loud one.

### 3.2 `engine-map` — asm ↔ C

One row per `(asm_address, c_file, c_symbol, mode)` where mode is `classic | enhanced | both`.
The raw material already exists: the C tree cites addresses in comments as a habit
(`flight.c` carries `/* asm/7030's orbit determination (entry L_007230) ... */`). The
generator scans C comments for `asm/XXXX` and `L_XXXXXX` references.

The value is the **negative space**: engine code that cites nothing is code with no evidence
behind it. That is a list someone can act on, and nobody can produce it today without reading
every file.

### 3.3 `coverage` — how much is understood

Per `asm/` file: named or `sub_xxxx`, `confidence:` value, whether it carries a
`body corrected:` or `NAME DISPUTED:` marker, and whether any C file cites it. Roll up to a
single number per subsystem. This answers "how much have we actually decoded" with a figure
instead of a feeling, and it makes the 87% visible instead of invisible.

### 3.4 `xref` — callers and callees, with a caveat that must be honoured

Static call graph from `asm/` **merged with** the runtime CFG corpus, because the command
handlers are reached by `jmp *%bx` through a jump table and are statically undiscoverable —
that is why Ghidra and Reko missed them and why `captures/spice86/spice86-cfg.json` is the
seed list. An xref built from `asm/` alone will confidently report live code as dead. The
index must record which edges came from which source, and `CFG blocks are not proof of
execution` applies: block entries are a seed list, `ExecutedInstructions` is the evidence.

---

## 4. Query surface

`tools/lookup <index> <key>`, plus `tools/lookup --what all <key>` to fan out. The point is
that one short command beats one correct grep, so the fast path and the correct path are the
same path. `AGENTS.md`'s "Look up before you derive" table gets rewritten to name this command
instead of prose about looking first — a rule that names a tool is followed, a rule that
advises an attitude is not.

---

## 5. Gates

Every index ships with a control that makes it red, run from the same directory and ref
position CI will use. This project has twice built a gate that could not fail where it
counted, so:

* `dataflow --verify` must fail if any DS offset appears in zero files (impossible), if the
  indexed-range flag is dropped, or if forced-keep coverage is reported as 100%.
* `engine-map --verify` must fail if a cited address does not exist in `asm/`.
* `coverage --verify` must fail if the low-confidence count is reported lower than the number
  of files actually carrying the marker.
* All four wired into `tools/check_all.sh`, and `--check-fresh` wired so a stale committed
  table goes red.

---

## 6. Sequencing inside this epic

1. `dataflow` + `lookup` (highest value, purely mechanical, no dependencies)
2. `engine-map` (cheap; the citation habit already exists in the C tree)
3. `coverage` (trivial once 1 and 2 exist)
4. `xref` (hardest, least urgent, needs the CFG merge done honestly)
5. fold `census` under `lookup` — do **not** rebuild it

---

## 7. Decisions to settle before building

Each has a recommended default. All are genuinely open.

**D1 — Where do generated tables live?** Recommend `disasm/index/`, committed, mirroring
`disasm/census/`. Alternative is a gitignored build dir, which loses reviewability of
generated diffs.

**D2 — How are indexed DS accesses (`0x4cd(%si)`) represented?** Recommend: one row per base
with an `indexed` flag and a `span` column when the span is provable from a loop bound, and
`lookup` resolves an offset inside a span to its owner. Alternative is expanding spans into
per-byte rows, which is honest but 64x bigger and hides which rows came from one instruction.

**D3 — Is forced-keep opacity reported per file or globally?** Recommend per file, so a file
with undecoded bytes is visibly suspect. Global is easier and less useful.

**D4 — Do we formalise the C citation marker?** Recommend: keep loose regex now
(`asm/[0-9A-Fa-f]{4}` and `L_[0-9A-Fa-f]{6}`), add a gate that fails on a citation to a
nonexistent file, and only formalise if false positives appear. Formalising first costs a
sweep of every C file for no proven gain.

**D5 — Does `engine-map` record enhanced mode at all, given enhanced has no asm counterpart?**
Recommend: yes, but as `derived-from: <asm addr>, diverges: <note>`, never as a plain mapping.
An unqualified enhanced row would claim evidence that does not exist.

**D6 — Who may edit `disasm/index/` by hand?** Recommend: nobody; the generator is the only
writer and a gate enforces it. This is the rule that keeps § 2 true.

**D7 — Does `xref` include Apple II 6502 files?** Recommend: separate index, not merged. The
cross-ISA anchors are a mapping, not a call graph, and mixing them invites false edges.

**D8 — What is the freshness contract?** Recommend: `--check-fresh` in `check_all.sh`, and
`--verify` in it too, matching census. Cost is a few seconds per run.

**D9 — Does this epic block Phase 4 feature work?** Recommend: no. It is leverage, not a
gate. The one exception is that any *new* decode finding about data ownership should be
checked against `dataflow` once it exists, so the index stays honest by use.

---

## 8. What success looks like

* `tools/lookup dataflow 0x4c7` returns the sweep's writers, including the `H` arm, in under
  a second.
* A number is available, publicly in the repo, for how much of the binary is understood, and
  it moves when decode lands.
* Engine code with no asm citation is a list, and the list shrinks.
* Nobody in a later session says "I grepped and couldn't find it" about a data-flow question,
  because there is a command for it.
