# Live capture findings — 2026-10-21

First session running the original under the vendored Spice86 with live memory
instrumentation. Everything here is observed, not inferred. Where it contradicts
the static analysis in `docs/phase4-implementation.md`, this file is right and
that file carries a retraction.

Tooling: `tools/spice86_start.sh`, `tools/spice86_stop.sh`, `tools/spice86_common.sh`.
Session: sector 20, character `TEST`, run dir `tmp/20261021-063019/`, harvest
`captures/spice86/create-test-live/` (901 blocks, 147 partitions).

## 1. Overworld passability is real and tile-based

The overnight CP4b claim that the walk loop never rejects a step was **false**.

| action | observed |
| --- | --- |
| EAST from (20,20) onto tile `0x00` | screen printed `CMD: EAST--INVALID MOVE!`; `DS:00` went 20 → 21 → back to 20 on **every** press |
| SOUTH onto tile `0x0C` | `DS:01` advanced 20 → 21 → 22 → 23 → 24 → 25, every step stuck |

The rollback happens **before the redraw**, which is why no screenshot could
ever have shown the illegal position and why the measured viewport evidence
never hinted at it. The owner's model — move, check the new tile, bounce back
without drawing — is confirmed.

Observed tile verdicts so far (only these two are proven):

| tile | verdict |
| --- | --- |
| `0x00` | **blocked** on foot (water) |
| `0x0C` | **passable** |

`mapx20` contains `0x00, 0x04, 0x08, 0x0C, 0x10, 0x18, 0x20, 0x24`; the rest are
unmeasured. The owner predicts a boat sitting on a water tile makes it
passable — plausible (the tile art set contains a ship, and `cmd_X` allows
casting over `0x00` only when the mode is `0x24`) but **untested**.

### Why the static read failed

The passability function was re-decoded as file `0x268B..0x2743` and treated as
complete. It is not: execution falls through at `0x2745` and continues to about
`0x2880`. The missing tail contains the decision:

* **accept** — runtime `0x2664` (file `0x2864`):
  `x &= 0x3F; y &= 0x3F; mov al,0; ret`. The overworld *does* wrap through the
  torus mask on an accepted move — the reading the project had previously
  retracted is back in play for this path.
* **reject** — runtime `0x25FF` (file `0x27FF`): `mov al,0xFF; cmp al,0; ret`.
* two inline copies of the `-19` food drain, runtime `0x2599` and `0x2626`.

Roughly 200 bytes short of the behaviour, and the absence was asserted anyway —
a direct violation of the standing rule that a negative result from a probe not
designed to detect the thing is not a negative result. What caught it was the
live CFG harvest, which enumerated the nine blocks calling the tile reader
(`0839, 2677, 2233, 2708, 0CDC, 212E, 215C, 216E, 2216`) in seconds.

## 2. Time and food, corrected

| event | food cost | evidence |
| --- | --- | --- |
| accepted move | **−19** | `039538 → 039519 → 039500 → 039481 → 039462` |
| idle auto-PASS | **−10** | owner observed the game passing itself when left idle; deltas match `sbb $0x0a` at file `0x0B0D` |

Both falsify notes in the CP4b report: "movement does not tick" (the −19 drains
are inline in the passability function, reached through a dispatch static grep
could not follow) and "no idle timer, so auto-PASS not implemented".

A fresh character holds `04 00 00` = 40000 units, displayed as `FOOD=0400`
because the band prints only `DS:53/54`. At 19 per move that is roughly 2100
moves to starvation; at 10 per idle tick, 4000 ticks.

Death on underflow jumps to runtime `0x248D`. Note the `stc; cmc; sbb; das; cmc`
idiom leaves carry inverted, so the following `jb` means "no borrow" — the
branch structure reads backwards unless you account for the trailing `cmc`.

## 3. Segment-register facts, live

`DS = 0x08BE` confirmed at the title screen and in the world (the capture doc's
value is right). Live landmarks that validate the read:

* `DS:26E` = `0E 19 05 17` — the direction codes, exactly as proven statically.
* `DS:5B0` = `30 06 72 06 B4 06 F6 06 …` → `0x0630, 0x0672, 0x06B4, 0x06F6`,
  deltas of exactly **66** — the tile index table stride, seen in live memory
  for the first time.
* Sector buffer is loaded at `DS:1800` (read from `DS:0x472`).
* The map buffer really holds `0x78` at the player's cell, because the game
  stamps the figure into the map data.

Corrections to field meanings:

* **`DS:13` is the player's own tile id**, not the travel mode. The Q-save
  "on foot" guard reading `DS:13 >= 0x78` needs revisiting.
* The walk loop passes `[DS:15]` to the passability call, and `DS:15` was
  observed **changing between moves** (`0x84, 0x06, 0x18, 0x98`). The mode
  dispatch is therefore not understood. Left open rather than guessed twice.

## 4. Character creation writes the save immediately

The record on disk after creating `TEST` was a complete, loadable character —
name, sector `2 0`, entry pair `20 20`, stats, hp `0400`, gold `0400`, and the
`0x1A` tail marker — even though `Q` was never pressed. Creation itself saves;
this matches the decoded creation tail ("sets entry pair, saves, clears, prints
TO PLAY ULTIMA"). Position itself is not in the record; the entry pair `DS:5A/5B`
is, and that is what `P` restores from.

Food in the saved record stayed `040000` while memory had drained to `039443`,
confirming the drained value only persists on an explicit save.

## 5. Spice86 tooling behaviour (measured, cost real time)

* **The CFG is not scraped from the API.** Spice86 writes it itself:
  `spice86dumpCfgBlocks.json`, `CfgPartitions`, `ExecutionFlow`, `MemoryDump`,
  `CpuRegisters`, `CfgReload`. The schema matches the committed
  `captures/spice86/spice86-cfg.json` exactly, confirming how the Phase-1
  artefact was produced.
* **`monitor dumpAll` is camelCase.** Lowercase `dumpall` is silently ignored
  and looks like a successful gdb call that writes nothing.
* **Headless and windowed Spice86 both ignore SIGTERM and SIGINT** — each sat
  through until SIGKILL. No signal produces the exit dump, so a harvest cannot
  depend on one: `stop.sh` forces `dumpAll` over the GDB port while alive and
  treats signalling as teardown.
* **`-r` does not pin the dump path.** Spice86 appends a subdirectory named
  after the SHA-256 of the emulated exe (`D7CC110C…` for `ultimaii.exe`).
* **Windowed Spice86 crashes under heavy API polling.** At ~40 req/s:
  `InvalidOperationException: Collection was modified` in
  `CfgCpuViewModel.CreateBlockHeaderTableEntry` (CfgCpuViewModel.cs:459) — a
  race between the CFG graph panel and the emulation thread. Keep polling to a
  few Hz and read one contiguous range.
* **`setsid --fork` reparents the emulator**, so `$!` is the dying setsid. The
  real pid is found by the unique free port the launcher picked.
* **`GET /api` self-documents**; `/api/status` reports CS:IP and cycles but not
  `DS`, and `/api/memory/<physical>/range/<n>` takes a physical address.
* The vendored build's own `--help` is the authority on options; upstream
  `main` differs (it documents `--mcp-http-port`, ours takes `--McpHttpPort`).
  Note `-h` is bound to **both** `--help` and `--HeadlessMode`.
* Harvest output must stay out of git: it lands in `captures/spice86/<label>/`
  subdirs, which the old top-level-only ignore patterns did not cover.

## 6. Open questions

1. The full passable-tile set for on-foot travel (only `0x00` blocked and `0x0C`
   passable are observed).
2. Whether a boat on a water tile makes it passable.
3. What `[DS:15]` actually is, and the real mode-dispatch semantics in the
   passability tail.
4. Monster record layout — the `DS:0x137` stride-`0x20` guess from the capture
   doc produced nonsense (`x=134, y=0, tile=0xFF`); derive it from the asm and
   a before/after diff instead.
5. Monster movement and combat — a monster was observed following the player;
   whether it engages, and at what range, is not yet captured.
6. Whether the `& 0x3F` accept-path wrap means the sector edge is a torus after
   all, and how that squares with the `DS:6E` arrival system.

## 7. Combat and monsters (second session, `play-test`)

Captured by diffing the CFG before and after a fight: 150 blocks appear only in
the post-fight harvest, which pins the combat code far more precisely than
reading the listing did.

### Controls (owner correction, recorded because my guess was wrong)

* `R` is **not** a combat key. `cmd_R` (`asm/3E50`, "READY WEAPON:") equips or
  swaps the readied weapon. With nothing in inventory there is nothing to swap,
  and the character fights bare-handed.
* `A` is the attack (`cmd_A`, `asm/2B8F`, prints `ATTACK--`).
* Arrow keys set facing/direction, and are also the dungeon turn keys.

### The attack path, from the new blocks

| runtime | what it does |
| --- | --- |
| `0x2BFB` | damage basis: `AL = [DS:61]` (readied weapon) `*8`, `adc [DS:4B]` (strength), then `rcr` twice — weapon and strength combine into the attack value |
| `0x2C2D` | `cmp [DI+0x197], 0x40` — monster slot active/sentinel check |
| `0x2CB5` | stamps `[DI+0x1B7]` (monster sprite) into the map cell |
| `0x2CD3`/`0x2CE5`/`0x2D22`/`0x2D60` | dispatch on the monster sprite: `0x60`, `0xFC`, `0xF0`, `0xF8` — distinct handlers per monster kind |
| `0x2D8C` | **kill**: zeroes `[DI+0x1B7]`, `0x177`, `0x137`, `0x157`, `0x197` — the whole monster record is cleared |
| `0x2DC1` | **gold award**: BCD `adc` into `DS:58/59` |
| `0x2DEE` | **exp award**: BCD `adc` into `DS:56/57` |
| `0x0C09` | **damage to player**: `sbb [DS:22]` from `DS:51` (HP), `das`, and `jae` to the death path |

So `DS:22` is the damage-about-to-be-applied scratch byte, and player damage and
death are the same BCD-subtract-and-test shape as the food drain.

### Monster record layout — the capture doc is wrong

Three **parallel byte arrays, stride 1**, 32 slots — not stride `0x20`:

```
x[i]    = DS:0x137 + i
y[i]    = DS:0x157 + i
sprite  = DS:0x1B7 + i
active  = DS:0x197 + i      (0x40 = empty sentinel, per 0x2C2D)
```

Reading it with stride `0x20` produces plausible-looking garbage, which is
exactly how I lost time on it earlier.

### Monsters move on the idle tick, and the world wraps

Observed while the player stood still at (16,20):

* food fell exactly **10** every ~7.6 s of real time at `--Cycles 3000` — the
  idle auto-PASS interval;
* on the same cadence the monster positions stepped one cell, i.e. **idle time
  advances monsters**. Standing still is not safe;
* monster 30 went `x = 63 -> 0 -> 1 -> 2`, direct live confirmation of the
  `& 0x3F` torus wrap in movement, independent of the accept-path instruction.

### Monsters are RNG-spawned, and the emulator makes them repeatable

`monx20` contains exactly one non-zero byte (`0x1A`, the tail marker), so
monster placement cannot come from the file — it is generated. The seed is taken
once at launch from the BIOS keyboard flags at `0x40:6C`, which in a freshly
started emulator is the same every time. Hence the owner's observation that it
was "the same monster as last time": it is not carried by the save, it is an
artifact of a frozen entropy source.

Worth stating plainly for the engine: our reproducibility comes from substituting
the clock and exposing `--seed`; the original under emulation is reproducible
only by accident. Do not mistake emulator determinism for game determinism.

### Also observed

* Walking into a **mountain** produced `--INVALID MOVE!` (owner report), so
  blocking is not water-only. The mountain's tile id was not captured — needs a
  read while standing next to one.
* Crossing a **land bridge** was accepted.
* Player HP fell `0399 -> 0397 -> 0396 -> 0395` during the logged fight.
* New walk-loop blocks `0x09AF` (`mov AL,[DS:18]; call 0x2423`) show the
  passability routine is called with `[DS:18]` from at least one arm, not only
  `[DS:15]` — so the "travel mode" argument has more than one source and the
  mode dispatch is still not fully understood.

## 8. Kill rewards — full loot model (resolved offline, no capture needed)

Derived from the harvested blocks cross-read against `asm/`. The harvest's
runtime addresses key directly onto the asm filenames, so `017D:2D8C` is
`asm/2D8C_combat_kill_reward.asm`.

### Sequence on a kill

1. **Per-monster-kind bonus**, dispatched on the monster sprite byte
   (`DS:1B7+i`) at `0x2CD3..0x2D8B`:

   | sprite | effect |
   | --- | --- |
   | `0x60` | `DS:65` (keys) `+= 2` |
   | `0xFC` | `DS:66` (tools) `+= 1`, **and** roll an item drop (below) |
   | `0xF0` | `DS:DB` `+= 1`, gated on `rand >= 0x40` |
   | `0xF8` | separate branch, not yet read |
   | other | straight to the reward |

2. **`0x2D8C` `combat_kill_reward`** — zeroes the whole monster record
   (`0x1B7`, `0x177`, `0x137`, `0x157`, `0x197`) and prints
   `KILLED--GOLD+`.

3. **Gold** at `0x2DB7`/`0x2DC1`: amount = `(rand & 0x17) | 1`, BCD-added into
   `DS:58/59`.

4. **Exp** at `0x2DE4`/`0x2DEE`: amount = `(rand & 3) + 1`, BCD-added into
   `DS:56/57`.

5. **Item drop** at `0x2D03`/`0x2D07`: `idx = rand & 0x0F`, then
   `DS:D6+idx += 1` (BCD). Only reached from the `0xFC` sprite branch.

So the owner's report — exp, gold *and* an item from one kill — is exactly the
`0xFC` path, and the item drop is not generic loot, it is specific to that one
monster kind.

### The item array, and its name table

`DS:D6+0..15` are **item counts**, sixteen slots, and the `rand & 0x0F` index
range matches the sixteen-entry item-name table in the binary exactly:

```
file 0x501d  [ 0] RING       [ 4] CLOAK      [ 8] RED GEM      [12] BLUE TASSLE
             [ 1] WAND       [ 5] HELM       [ 9] SKULL KEY    [13] STRANGE COIN
             [ 2] STAFF      [ 6] GEM        [10] GREEN GEM    [14] GREEN IDOL
             [ 3] BOOTS      [ 7] ANKH       [11] BRASS BUTTON [15] TRI-LITHIUM
```

Index 16 is `STRENGTH`, where the attribute table starts, so the sixteen-entry
boundary is real and not a parsing artifact.

Corroboration that this table is the right one: block `0x487A` special-cases
index **3** in the item display path, and index 3 is `BOOTS`, the only item with
bespoke messaging elsewhere in the binary.

### Measured final state, from the harvested memory dump

`DS` base `0x8BE0`, read out of `spice86dumpMemoryDump.bin`:

```
hp 0184 (184)   food 036244   exp 0010 (10)   gold 0452 (452, started 400)
str 0x50  dex 0x12  int 0x18   boots DS:6D = 00 (none)   ready DS:61 = 00
DS:76..0x8D  all zero          <-- NOT the inventory
DS:D6..0xED  00 00 01 00 00 00 00 00 00 00 01 00 00 00 00 00
             index 2 = STAFF, index 10 = GREEN GEM
```

The save file mirrors the same array at **save offset `0x0a0`**, holding only
index 2 — so the green gem was picked up after the last save. Save and live
memory agree.

### Resolved: there was never a gem discrepancy. I miscounted.

An earlier draft of this section claimed the second item sat at index 12
(`BLUE TASSLE`) and contradicted the owner's memory of a green gem. It does not.
`DS:D6[10] = 1`, and index 10 is `GREEN GEM`. The owner was right throughout.

The error came from splitting a hex string by eye instead of computing it, and
then reasoning confidently from the miscount. The table was never wrong.

The display confirms the table independently: `asm/4881` prints a literal `"S"`
for the plural, and `0x487A` skips that `S` when the index is 3 — which is
`BOOTS`, the one already-plural entry. So the screen rendering `STAFFS-01
GREEN GEMS-01` is `STAFF` + `S`, `GREEN GEM` + `S`, from the table at
`0x501d`. Index 3 being the special case is the proof that this table is the one
in use.

**Method note worth keeping:** derive indices in code, never by reading hex
aloud. Every other error this session came from an ambiguous byte; this one came
from arithmetic I did not do.

### Paralysis resist — now backed by a string, not just a byte test

`asm/0E70_combat_paralyzed_legs.asm` already exists and carries both
`LEGS PARALIZED!` and `SAVED BY MAGICAL BOOTS!`. The boots test seen at
`0x22FD` (`mov AL,[DS:6D]; or AL,AL; je ...; ret` — return early when boots are
present) is consistent with it. Still not proven to be the *same* call site as
the paralysis event; the string file and the boots gate have not yet been joined
by a traced edge.

### Theft

`asm/3F5D_steal_attempt.asm` holds the thief outcomes: `NO LUCK!`,
`STEAL FOOD!`, `STEAL ARMOUR!`, `STEAL WEAPONS!`. The matching decrement path
is `0x0FE7`: `sbb AL,1; das; mov [DI+0xD6],AL` — the inverse of the drop at
`0x2D07`, operating on the same item-count array. So a thief can remove an item
from `DS:D6+i`.

## 9. Provenance cross-reference, and a corpus defect it caught

`tools/xref_provenance.py` makes the docs <-> asm link machine-checkable in both
directions: every `asm/XXXX` citation in `docs/` must resolve to a real function
range, and every named function should carry a `# provenance:` line saying
whether its behaviour is `static` (read from the listing), `live:<harvest>`
(observed), `owner` (reported, not yet seen in bytes) or `retracted`.

Bare `0xXXXX` mentions are counted but not resolved: the first run flagged 47
" dangling" citations and nearly all were data addresses, segments or constants
like `0x9000` and `0x00FF`, not function references.

First run over the existing corpus:

```
functions parsed              1298  (named: 111)
named functions cited in docs   21
named functions with provenance  0 / 111
dangling asm/ citations           2   (asm/0964, asm/5002)
```

So 90 of 111 named functions are never cited by any finding, and none record
whether anything about them was ever observed rather than inferred. That is the
condition that let the T1 misreading stand for an hour as a confident finding.

### Defect: `2DC1` and `2DEE` are misnamed in `asm/`

`asm/2DC1_combat_exp_gain.asm` adds `DS:1F` into `DS:58/59`, which is **gold**
(`engine/src/core/state.h` pins `0x56/57 = exp`, `0x58/59 = gold` with
`_Static_assert`, cited to the display sites), and only *then* prints
`--EXP.+`. The string introduces the following operation, `2DEE`, which is the
one that writes `DS:56/57`. The namer assumed a printed string describes the
function that prints it.

Independent confirmation from the arithmetic and a live read: `2DC1` adds
`(rand & 0x17) | 1`, i.e. 1..23, and `2DEE` adds `(rand & 3) + 1`, i.e. 1..4.
The harvested dump had `DS:58/59 = 452` against a starting 400 and `DS:56/57 =
10` against a starting 0. Swapping them would require gold to have fallen 400 ->
10, which cannot happen from a reward path.

Correct reading: **`2DC1` = gold gain, `2DEE` = exp gain.** The filenames are
transposed. Renaming corpus files is a CP3-wide convention change, so this is
recorded rather than applied unilaterally.

### Corrected: the reward sequence, and why the boundary caused the mislabel

Owner's reading, confirmed against the blocks: each message announces the award
that *follows* it, not the one preceding it.

```
2D8C  clear monster record, print "KILLED--GOLD+"   <- announces the gold below
2DB7  gold_amount = (rand & 0x17) | 1  -> DS:1F
2DC1  DS:58/59 += DS:1F                  (GOLD)
      print "--EXP.+"                                <- announces the exp below
2DE4  exp_amount  = (rand & 3)  + 1  -> DS:1F
2DEE  DS:56/57 += DS:1F                  (EXP)
```

The string `--EXP.+` lives at the tail of the block CP3 named `2DC1..2DE0`, while
the exp roll and add are `2DE4` and `2DEE`. The block boundary cuts through the
middle of one semantic unit, so "the function that prints `--EXP.+`" and "the
function that awards exp" genuinely are different files. Both renames are now
applied: `2DC1_combat_gold_gain.asm` and `2DEE_combat_exp_gain.asm`, each citing
the display site that proves which pair it writes.

**Every kill awards both, minimum 1 each.** Neither roll can produce zero: the
`| 1` on the gold mask forces an odd non-zero value, and the exp roll is
`(rand & 3) + 1`, i.e. 1..4. So gold is not a chance drop; only the amount is
random. The item drop at `0x2D07` is the part that is conditional, and it is
gated on the monster sprite being `0xFC`.

### Also: `asm/2423` declares a boundary that is wrong, and it is on purpose now

`asm/2423_sub_2423.asm` declares `func 2423..242C`, ten bytes, while the
passability routine runs to roughly runtime `0x2680`. Reading only that file
makes the routine look like it performs no rejection whatsoever, which is exactly
how the static pass on 2026-10-21 produced the retracted "no passability"
finding. The file now carries a WARNING and a provenance line so the next reader
does not repeat it. The boundary itself is still wrong and needs a CP3 re-decode
of `2423..2680`.

### Also already named, from the same listing

`asm/` already contains `0E70_combat_paralyzed_legs`, `0EC2_combat_paralyzed_arms`
(two distinct paralysis effects, not one), `0FCD_combat_thief_steal`,
`0F12_combat_magic_missile`, `0F48_combat_sleep_spell`, `0F8E_combat_minax_attack`,
`2BEC_combat_hit_msg`, `2E09_combat_miss_msg` and `2E3D_combat_attack_flow`. The
thief-steal and paralysis questions raised during live play had named functions
sitting in the tree the whole time.

## 10. The screen is recoverable from the memory dump — `tools/vga_screen.py`

The owner's question: is the fight and reward text that Spice86's UI fails to
show actually present in the captured data?

**Yes.** Ultima II runs in a text video mode. The dump's framebuffer at `B8000`
is character/attribute pairs — 16384 bytes, exactly half printable — so the
screen as the player saw it renders straight out of a capture we already have.
No trace and no replayer needed.

```
 0| TEST                        A LEVEL 00 MALE HUMAN FIGHTER
 1|                             WEAPON-HANDS        TORCHES-00
 2|   ARMOUR-SKIN    KEYS-00    SPELL-NONE          TOOLS-00
 3| STRENGTH-50  STAMINA-18  WISDOM-10  AGILITY-12  CHARISMA-10  INTELL.-15
 4|                             WEAPONS:
 6| ARMOUR:
 7| SPELLS:
 9| ITEMS: STAFFS-01 GREEN GEMS-01
```

Diffing two harvests shows the gain directly: the earlier dump has a bare
`ITEMS:` and the later one has `ITEMS: STAFFS-01 GREEN GEMS-01`.

### The honest limit

This is a **snapshot at dump time, not a recording**. Both dumps above caught the
inventory screen, and the combat messages had already scrolled away. So the
missing fight text is not preserved retroactively; it is preserved only if the
dump happens while it is still on screen.

Consequences for how we capture:

* dump immediately after an event of interest, before the player scrolls it off;
* a real replayer would need Spice86's execution-trace recording, which the
  current start script does not enable — the harvest holds CFG, execution flow,
  registers and one memory snapshot, and no per-instruction trace;
* cheap middle ground: poll the framebuffer at a few Hz into a ring of dumps, so
  the last few screens survive any event.

That last one is worth building before the next fight, because it converts "I
missed the text" into "read the last screen".

## 11. Offsets and lookup tables for a save / live-memory editor

Everything here was written and read back through the Spice86 MCP on a live
session on 2026-10-21, so the DS column is byte-proven. The save column is
partly inferred and marked as such.

> **Layout authority: `engine/src/core/state.h`, not this table.** This section
> is the live *evidence*; the struct is the *table*, because it is compiled and
> every named field is pinned by a `_Static_assert`, so drift there is a build
> failure rather than a stale document. Per `AGENTS.md § 3` there is one lookup
> table per domain and derived views are generated, not hand-kept. Where the two
> disagree, `state.h` wins and this section is the bug. Two entries below are
> already known to be wrong and are corrected in the struct: `0x13` is the
> player stamp, not a "player tile id", and `0x20` is scratch, not an
> invalid-move counter.

### Live data segment

`DS = 0x08BE` this session. The MCP takes **decimal** integers for segment and
offset, not hex strings: `segment=2238, offset=75`.

| DS | field | encoding | notes |
| --- | --- | --- | --- |
| `0x00` / `0x01` | player x, y | byte | **viewport** coords; masked `& 0x3F` on accept |
| `0x13` | player tile id | byte | `0x78` = on foot |
| `0x20` | invalid-move counter | byte | increments on each rejected step |
| `0x23` / `0x24` | attempted destination | byte | valid to read on a reject; nothing scrolls |
| `0x49` / `0x4A` | sector hi / lo | byte | |
| `0x4B` | STRENGTH | BCD | |
| `0x4C` | AGILITY | BCD | gates weapon equip: `index*8 < AGILITY` |
| `0x4D` | STAMINA | BCD | |
| `0x4E` | CHARISMA | BCD | **proven 2026-10-29** — see the section below |
| `0x4F` | WISDOM | BCD | **proven 2026-10-29** — see the section below |
| `0x50` | INTELLIGENCE | BCD | attribute order is STR, AGI, STA, CHA, WIS, INT |
| `0x51` / `0x52` | HP | BCD, high pair first | `0x40 0x00` = 4000 |
| `0x53..0x55` | food | BCD, 3 bytes | the turn odometer: -19 per move, -10 per pass |
| `0x56` / `0x57` | EXP | BCD | display site runtime `0x5167` loads `[56]`,`[57]` |
| `0x58` / `0x59` | GOLD | BCD | display site runtime `0x5188` loads `[58]`,`[59]` |
| `0x61` | readied weapon | index | into the weapon table below; `0` = HANDS |
| `0x62` | worn armour | index | into the armour table below; `0` = SKIN |
| `0x64` / `0x65` / `0x66` | torches / keys / tools | BCD | |
| `0x6D` | boots | count | non-zero gates the paralysis save at runtime `0x22FD` |
| `0x76 + i` | weapons owned | count | `i` indexes the weapon table |
| `0xD6 + i` | items held | count | `i` indexes the item table, 16 slots |
| `0x137+i` `0x157+i` `0x1B7+i` `0x197+i` | monster x, y, tile, kind | byte | stride 1, 32 slots |
| `0x23F..0x244` | PRNG state | 6 bytes | |
| `[0x472]` | sector map buffer | 4096 B | 64x64 tile bytes, player and monsters stamped in |

### Name tables (file offsets in `original/ultimaii.exe`)

Weapons, from `0x4F53` — `DS:61` and `DS:76+i` index this:

```
0 HANDS   1 DAGGER  2 MACE    3 AXE     4 BOW     5 SWORD
6 GREAT SWORD       7 LIGHT SWORD      8 PHASER  9 QUICK SWORD
```

Armour, from `0x4F9E` — `DS:62` indexes this. Eight entries, which is why the
damage roll is masked `& 7`:

```
0 SKIN  1 CLOTH  2 LEATHER  3 CHAIN  4 PLATE  5 REFLECT  6 POWER  7 NONE
```

Items, from `0x501D` — `DS:D6+i` indexes this. Exactly sixteen entries; index 16
is `STRENGTH`, where the attribute table begins.

```
0 RING      1 WAND      2 STAFF     3 BOOTS     4 CLOAK     5 HELM
6 GEM       7 ANKH      8 RED GEM   9 SKULL KEY 10 GREEN GEM 11 BRASS BUTTON
12 BLUE TASSLE  13 STRANGE COIN  14 GREEN IDOL  15 TRI-LITHIUM
```

The display appends a literal `S` for the plural (`asm/4881`) and skips it for
index 3 only, because `BOOTS` is already plural — which is what proves this table
is the one in use.

### Proven 2026-10-29: `0x4E` is CHARISMA and `0x4F` is WISDOM

The old text of this section said the order was not provable and listed the two
bytes as `WISDOM or CHARISMA` / `CHARISMA or WISDOM`, which disagreed with
`engine/src/core/state.h`. **The struct was right and this table was the bug** —
which is the standing rule of § 11 ("where the two disagree, `state.h` wins and
this section is the bug"), now with the evidence to back it.

`captures/spice86/d13-cha-vs-wis`: a Wizard seeded with `DS:4E = 0x20`,
`DS:4F = 0x40`, `DS:50 = 0x40`, and the `Z` status screen drawn live. It printed
`CHARISMA-20` and `WISDOM-40`, so the byte under the CHARISMA label is `0x4E`.
Frame `a-z-screen.png`.

The same character then bought a horse for **52 gold** where the maxed character
yesterday paid 32 for the same horse, which is what `asm/20AB` predicts for a
routine reading `int + [DS:4E]` and not what it predicts for `[DS:4F]`. Owner
played, frame `b-horse-52gp.png`, gold read live as `DS:58/59 = 99 47`.

### Not proven: which of `0x4E` / `0x4F` is WIS and which is CHA

**Retracted — see the section above.** Kept as a heading so a reader who arrives
from an old link finds the answer rather than a gap.

The test character has WISDOM 10 and CHARISMA 10, so the live block cannot tell
them apart, and the save bytes match either reading. `engine/src/core/state.h`
labels `0x4E` as `stat_cha` and `0x4F` as `stat_wis`; an earlier draft of this
section asserted the opposite as fact and was not entitled to. Settle it with a
character whose WIS and CHA differ, or by reading the status draw code, before
any editor ships a label for those two bytes.

### Save file (`player`, 384 bytes)

| offset | field | confidence |
| --- | --- | --- |
| `0x00..0x0B` | name, NUL-padded | proven (`TEST`) |
| `0x15..0x1A` | STR, AGI, STA, WIS, CHA, INT | proven by value match against the live block |
| `0x22..0x23` | gold | proven by value match |
| `0x0A0..0x0AF` | item counts, same 16-slot table | proven by value match |
| `0x100` | `0x1A` | the DOS EOF marker, not data |

### Two traps for whoever writes the editor

1. **Packed BCD, not binary.** Writing `0x9A` into a counter produces invalid
   BCD and corrupts it. Validate both nibbles are `0..9`.
2. **The status band is a cached render, and `Z` pauses the game on it.** Writes
   take effect in memory immediately and combat reads them live, but the screen
   only repaints on the game's own status draw. A frozen `Z` screen will keep
   showing pre-write values, and while paused the game generates no turns, so a
   turn-triggered recorder records nothing.

## 12. Caster gauntlet, theft, force field and the plane tile (2026-10-21)

Harvests `captures/spice86/airport-castle` and `captures/spice86/castle-03-live`.
Turn evidence is 3000 per-turn DS+VGA snapshots from `tools/turn_recorder.py`,
kept in `captures/spice86/fight-20261021-castle03/` (`index.jsonl` plus `turns/`,
64 MB, gitignored as derived dumps of the copyrighted binary).

The session started by seeding a maxed character (`tools/u2_edit.py maxout`) into
sector 00 at (30,32), which turned out to be a walled compound holding 31
simultaneous monsters. Four of the seven monster types present cast, and the
owner's guess of "final boss" was wrong: `MINAX CRIES: DIE FOOL!` never executed,
and it is reached from a different site (file `0x126A`). Grade for everything in
this section: **measured**, from `ExecutedInstructions` plus live DS, not from
discovered blocks.

### The four monster specials, and what actually resists them

`asm/0AD3` dispatches on the monster type byte at `[DI+0x197]`:

| monster type | attack | function | resist item, live gate |
| --- | --- | --- | --- |
| `0x34` | `LEGS PARALIZED!` | `asm/0E70` | BOOTS, `[DS:D9]` |
| `0x38` | `ARMS PARALIZED!` | `asm/0EC2` | CLOAK, `[DS:DA]` |
| `0xF8` | `MAGIC MISSILE!` | `asm/0F12` | **none** |
| `0x3C` | `SLEEP SPELL!` | `asm/0F48` | GREEN IDOL, `[DS:E4]` |

The three gates are the item array slots themselves: `DS:D6+i` with BOOTS at `i=3`,
CLOAK at `i=4`, GREEN IDOL at `i=14`, which is the same order as the name table at
file `0x501D`. Arms and legs are independent counters (`DS:26B`, `DS:26C`) with
independent items, so a character can be paralysed in one and free in the other,
and both were observed non-zero at once (9 and 8) alongside sleep at `DS:26D`.

Two things that are easy to get wrong and are now settled:

1. **Owning the item is not immunity.** With the item present the roll from
   `L_005417` must be `>= 0x40`, so each is a roughly 75% resist. Without the item
   the effect lands unconditionally, with no roll at all.
2. **The magnitude is random, not fixed.** On a failed resist the counter is set to
   `rand & 0xF`, i.e. 0..15 turns.

`asm/0AAA` is the gate in front of the whole dispatch: `[DS:19] < 3`,
`[DS:1A] < 3`, and `rand < 0x20`, so a special is attempted on about 12.5% of
monster turns. `[DS:19]` and `[DS:1A]` are zeroed by the force field (below), which
is the only link found between the two systems; what the counters mean is not
proven.

### Correction: the boots gate is `DS:D9`, not `DS:6D`

Section 8 recorded a `[DS:6D]` test at runtime `0x22FD` as consistent with the legs
resist but untraced. It is the wrong byte. During this session a thief took the
boots, and `[DS:D9]` fell to 0 while `[DS:6D]` stayed 1 and the resist stopped
firing. `DS:6D` is the save-record slot and does not track theft; the live gate is
`[DS:D9]`, tested inside `asm/0E70` at file `0x108D`. Anyone writing a save editor
should treat the two as separate fields, not as one field with two spellings.

### Theft is permanent

`asm/0FCD` is the steal routine and it identifies the thief by type: the loop tests
the type byte against `0x40` at file `0x1244`, so **the thief is monster type
`0x40`**. Type `0x40` was present in the recording, which supplies the traced edge
section 8 said was missing.

> **Correction, 2026-10-22: the thief is NOT monster type `0x40`.** The spawn arms
> store type and HP together, and the thief's arm (`asm/0D6F_monster_hp_thief_daemon`)
> sets type `0xFC` with HP `0x20` = 32 — which matches art `0x3F`, named Thief by
> bitmap identity, and the third-party HP for Thief, also 32. `0x40` is the **empty
> slot sentinel** recorded elsewhere in this document (`cmp [DI+0x197],0x40` at
> `0x2C2D`), so the `cmp $0x40` at file `0x1244` is skipping empty slots and names no
> creature. The conclusion "the thief is type `0x40`" and the observation "`0x40` =
> empty" were both correct readings of the same byte by two sessions that could not
> see each other; only the second survives. See `docs/bestiary.md` § 6 and
> `tools/monster_hp_dispatch.py`.

Measured over the whole fight: eight items went `1 -> 0` (RING, GEM, ANKH,
TRI-LITHIUM, BLUE TASSLE, BOOTS, GREEN GEM, SKULL KEY) and **none ever came back**.
The kill that ended the fight paid `HELM 1 -> 2` and gold +15, indistinguishable
from any other kill's drop (compare `STAFF 1 -> 2` with gold +17 earlier in the
same run). There is no restore-stolen-goods path in the binary. The engine must
therefore delete a stolen item from the array rather than flag it, because the
resist gates read that array live.

### The force field, and why the theft mattered

`asm/24B5` gates on `[DS:D6]`, the RING slot, at file `0x26B8`. Non-zero prints
`RING PROTECTS FROM FIELD!` and does nothing. Zero takes the damage path: it clears
`[DS:19]` and `[DS:1A]`, then subtracts with `stc; sbb al,0x10; das` on `[DS:51]`, the
high byte of the 4-digit BCD HP, which is exactly **-1000**. The owner's HP went
`8842 -> 7842` on the tile, and after `[DS:D6]` was set to 1 the protect branch
fired with no damage. Both branches are now in the executed set, where previously
they had only ever been discovered.

If the subtract borrows, the branch at file `0x271B` goes to the death handler, so
this tile kills any character under 1000 HP outright. That makes it a difficulty
gate rather than a tax.

The causal chain is worth keeping as an example of why these systems are not
independent: the thief took the ring, so the field cost 1000 HP.

### The plane tile: a wrong answer, the correction, and how it was settled

**This section first claimed the plane was map byte `0x34`. That was wrong and the
retraction matters more than the original claim, so it stays here with its cause.**
The render used `%02d` against hex-named tile PNGs, so `tile_13.png` - which is art
`0x13` - loaded under the label "13" and got multiplied into the wrong byte. The
shape described was real; the byte attached to it was not. `0x34` is art `0x0D`, a
humanoid figure with arms raised out to the sides, which is why the owner's "you're
not seeing the winged monsters as planes are you?" was the correct challenge.

The plane is map byte **`0x4C`**, art **`0x13`**. The chain that proves it is
measurement, not eyeballing:

1. `[DS:14] = map_byte >> 1`, measured live on **53 of 54** walk samples across four
terrain bytes: `0x0C`->`0x06`, `0x5C`->`0x2E`, `0x70`->`0x38`, `0x08`->`0x04`.
2. `asm/2FAD` dispatches BOARD on `[DS:14]` against `0x22` horse, `0x24` ship,
`0x26` plane, `0x28` rocket.
3. Therefore the vehicle map bytes are `0x44`, `0x48`, `0x4C`, `0x50` - arts `0x11`,
`0x12`, `0x13`, `0x14`, and three of those are already this file's own horse,
frigate and rocket candidates in `disasm/tile-art.tsv`.
4. The game then confirmed it directly: standing on `mapx32` (4,10), a `0x4C` tile,
`B` set `[DS:13] = 0x26` and printed `' PLANE'`; `L` printed `LAUNCH--PLANE`.

Every plane tile in the game, map byte `0x4C`:

| sector | cells | what the letter tiles say |
| --- | --- | --- |
| `mapx41` | 12 tiles at (2-11, 9-11) | `CCCP DEFENCE`, `KGB`, `CCCP TEST LAUNCH`, `PIRATES HARBOUR` |
| `mapx32` | (4,10) | `AIR PORT` at y13, directly below the tile |
| `mapx03` | (38,24) | a four-vehicle depot: horse (36,24), ship (37,24), plane (38,24), rocket (39,24) |
| `mapx80` | (9,40), (50,46), (61,61) | not decoded |

**The sector-00 "vehicle depot" from the first draft of this section did not exist.**
`mapx00` contains zero horse, ship, plane or rocket tiles. What I read as parked
vehicles were the `0x34` and `0x38` creature tiles - humanoid figures - which is
also why that compound turned out to be a 31-monster caster gauntlet. The owner had
said "there's a town with an airport", and the letter-density table generated in the
same session already showed `mapx00` had 0 letter tiles while `mapx03` had 113. The
disconfirming evidence was in my own output and I selected on the wrong column.

Landing is gated: `LAND PLANE.` is accepted only on tile `0x08` (grass), anything
else gives `LAND--NOT HERE!!!!!` and inertial flight continues. Both branches are in
the executed set of `captures/spice86/dungeon-35-live` - gate `cmp` at runtime
`0x3B56`, accept at `0x3B5A`, refuse at `0x3B72`.

### Every vehicle tile in the game, all four kinds

The plane census above existed and the others did not, which is how "the rocket is in era 0"
survived into a routing plan. Full sweep of the map byte at offset 0 over the first 4096 bytes
of every `mapxNN` in `original/` and every `MAPGNN` in `original/updated/`:

| sector | horse `0x44` | ship `0x48` | plane `0x4C` | **rocket `0x50`** | what the letter tiles say |
| --- | --- | --- | --- | --- | --- |
| `mapx03` | (36,24) | (37,24) | (38,24) | **(39,24)** | four vehicles side by side in a Legends-era castle — a depot, not a launch site |
| `mapx31` | — | 5 cells | — | — | |
| `mapx32` | 5 cells | — | (4,10) | — | `AIR PORT` |
| **`mapx41`** | — | 3 cells | 12 cells | **(33,3) (38,3) (43,3)** | `CCCP DEFENCE DA KGB CCCP TEST LAUNCH EXIT HERE` at y=0 |
| `mapx61` | — | 2 cells | — | — | |
| `mapx80` | — | (15,47) | 3 cells | — | |
| `mapx82` | 3 cells | — | — | — | |
| `mapx92` | 3 cells | 2 cells | — | — | |
| `MAPG41` | — | 3 cells | — | — | Jupiter's village; **no rocket** |
| `MAPG61/80/82/92` | — | see root | — | — | byte-identical to the root `mapx` of the same number |

**Four rockets exist, not one.** The three in `mapx41` sit at `y=3`, directly below the row
`y=0` that spells `CCCP TEST LAUNCH EXIT HERE` — so they are the Soviet test-launch strip, and
the sign is pointing at them. Sector 41 is the place the letter tiles call `PIRATES COVE` (y=17)
and `PIRATES HARBOUR` (y=63), so the pirate town and the Soviet launch site are **the same
sector**, which is why the owner's recollection of "rockets at both Pirates Cove and the USSR"
reads as two places and is one. The `mapx03` rocket is the only other one, and it is in a
castle in era 0 alongside the other three vehicle kinds, which makes it a depot rather than a
launch site.

**No planet has a rocket.** `MAPG41` — Jupiter's village, which is what sector 41 becomes under
the Galaxy patch — has three ships and no rocket. Rockets are an Earth object.

The consequence for routing is the opposite of what was assumed: Earth's rocket **landing**
sector is hardcoded to 40, which is era 4, and era 4's village 41 is where three rockets are
waiting. Era 4 is both the arrival and the departure hub, so a bot that lands from space is
already standing next to its next rocket rather than needing a time door to era 0.

### Trap: two different "art index" scalings

`DS:14` and the passability ladder use **`byte >> 1`**. The alphabet-tile decoder in
`tools/place_index.py` uses **`byte >> 2`**. Both are called "the art index" in this repo, and
using the wrong one silently produces confident nonsense: decoding `mapx41` with `>> 1` turns
the 12 plane tiles into the letter `G`, the armour and weapon racks into `K` and `M`, and the
terrain tile `0x70` into `Y`, which reads as a wall of filler rather than as a shift error.
Under the correct `>> 2` those same bytes are not letters at all. When a decode returns
uniform junk, check which shift the tool means before believing the junk.

**Still open:** the `[DS:14]` writer at file `0x6238` calls `0x6AC0`, whose index
arithmetic came out as `y*16 + x` against a 64-wide grid. Unexplained.

~~Unexplained~~ **Resolved 2026-10-22 (overnight T7), measured.** `asm/68C0` (file
`0x6AC0`) is not the sector-tile lookup. Its address is
`word[DS:472] + [DS:25]*256 + [DS:24]*16 + [DS:23]` — the map-grid buffer
(`word[DS:472] = 0x1800` in every live capture) read as a **16-wide grid with a
256-byte page selector `[DS:25]`**, not the 64-wide `y*64+x` that
`asm/50B3 map_tile_ptr` uses. It is reached only from the dungeon path
(`asm/6000` ← `asm/1151 dungeon_darkness`; `asm/6078` ← `asm/6000`), and the
dungeon map `mapx35` decomposes into **16 pages of 16×16 cells** that all use the
same 7 dungeon bytes, with dungeon player/monster coordinates in **0..15**
(measured in `dungeon-35-live`). So `y*16 + x` is the dungeon grid's row-major
index within a page, and the anomaly was only an artefact of assuming the array
was the overworld sector grid. `[DS:25]` as a floor/page selector is **inferred**
(it is 0 in every capture, so the multi-page path is reasoned from the `*256`
term, not witnessed). This is a different `[DS:14]` writer from the overworld
walk path (`asm/5B71`/`asm/5C49` set `[DS:14] = [DS:2DF]`), so the proven
`[DS:14] = map_byte >> 1` is untouched. Files renamed to
`asm/68C0_dungeon_grid_byte.asm` and `asm/6000_dungeon_render.asm`; bodies byte-
identical (clean rebuild 37344 B). The full dungeon geometry (how a 16×16 page
becomes the first-person wireframe) remains the backlog item, out of scope here.

### The castle is a sector link, not a separate map format

**Correction to the first draft of this section, which had the conclusion backwards.**
I wrote that entering the castle "did not load a new file" because the live grid
matched `mapx03`. It matched `mapx03` because the castle **moved the player from
sector 00 to sector 03**. The seed is proven: `tmp/20261021-211408/player` still
carries sector digits `0`/`0` and entry `(30,32)`, so the fight was in sector 00,
and the post-castle read of `DS:49/4A` = `0`/`3` plus a grid matching `mapx03` on
4066 of 4096 cells is a sector change, not the same sector. Entering a castle is a
sector transition, and `-CASTLE` (`017D:34B5`) is its message.

What survives from the first draft: the castle interior is an ordinary sector map
with no special format, and the `A`-`Z` letter tiles spell its room names in situ.
`mapx03` holds 113 letter tiles and they decode, from the file alone, to exactly the
rooms seen live — which is the strongest support yet for F13. `mapx00` holds zero.

```
y=0   SHADOW  GUARD      CHAMBER ONE
y=29  TIMEZ  ONE         MUSEUM
y=34  TORTURE  CHAMBER
y=36  APOTHECARY
y=46  THIEVES            MORGUE
y=48  GUILD
y=62  DEVIL              WORKSHIP?
```

### Corrections and defects this session produced

* `tools/u2_edit.py` documented class as `0 fighter, 1 mage, 2 paladin, 3 thief`.
  The game's own table at file `0x50E8` is `FIGHTER, CLERIC, WIZARD, THIEF`. There
  is no paladin.
* `tools/u2_edit.py set` crashes on its own verification step for text and char
  fields (`int('AIRTEST')`), after the write has already landed. The write is fine;
  the check is wrong. Cosmetic, but it makes a successful edit look like a failure.
* `tools/turn_recorder.py`'s `screen` field is useless here: the game runs in CGA
  graphics mode, so `0xB800` holds pixels, not a text page. Every message claim in
  this section comes from `ExecutedInstructions` instead. The recorder logged 76
  odometer anomalies across 3000 turns, so single-turn boundaries in dense combat
  are not fully trustworthy and should be re-checked against the raw `.ds` files.

### Still unexplained

* ~~Gold went `9999 -> 14` in one turn at turn 209. Cause not identified; not
guessing at it.~~ **Resolved 2026-10-22 (overnight T4), measured.** It is not a
theft or a bribe: at turn 209 the five monster-record fields for slot 4
(`DS:13B/15B/17B/19B/1BB`) were zeroed together exactly as
`asm/2D8C_combat_kill_reward` does, a map cell reverted `0x34`->`0x08`, and food
did not change (no turn spent). The character had been seeded to the 9999
packed-BCD ceiling by `maxout`, and `asm/2DC1`/`asm/2DEE` add the kill reward into
the 2-byte gold/exp fields with `adc`/`daa` and **no saturation**, so the first
kill's reward wrapped them mod 10000: `9999 + 15 -> 0014 = 14` (gold, reward
`0x15` from `asm/2DB7`) and `9999 + 2 -> 0001 = 1` (exp, reward from `asm/2DE4`).
All result nibbles are valid BCD, so it is a field-width overflow, not nibble
corruption. See `docs/phase4-overnight-cp5-report.md § T4`.
  **Generalised the same day, measured:** the unguarded-carry add is the house style,
  not a gold-specific bug. `tools/bcd_census.py` enumerates every packed-decimal
  site in `asm/` — of 30 add sites on known counters, 28 have no ceiling guard, so
  every such field wraps at its digit capacity (1-byte stats/keys/item counts at 99,
  2-byte HP/EXP/GOLD at 9999, 3-byte food at 999999). Subtractions differ: a
  multi-byte field tests the borrow once on its top byte via `cmc; sbb; das; cmc`,
  while the single-byte counters test nothing. See
  `docs/phase4-state.md § BCD counter tickets`.
* Which map byte the force-field tile is, and what drives its animated
  white/blue/purple render. **Narrowed 2026-10-22 (overnight T8), measured.** The
  trigger is **mode byte `0x2e`**, not a tile byte: the passability ladder
  (`asm/248B`→`asm/2495`→`asm/24B1`) reads no tile and falls through to
  `asm/24B5` only when `[DS:29] & 0x7f == 0x2e`, where `[DS:29]` is the travel-mode
  byte `[DS:15]`/`[DS:18]`. Confirmed at the live force-field turn
  (`fight-20261021-castle03` turn 2909: HP −1000, `[DS:D6]=0`, `[DS:15]=[DS:18]=0x2e`).
  `0x2e` occurs 0 times in `mapx00`, so it is a derived mode, and the tile→mode
  mapping is the unattributed indirect store into `DS:2CB`/`DS:2DE` (F3), so the
  map byte is **not statically resolvable** and is left unguessed. The animated
  render is not in `asm/24B5` (it only flashes/sounds). Settled by the backlog
  **five-question** passability capture — specifically its question 5, attributing
  the `DS:2CB`/`DS:2DE` writers at the `0x2e` instant. Amended 2026-10-22: the
  capture's original question 2 asked whether the ladder reads a tile, which this
  finding had already killed, so the spec was rewritten to ask the live question.
  See `docs/passability-findings.md § Open` and `docs/phase4-overnight-cp5-report.md § T8`.
* The 30 live-grid cells that differ from `mapx03`.

~~The `map byte -> vehicle code` transform behind `[DS:14]`~~ - resolved in section
13's predecessor above: `[DS:14] = map_byte >> 1`.

## 13. Vehicles are map tiles, and the dungeon is a sector with a light timer

Harvests `captures/spice86/flight-32-live`, `flight-launched-live` and
`dungeon-35-live`, plus a purpose-built change-triggered recorder,
`tools/dungeon_capture.py`, writing to `captures/spice86/dungeon-entry/` (55 events,
each with a full DS and VGA snapshot).

### The map grid is the vehicle store

There is no separate vehicle list. A parked vehicle **is a tile**, and a ridden
vehicle is one byte.

| step | effect on the world |
| --- | --- |
| `B` on a vehicle tile | `[DS:13]` set to the vehicle code; the tile is rewritten to ground (`0x4C` -> `0x08`) |
| mounted | the vehicle exists only as `[DS:13]`; there is no tile for it |
| `L` | inertial flight: the vehicle keeps travelling in the facing direction until turned or landed |
| `L` on grass | `LAND PLANE.`, and the vehicle tile is **written back at the player's cell** |
| `L` elsewhere | `LAND--NOT HERE!!!!!`, flight continues |

Measured directly: the plane left `mapx32` (4,10) and appeared at (50,49), the cell
where the owner got out. Boarding a horse at (58,50) removed that tile and the other
four horses in the row were untouched. So vehicles are **relocated, not consumed**,
and combined with `asm/2305` writing the sector back this means leaving a vehicle
somewhere is a persistent world mutation.

It also means vehicles are finite per sector and cannot be farmed: there are exactly
17 plane tiles in the shipped maps.

### Passability is per-mode, not per-vehicle, and not per-tile-for-everyone

The owner found the plane could not cross forest, water or mountain. That looked like
a per-vehicle passability table and would have been the wrong lesson: the real cause
is **launch state**. Boarded-but-not-launched is ground-bound and obeys normal
passability, with `DS:20` (the invalid-move counter) climbing while the position does
not change. After `L` it flies.

This is the second time today the "one boolean per tile" model broke, and it does not
rescue the CP4b finding either - `asm/2423` still never reads a tile on foot, so
on-foot overworld walking remains unrejected. What is now established is that the
reject path exists and is shared: vehicles use the same `--INVALID MOVE!` counter as
a blocked foot step.

### The command set is context-dependent

| key | overworld | dungeon |
| --- | --- | --- |
| `A` | ATTACK | **ADVANCE** |
| `I` | inventory | **IGNITE TORCH** |
| `K` | (no-op message) | **climb the rope / exit** |
| `G` | GET | GET, and it looted a box containing a weapon |

`'IGNITE TORCH'` executed at runtime `0x3794` and `IT'S DARK!` at `0x129C`. The
engine cannot have one keymap; dispatch must be mode-aware.

### Dungeons are sectors, and the tiles mean something else in them

Entering the cave loaded a **different sector**: `DS:49/4A` went `3,0` -> `3,5`, and
the live grid at `DS:1800` became `mapx35`. Same mechanism as the castle link
(`00` -> `03`). No video mode change - it stays CGA mode 4.

This confirms the owner's recollection and resolves a tension with F13. `mapx35` is
42% `0x80` (`A`) tiles, and the letter-dense sectors are `mapx34` 68%, `mapx44` 53%,
`mapx24` 46%, `mapx15`/`25`/`35`/`45` ~42%, `mapx85` 30%. A field that is two thirds
one tile is a wall material, not shopfronts. So F13 needs splitting: `A`-`Z` tiles
spell signs **where they form words** (`AIR PORT`, `THIEVES GUILD`, `APOTHECARY`,
`TORTURE CHAMBER`), and a uniform field of `A` is dungeon rock rendered as wireframe.

The dungeon renders as a first-person wireframe corridor in mode 4, with VGA ink
around 314-542 against roughly 2970 on the overworld.

`mapx25`, `mapx35` and `mapx45` are **byte-identical** (md5 `e274897a...`, 0 of 4224
bytes differ). Any "which sector is this" check that compares bytes rather than the
sector digits will conflate them, and a content-addressed save would too.

### The dungeon grid carries two layers in one byte array

`mapx35` on disk uses exactly **7 distinct bytes**, all multiples of `0x10`: `0x00`
(46%), `0x80` (42%), `0xC0` (6%), `0xE0` (2%), and `0x40`, `0x10`, `0x20` at 15-19
cells each. Overworld maps use 30-50 distinct bytes. Seven aligned values is not an
art index; it reads as a flag or small-enum encoding of geometry. Only 7 of the 16
combinations a free 4-bit mask would allow occur, so it is not independent
per-direction wall bits either.

Objects are **written into that same grid at runtime as small integers**. Comparing
the shipped file against the live grid mid-visit, 32 of 4096 cells differ, and every
addition is a value `0x01..0x07` stamped into a cell that was `0x00` on disk:

```
0x1 x8   0x2 x8   0x3 x6   0x4 x2   0x5 x2   0x6 x4   0x7 x1
```

So the answer to "randomly generated or represented by tiles" is **both**: placement is
generated when the sector loads, representation is a tile in the map grid. This is the
same mechanism as overworld vehicles, where boarding stamps a cell and landing moves
it. It also means the shipped map files understate what the grid can hold, and a
reconstruction that treats the file as the whole truth will have no place to put
monsters, boxes or traps.

Not established: what the seven static bytes mean, and how the 64x64 grid maps to a
first-person crawl. The rope is **not** the tile under the player - the climb-out cell
(5,5) is `0x80`, the same byte as 32 of 48 dungeon positions - so it is either
facing-relative or a property of the entry point. Leads are recorded as a backlog
item in `docs/phase4-state.md` rather than guessed at here.

### Darkness and the torch

| observation | value |
| --- | --- |
| on entering a dungeon | `vga_ink` = **0** exactly, and `DS:26` = **0** |
| `I` ignite | spends **one** torch (`DS:64` 50 -> 49, once) and sets `DS:26` to about **150** |
| per dungeon move | `DS:26` decrements, roughly 1 per move |
| on leaving | `DS:26` keeps its value and stops ticking |
| on re-entering | `DS:26` is **zeroed again** and the screen is dark - you must relight and pay another torch |

So the light is a **step counter, not a clock**, and it is per-visit. The first draft
of this section claimed exiting ended the torch; that was wrong, the timer survived
on the overworld at 64, and it is entry that clears it. Reading one surviving value
as persistence was the error.

**Graded inference, owner-endorsed, not observed:** when `DS:26` reaches 0 mid-dungeon
the screen goes dark again. Never witnessed - the visit ended with 64 left, and the
one dark event in the log was an entry reset, not a burn-out. The rebuild may
implement it, on the owner's confidence, and this line is the record of it being an
inference rather than a measurement.

### Capture-tool defects found while doing this

* `tools/cfg_printed.py` did not list `LAND--NOT HERE!!!!!` as printed even though
  runtime `0x3B72` is present in `ExecutedInstructions`. A tool that silently drops a
  real executed site is the failure mode `AGENTS.md` warns about; until it is fixed,
  check `ExecutedInstructions` directly for any negative result it reports.
  **Fixed 2026-10-22 (overnight T1).** Root cause: the site was discovered, but
  `extract()` split the inline bytes on the first NUL and required the whole run to
  be printable; `LAND--NOT HERE!!!!!` is terminated by `\r` with the next string
  (`CMD: `) before any NUL, so the run failed the printable check and returned
  `None`. The tool now derives sites from `ExecutedInstructions` (primary) rather
  than CFG blocks, reads a message up to the first non-printable byte, fails loudly
  on an unrecognized dump shape, and carries a `--selftest` positive/negative
  control. See `docs/phase4-overnight-cp5-report.md § T1`.
* A probe that caches the 4 KB grid at startup produces **stale tile labels**, because
  the map mutates during play. That is what made me describe a vehicle depot that did
  not exist. `tools/dungeon_capture.py` re-reads the grid every sample for this
  reason.
* Reading the BIOS video mode needs physical `0x449` (`0040:0049`). Multiplying the
  whole address by 16 reads unrelated RAM and reported mode 30 instead of 4.
* The HTTP memory endpoint returns exactly what is asked for; an `IndexError` seen
  while writing the recorder was my own out-of-range index, not truncation.

### Still unexplained after this section

* ~~How a `0x50` rocket tile came to exist at `mapx30` (0,0), surrounded by water,
  when `mapx30` on disk has none - and a boat-art `0x30` at (27,11) where the disk
  has grass. Two cells, no attributed cause.~~ **Resolved / corrected 2026-10-22
  (overnight T5/T6), measured.** The overworld grid is the live object store, the
  same mechanism as this section's dungeon finding: a monster's grid cell holds its
  type byte (`DS:197+i`) and the shipped `mapx` file bakes monsters in at their
  spawn cells (`mapx03` has 43 monster-type cells on disk). Moved monsters
  therefore differ from the file in pairs (vacate spawn `sprite->70`, stamp new
  `70->sprite`); this accounts for all 46/12/50 diffs in the sector-03/32 captures
  with 0 unexplained. The `0x30` at (27,11) is decimal 48, a **monster type**, not
  boat art, and does not reproduce in the only sector-30 capture (there (27,11) is
  `0x08` in disk and live). The `0x50` rocket at (0,0) is real but appears in 1 of 8
  captures, is not a monster or the player's vehicle, and has no attributed writer
  — left unexplained rather than guessed. See
  `docs/phase4-overnight-cp5-report.md § T5/T6`.
  **Bounded further 2026-10-22, measured.** Four things ruled out, none of them the
  answer:
  * **not a partial or mis-based load.** In `flight-launched-live` the grid is
    otherwise **byte-perfect** against `mapx30` — exactly 1 differing cell in 4096.
    A wrong base pointer or off-by-N load would shift or smear many cells; this is a
    single one-byte store at grid offset `0x0000`.
  * **`(0,0)` is not structurally special.** Across all 33 `mapx` files the (0,0)
    byte is ordinary terrain (`0x00`/`0x08`/`0x0C`/`0x10`/`0x78`/`0x80`), and 7 of
    10 captures match disk at (0,0). The toroidal-wrap-origin idea is dead.
  * **not a stale sector carry-over.** No `mapx` file anywhere has `0x50` at (0,0),
    so the value cannot be a leftover from a previously loaded sector.
  * **the apparent extra instances are false.** `e2e-check-live` and
    `edge-test-live` also differ from `mapx00` at (0,0) (`0x03`), which looks like
    corroboration and is not: their grids differ in **2345** cells with non-terrain
    values, and the run never entered the world (player and entry both (0,0), stamp
    `0x00`). The grid buffer was simply never loaded. Do not count them as
    instances of this anomaly.

  Rocket is a legitimate tile value elsewhere in the game — `mapx03` (39,24) is a
  rocket in the four-vehicle depot — so the byte is not exotic, only misplaced.
  *Remaining shape, inferred not proven:* a lone store at grid offset 0 is the
  signature of a write through a zero or uninitialised index register, not of a
  mechanic, which predicts an arbitrary value and non-reproducibility — both
  observed. Settling it needs a turn-resolved capture of sector-30 entry, which is
  not worth a session on its own.
* ~~The 30 live-grid cells that differ from `mapx03`~~ - resolved above: moved
  monsters, the grid-is-the-object-store mechanism.
* **What `original/monsters` actually draws — unresolved, and now known to be
  downstream of the dungeon renderer rather than independently solvable.**
  Verified 2026-10-22, measured: the file is byte-identical at `DS:2900` in 8 of 10
  live DS dumps (record *r* at `DS:2900 + r*34`). The two exceptions are exactly the
  captures that never entered the world, so the load is conditional on entering the
  world. Structure is 64 records × 17 `(byte,byte)` pairs; `00 00` behaves as a
  stroke separator or pad, and records 13, 20, 27, 28 and 29 are entirely empty.
  Every populated record spans a narrow band on one axis (3–6 values) and a wide
  range on the other (up to 150), and renders as a **stroke** — rec30 is a clean
  diagonal — so a record is a piece of a drawing, not a whole monster. **The
  consumer is not in the decoded tree:** `INT 10h` is called only with
  `AH=01/02/06/09`, all text services; there is no `AH=0C` pixel plot anywhere, and
  no direct framebuffer write outside `asm/310B_cmd_C`. The dungeon wireframe
  renderer is therefore undecoded — the same blocker as the backlog's
  dungeon-geometry item. T9's rendering question belongs to that item and should
  not be counted as a separate open question.
* Whether the dungeon light timer reaching 0 darkens the screen (inference above).
* What the dungeon's `0x80`-heavy bytes actually encode for the wireframe renderer.

## 14. Combat entry is a command, and the fight *is* the world view (2026-10-23)

Capture: `captures/spice86/cp5-fight/` (`screen1..4.png`, `player-seed`, CFG and memory
dumps). Run: headless, seeded from `tmp/seed/player` (G23MAX, sector 31, entry (50,21)),
`--patch-arrows --auto-play`, driven by `tools/spice86_type.py` and read back over the
memory API. No display, no manual play.

### What a fight looks like (grade: observed, on screen)

`A` plus a direction attacks the adjacent cell in that direction. Verbatim from the
screenshots, in the rolling `CMD:` log at the foot of the world view:

```
CMD: ATTACK--EAST--HIT!!!
CMD: ATTACK--EAST--HIT!!!
CMD: KILLED--GOLD+1--EXP.+02
```

Nothing else about the screen changed. **There is no battle screen, no overlay, no mode
switch, no prompt** — the fight UI is one line of the command log drawn over the map the
player is already looking at. This is the strongest possible confirmation of the owner's
*"there's not really a leaves-combat state"*: it is not a recollection any more, it is what
the screen does.

The kill paid gold and exp and **both fields wrapped on screen** — GOLD `9999 -> 0010`,
EXP `9999 -> 0001` — because the seed was maxed at 9999. That is the BCD wrap the BCD
tickets describe, seen live rather than inferred from `asm/2DC1`/`asm/2DEE`.

Other observations from the same exchange:

* A creature **blocks** movement. `Right` onto `(51,21)` printed `EAST--INVALID MOVE!`
  although the tile reads `112`, the same map byte the player is standing on. So an
  occupied tile is impassable and `A` is how you clear it — which puts C3's "paralysis is
  a movement rejection" alongside an existing rejection rather than inventing one.
* Creatures move without any player input: slot 18 (`type 0xF0`) was adjacent east at
  `(51,21)` with `[DI+0x1D7]=0x03`; after the exchange it was gone and slot 7
  (`type 0x60`, hp 255) had walked to `(49,21)`, adjacent west.
* `[DS:29]` went `0x14 -> 0x00` across the first attack. Recorded, **not** interpreted;
  that byte is the mode byte C5's field trigger reads (`[DS:29]&0x7f == 0x2e`) and G4 owns
  the tile→mode question.

### The status tick is time, not moves (grade: measured, from an existing capture)

`captures/spice86/fight-20261021-castle03/` holds 3000 recorded turns of DS+VGA from a
real fight. Across it `DS:26B/26C/26D` were set and then decremented by 1 repeatedly
**while the player position never changed** — every turn tagged `reject`, pos pinned to
`(30,32)`/`(31,32)`. Example run: `00 00 03 0b -> 00 00 02 0a -> 00 00 01 09 -> 00 00 00 08`.

The owner's open question was whether paralysis lasts "some amount of time or moves".
**Not moves.** The tick is the game's own loop, not the movement path, so in the engine it
belongs to the main loop and not to `overworld` movement.

The tick routine is `asm/0C33..0C53` — six bulk-wave fragments, contiguous, each of
`mov`/`or`/`je` past a `decb`, one per counter. It ticks a **fourth counter at `DS:26A`**,
which read `0x00` for the whole recorded fight and has no name. `asm/080D` zeroes
`DS:26A..26D`; its caller is still undiscovered, so init-vs-load remains unproven.

### What this changes for CP5

* Combat entry needs no new subsystem: a command handler plus an adjacency test against the
  creature arrays at `DS:137/157/177/197`. It goes on the engine's `cmd_*` surface.
* The acceptance bar "a rendered fight" is now a defined target, not a vibe: world view,
  `A`+direction, `--HIT!!!` / `KILLED--GOLD+n--EXP.+m` in the CMD log, HP/gold/exp as BCD.
* C3 gains a sibling path (occupied tile = rejected move) instead of needing its own.

### Still open after this section

* G24 — whether the counters survive SAVE. The static argument says they cannot (they sit
  outside `DS:36..135`), but the empirical seed-save-reload is still owed.
* What distinguishes a hostile creature from a merely present one. Nothing here
  distinguished them: the attack landed on a `role=03` creature with no provocation, and no
  guard response was seen in that short exchange. The guard-hostility hypothesis is
  untested, not refuted.
* What `[DS:29]`'s `0x14 -> 0x00` means, and what `DS:26A` is for.

## 15. Hostility: what attacks you, what happens when you win, and what happens when you lose (2026-10-23)

Captures: `captures/spice86/cp5-guard-aggro/` and `captures/spice86/cp5-weak-fight/`.
Driven headless with a seeded character; the owner's suggestion to lower the stats is what
produced the miss and death frames, which a maxed character cannot show.

### The whole fight arc renders in the one log line

| stage | on screen |
| --- | --- |
| miss | `CMD: ATTACK--EAST--MISS` |
| hit | `CMD: ATTACK--EAST--HIT!!!` |
| kill | `CMD: KILLED--GOLD+1--EXP.+02` |
| low HP | `H.P.=0030` drawn in **red** — a renderer state CP4 never had to produce |
| death | `G23MAX IS DEAD!`, and HP, FOOD, EXP **and** GOLD all read `0000` |

The death frame is `asm/0C62` seen live: it stores 0 into `DS:51..59` and calls
`L_0052f9`. Four separate on-screen fields going to zero from one nine-store routine is now
confirmed by the screen rather than by the disassembly alone.

### Hostility is provoked, not ambient (grade: observed)

* A `Fighter` (`type 0xF0`) stood **adjacent** to the player in a town across several runs
  and never struck unprovoked. `P` (PASS) x4 produced no monster turn either, so there is
  no "creatures act while you wait" loop doing the work here.
* Strike a **Guard** (`type 0x60`) and everything changes: guard HP `255 -> 221`, **player
  HP `9999 -> 9797`**, and the other guards converged. Sum of manhattan distances to the
  player fell `203 -> 165 -> 156 -> 137 -> 133` over ~50 s, with individuals going `d9 -> d4`
  and `d12 -> d6`. `guard2.png` shows them arriving in a line.
* A **missed** swing provoked nothing: the first attack on that guard was
  `ATTACK--WEST--MISS` and 14 s of watching showed zero reaction.
* Monsters 16 tiles away wandered randomly and did not pursue, so aggro has a **range**; it
  is not "the whole sector".

This corroborates the owner's model (`recalled`, and it has now survived every test so far):
monsters do not simply attack on sight in a town, town NPCs do not start fights, and hitting
a guard brings the town's guards down on you.

### The role byte carries a state, not only an identity (grade: measured, n=2)

Two separate runs showed an **attacked** `Fighter`'s `[DI+0x1D7]` go `0x03 -> 0x01`.

~~Reading the whole table afterwards: every Guard is `0x01`, every Merchant `0x02`,
untouched Fighters `0x03`, scripted specials `0x81/0x82`. So the low nibble is not purely a
class — a Fighter that has fought the player ends up carrying the value guards are born
with. Either `01` means "willing to hit you" and guards are born hostile, or the transition
is a combat flag and the coincidence with the guard value is meaningless.~~

**Retracted the same day — see § 16.** The guard values were read *after* a fight, and a
baseline taken seconds after load shows guards are born `0x03`/`0x00`. `0x01` is not a class
value at all; it is an alarm set by landed violence, sector-wide for the guard class. The
surviving half of this observation is the interesting half: the byte moves during play, and
the thing that moves it is combat.

### What this section does not answer

* Whether the low nibble is cause or effect: does `role=01` make a creature attack, or does
  attacking set it to `01`?
* Whether the guard response is sector-wide or radius-based.
* Whether *killing* a guard escalates further than wounding one.
* What `DS:26A` is, and what `[DS:29]`'s `0x14 / 0x09 / 0x00` values mean.

### Consequences for the CP5 design

* The encounter model is **not** a random-encounter roll and not an always-on aggro: it is a
  per-creature willingness byte plus a proximity test, and provocation flips it. Build the
  engine so hostility is data on the creature, not a rule about creature kinds.
* Retaliation is the monster's attack, so the engine needs no separate monster AI turn to
  reach a rendered fight — which is what makes "a rendered fight" a small piece of wiring
  rather than a subsystem.
* Death is a state of the world view, not a screen: zero the fields, print
  `<NAME> IS DEAD!` into the CMD log. `asm/0C62` is the citation.

## 16. G25 answered: `0x01` is an alarm, and violence in a town sets it sector-wide (2026-10-23)

Capture: `captures/spice86/cp5-g25-aggro/` (`roles-at-load.txt`, `probe.py` is the reader
that produced every number here, `player-seed`). Two fresh runs of the same seed, one to
strike and one as a time control.

**First, a correction to § 15, which got this backwards.** § 15 claimed every Guard is
*born* `0x01`. A baseline taken seconds after load says otherwise: guards at load are
`0x03` and `0x00`. The `0x01` values § 15 saw were already the aftermath of the fight it
was describing. Reading the table before touching anything was the step missing the first
time.

### What was measured

| observation | value |
| --- | --- |
| guard roles at load, before any input | `0x03` (six slots) and `0x00` (two slots) |
| one landed strike on an adjacent Fighter, `slot 18` | that Fighter `0x03 -> 0x01` |
| **every guard in the sector, same moment** | all eight `-> 0x01`, **including slots at d31, d44 and d46 that were never touched** |
| never-struck non-guards in the same tick | Fighters and `0x30` creatures stayed `0x03` |
| player HP after the guards arrived | `9999 -> 9796`, with the nearest guard at d1 and four formed up at d9-d11 |
| **time control: fresh load, 65 s, no input at all** | **zero role changes, zero HP changes** — guards wandered (one moved `(30,14) -> (36,20)`) and stayed `0x03`/`0x00` |
| second control: 35 s idle after the flip | zero further changes |
| a **missed** swing at a guard (§ 15, `guard1.png`) | no reaction in 14 s |

### The model, graded

`[DI+0x1D7] == 0x01` reads as **"will attack the player"**, and — this is the part worth
stating carefully — it is **both a birth value and an acquired one**. `original/monx31` ships
`0x01` on the `0xFC` and `0x2C` slots, so those kinds start hostile; Guards (`0x60`) are born
`0x03`/`0x00` and **every guard in the sector flips to `0x01` the moment a blow lands on
anything in town**, while a missed swing flips nothing and 65 s of idle flips nothing.
Merchants are `0x02` and were never seen to attack.

That is a second correction to this section while writing it up, and it goes in the order it
happened: the first draft said guards are born `0x01` (they are not — that was the aftermath
of the fight), and the revision said `0x01` is therefore purely an alarm (also not — the
sector file hands some kinds to the game already set to it). The byte is a hostility flag
that some kinds are born with and guards acquire.

**What is still inferred, not observed:** that `0x01` is read back as the *cause* of
attacking. The kinds born with it sat 25-35 tiles away in every capture, so they never got
within range to prove it. The claim this section can make is: landed violence sets `0x01` on
guards sector-wide, and guards carrying `0x01` then close on the player and take HP.
Distance from the player did not predict which guards flipped (d1 and d46 both did), so the
alarm is sector-wide for the guard class.

This is the aggro marker the handoff was reaching for. It was in `DS:1D7` after all — in the
low nibble, not bit 7. The owner's recollection ("attack a guard and all the guards in town
want to fight you") was right and is *wider* than recalled: it does not require attacking a
guard, only landing on anything in town.

### Still open

* Whether `0x01` is read back as a cause — i.e. does a creature with `0x01` chosen by some
  other means actually attack. Everything here sets it by hitting things.
* What the other low values mean (`0x00` vs `0x03` both appear at load on guards).
* Whether the alarm survives leaving and re-entering the sector, and whether it survives
  SAVE — that is the same persistence question as G24, now worth asking about `DS:1D7` too.
* Whether killing rather than wounding escalates further.

## 17. G24 answered: status does not survive a save — and `DS:6D` is not what C6 thought (2026-10-23)

Capture: `captures/spice86/cp5-g24-save/` (`player-seed`, `player-after-save`,
`check.py`, three screenshots). Method: `write_memory` over the MCP endpoint (the HTTP API
cannot write), then `Q` in-game, then read the file the run wrote.

### The experiment

Poked `DS:26A..26D = 07 09 08 08` (the values actually observed together on 2026-10-21,
plus the unnamed fourth counter) and slot 0's role byte to `0x01`, pressed `Q`, and read the
384-byte `player` file back.

| check | result |
| --- | --- |
| is `07 09 08 08` anywhere in the saved file | **no** |
| bytes differing seed → saved | **five**, all inside the proven record: `DS:55 99->89` (food, −10 = one PASS), `DS:5D 00->51`, `DS:6D 01->00`, `DS:74/75 0000->123a` (the known scratch pair) |
| where `DS:26A` would have to sit | file offset `0x234` = 564, in a **384-byte** file — past the end |
| reload the saved character | `DS:26A..26D = 00 00 00 00` |

**G24 is closed: the status counters are not persisted, and not restorable.** The save
record boundary in `docs/save-format.md` is right; `DS:26B/26C/26D` are simply outside the
file. The same arithmetic covers the town alarm from § 16 — `DS:1D7` maps to file `0x1A1`
(417), also past the end — so **an alerted town cannot survive a save either**. Grade:
measured (the poke-save-reload cycle) plus arithmetic (the offsets).

That settles C1's design question: status is in-session state, full stop. The engine must
not grow a persistence path for it, and `combat_check.c`'s assertion that the counters live
outside the record is now backed by a save file rather than by an offset argument alone.

### The tick, seen live rather than reconstructed

In sector 00 the poked counters were watched decaying while the player never moved:
`07 09 08 08 -> 03 05 04 01 -> 02 04 03 00 -> 01 03 02 00 -> 00 00 00 00`. All four, in
lockstep, at rest. That is `asm/0C33..0C53` running, observed directly, and it is the third
independent confirmation that the tick is time and not moves.

### `ONLY ON EARTH!` — seen live for the first time, and it contradicts C6

`Q` outdoors in sector 00 printed `QUIT OR SAVE GAME.` followed by **`ONLY ON EARTH!`** and
wrote nothing. The gate is `asm/3DFA_offer_only_outdoors_earth`:

```asm
mov    0x6d,%al
or     %al,%al
je     L_004017        ; zero -> allowed
; else print 'ONLY ON EARTH!'
```

**Correction while writing this up:** this gate was *not* unrecorded. The engine already
lifts all three save gates — `u2_save_policy` in `engine/src/game/overworld.h` names
"only on Earth (`DS:6D == 0`)" and cites `asm/3DE4`/`asm/3DFA`. What was new to me, and
worth saying plainly so nobody repeats the detour: an agent can observe a gate firing live,
walk to the routine that owns it, and call it a discovery, without noticing the engine
already implements it. Grep the engine before crediting a finding.

What *is* new is the contradiction. `[DS:6D] != 0` means "not on Earth", and the byte is
inside the saved record (the poked `0` persisted: `DS:6D 01->00` in the diff above).
**That collides with the CP5 design constraint claiming `DS:6D` is a stale duplicate of the
boots count** — the claim behind "the original keeps two copies of some item counts and one
goes stale". A boots count does not gate whether you are on Earth, and the value `1` seen
after a thief took the boots is exactly what an on-Earth flag looks like. The engine's
reading is the one with a branch behind it. **Flagged, not resolved** — but it needs
resolving before the engine adopts "single source of truth for item counts" on the strength
of the boots story.

### Capture notes worth keeping

* Sector 00 at (30,30) is a monster hot-spot where the CMD loop never regains control —
  messages scroll continuously and injected commands are eaten, so `Q` does nothing there.
  Sector 10 authors no monsters (`monx10` is empty, per `docs/monster-spawn.md` § 6) and is
  the quiet ground for save/state experiments.
* `SCRUBED BY MAGICAL BOOTS!` was seen live in the sector 00 crowd — the C2 boots resist
  firing, with the game's own spelling.

## 18. The turn is a timeout: the game passes for you, and creatures mostly do nothing (2026-10-23)

Capture: `captures/spice86/cp5-turn-timeout/` (`turn-t0.png`, `turn-t1.png`,
`halfaction.png`, plus `timeout.py` and `per_turn.py`, the two readers that produced every
number here). Sector 31, seeded character, **no input sent during any measurement**.

The owner proposed this model from memory — *"each turn has a timeout, so if I don't perform
an action before that timeout it PASS, or if I only do half an action it does
`ATTACK--PASS`"* — and it is exactly right on both halves.

### The auto-pass is real, and it is the clock

At `t0` the CMD log was empty: one line, `CMD:`, waiting. Twenty-five seconds later, with
nothing sent, it read:

```
CMD: PASS
CMD: PASS
CMD: PASS
CMD:
```

Measured properly via the food field (`turn_recorder.py` already established a PASS costs 10
food and a move 19, and the player did not move): **6 auto-passes in 45 s, so a turn is
~7.5 s.** That is the missing mechanism behind § 15 and § 17 — the status counters tick on
*something*, and the something is this turn timer.

### A half action becomes a pass

Pressing `A` alone and letting the timer expire printed, verbatim:

```
CMD: ATTACK--PASS
```

The command was accepted, its direction never arrived, and the turn expired into a pass with
the half-typed command still on the line. This is why `CMD:` lines in older captures show
`PASS` the player never typed, and why `ATTACK--PASS` had never been reproduced: nobody had
left the timer run out on purpose.

### Do creatures each get a turn? They get one, and it is usually a no-op

The naive measurement — net displacement over a window — cannot answer this, because a
random walker that leaves and returns reads as stationary. So the food field was polled fast
and creature positions snapshotted **at each pass boundary** instead. Ten boundaries, 32
slots, 288 (creature, turn) pairs:

| result | value |
| --- | --- |
| creature-turns that produced movement | **21 of 288 — 7%** |
| movement when it happened | **2 cells**, consistently |
| the persistent mover | slot 5, a Guard (`0x60`): 2 cells on **9 of 9** boundaries |
| the occasional movers | slot 2 (Guard) 5/9, slot 12 (`0x30`) 3/9, slots 14, 19, 6 |
| the rest | 26 of 32 slots never moved at all, including the Fighter standing adjacent to the player and every Merchant |

So the shape is **per-creature turns that mostly do nothing**, not "every creature steps each
turn". An engine that loops `for each creature: move` per turn would churn the world roughly
six times faster than the original, and would empty the streets of the stillness that makes
the town look inhabited. Two further notes: the movers shift **2 cells per turn**, not 1,
which is worth pinning before it is implemented from intuition; and an idle Guard sitting
still is not a contradiction with § 16 — provoked guards converge, unprovoked ones largely
stand where they were put.

**Grades.** Auto-pass existing and its text: observed. The ~7.5 s figure: measured, one
45 s window, one sector. `ATTACK--PASS`: observed. The 7% and the 2-cell step: measured, one
sector, one 10-boundary window — **treat as a first measurement, not a constant**, and
re-measure in a wilderness sector before the engine hard-codes either number.

### Addendum, same day: the timeout is a poll counter, and the counters do not tick per turn

Two corrections to this section, both found by reading the engine before changing it.

**1. The turn budget is not a clock.** `engine/src/game/overworld.c` already implements the
auto-PASS, and its comment block cites the bytes: the overworld wait loop polls the keyboard
(runtime `0x08CE`), tests `cmp $0xff,%ah` for "no key" (`0x08D1`), and advances a **byte
counter** with `incb DS:21; jne <loop>` (`0x08D6`). `incb`/`jne` is iterate-until-wrap, so
the original's timeout is a **poll count**, not a timer. That
earlier note also records a measured **~7.6 s at `--Cycles 3000`** and deliberately declines
to encode it, because the wall-clock figure is an artifact of the emulator's cycle setting.
The ~7.5 s measured above reproduces that number independently, which is corroboration of
the *observation*, not a licence to hard-code it: a machine that polls faster gets a shorter
turn in the original, and an engine that budgets milliseconds would be **less** faithful
than the poll count it would replace. An agent about to "fix" this by switching to a
wall-clock deadline should read that comment first; the knob for the owner's enhanced-mode
request already exists as `--auto-pass-polls` (`-1` disables it for pure turn-based play).

> **CORRECTION 2026-10-28d — the reasoning above was right and the evidence under it was
> false.** This paragraph went on to say "there is no `INT 1Ah` anywhere in the corpus, and the
> only read of the BIOS tick at `0x40:6C` is the startup RNG seed", and that sentence has been
> standing since 2026-10-21. It is wrong on both halves. `CD 1A` occurs four times in the
> binary, three of them inside one routine at runtime `06AC..06EC`, and all three of those
> execute in **136 of 136** harvested `ExecutedInstructions` sets — the game reads the BIOS
> tick on every single run. The routine is now decoded and named
> (`asm/06AC_clock_tick_wait`): it opens a 20-tick window (`cmp $0x14,%dx`), counts how many
> 100-iteration busy passes fit in it into `DS:74`, and the code immediately after the `ret`
> multiplies and divides by that count, which reads as machine-speed calibration — inferred,
> not proven, and nobody has dumped `DS:74` live.
>
> **The conclusion stands; the reason given for it does not.** The PASS timeout is a poll count
> because the PASS loop's own bytes say so (`incb DS:21 ; jne` at runtime `0x8D6`), not because
> no clock exists. Two things to take from that. First, a negative-existence claim over a
> *partially decoded tree* is not a negative result about the binary — this is the third time
> that shape has bitten (F6, T2, and the `strings_atlas` trailing-`db` drop). Second, and this
> is the trap: the clock routine and the PASS loop are **different code that share a number
> across bases** — the third `INT 1Ah` is at *file* `0x8CE`, and the keyboard poll is at
> *runtime* `0x08CE`. Anyone re-deriving this by grepping `8CE` will land on the wrong one and
> conclude the opposite of whatever they were trying to check.

**2. The status counters do not tick once per turn.** Poking `DS:26A..26D = 0f 0f 0f 0f` and
sampling at 0.7 s intervals with the player idle:

| t | counters | food |
| --- | --- | --- |
| 0.0 s – 4.2 s | `0f 0f 0f 0f` | 999931 |
| 4.9 s | `0a 0a 0a 0a` | 999921 (**one** pass) |
| 5.6 s | `00 00 00 00` | 999921 |

Fifteen decrements across roughly one turn, in two bursts, while the food field recorded
exactly **one** PASS. So the "time, not moves" conclusion of § 15 still stands — they decay
with the player at rest — but the cadence is **not** one-per-turn and is currently
unexplained. `asm/0C33..0C53` is the decrement site and its **caller is still undiscovered**,
which is consistent with a fast internal loop rather than the turn boundary. The engine must
**not** grow a per-turn decrement on the strength of § 15; finding the caller of `0C33` is
the prerequisite, and it is now the open item.
