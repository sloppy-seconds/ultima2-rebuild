# Apple II reference disks (UCSD p-System volume set)

Owner added `references/apple/Ultima II (4am crack)/*.dsk` alongside `references/ultima3/`.
Nothing in this tree referenced them until the session that produced this file, when they
were read, parsed and extracted.

They matter because they are the **other half of the lineage claim** in `AGENTS.md` § 1.
That section asserted the DOS port is a translation of Garriott's 6502. Until now that
was an assumption carried in prose. It is now something we can test byte by byte, which
changes Step 3 of gameplay-logic work from "guess at 6502 idioms" to "read the 6502".

## 0. Status: reference only, not gated

Owner ruling, 2026-11-23: nothing in `tools/check_all.sh` gates the Apple II side. The disks and
their Ghidra decompilation are here so the lineage claim can be re-checked if it is ever doubted,
not so a gate has to stay green between doubts. Gating them bought noise: the inputs are gitignored
by design, so the three gates skipped in CI by accident and false-alarred `STALE` in a clean tree.
The tools remain and are run by hand when the reference is consulted, and the § 8 crosswalk is a
recorded finding rather than a checked invariant.

## 1. What the images actually are

Three 140 KB unadorned images. `cp2` identifies disk 1 as:

```
Disk image (Unadorned, order=dos/sect) - 140KB ProDOS "ULTIMA2", 17 blocks free
```

Two things that cost time, recorded so nobody pays them twice:

* **`order=dos/sect`.** The `.dsk` is in DOS sector order, not ProDOS block order. A
  linear byte read across the image crosses the physical-to-logical sector skew, so the
  39-byte directory records split out of sequence. Hand-deriving the skew produced a grid
  that looked right and was off by exactly one record, which mislabelled every file after
  the first: it reported `ULTIMA.II.OBJ` as 1597 bytes when it is 13961. The parse was
  plausible and wrong, which is the shape `AGENTS.md` warns about. Use the tool.
* **The volume carries a UCSD p-System runtime** (`MI.SYSTEM`, `MI.HELLO`, `CREATE`,
  `STD.SUBS.OBJ`, `UPDATE.OBJ`, `BIOS.SUBS`, and a `PRODOS` boot file). That scaffolding is
  *not* the game and is not p-code. The game itself is native 6502 machine code in `BIN`
  files. Do not conclude "the Apple version is p-code" from the file names.

## 2. Extraction

`tools/ciderpress2/` holds the CiderPress2 `cp2` CLI, v1.2.0, linux-x64 self-contained.
It is GPL-3.0, vendored and gitignored exactly like `tools/spice86/` and `tools/reko/`:
infrastructure, re-fetchable, never committed.

```bash
D="references/apple/Ultima II (4am crack)/Ultima II (4am crack) disk 1 - program.dsk"
./tools/ciderpress2/cp2 catalog "$D"          # types, aux, sizes
./tools/ciderpress2/cp2 extract --exdir=decompiled/apple2 "$D"
```

`extract` is the verb, not `export`; `--exdir` is the destination. `BIN` files come out as
the raw memory image at their load address, no header to strip.

Output goes under `decompiled/`, which is gitignored. **The extracted blobs are copyrighted
game content and must never be committed**, same rule as `original/` and `references/`.
`references/` stays read-only; `cp2` is only ever pointed at it for reads.

## 3. File map, disk 1

Aux type on a `BIN` under ProDOS is the 6502 load address, so this table is a memory map.

| file | load | bytes | reading |
| --- | --- | --- | --- |
| `ULTIMA.II.OBJ` | `$5000` | 13961 | the world. 229 `JSR $4006` calls, 265 inline strings |
| `CREATE` | `$6000` | 1597 | character creation overlay (overlaps `ULTIMA.II.OBJ`, so it is an overlay) |
| `DNGDRAW.OBJ` | `$8900` | 1813 | dungeon wireframe renderer, holds the shape tables |
| `SHAPES` | `$0800` | 2048 | vector shape data for the dungeon view |
| `TABLES.903C.OBJ` | `$903C` | 1220 | filename is its own load address; opens `01 02 04 08 10 20 40`, the HGR bit table |
| `BIOS.SUBS` | `$9700` | 2592 | I/O and status-line subs, opens with a `JMP` vector table |
| `STD.SUBS.OBJ` | `$4000` | 2787 | runtime library, opens with a `JMP` vector table. **`$4006` is one of those vectors, not code** |
| `FL`, `II`, `WII`, `UL` | `$4000` | 2981/1419/734/2617 | **data, not code and not overlays** — see § 3b |
| `PIC.CAS/DNG/MIN/OUT/RUS/SPA/TWN` | `$2000` | 8184 each | double hi-res pictures, one per scenario |
| `HTXT` | `$5000` | 2048 | hi-res text page |

The overlay structure is itself a finding: `CREATE` at `$6000` and `ULTIMA.II.OBJ` at
`$5000` overlap, so the game swaps segments rather than living in one flat image.

## 3b. `FL`, `II`, `WII` and `UL` are data, and they are one format

Closed 2026-10-28c. All four were the last unnamed files on disk 1. The answer is **data**, and
the interesting part is that all four are the *same* structure.

**They are not code.** Three signatures separate the known code files from these four, and all
three agree:

| file | `JSR $4006` (the inline-string printer) | leading `JMP $xxxx` vector table | printable runs |
| --- | --- | --- | --- |
| `ULTIMA.II.OBJ` | 229 | 0 | 117 |
| `STD.SUBS.OBJ` | 6 | **28** | 20 |
| `BIOS.SUBS` | 18 | **20** | 12 |
| `CREATE` | 0 | 0 | 6 |
| `FL` / `II` / `WII` / `UL` | **0** | **0** | **0** (only byte runs that happen to be ASCII) |

**They are not overlays either, and the catalog says so without any content analysis.** Every
file on disk 1 dated `14-Dec-88` or later is code (`STD.SUBS.OBJ`, `DNGDRAW.OBJ`, `CREATE`,
`ULTIMA.II.OBJ`, `BIOS.SUBS`, `UPDATE.OBJ`). Every file in the `26-Nov-88 15:36..15:39` batch is
content: `SHAPES`, `TABLES.903C.OBJ`, the seven `PIC.*`, and these four. Two export sessions,
and the four unknowns are on the content side of the line. The shared `$4000` aux type that made
them look like library overlays is not a discriminator — `PIC.*` load at `$2000` and `SHAPES` at
`$0800`, so the aux type is where each loader wanted them, not what they are.

**The format, measured.** Each file is a strictly monotonic sequence of records:

```
record := dest:2 (little-endian, $2000..$3FFF)  payload:*  $00
```

Parsing that rule from offset 0 consumes **99% of every one of the four files** with **zero
out-of-order destinations** — 124 records in `FL` (`$205d..$3fe5`), 204 in `II`
(`$2067..$3ff3`), 125 in `WII` (`$2067..$3fc5`), 441 in `UL` (`$202b..$3fa2`). A code file, or a
picture, does not survive that parse; the parse failing is the negative control and it is what
makes the result worth writing down.

**And every destination lands inside the picture.** The seven `PIC.*` files are 8184 bytes
loading at `$2000`, so a picture occupies `$2000..$3FF7`. The highest destination anywhere in the
four tables is `$3ff3` and the lowest is `$202b`: **all 894 records address bytes inside the
`PIC.*` window.** Measured, and not the kind of fit that happens by accident.

What that probably means, **inferred and not proven**: these are patch tables applied to a
backdrop already in memory — byte runs written into a loaded picture. `FL`'s payloads are 94%
classic 7 MHz-artifact bytes (`$aa`, `$d5`, `$55`, `$2a`, `$95`), which is what a double-hi-res
byte run is made of; `UL` is 50%, `II` 20% and `WII` 0%, so if this reading is right the four are
not four instances of one thing and `WII` is the odd one out. `II` and `WII` share 71 destination
addresses with only 2 of them byte-identical — same layout, different content, which is what two
variants of one screen would look like.

**What is not claimed:** what any record draws, why the destinations skip around instead of
trending with file position, and what the four names stand for. None of it is needed for the
classification, and per § 6 none of it may be used to derive DOS behaviour anyway. The extracted
files are gitignored game content, so re-derive from `references/apple/` with the § 2 recipe
before quoting any number here.

## 4. Cross-ISA anchors

**Measured, not remembered: 27 distinct strings match verbatim (28 DOS rows), out of 453 DOS
strings — 6.0% of the corpus by distinct text — in 17 clusters, and every cluster preserves
order on both sides.** The figure this section used to carry — "35 strings" — was a remembered
count from the first anchor pass and it does not survive measurement. The number of record comes
from `tools/apple2_anchor_map.py`, and the artefact is `disasm/apple2-anchors.tsv`, both
generated:

```bash
python3 tools/apple2_anchor_map.py --selftest    # controls; must pass
python3 tools/apple2_anchor_map.py --summary     # counts and clusters
python3 tools/apple2_anchor_map.py -o disasm/apple2-anchors.tsv
```

The old prose table of matched strings is **gone**, replaced by the generated TSV. Not for
tidiness: it was a hand-maintained duplicate of game text in `docs/`, which is the category
`AGENTS.md` forbids, and it had already drifted — it listed `MAGIC MISSILE` for a string that
ends in `!`, and `ANOS: LEGENDS!` which the DOS side cannot even see. The TSV carries the same
information in the only form that cannot rot: DOS file offset, DOS runtime offset, owning `asm/`
function, Apple address, segment, length, and `sha256[:10]` of the normalised text. `--show`
prints the text itself to stdout for a human and refuses to write it to a file.

Two things the tool's design is doing on purpose, because both change the number. The DOS side
is `tools/strings_atlas.py`, imported rather than reimplemented, so every match carries an
owning `asm/` function. It used also to be capped at what `asm/` renders as data, and that cap
hid the single best cluster in this table: three of the five `ANOS:` era lines sit in spans
`asm/` draws as instructions, so the crosswalk could only ever confirm two of them. Since the
atlas grew a second source — the `str_out` calling convention, which does not care how anything
is rendered — the era table matches as **five anchors, in order, on both sides**, which is the
strongest single cluster here and the one the prose table above always claimed. The Apple side
requires every byte of a candidate to be high-bit-set and printable once masked, terminated by
`$00`; a generic printable-run regex is what produced the "287 of 288" artefact in § 6.

The known-limit of the verbatim rule, found the hard way: the Apple build's case and punctuation
can differ from DOS — the coin-rub text at `$7DC6` is sentence-case with four dots against DOS
upper-case with three — and the crosswalk misses those. A targeted search found that one; the
tool did not.

Order is preserved on both sides in every cluster. That is the anchor: a routine that prints the
same bytes at `$8153` in the Apple tree as the one that prints them at DOS file offset `0x41FD`
is the same routine, and the 6502 around it is the specification the 8086 was translated from.

### What the anchors have actually settled

Six correspondences, each hand-checked against image bytes rather than against Ghidra's
decompiler output, and each recorded where `AGENTS.md` says it belongs — in the owning `asm/`
file's header as a `# apple2-twin:` line, at corroborated grade, renaming nothing:

| DOS | Apple | verdict |
| --- | --- | --- |
| `asm/4F11_monster_step_vector` | `$4054` (vector `$4000`) | same per-axis delta, `x4`, shared sign call, same table pair |
| `asm/4F36_monster_pursuit_vector` | `$406F` (vector `$4003`) | the raw unclamped delta |
| `asm/50D8_sign_of_al` | `$44D5` (vector `$4036`) | `-1/0/+1`, three exits, same shape |
| `asm/2199_creature_step_gate` | `$6672` | clause-for-clause: freeze counter, the two water kinds, the `0x78`/`0x5C` split, the refuse set, the site guard |
| `asm/2233` + `asm/225A` | `$66C5` + `$6701` | the save-and-restore pair behind the per-slot terrain byte |
| `asm/3C04_coin_rub` | `$7DC6` | print, then store the same counter value into the same freeze byte |

The table mapping that falls out of the last three — `$4D00`/`$4D20`/`$4D60`/`$4D80` =
`DS:137`/`157`/`197`/`1B7`, and the zero-page scratch `$F0`/`$F4`/`$F5`/`$D9`/`$4E14` =
`DS:1F`/`23`/`24`/`26A`/`4A` — is corroborated by repeated independent use, not proven, and it
is written into the headers of the files that discovered it rather than into a hand-maintained
index here.

One question came back **silent**, which is also an answer: the DOS movement-mode table copy at
`asm/5B71_mode_table_copy` has no Apple counterpart — no copy-block shape, no tile-indexed
shift-and-read, and the Apple's step bytes are written only by the routines that compute them.
See that file's header.

`JSR $4006` occurs 253 times across the volume, 229 of them in `ULTIMA.II.OBJ`. It is the
same idiom as the DOS `str_out`: `JSR` to the printer, inline high-bit-set string, `00`
terminator.

### The string printer, in full

This is the first routine the cross-reference settled, and it is worth writing out because it
fixes the calling convention every other anchored routine will be read against. `$4006` is a
vector: `4C 80 40` = `JMP $4080`. The body is at `$4080` in `STD.SUBS.OBJ`:

```asm
$4080: 68        PLA           ; pull the JSR return address: it IS the string pointer
$4081: 85 FE     STA $FE
$4083: 68        PLA
$4084: 85 FF     STA $FF
$4086: A0 00     LDY #$00
$4088: E6 FE     INC $FE       ; 16-bit advance: INC lo, and INC hi only if lo wrapped
$408A: D0 02     BNE $408E
$408C: E6 FF     INC $FF
$408E: B1 FE     LDA ($FE),Y
$4090: F0 08     BEQ $409A     ; $00 terminator ends the string
$4092: 09 80     ORA #$80      ; text is stored plain, emitted with the high bit set
$4094: 20 12 97  JSR $9712     ; char out, in BIOS.SUBS
$4097: 4C 88 40  JMP $4088
$409A: A5 FF     LDA $FF
$409C: 48        PHA           ; push the resume address back ...
$409D: A5 FE     LDA $FE
$409F: 48        PHA
$40A0: 60        RTS           ; ... so RTS lands on the byte after the terminator
```

Three consequences for the reconstruction:

* The call chain is three tiers, `ULTIMA.II.OBJ` -> `JSR $4006` -> `JMP $4080` -> `JSR $9712`.
  A harness that treats the vector as the function will mis-scope every caller.
* The printer **consumes and rewrites the return address**, so the caller's `JSR` is really a
  "pass the address of the following bytes" mechanism. This is the same contract the DOS tree
  records for `str_out`, which is the strongest single piece of evidence that the two binaries
  share ancestry rather than merely sharing assets.
* The string pool is stored **high-bit-set on disk**, verified at the byte level: `$561F` holds
  `cd c1 c7 c9 c3 a0 ... a1 0d 00` = `MAGIC MISSILE!` + CR + `$00`. So `ORA #$80` in the
  printer is redundant for these strings and exists to let the same routine emit plain text as
  well. This differs from the earlier assumption worth stating plainly, because `tlkxNN` on the
  DOS side is also high-bit-set: here the convention is on disk *and* at emit, and the
  terminator is a plain `$00` while the preceding CR is a plain `$0D`.

## 5. 6502 idioms confirmed in place

Observed directly, each one a claim the working brief made on assumption:

* zero-page globals with boundary checks: `A5 01 C9 20 B0` = `LDA $01; CMP #$20; BCS`
* `$4Exx` global tables: `AD 14 4E` = `LDA $4E14`; fill loop `A2 07 A9 01 9D A0 4D CA 10 FA`
* 8-bit carry arithmetic on multi-byte counters: `AD 1D 4E 69 01 8D 1D 4E`, and the indexed
  form `BD 60 4E 69 01 9D 60 4E`
* BCD: `F8`/`18` = `SED`/`CLD` bracketing decimal adds, which independently corroborates the
  `cmc; sbb; das; cmc` idiom already recorded on the DOS side

## 6. Limits on what this evidence may prove

* **This is the 1989 Origin re-release, not the 1982 original.** Disk dates are Nov 1988 to
  Feb 1989. It is a sibling or a descendant of the shipped Apple build, not necessarily the
  build the DOS port was made from. Where the two disagree, `original/ultimaii.exe` stays
  ground truth for this project and the Apple side is evidence about intent.
* **No Apple-derived name goes into an `asm/` filename.** `AGENTS.md` requires a discovered
  name to be evidenced by *this* binary. A 6502 routine that looks like the same function
  supports a hypothesis and a `# name-evidence:` line; it does not on its own rename a file.
* **Shared text does not mean shared behaviour, and the first probe already says so.**
  `--INVALID MOVE!`, the DOS reject echo at `asm/0A02_invalid_move_msg.asm`, **does not exist
  anywhere in the Apple build** — searched plain and high-bit-set across every resident
  segment. The era table and `ANKH` are present, so this is not a search artefact. Either the
  DOS port added the reject message, or the Apple build rejects a move silently or words it
  differently. Until that is settled, the walk loop cannot be aligned by message anchor, and
  no contract for movement may cite an Apple routine as the origin of the DOS reject path.
* **Dungeons stay parked.** `DNGDRAW.OBJ` and `SHAPES` are the first-person wireframe, and
  nothing in them settles how the overworld or a town draws or stamps anything.
* The "287 of 288 Apple-only strings" figure from the first anchor pass is an artefact: the
  regex caught hi-res bitmap noise, not text. Do not cite it.

## 7. Disks 2 and 3: the data correspondence

Disk 2 is volume `PLAYER`, disk 3 is volume `GALACTIC`. Both hold a subdirectory `M` of
per-sector files, and the naming maps onto the DOS side one-for-one:

| Apple | bytes | DOS | bytes | relation |
| --- | --- | --- | --- | --- |
| `M/AP.NN` | 4096 | `mapxNN` | 4224 | **first 4096 bytes identical** on 31 of 33 sectors |
| `M/ON.NN` | 256 | `monxNN` | 384 | **first 256 bytes identical**, tested 5 of 5 |
| `TLK.NN` | 256 | `tlkxNN` | 384 | same shape, content differs (~100/256 equal) |

Every DOS file is exactly 128 bytes longer than its Apple counterpart, and in all three
families the extra block begins with `0x1A`. See `docs/map-format.md` § 2 for what that
proves about the tail and what it does not.

Two things to know before using these as evidence:

* **Sector numbers are not unique across disks.** `AP.10` and `AP.15` exist on both disk 2 and
  disk 3 with completely different content (44.8% and 35.4% of cells differ). The disk selects
  the scenario set; the two digits do not identify a world on their own. The DOS build carries
  one flat `mapxNN` set, so where the two Apple disks disagree, disk 2 is the one that matches
  DOS.
* **`TLK` content genuinely differs between platforms**, unlike `AP` and `ON`. Do not treat
  Apple dialogue data as a cross-check on `tlkxNN` until that difference is explained.

Also on these disks and not yet examined: `MONSTERS.` (2048, note the trailing dot),
`PLAYER` (256), `STARS.OBJ` (1899, loads `$1000`), `STARTUP` (1303), `END` (316), and
`SHAPES`/`DNGDRAW.OBJ` duplicated from disk 1.

## 8. Ghidra 6502 import

Working, and the decompiler reproduces what the hand disassembly says, which is the point of
doing it this way. The whole chain is scripted — `tools/apple2_ghidra.sh` runs extract → image
→ seeds → import → decompile and prints the four md5s at the end, so a quoted C file can be
diffed against the index it came from. The by-hand recipe it wraps:

```bash
source tools/env.sh
python3 tools/apple2_image.py -o /var/tmp/u2_6502.bin
python3 tools/apple2_seeds.py -o /var/tmp/u2_6502_seeds.txt
WORK=$(mktemp -d /var/tmp/u2-ghidra-6502.XXXXXX); mkdir -p "$WORK/proj"
"$GHIDRA_HOME/support/analyzeHeadless" "$WORK/proj" u2apple -deleteProject \
    -import /var/tmp/u2_6502.bin \
    -loader BinaryLoader -loader-baseAddr 0x4000 \
    -processor "6502:LE:16:default" \
    -scriptPath tools/ghidra \
    -postScript Seed6502.java seeds=/var/tmp/u2_6502_seeds.txt \
    -postScript DecompileAll.java out=/var/tmp/u2_6502.c index=/var/tmp/u2_6502.tsv
```

Three things that are not obvious:

* **The flag is `-processor`, not `-language`.** `analyzeHeadless` rejects `-language` with
  `Bad argument`; the language spec goes in `-processor "6502:LE:16:default"`.
* **A raw image yields zero functions.** Ghidra has no entry point to start from, so
  auto-analysis finds nothing at all. `Seed6502.java` creates functions from a list of JSR/JMP
  absolute targets, and `tools/apple2_seeds.py` is that scan — byte-granular, so a handful of
  seeds land inside an operand and Ghidra reports `Unable to resolve constructor` for them.
  **The scan must exclude `$4000..$403E`.** That range is `STD.SUBS.OBJ`'s 21-entry JMP vector
  table, not code, and a byte-granular seed lands on every third byte of it: Ghidra then
  decompiles the vectors and reports their *targets*' bodies under the vectors' addresses, so
  the first import presented the monster step vector as `FUN_4000`. With the vectors excluded
  the scan gives 309 seeds → 309 functions and `$4054`/`$406F`/`$4080` decompile to the step
  vector, the raw delta and the printer. The committed index is `disasm/apple2-functions.tsv`
  (generated by `tools/apple2_index.py`); it carries seed-to-seed spans, not Ghidra's
  `body_bytes`, which reports 1 for every function in this import.
* **`tools/apple2_image.py` refuses overlapping segments.** `CREATE` loads at `$6000` inside
  `ULTIMA.II.OBJ`, so it is an overlay and is opt-in via `--with create` rather than silently
  stomping bytes. The manifest prints the zero-filled gaps, because zero is a legal 6502 opcode
  and an unloaded range must not look like code.

The decompiler's output for `$4080` matches the hand disassembly in behaviour, including
`unaff_retaddr` becoming the string pointer and the `| 0x80` at emit. A wrong load base would
not produce a coherent printer, so this is also the confirmation that the segment map is right.

## 9. Next

1. ~~Import `ULTIMA.II.OBJ` and `STD.SUBS.OBJ` into Ghidra~~ done, see § 8. ~~Prune the junk
   seed functions~~ done 2026-10-28d: the junk was not junk, it was the vector table, and the
   scan now excludes it. ~~Name routines from the anchor table~~ done for the six the first
   pass could reach — see § 4 "What the anchors have actually settled". Next there: the
   remaining questions are in `docs/archive/handoff-apple2-crosswalk.md` T3, and the ones that did not
   move are recorded as silent rather than left open.
2. ~~Resolve `FL`, `II`, `WII`, `UL`~~ done 2026-10-28c, see § 3b: data, one shared record
   format, every destination inside the `PIC.*` picture window. What is left there is what § 3b
   explicitly declines to claim — what the records draw and what the names mean.
3. ~~Catalog disks 2 and 3~~ done, see § 7. Next there: explain the `0x1A` +128 block, and
   the sector `10`/`15` content deltas if they ever matter.
4. Then the actual deliverable: behavioural contracts, starting with the overworld movement
   tick and time-door spawning.
