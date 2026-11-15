# G22 — function-boundary proposal for `asm/19D7` and the region past its end

**Status: APPROVED AND EXECUTED 2026-10-23.** The owner ruled in favour of § 2 — merge
where the evidence is hard, leave `19D7..2027` alone — and added that charisma affecting
costs is known game behaviour. The three merges landed in `4644eb9`; § 6 below is the
execution record. What follows in § 1–5 is the proposal as written, kept intact because
the reasoning is the part worth keeping.

**Partly reversed 2026-11-15, and worth an owner look.** `19D7..2027` was split after all, into
`asm/19D7_magic_shop_menu.asm` (`19D7..1B6A`), `asm/1B6B_clue_pool.asm` (`1B6B..1D1C`) and
`asm/1D1D_shop_menus.asm` (`1D1D..2027`). Two things changed since the ruling: the span's
`1D6A..1F1C` was proved to be ten NUL-separated clue strings drawn as instructions rather than
code, and `make content-check` requires a committed string to be present *as data* in `asm/`,
which a `.byte` forced-keep is not. So the split corrected a false body and unlocked the clue
text, and `assemble_clean.sh` still rebuilds the binary byte-ident. The § 2 argument for leaving
the range alone — that the boundaries are uncertain — was weakened by the same finding, since the
pool boundary is exact. Flagged here rather than slipped in, because it overrides a ruling.

Splitting a function boundary is a Phase 2/3 change to the byte-proof harness and needs the
owner's approval, plus a `check_func.py` run per new range. Opened by ticket G22; this
document was task A5 of `docs/overnight-2026-10-23.md`.

Grades per `AGENTS.md` § 5. Every address below is **observed** from
`original/ultimaii.exe` (runtime = file − `0x200`) or **measured** from the `asm/` tree.

## 1. The premise in G22 was half wrong, and the correction matters

G22 and `docs/commerce.md` § 3 recorded that the first observed purchase executed
`2052..2130`, entirely **past** `asm/19D7_magic_shop_menu.asm`'s declared end of `2027`,
and concluded the span was "short at the top" and that a split "must extend the range
upward".

**It must not.** Three measurements say the region past `2027` is not shop code at all:

1. **Only three addresses in `2028..2130` are inbound `call` targets**, and their caller
   sets are broad, not shop-local:

   | entry | inbound callers |
   | --- | --- |
   | `2028` | `15E1_tavern_menu`, `1869_cleric_shrine`, `19D7_magic_shop_menu`, `3B92_cmd_M`, `3C2E_cmd_O`, `3E9F_sub_3e9f`, `43F3_cmd_W` |
   | `2052` | `15E1_tavern_menu`, `1869_cleric_shrine`, `19D7_magic_shop_menu`, `3C2E_cmd_O`, `4282_sub_4282` |
   | `20AB` | `17B2_sub_17b2`, `1869_cleric_shrine`, `19D7_magic_shop_menu` |

   Seven, five and three callers respectively, spread across the tavern, the shrine, the
   shop, three command handlers and a fourth routine. **These are shared helpers.** A
   routine that seven unrelated callers invoke is not a fifth shop menu.

2. **The only `ret` instructions in the whole span `19D7..2140` are at `2051`, `20AA` and
   `2121`** — three of them, and each is the last byte before the next call target. The
   shop handlers inside `19D7..2027` contain none; they end in tail `jmp`s to the
   return-to-prompt path, which is why they read as menu blocks rather than functions.

3. **The fragments already tile `2028..2130` exactly**, with no gap and no overlap:
   `2028, 202C, 2031, 2035, 2039, 2048, 204D, 2052, 2070, 20A4, 20A7, 20AB, 20CF, 20D7,
   20EB, 2121`. Fifteen pieces for three routines. The mechanical clean wave cut on every
   branch target instead of on every *routine* boundary.

So the transaction did not run "past the end of the shop file". It ran **through three
shared helpers that already have their own files**, reached by ordinary `call`s from the
shop. The commerce code is not misfiled at the top of `19D7`; it is correctly inside
`19D7..2027` and it calls out.

## 2. Proposed boundaries for `2028..2130` — merge, do not extend

| proposed range | len | evidence | what it does |
| --- | --- | --- | --- |
| **`2028..2051`** | `0x2A` | entry = `call` target ×7; ends at the `ret` at `2051` | **Read one decimal digit from the keyboard.** Loops on a key read (`L_00514d`), rejects `AH != 0xFF` and anything outside `'0'..'9'`, converts with `stc; cmc; sbb $0x30; cmc`, keeps the value in `DI`, echoes the digit and then `0x8D`, and returns with `AL = digit`. Merges the seven fragments `2028, 202C, 2031, 2035, 2039, 2048, 204D`. |
| **`2052..20AA`** | `0x59` | entry = `call` target ×5; ends at the `ret` at `20AA` | **Charge `DS:27/28` to gold.** BCD `cmc;sbb;das;cmc` against `DS:58/59`; the trailing `cmc` inverts the flag so `jb` is the **affordable** path and tails to `20A7` (`mov $0,%al; ret`) with gold already reduced; the fall-through at `2070` adds the pair back and prints `YOU DONT HAVE THAT MUCH!`. Merges `2052, 2070, 20A4, 20A7`. |
| **`20AB..2121`** | `0x77` | entry = `call` target ×3; ends at the `ret` at `2121` | **BCD multiply by repeated addition.** Seeds `DS:27/28` and `DS:29/2A`, derives a loop count from `popcount([DS:50] + [DS:4E])` — int plus charisma — sets `DI` to it, and the loop at `20EB` adds `DS:27/28` into `DS:29/2A` under `dec %di; jne`. Merges `20AB, 20CF, 20D7, 20EB, 2121`. |
| **`2122..`** | — | inbound `jmp` from `0B36`, `0B4A`, `107F` | A separate routine reached by jump, not part of any of the three. Its end is outside the window this document examined; **not proposed here.** |

Naming is deliberately left out of the proposal — `20AB`'s stat-derived count is the
open half of G23 and naming it before that settles would be a guess in a filename.

## 3. The real G22 defect is still inside `19D7..2027`

That is the part of the ticket that survives the correction. The declared span holds at
least four distinct shop handlers — armour (`1D20`/`1D39`), weapons (`1E52`/`1E69`),
horse (`1F93`), clue seller (`1B11`) — plus the magic shop the file is named after, each
ending in a tail `jmp` to the prompt path rather than a `ret`.

Because they end in tail jumps, **the `ret`-based boundary detector used in § 1 cannot
find them**, and neither can a call-target scan. The evidence for their boundaries is
different and each candidate needs to be stated with its own kind of proof:

| candidate | evidence available | what would prove it |
| --- | --- | --- |
| `1B11` clue seller | first string of the run; `1AE0` is a `jmp` target from `15BA` | the `jmp` target is an entry; `check_func.py 1AE0` proves the bytes from there |
| `1D39` armour shoppe | string label site; falls through from the handler above | only a tail-jmp/`jmp` target makes it a boundary; a fall-through does **not** |
| `1E69` weapons shoppe | same shape as the armour site | same |
| `1F93` horse shoppe | same shape | same |

**This is the honest limit of the proposal.** Four of these are *fall-through* neighbours,
not dispatched entries, and a fall-through boundary is a judgement about where one
routine ends and the next begins, not a fact the bytes assert. Splitting them would
invent boundaries the original does not contain. The recommendation is therefore:

* **Do** merge `2028..2051`, `2052..20AA`, `20AB..2121` — three routines with hard
  call-target and `ret` evidence, currently cut into fifteen files.
* **Do not** subdivide `19D7..2027` on fall-through grounds. If it is split at all, split
  it only at addresses that are `jmp` targets from elsewhere (`1AE0`, `1D1D`, `1E4F`,
  `1F75` are the ones with inbound jumps in the tree), and say in each header that the
  boundary is a dispatch target, not a return.

## 4. What `check_func.py` would need per new range

`tools/check_func.py <SEG>` assembles `asm/<SEG>_*.asm` in isolation, `.org`'d to its true
file origin, stubs out-of-function `L_xxxxxx` labels at their real addresses, and requires
the assembled bytes over `[start, end]` to equal `original/` **and** the body length to
match `disasm/function-map.tsv` exactly. So for each proposed range:

1. **`disasm/function-map.tsv` must carry the range first.** The length check reads the
   map, not the file, so a new or merged range with no map row fails at the length
   assertion rather than at the byte comparison. Add or amend the row before creating the
   file.
2. **The `# func` header must agree with the map row** (`START..END`, inclusive, and the
   `len` field), or the two sources disagree and the run is meaningless.
3. **Every branch that leaves the range must stay an out-of-function `L_xxxxxx`** so the
   stub mechanism encodes it. The three merges are safe on this point: their internal
   branches (`2039`'s retry loop, `206E`'s `jb 20A7`, `211F`'s `jne 20EB`) become
   intra-range once merged, which is the actual benefit — three of today's fifteen
   fragments branch into the next one.
4. **Run it and require exit 0**, then run `bash tools/assemble_clean.sh` for the whole
   tree and require `CLEAN REBUILD BYTE-MATCH: 37344 bytes identical`. A merge that is
   byte-correct per function but shifts a total will show up only there.
5. **`make -C engine test`** afterwards, because CP0 md5s every non-`#` line of `asm/` and
   a merge moves body lines between files. Labels must not be renamed while doing it —
   `L_xxxx` labels are body.

Suggested order, cheapest proof first: `2052..20AA` (five callers, one `ret`, contains a
string so a mis-slice shows immediately), then `2028..2051`, then `20AB..2121` (largest,
and its `20EB` loop is the one whose boundary is easiest to get wrong).

## 5. Corrections this forces elsewhere

* `docs/commerce.md` § 3 said the transaction body is "entirely **outside** that file's
  declared range" and that a split "must extend the range upward". **Wrong**, per § 1
  above; amended 2026-10-23 when the owner ruled on this proposal.
* `docs/commerce.md` § 3b's description of `asm/2052`'s branch polarity was corrected in
  place on 2026-10-23: the trailing `cmc` inverts the flag, so `jb` is the **affordable**
  path. Simulated against the instruction sequence: `9999 − 0200 → 9799` taken,
  `9959 − 0040 → 9919` taken, `0020 − 0200` not taken, `0004 − 0005` not taken.

## 6. Execution record, 2026-10-23

### The prerequisite nobody asked for: CP0 could not survive a merge

`engine/tests/run_cp0.sh` md5'd each changed `asm/` file's non-comment lines against HEAD
and treated **any deletion as a failure**. A merge deletes files, so the gate would have
reported this exact work as vandalism — six of the sixteen files here are deletions.

The invariant was restated rather than relaxed. What must not change is the tree's
**assemblable content**, not which file a line lives in. `engine/tests/asm_body_guard.py`
now builds a canonical stream — every `asm/*.asm` sorted by the start address in its
filename, non-comment lines concatenated — and compares it against HEAD. Because the
fragments tile their ranges contiguously, ordering by start address makes the stream
independent of where the boundaries sit: renames, merges and splits preserve it, and any
body edit, insertion, deletion or reorder breaks it.

Demonstrated failing before being trusted, per `AGENTS.md`:

| case | result |
| --- | --- |
| one body line `das` → `daa` in `asm/2052` | **FAIL**, naming body line 5744 and both values |
| a whole `asm/` file removed | **FAIL**, "13 body line(s) at HEAD now missing" |
| clean tree | PASS, 26080 lines, 1300 → 1300 files |
| after all three merges | PASS, 26080 lines, 1300 → 1287 files |

Both perturbations were reverted; `asm/` was untouched by the gate commit itself.

### What was merged

| new file | from | size | inbound callers |
| --- | --- | --- | --- |
| `asm/2028_read_decimal_digit.asm` | 7 fragments | 42 B | 7 |
| `asm/2052_gold_spend_price.asm` | 4 fragments | 89 B | 5 |
| `asm/20AB_bcd_price_from_stats.asm` | 5 fragments | 119 B | 3 |

Executed cheapest-proof-first, as § 4 suggested. `check_func.py` PASS per range;
`assemble_clean.sh` → `CLEAN REBUILD BYTE-MATCH: 37344 bytes identical`; `make -C engine
test` CP0..CP4 all PASS; `xref_provenance --strict` **136/136**, up from 124/124, because
the merged files carry `name-evidence:` headers the fragments never had.

### What the merge exposed, and the doubt it left

Writing the headers forced a full read of `20AB`, and two claims made in a hurry earlier
the same day turned out to be wrong. Both are recorded in the file header and in
`docs/commerce.md` § 3, not silently fixed:

* the loop count is the **bit length** of `int + cha`, not a popcount;
* the loop is a **Fibonacci-style BCD walk** seeded 4,4, not a multiply by repeated
  addition. The first simulator of it also had `DAA` subtracting 6 instead of adding, and
  printed plausible nonsense (`01A6` where the answer is `0012`) — a model that agrees
  with nothing but itself.

The corrected model yields `0020` for the tavern call on a maxed character where the game
printed `0200`: the same digits, transposed across the pair. **That discrepancy is open,
and the routine is named for what it demonstrably does rather than for the price it has not
yet reproduced.** The owner's confirmation that charisma affects costs supports the
direction of the mechanism; it does not close the arithmetic. G23 now carries the three
separate unknowns as a falsifiable capture.

`19D7..2027` was left alone, as recommended. Its four handlers end in tail jumps, so a
fall-through boundary there would be a judgement presented as a fact.
