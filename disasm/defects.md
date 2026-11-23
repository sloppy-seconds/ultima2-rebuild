# Phase 2 CP5 — Defect verdicts

Ground truth for every verdict: the per-function asm slice (`disasm/functions/`) which
re-assembles byte-identically. "FP" = bytecheck false positive (claim is a legitimate
algebraic form of bytes that ARE in range).

## The six "fabricating" functions (bytecheck-map.tsv)

| Start | Claim | Verdict | Evidence |
| --- | --- | --- | --- |
| 0A5F | stack delta 0x1757 | **true Reko bug** | Slice is a cmp/jne/jmp dispatch chain; Reko's stack model invents a 5975-byte adjustment. Asm correct. Workaround: read asm slice; do not trust Reko's frame math here. |
| 4FA5 | `ax > 0x3800` | **FP — constant folding** | Asm: `add $0x200,%ax; cmp $0x3a00,%ax`. Reko folded 0x3a00-0x200 = 0x3800. Legitimate transform; bytes are in range. |
| 47BD | 0x739 | **FP — negative displacement** | Asm: `add %cl,-0x739(%bp,%di)` (bytes C7 F8 in range). Bytecheck looked for +0x739. NOTE: whole 13-byte region decodes as absurd instructions (`fisttps`, neg disp) — a **data island** inside a seed range; Phase 3 should reclassify. |
| 4815 | 0x739 | **FP — same pattern** | sibling fragment of 47BD, same disposition. |
| 486D | 0x739 | **FP — same pattern** | sibling fragment of 47BD, same disposition. |
| 6999 | `~0x1701` | **FP — complement form** | Reko models a mask as `~0x1701`; the file carries the mask's own bytes. Lowering style, not invention. |

Net: **1 genuine Reko fabrication (0A5F) in 1299 procedures**, not 6. The bytecheck's
absent-from-file test needs the derived-constant caveat; every fab is human-adjudicated here
(the CP6 methodology always required this — the flags were the point, the verdict is CP5's).

## The three Reko procedure failures

| Start | Symptom | Verdict |
| --- | --- | --- |
| 2395 | rewrite failure; no aligned label in linear asm (CP1) | **Retracted 2026-10-28e.** Recorded as a single `nop` (0x90) and a NOP-padding seed. It is not padding: the map cut `out %al,$0x90` (`e6 90`) after its opcode at asm/2393, so 2395 is the operand of a real instruction. Reko failed because a one-byte tail is not a procedure, which is a fact about the boundary, not about the byte. |
| 35DD | rewrite failure | **Retracted 2026-10-28e.** Same shape: the map cut `aam $0x90` (`d4 90`) after its opcode at asm/35DB. Not padding. |
| 531B | unbounded-recursion SSA exception | Slice: `call str_out` followed by a run of 0x20 (space) string bytes. A string-display site, not a control-flow function; Reko's recursion on the shared routine blows up. Phase 3: treat as code-prefix + string data. |

**The two retractions are the same mistake as the rendering gaps, one layer down.** A byte was
read out of context, a plausible story was formed about it (`nop` = padding), and that story then
appeared in three documents and a disposition, which made it look adjudicated. The bytes had been
saying otherwise since the map was drawn; nobody re-read them after the boundary was placed.
`tools/forced_keep_audit.py` finds both now, because a forced-keep whose instruction is longer
than the bytes kept is the boundary admitting it cut something.

## Follow-ups recorded (not hidden)

1. Bytecheck v3 (if ever built): whitelist folded/complement/negated forms before flagging fab.
2. Data-island candidates inside seed ranges: 47BD/4815/486D family — CP2 kind said unclassified;
   Phase 3 data-separation pass should absorb them.
3. ~~NOP-padding seeds (2395, 35DD family): consider `kind=nop-pad` in the map for Phase 3.~~
   **Dropped 2026-10-28e: there are no NOP-padding seeds.** Both bytes are the tail of an
   instruction the map cut in half, so a `kind=nop-pad` would have encoded a wrong reading into
   the map, where it would have been harder to correct than the prose was.

## 2026-10-28d — the misclassification direction nobody could check until now

The two follow-ups above are the same defect as a class, and the class was invisible: there was
no way to ask "is this byte rendered as the wrong kind?" and get an answer. There is now, in one
direction, and it is `tools/str_out_audit.py`.

The lever is a proven calling convention rather than a heuristic. `call L_0051df` and the bytes
immediately after the return address **are** the message — NUL-terminated, optionally wrapped in
`0x8D`/`0x0D`. That is a property of the executable, so the set of print sites can be enumerated
from `original/ultimaii.exe` alone, with no reference to how `asm/` chose to render anything.
Compare that set against what `strings_atlas.py` reports and the gap is exactly "the game prints
this and `asm/` draws it as instructions".

**Measured: 334 near calls to the printer, 238 of them carrying a printable message, 233
rendered as data by `asm/`, 5 not.** Those 5 are in `disasm/str-out-rendering-gap.tsv` (offsets,
lengths and the owning `asm/` file — no game text, per `AGENTS.md`), and `--check` goes red on
drift in either direction: a new gap, or a listed one that got fixed and left the list stale.

**Update, 2026-10-28e: the list is empty, and the reason it was open is gone.** The
paragraph below is kept because it is the accurate record of why the tree was left this way, but
its premise no longer holds: `asm_body_guard` now permits a marked body correction
(`# body corrected: DATE -- evidence: ...`), so "the fix is a body edit, which the guard
forbids" stopped being true. Every one of the nine was re-cut — `asm/02A3`, `asm/02E6`,
`asm/3566`, `asm/3577` + `asm/3582`, `asm/3596`, `asm/3795`, `asm/37F2` + `asm/380B` and
`asm/464E` — each verified by `check_func`, by `str_out_audit --check` and by the whole-binary
byte match. 238 of 238 printable print sites are now rendered as data.

Two things fell out that were not in the original diagnosis. The first is that a message the map
cut in two is still rendered as data, just in two pieces: `db "ANOS:"` in one fragment and
`db " 9,000,000 B.C.",0x00` in the next. Neither half passes the terminator test alone, so the
atlas was calling a fixed file a gap. `strings_atlas` now reunites halves on exact evidence
(adjacent addresses, the first genuinely unterminated, different files, byte-checked afterwards)
and its selftest stopped requiring the contract source to have work to do — a control that can
only pass while the defect exists is not a control, and it went red the moment the last gap
closed. The second is that a four-byte forced-keep in `asm/37F2` was not a gas limitation at
all: gas encodes `movb 0x4a,%al` as `a0 4a 00` perfectly well, and the keep existed because the
byte before it was the NUL of a string nobody had rendered as one.

The era table turned out to be worth the grind on its own account. All five `ANOS:` lines are
now visible in `asm/` as data, and the table's real shape came out with them: five entries of
`call str_out / db text / jmp dispatcher` with no padding, whose boundaries the map had cut in
three places, which is why three of the five were invisible and why `asm/3596_show_year_1990`
was named for the second-to-last thing in its own range.

**The 9 are documented, not fixed, and that is a deliberate limit rather than a leftover.** The
only way to make `asm/` render them as data is to replace instruction body lines with `db` lines.
That keeps `assemble_clean.sh` green — the bytes are identical — but `engine/tests/asm_body_guard.py`
builds a canonical stream of every non-comment line in the tree and requires it unchanged from
HEAD, and its docstring is explicit that renaming, merging and splitting are legitimate while
**editing is not**. It has no bless mode, and adding one would put a door in the only guard that
currently cannot be talked into accepting a change. So the atlas grew a second source instead:
`strings_atlas.py` now carries these nine from the calling convention itself, tagged
`source='str-out-contract'` and verified byte-exactly like every other row, and each owning file
carries a `# RENDERING GAP:` header saying so. The presentation is weaker than the evidence, and
that is now visible in both places someone would look.

**The first version of this section said 16 and attributed all of them to `asm/`. That was
wrong, and wrong twice over.** Six of the sixteen were `strings_atlas` failing to extract a
string that `asm/` had rendered perfectly well as `db` — including `MALE` in
`asm/5066_print_sex_male` and `PASS` in `asm/08FF_sub_08ff`, which is the pair this section
built its rhetoric on. The cause was the `exe_anchor` fallback added the same day: it required a
*unique* byte-pattern hit in a wide window and dropped anything ambiguous, and `MALE\0` and
`PASS\0` each occur twice nearby. Fixed by making proximity a tie-breaker rather than a gate
(438 strings now, up from 433, with zero regressions — the first attempt at that fix silently
*lost* four strings a unique wide hit had been anchoring correctly, and the diff against the
previous version is the only reason that was caught instead of shipped).

Of the 11 that remain, roughly eight are genuine classification gaps — a printed message sitting
in a span `asm/` renders as instructions — and the rest are a third thing: a `db` followed by a
`.byte` filler line breaks the atlas's "subtract the byte count from the next label" rule, which
assumes the `db` is the last thing emitted before that label. `asm/334D_cmd_D` and the merged
`asm/4050_cmd_T` are both that shape. Neither is fixed here.

What is in the list is still the argument for the gate. Three of the five `ANOS:` era lines,
including one claimed by `asm/3596_show_year_1990` whose declared range swallows a neighbouring
string — both now fixed, see the update above. `KLIMB`, `IGNITE TORCH`, `SPELL-`. And the fact that
the audit was right about *which offsets* were invisible even while the explanation for why was
wrong twice — the number held, the story did not.

Two things this does **not** claim. It does not say `asm/` is wrong about the bytes: every one of
those files reassembles byte-identically, and the proof is unaffected. And it does not fix them —
reclassifying a body is a change to a CP0-verified artefact and needs a human decision about how
far to go, so the tool nominates and stops. `--show` prints the text to stdout for whoever
adjudicates.

The reverse direction (a `db` blob that is really code) is still only covered by `redecode.py`
reading the five blob files, and the known instance there is the opposite sign: `redecode.py`
mis-decodes 21 bytes of real code at file `0x27D2` as a printable string. That one is still
open and still needs a length-decoder-based check, not a string heuristic.

## 2026-10-28d — a forced-keep byte swallowed a four-byte instruction, and it cost a week

`asm/353A` ends with `.byte 0x8a` marked forced-keep; `asm/353D` opens with `push %ds`. Both are
artefacts of one abandoned decode. The bytes at runtime `353C..353F` are `8a 1e 49 00`, a single
instruction `mov 0x49,%bl` — the sector tens digit. The wave took the opcode, gave up, and the
next fragment re-read the ModRM byte `1e` as `push %ds` and the displacement `49 00` as `dec %cx`.

The cost was concrete: `DS:49` is the era index G14 had been asking "who writes `%di`" about since
the ticket was filed, and the answer was sitting inside the mis-decoded instruction the whole time.
It was found by dumping six bytes and running `objdump` over them, not by a capture.

Two general lessons, both of the "a tool that lies is worse than no tool" family:

* **A forced-keep followed by an implausible instruction stream is one instruction, not two
  facts.** `push %ds` then `dec %cx` then a three-displacement `add` with no matching pop is not a
  shape anyone writes. When gas refuses to re-encode an instruction, the bytes immediately after
  it are the rest of *that* instruction, and the next fragment's opening instruction is the tell.
* **A mis-decode can hide a read rather than erase it.** Every search for `DS:49` reads kept
  finding the seven legitimate sites in `asm/2290`, `22D5`, `2305`, `26B7`, `26CC`, `2708` and
  `cmd_O`, and none of them was this one. The grep was correct and the corpus was wrong, so the
  negative result looked earned. When a byte is *known* to be read somewhere and the sites found
  are all explainable otherwise, suspect the decode before concluding there is no site.

Not fixed in `asm/`, and cannot be: instructions and labels are body, `asm_body_guard` freezes it
and CP0 md5s it. The correct decode is recorded in the two file headers and in
`asm/3546_time_index_message`, which now carries the complete era table. Whether to reopen the
body-freeze for known-wrong decodes is an owner question, and it is a different question from the
seam-label one in `docs/fragment-splits.md` — that one is about merging, this one is about
leaving a known falsehood in the tree.

## 2026-10-28e — a third data-island candidate, recorded and deliberately NOT reclassified

`asm/5D58` (span 5D58..5DA8, evidence `seed+reko`, no name) has the signature of the two
islands corrected the same day — and one piece of missing evidence that makes it a candidate
rather than a verdict.

What points at data:

- **Nothing enters it.** No `call` or `jmp` anywhere in `asm/` targets 5D58..5DA8, and the
  fragment before it (`asm/5D50`) ends `add $0x3,%bp ; jmp L_005f22`, so nothing falls through
  either. A routine nobody can reach is a data block until proven otherwise.
- **A 55-byte run of 0x00** in the middle, plus eight more at the end. Code does not contain
  that; the clean wave already renders it as a `db ... # fill` line, which is the tree
  admitting it is not instructions.
- **Two decodes that this CPU cannot execute**: `popa` at 5D9D (80186+) and `gs pop %bp` at
  5D9F. Same absurd-decode test that settled 47BD, 5287 and 0720.
- **Three stride-4 pointers at 5D9E..5DA3**: as little-endian words they are 0x5D5D, 0x5D61,
  0x5D65, which in file space are runtime 5B5D, 5B61, 5B65 — three entries four bytes apart
  inside `asm/5B5D`. A table of equally-spaced entry addresses is a dispatch or vector table.

What is missing: a **positive** reference. 5287 was settled by `lea %cs:0x5288,%cx` two
instructions earlier and 0720 by `dec %bx ; js L_000920` landing on it. Here nothing takes the
address of the block, and the first five bytes (`pop bp ; pop cx ; pop bx ; pop ax ; ret`) are a
textbook epilogue that could be a real routine tail reached by a route the static scan has not
found. Reclassifying on the negative evidence alone would be exactly the plausible-wrong-answer
this project has been bitten by twice.

Disposition: left as `sub_5d58`, with this note as the record. To settle it, either find what
computes an address in 5D58..5DA8 at runtime, or capture a session and check
`ExecutedInstructions` for any offset in the span — the second is a scripted run, not an
owner task, and belongs on `docs/needs-gameplay.md` if someone picks it up.

## 2026-10-28e — the largest remaining defect: `asm/72A0` is a 4.3 KB map row that is 56% padding

`disasm/function-map.tsv` has one row, `72A0..8386`, evidence `call+reko`, and `asm/72A0`
faithfully renders all 4327 bytes of it. It is not a routine, and the tree already knows:
**1241 of its 1265 body lines are `db` lines every one of them marked
`split: branch target inside data`**, with a contiguous run of 1137. The clean wave gave up on
it.

Measured over the span's bytes:

- **2463 of 4327 bytes are 0x00 (56%)**. No routine is.
- The printable runs are the game's **ending text**: `MINAX IS DEAD!!`,
  `ALL HER WORKS SHALL DIE!`, `YOU FEEL A STRANGE FORCE!`, `YOU HAVE SAVED THE UNIVERSE,`,
  `AND COMPLETED ULTIMA ][! SEEK`, `NOW TO CONQUER WICKED EXODUS,`,
  `FOUND IN ULTIMA ]I[-D ]II[-P!`, and a `MAPX30` — the sector-name self-patch this tree
  already documented at `asm/2290`.
- It is **not** pure data either. Two of the speed-delay call sites found on 2026-10-28e are
  inside it (`mov $0xe,%bx ; call L_000911` at 750B, `mov $0x6,%bx ; call L_000911` at 7603),
  so real code lives in the same span as the ending strings and the zero fill.

So the row is a **tiling artefact swallowing three different kinds of thing at once**, which is
the same failure as `asm/3596`'s range swallowing an era entry, at forty times the size and with
the game's ending inside it. `tools/str_out_audit.py` reports no gap here because these strings
are not printed through the `str_out` near-call contract, so the gate that caught the era table
is blind to this one.

**Not fixed, deliberately.** Splitting it means adding routine starts to
`disasm/function-map.tsv`, and a map change is an owner ruling, not an agent edit — the map and
the `asm/` file set cannot diverge, so every new start is a new file and a new boundary. What
would make it safe to propose: run the existing split detector over the span
(`tools/fragment_splits.py --tsv --all`) and bring the candidate starts plus the byte histogram
above. That is the work order; it is not a re-cut.

**The same failure sits immediately after it, and the two together are the largest un-decoded
area in the tree.** `asm/8387` is one row, `8387..8CCF`, 2377 bytes, **100% of its body lines
raw-byte `db`**, 34% zeros. So `72A0..8CCF` — about 6.7 KB, 18% of the executable's code
segment — is rendered as bytes across two rows. `asm/8DCD` sits past it and is already noted
below as a real 8-byte INT 1Ah wrapper whose row simply claims 563 bytes of mostly-zero tail.

Ranked scan, reproducible in one line (this is what found all of the above):

```python
# for each asm/ file with span >= 64 B: fraction of body lines that are `db`/`.byte`,
# and fraction of the span's bytes that are 0x00
python3 - <<'PY'
import glob, re
exe = open("original/ultimaii.exe", "rb").read()
for p in sorted(glob.glob("asm/*.asm")):
    t = open(p, errors="replace").read()
    m = re.search(r"#\s*func\s+([0-9A-Fa-f]{4})\.\.([0-9A-Fa-f]{4})", t)
    if not m: continue
    s, e = (int(g, 16) for g in m.groups()); span = e - s + 1
    body = [l for l in t.splitlines() if l.strip() and not l.lstrip().startswith("#")
            and not re.match(r"^(L_[0-9A-Fa-f]{6}:|[A-Za-z_$.][\w$.]*:)", l)]
    if span < 64 or not body: continue
    raw = sum(1 for l in body if l.strip().startswith(("db ", ".byte"))) / len(body)
    zero = exe[s + 0x200:e + 0x201].count(0) / span
    if raw >= 0.6 or zero >= 0.35:
        print(f"{span:6d} zero={zero:4.2f} raw={raw:4.2f}  {p}")
PY
```

Fourteen files come back, and they are three different things. **Correctly raw**: the menu
string blobs (`15E1_tavern_menu`, `19D7_magic_shop_menu`, `5400_sub_5400`) — those are text, and
`db` is the right rendering. **Already documented**: `8DCD_clock_tick_read`, whose header says its row
claims a mostly-zero tail. **The actual defect**: `72A0`, `8387`, `6144`, `48CA`, `277B`,
`437E_cmd_V`, `3048_vehicle_plane`, `68ED` — spans where the clean wave could not tell code from
data and rendered everything as bytes, of which `72A0` and `8387` are an order of magnitude
bigger than the rest.

## 2026-10-28e — `tools/recut_bytes.py` exists; the raw-byte spans are now a review queue, not a wall

`asm/6144` was re-cut the same day: 160 `db` lines became 155 instructions and 5 honest
forced-keeps, each one accepted only after gas re-assembled it at its own address and produced
the same bytes. `check_func` and the whole-binary match both pass.

The tool deliberately prints to stdout and writes nothing, because for the other spans a
generated mnemonic is not the same thing as a verdict:

- **`asm/48CA`** (338 B, 8 zeros, no printable runs) — `recut_bytes` produces decodes like
  `adc %cx,0x33cc(%bx,%si)` and `mov $0xba00,%ax`, and **nothing anywhere in `asm/` references
  a label inside its span**: no `call`, no `jmp`, no jump-table entry. Unreachable and
  nonsense-decoding is the data-island signature, so the generated mnemonics would be a
  confident wrong answer dressed as a decode. Left as `db`. Candidate for the same treatment
  `asm/5287` got, once someone finds what reaches it.
- **`asm/277B`, `asm/3048_vehicle_plane`, `asm/437E_cmd_V`, `asm/68ED`** — mixed spans where
  some lines are clearly code (these are where three of the newly-found speed-delay slots
  live) and some are not. Review line by line; `recut_bytes --selftest` proves the tool's own
  comparison works in both directions.
- **`asm/72A0` and `asm/8387`** — 6.7 KB together, and the honest answer is a map change, not
  a re-cut. See the section above.

So the ranking above is now a queue with a named tool attached and a stated reason each entry
is still open, which is different from an unexplained pile of `db` lines.
