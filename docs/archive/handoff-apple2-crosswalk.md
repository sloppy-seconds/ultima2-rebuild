# Handover — the Apple II ↔ DOS anchor crosswalk: a static way to retire open questions without gameplay

Branch **`main`** (the `apple2-reference` work was fast-forward merged on 2026-10-28; 22 commits,
**not yet pushed to `origin`** — ask before pushing, it triggers CI).

This is a proposal with its prerequisites already checked, not an open-ended investigation. Read
`AGENTS.md` first, then `docs/apple2-reference.md` § 4 (anchors), § 6 (limits) and § 8 (the Ghidra
6502 recipe). Everything below assumes that.

---

## Status — executed 2026-10-28d, T0–T4 done, nothing here needs a capture

All five open questions have a written verdict and every one of them is static, as designed.

| | verdict | where it lives |
| --- | --- | --- |
| **Q0** step vector `x4` | corroborated (was already answered in T0) | `asm/4F11`, `asm/4F36`, `asm/50D8` `# apple2-twin:` |
| **Q1** the `0x5C` species clause | **corroborated** — `$6672` is a clause-for-clause match, `CMP #$78` guarding `CMP #$5C` | `asm/2199_creature_step_gate` |
| **Q2** the step-gate tile ladder | **corroborated** — same accept set, and a compare ladder on both sides, not a table | same file |
| **Q3** `DS:1B7` | **corroborated** — `$66C5`/`$6701` are the save-and-restore pair, fourth witness | `asm/2233`, `asm/225A` |
| **Q4** the `DS:15..18` ticket | **Apple-silent** — no equivalent indirection on three shape scans; closed as a DOS-port artefact | `asm/5B71_mode_table_copy`, `docs/needs-gameplay.md` G3 |
| **Q5** the coin-rub freeze | **corroborated** — `$7DC6` prints, then `LDA #$14 ; STA $D9` | `asm/3C04_coin_rub` |

Built: `tools/apple2_seeds.py` (the scan that was never in the tree; reproduces the documented
328 exactly, and 309 with the vector table excluded), `tools/apple2_ghidra.sh` (the whole chain,
md5s at the end, exits non-zero if the seed list drifts from the committed one),
`tools/apple2_index.py` + `disasm/apple2-functions.tsv`, and `tools/apple2_anchor_map.py` +
`disasm/apple2-anchors.tsv`. Committed structure, no bulk, no game text anywhere in git.

**The measured anchor count is 27 distinct strings of 453 DOS strings (6.0%), 17 clusters, all
order-preserving.** The remembered 35 is gone from `docs/apple2-reference.md` § 4. It started at
24 of 433; the count rose when `strings_atlas.py` grew a second source (see
`disasm/defects.md`), which un-hid three `ANOS:` era lines and turned the era table into a
five-anchor order-preserving cluster — the strongest thing in § 4, and it had been sitting
invisible because `asm/` renders those bytes as instructions.

Two things the plan did not predict, both worth more than the questions it was written to close:

* **`tools/strings_atlas.py` was silently dropping a fifth of its own corpus.** It located a
  string by subtracting its length from the next `L_` label and skipped any `db` with no label
  after it — which is every file ending in its string, 232 files, including `--INVALID MOVE!`,
  the negative control this handoff was built around. The atlas reported a clean 337/337 and its
  selftest passed while it happened. Fixed with a verified-search fallback: 433 strings, all
  verifying byte-exactly. **A control that only exercises the easy path certifies nothing.**
* **The vector-table exclusion was not housekeeping, it was the difference between a usable
  import and a lying one.** With the vectors out of the seeds, `$4054`/`$406F`/`$4080` decompile
  to the step vector, the raw delta and the printer. With them in, those addresses held vectors
  whose bodies were somebody else's code, and the output looked confident.

What is left is the "needs a person" list below, unchanged: push or stay local, and whether the
crosswalk earns a CI home (it cannot run on a clean checkout — `references/apple/` and
`original/` are gitignored, so the most CI could check is a committed count manifest; the
`--selftest` gates are local).

## Why this exists

The DOS binary is a translation of the Apple II 6502. Where the two agree, **the 6502 around a
matched string is the specification the 8086 was translated from** — that sentence is already in
`docs/apple2-reference.md` § 4, and it has been used exactly once, to confirm the string printer.

The value is not tooling. It is that a large class of open questions in this project are *static
ambiguity* questions — which of two readings of a compare is right, whether a table is what we
think, whether a branch is a guard or a fallback — and a second, independent implementation of the
same logic is a tiebreaker for exactly that class. The Apple side also has a structural advantage:
`ULTIMA.II.OBJ` is one contiguous 13,961-byte object with real 6502 control flow. The CP3
fragment-split problem that caused several misreadings — one instruction per file, a flag set in
one file and consumed in another — does not exist over there.

## What this cannot do, and it matters more than what it can

* **It cannot prove execution.** Nothing about "was this branch ever taken" or "what does this
  value do in play" transfers across an ISA. Those stay gameplay questions.
* **It is a corroborator, not a parent.** 1989 Origin re-release, not the 1982 build the DOS port
  may have come from. `original/ultimaii.exe` stays ground truth. See § 6 of the Apple doc.
* **It can mislead when the ISAs differ in the exact place you are looking.** Live proof, same day
  as this doc was written: a claim that the DOS creature step vector was `player - creature - 1`,
  because `stc` sets CF and `sub` was believed to subtract it. `SUB` ignores CF; only `SBB`
  consumes it. **The Apple code would not have caught this** — on the 6502, `SEC` before `SBC` is
  genuinely required, so the 6502 would have looked *consistent with the wrong reading*. It was
  killed by reading two bytes out of live memory. Full retraction:
  `docs/contract-creature-tick.md` § "What this contract deliberately does not claim".
* **No Apple-derived name goes into an `asm/` filename.** It supports a `# name-evidence:` line.
  That is the ceiling.
* **Dungeons stay parked.** `DNGDRAW.OBJ` and `SHAPES` are out of scope, same as the DOS dungeon.

## Already built — do not rebuild it

This is the Step 0 section, pre-answered, because this project's dominant failure mode is
re-deriving what already exists.

| thing | state |
| --- | --- |
| Apple disk parsing / extraction | done. `tools/ciderpress2/cp2` (vendored, gitignored) is the only correct reader. Extracted set lives in `decompiled/apple2/`, gitignored |
| Apple load-address map | done. ProDOS BIN aux type **is** the load address; table in `docs/apple2-reference.md` § 3 |
| Flat 6502 image for Ghidra | done. `tools/apple2_image.py -o /var/tmp/u2_6502.bin [--with create]`, refuses overlapping segments, prints a manifest of which ranges are backed by bytes |
| Ghidra 6502 import | works. Recipe in § 8. `-processor "6502:LE:16:default"`, **not** `-language`. **But the C output mislabels the vector table as functions — see T0 before trusting any address in `$4000..$403E`** |
| Function seeding | `tools/ghidra/Seed6502.java` exists, takes `seeds=<path>`. The seeds file was generated but never committed; it now survives in `decompiled/apple2-6502/` and must be committed with the vector-table range excluded |
| DOS string → owning function | **done and gated.** `tools/strings_atlas.py` — 337 strings, `--selftest` verifies every offset against the executable, `--grep` to query, prints both file and runtime bases plus the owner's `name-evidence` line |
| Anchor pairs | **only ~10 clusters recorded as prose** in § 4. The "35 strings" figure is not recoverable from the repo. This is the gap |
| The crosswalk itself | **does not exist.** Checked: no tool in `tools/` joins the two sides |
| 6502 seeds file | **not reproducible.** `Seed6502.java` needs `seeds=<path>` and nothing generates it; the § 8 run wrote to `/var/tmp` and it is gone. `disasm/seed-entries.txt` is the *DOS x86* list, not this |

So the work is three pieces, not a research programme.

## The plan

### T0 — persist the 6502 artifacts, and fix what the decompiler got wrong

**Part of this is already done, on 2026-10-28, as insurance.** The four artifacts were sitting in
`/var/tmp` and have been copied to `decompiled/apple2-6502/` (gitignored, correct: it is derived
output from copyrighted bytes, same class as `decompiled/ghidra`). md5s so a future run can be
diffed against them:

| file | bytes | md5 |
| --- | --- | --- |
| `u2_6502.bin` | 28,672 | `508d525c414b7c42f3442405425baa1a` |
| `u2_6502_seeds.txt` | 1,640 | `df0e4b6713fd12f21d61f6ce5350f2de` |
| `u2_6502.c` | 912,488 | `4c2bb3b3c9dfba37dc20f4ab2d8ef0a7` |
| `u2_6502.tsv` | 13,169 | `824acebe5deed547f670043793e2aefc` |

**What still has to be done:** commit the *structure* and a generator, not the bulk. The repo
already has the precedent in `.gitignore`: "Curated analysis in `disasm/*.md|tsv|txt` IS
committed." So `disasm/apple2-seeds.txt` and `disasm/apple2-functions.tsv` belong in git — they
are addresses and sizes, not content — plus one script that runs extraction → image → seeds →
Ghidra end to end and prints the checksums above, so anyone can tell whether the C on disk matches
the committed index.

**And the reason T0 is first rather than housekeeping: the decompiler lied about the very first
thing it produced, and only the bytes caught it.** `u2_6502.c` presents the step vector as
`FUN_4000` / `FUN_4003`. `$4000` is not code. It is `STD.SUBS.OBJ`'s opening **JMP vector table**
— 21 vectors, `$4000..$403E`, and `$4006` (the string printer everyone already knew was a vector)
sits in the middle of it. The seed scan is byte-granular, so it seeded every third byte of the
table, and Ghidra decompiled the vectors and reported their *targets*' bodies under the vectors'
addresses. `Seed6502.java`'s own header warns about junk seeds; this is that, wearing a
convincing costume. **Rule for everything in T3: a Ghidra 6502 function whose address is in
`$4000..$403E` is a vector, not a routine, and any claim must be checked against raw bytes.**

Fix in the generator: exclude the vector-table range from the seeds.

#### Q0 is answered, and it came out of this

The real routines are the vector targets. Hand-decoded from the image:

```
$4054  SEC                 $406F  SEC                 $44D5  CMP #$00
$4055  LDA $00   ; player x     LDA $00   ; player x         BEQ +7
$4057  SBC $4D00,X ; mon_x[slot]     SBC $4D00,X            BPL +3
$405A  ASL A                     STA $17                    LDA #$FF
$405B  ASL A                 $4077  SEC                     RTS
$405C  JSR $4036 ; sign         LDA $01                     LDA #$01
$405F  STA $17                 SBC $4D20,X                  RTS
$4061  SEC                 $407F  STA $18
$4062  LDA $01             $4081  RTS
       ... y pair, identical ...
```

That is a three-for-three match against routines already named on the DOS side:

| DOS | Apple | what it is |
| --- | --- | --- |
| `asm/4F11_monster_step_vector` | `$4054` (via vector `$4000`) | delta, `×4`, sign, store to `$17`/`$18` |
| `asm/4F36_monster_pursuit_vector` | `$406F` (via vector `$4003`) | the raw unclamped delta |
| `asm/50D8_sign_of_al` | `$44D5` (via vector `$4036`) | `-1 / 0 / +1`, byte for byte the same shape |

Same 32-byte-strided table pair (`$4D00`/`$4D20` vs `DS:137`/`DS:157`), same zero-page destination
pair, same `×4`, same shared sign routine. **The torus-shortest reading of the `×4` is original
1983 design, not a DOS-port invention.**

And it independently corroborates this morning's retraction. The 6502 writes `SEC` before `SBC`
because on the 6502 that is *required* for a plain subtract (`SBC` = `A − M − 1 + C`). The DOS port
translated it to `STC` before `SUB`, where it does nothing. Both sides compute the plain
difference. There is no `-1`, confirmed from two implementations.

**What this tells us about the cost of T3:** one sitting produced a three-routine match on the
first attempt. That is the optimistic case — these routines are small, adjacent, and in the runtime
library. Expect the questions that live in `ULTIMA.II.OBJ`'s 14 KB of game logic to be slower, and
expect some to be unresolvable because the decompiler cannot be trusted there.

### T1 — make the 6502 listing reproducible (prerequisite, small)

`Seed6502.java` says the seeds come from "scanning for JSR/JMP absolute targets (`tools/apple2_image.py`
plus a short scan)". That scan is not in the tree. Write it — `tools/apple2_seeds.py`, reading the
flat image, emitting hex addresses one per line — and commit it. Then re-run the § 8 recipe and
**keep the output** somewhere durable (`/var/tmp` is fine for the session; the *recipe* is what must
be committed).

Gate it: the scan should reproduce roughly the documented result (328 seeds → 328 functions, a few
junk ones landing inside an operand). If it returns 40 or 4000, the scan is wrong, not the game.

**Do not trust Ghidra's decompiler output as a source of claims.** Measured: the `JSR`-inline-string
idiom breaks its stack assumptions and it emits 18 KB blobs. It confirmed the printer. Read bytes
for anything you intend to assert.

### T2 — the crosswalk tool (the actual deliverable)

`tools/apple2_anchor_map.py`. Inputs: `original/ultimaii.exe`, the extracted Apple BINs, and the
load-address map. Output: one row per matched string.

Design constraints, in priority order:

1. **Emit no game text into git.** `AGENTS.md`'s text line is explicit: copyright protects game
   text exactly as much as a sprite, and the reconstruction in `asm/` already carries what it
   needs. A hand-handed duplicate in `docs/` or a committed TSV adds drift *and* exposure. So the
   committed artefact is **offsets, lengths, segment names, match status and a short hash of the
   string** — never the string itself. `tools/content_inventory.py`'s docstring is the model: it
   deliberately emits structure, never content. If a human needs to see the text, print it to
   stdout with a `--show` flag and keep it out of files.
2. **Match on the normalised string.** Apple text is stored high-bit-set per character
   (verified at the byte level at `$561F`); DOS text is plain. Mask with `& 0x7F` on the Apple
   side. Strip the `0x8D`/`0x0D` control wrappers the DOS side uses — `strings_atlas.py` already
   handles that and its docstring explains why `cfg_printed.py` once silently dropped a string
   over exactly this.
3. **Re-use `strings_atlas.py` for the DOS side.** Import it or shell out to it. Do not write a
   second DOS string scanner; that is the duplication this project punts on.
4. **Report order preservation per cluster.** The claim in § 4 is that clusters keep their order
   on both sides, and that is much stronger evidence than set membership. Compute it.
5. **It must be able to fail.** Negative control is already known and documented:
   **`--INVALID MOVE!` does not exist anywhere in the Apple build** (searched plain and
   high-bit-set). So the tool's self-test asserts: that string is present on the DOS side and
   **unmatched** on the Apple side. If it ever reports a match, the matcher is broken. That is a
   real gate, not a decorative one.

Expected honest outcome: the matched set is small. 35 out of 337 DOS strings is ~10%. That is fine
— say it in the output. The tool's value is that it turns "I think this is the same routine" into a
lookup with a citation.

### T3 — run the open questions through it

Ordered by how much a good answer is worth. For each: what is open, what to look for on the Apple
side, and **what a null result means** — because several of these will not move, and "the Apple side
is silent" is a usable answer that stops the next agent trying.

**Q0 — the step vector's ×4. ANSWERED 2026-10-28, see T0.** `sign(4 * (player - creature))` per
axis is present on the Apple side at `$4054`, with `ASL A` / `ASL A` feeding a shared sign routine
at `$44D5`, and the raw-delta twin at `$406F`. The torus reading is corroborated by a second
independent implementation, and the absence of the `-1` is corroborated too. What is left: write it
into `asm/4F11`, `asm/4F36` and `asm/50D8` as `# name-evidence:` lines citing the Apple addresses —
**corroborated, not observed, and it does not rename anything** (`AGENTS.md`: no Apple-derived name
goes into an `asm/` filename).

The remaining questions, unchanged:

**Q1 — the `0x5C` species clause.** Settled this month that `0x5C` is the force field
(`captures/spice86/g29-field`: exactly −1000 HP with no ring, nothing with one). The step gate
accepts it for `mon_type < 0x78` and refuses it above, and the `>= 0x78` reading as "the four NPC
classes" is **inferred from `type >> 2` being the art index**, not observed. Look for the same
split on the Apple side. A match upgrades the reading from inferred to corroborated; silence means
the clause stays inferred and stops the next agent trying to promote it.

**Q2 — the step-gate tile ladder.** `asm/2199_creature_step_gate` accepts exactly five of the 64
reachable tile bytes: `0x08`, `0x0C`, `0x70`, `0x5C`, and `0x00` for the two water kinds. The ladder
is observed; the *semantics* of `0x70` as "town ground" is inferred from a spatial distribution. A
second implementation of the same accept-set would be a genuinely independent witness. Note the
DOS-side gate is a compare ladder over byte values; the Apple side may express the same rule as a
table lookup, so match on the **accept set**, not the shape of the code.

**Q3 — `DS:1B7`.** Now understood as the per-slot saved terrain byte that gets written back into
the old cell when a creature moves (`asm/2233`/`asm/225A`, corroborated by `asm/0CEE` at spawn).
Three DOS sites, one meaning, still graded *inferred*. A fourth independent witness on the Apple
side would settle it.

**Q4 — the `DS:15..18` ← `DS:2CB/2DE/2E0/2F3` ticket.** This is the one place the plan has been
said to *require* a watchpoint, because those four bytes have no static writers anywhere. **It is
probably now dead**: the whole reason the ticket existed was to explain how a tile became mode
`0x2e`, and we now know `0x5C >> 1 == 0x2e` and that `0x5C` is the field. Check whether the Apple
side has an equivalent indirection at all. If it does not, the ticket is a DOS-port artefact and
should be closed as such rather than spent an afternoon on. Say which.

**Q5 — the coin-rub freeze.** `DS:26A` is the creature-move freeze counter; the only non-zero writer
is `asm/3C04_coin_rub` storing `0x14`. Measured live (A/B/A, `captures/spice86/g28-pursuit`) but
`coin_rub`'s entry has never appeared in `ExecutedInstructions` of any of the 60 harvests, so nobody
has seen a player trigger it. If the Apple build has the same `YOU RUB A COIN...` path with the same
counter, that is a second witness that the behaviour is intended and not a decode error.

### T4 — write it down where it belongs

Per `AGENTS.md` § 3: a corrected address→behaviour fact renames the `asm/` file and writes the
header **in the same commit**, and only the filename and `#` comment lines may change — labels are
body and CP0 md5s them. A corroborated-but-not-proven claim gets a `# name-evidence:` line citing
the Apple address and stays at `inferred`/`corroborated` grade. It does **not** get promoted to
`observed`, and it does not rename a file.

Update `docs/apple2-reference.md` § 4 with the real anchor count and a pointer to the generated
artefact. Add a line to `docs/harness-portability.md` § 6 if the method generalises — method only,
never addresses from this binary.

## Hard rules for unattended work

* **Never commit** `decompiled/`, `original/`, `references/`, `tools/ciderpress2/`. All gitignored
  for a reason. The Apple extractions are copyrighted game content.
* **Never write to `references/`.** Read-only. `cp2` is pointed at it for reads only.
* **No game text in committed files.** See T2 constraint 1.
* **`bash tools/assemble_clean.sh` and `make -C engine test` must be green** at any commit touching
  `asm/`, engine source, tests or tooling. Docs-only commits may skip the suite.
* **Scratch and logs to `/var/tmp`, never `/tmp`** (`/tmp` is tmpfs here and takes RAM from the
  model).
* **Never edit a file with a script; never heredoc a non-trivial script.** Use the edit tool; write
  the file, syntax-check it, run the file.
* **No live capture without the owner's approval.** Nothing in T1–T4 needs one. That is the point.

## Hazards already paid for — do not pay twice

* **The `.dsk` is `order=dos/sect`.** A linear byte read crosses the sector skew and splits the
  39-byte directory records; hand-deriving it produced a grid that parsed cleanly and attributed
  every file's size and load address to its neighbour. Use `cp2`. Never hand-parse the container.
* **A raw 6502 image yields zero functions in Ghidra.** No entry point, so auto-analysis finds
  nothing at all. That is why `Seed6502.java` exists, and why T1 is a prerequisite rather than an
  afterthought.
* **Ghidra's 6502 decompiler is weak on this code.** 18 KB blobs, broken stack assumptions.
  Orientation only. Read bytes for claims.
* **`analyzeHeadless` takes `-processor`, not `-language`.**
* **`CREATE` loads at `$6000` inside `ULTIMA.II.OBJ`.** It is an overlay; `apple2_image.py` refuses
  it unless you opt in. Do not silently stomp those bytes.
* **Two address bases, always.** `asm/` filenames are runtime offsets; `L_xxxxxx` labels inside them
  are file offsets; file = runtime + 0x200. On the Apple side, addresses are 6502 load addresses
  with no relation to either. `strings_atlas.py` prints both bases on purpose; do not fix that.
* **The "287 of 288 Apple-only strings" figure from the first anchor pass is an artefact** — the
  regex caught hi-res bitmap noise, not text. Do not cite it. Same trap as the `0x1A` byte that
  looked like a rare tile and was a container header.
* **The flat 6502 image covers `$4000..$AFFF` and holds only five segments** — `STD.SUBS.OBJ`,
  `ULTIMA.II.OBJ`, `DNGDRAW.OBJ`, `TABLES.903C.OBJ`, `BIOS.SUBS` (measured: 28672 bytes, and the
  manifest names the zero-filled gaps). Everything else is excluded because it would overlap:
  `SHAPES` at `$0800` and the `PIC.*` at `$2000` sit below the base, and `HTXT` at `$5000` plus
  `FL`/`II`/`WII`/`UL` at `$4000` collide with resident code. That is fine for the crosswalk — every
  anchor in § 4 lands in one of the five segments that *are* in the image — but the tool must read
  the extracted BINs directly for anything else, and must never treat a zero-filled range as code.
  Zero is a legal 6502 opcode; that is exactly why the manifest exists.
* **`$4000..$403E` in `STD.SUBS.OBJ` is a 21-entry JMP vector table, not code.** The byte-granular
  seed scan puts a seed on every third byte of it, so Ghidra reports functions there whose bodies
  are actually their *targets*' code. That is how `u2_6502.c` presented the step vector as
  `FUN_4000`. Verified by hand: `$4000` = `JMP $4054`, `$4003` = `JMP $406F`, `$4006` = `JMP $4080`
  (the printer), `$4036` = `JMP $44D5` (the sign routine). **Check any Ghidra 6502 claim against
  raw bytes, and exclude the vector range from seeds.**
* **Do not recall an ISA semantic; look it up or measure the value.** See the `SUB`/`SBB`
  retraction at the top. This is the single most transferable lesson of the month.

## Verification

```bash
# DOS side, already green — confirm before building on it
python3 tools/strings_atlas.py --selftest

# Apple side
./tools/ciderpress2/cp2 catalog "references/apple/Ultima II (4am crack)/Ultima II (4am crack) disk 1 - program.dsk"
python3 tools/apple2_image.py -o /var/tmp/u2_6502.bin

# the crosswalk, once it exists — must fail on the negative control
python3 tools/apple2_anchor_map.py --selftest        # asserts --INVALID MOVE! is UNMATCHED
python3 tools/apple2_anchor_map.py --summary

# the proof and the suite, at any commit that can break them
bash tools/assemble_clean.sh
make -C engine && make -C engine test
python3 tools/cmd_handlers.py --check
```

## Needs a person

1. **Push or keep local.** `main` is 22 commits ahead of `origin/main`. Pushing triggers CI.
2. **Any live capture.** Q0–Q5 are all static by design. If one turns out to need the emulator, stop
   and ask — do not spend an afternoon on a watchpoint that T3 Q4 may have just made unnecessary.
3. **Whether the crosswalk earns a permanent home in CI.** It is a gate that can fail, which makes it
   a candidate, but it depends on gitignored inputs (`references/apple/`, `original/`) and so cannot
   run on a clean checkout. Decide: keep it a local tool, or vendor a manifest of offsets (no text)
   so CI can at least check the counts.

## Definition of done

Not "the tool exists". Done is: **each of Q0–Q5 has a written verdict — corroborated, contradicted,
or Apple-silent — in the same commit that records it**, and the anchor count in
`docs/apple2-reference.md` § 4 is a measured number from the tool rather than a remembered 35.
Expect some of them to come back silent. That is a result, and it is what stops the next agent
spending a session on a question that has no second witness.
