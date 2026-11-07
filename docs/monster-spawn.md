# Monster spawn — two sources, and `monxNN` is the second one

Found 2026-10-22 while trying to trigger a shop. It closes the HP question that was open
all day, and it corrects a documented claim about the `monx` file format.

Grades per `AGENTS.md` § 5.

## 1. There are two spawn sources, not an area override

Everything earlier today was confused because one model was assumed for all monsters.
There are two:

| source | HP comes from | evidence |
| --- | --- | --- |
| **placed monsters** | `monxNN[64 + slot]` — a byte in the sector's data file | **32/32, 32/32, 31/32** across three captures in two sectors |
| **wandering spawns** | the nine per-kind immediates in the spawn arms | ~40 captures in sector 20 match the arms exactly |
| dungeon spawns | computed in `asm/147F_dungeon_monster_spawn` — **now validated live, 54/54 slots, see § 9** | read + `tools/dungeon_spawn_check.py` |

**There is no "area override" rule.** That was the wrong frame. Sector 00 reads HP 255
for every monster because `monx00[64:96]` is literally all `0xFF`. Sector 20 matches the
spawn arms because `monx20[64:96]` is all zeros — that sector has no placed monsters, so
everything in it is a wandering spawn.

This retroactively explains the whole day's confusion: the compound at sector 0 was never
special, it was just fully authored.

## 2. `monxNN` layout — the documented format is wrong

`docs/phase4-implementation.md` described `monxNN` as "256 monster-ID slots (16x16) +
128 B tail". It is not. The file is **six parallel 32-byte arrays**, one entry per
monster slot, followed by a tail. (Five when this section was first written; the sixth
was found 2026-10-23 by the corpus-wide re-scan in § 6.)

| offset | field | live DS array | match, three captures |
| --- | --- | --- | --- |
| `0..31` | x | `DS:0x137+i` | 18/32, 27/32, 8/32 — **drifts, expected** |
| `32..63` | y | `DS:0x157+i` | 18/32, 29/32, 13/32 — **drifts, expected** |
| `64..95` | **HP** | `DS:0x177+i` | **32/32, 32/32, 31/32** |
| `96..127` | **type** | `DS:0x197+i` | **32/32, 32/32, 32/32** |
| `128..159` | kind | `DS:0x1B7+i` | 32/32, 28/32, 32/32 — **does not hold everywhere, see § 6** |
| `160..191` | **flag** | `DS:0x1D7+i` | new 2026-10-23; values `01..03` and `81..85` |

x and y under-match because monsters move after spawn; the file holds spawn positions.
The three static fields match at or near full width. The one 31/32 HP miss is a monster
that took damage.

Captures used: `shop-probe` (sector 03, taken today), `autoplay` (sector 00),
`castle-03-live` (sector 03).

## 3. This closes the townsfolk question

The open item was *where townsfolk HP is assigned*, given that no spawn arm covers Guard,
Merchant, Cleric or King. **They are placed monsters.** The Merchants, Clerics and Guards
in sector 03 are entries in `monx03`, and their HP is the byte at `monx03[64 + slot]`.
That is why no arm names them: arms only handle wandering spawns.

The static search had concluded "every write site of `[DI+0x177]` is enumerated and none
can be a town NPC spawner" — correct, and the reason is that there is no spawner to find.
The values are loaded from disk.

## 4. What this does not settle

* **Only two sectors tested.** 00 and 03. Nothing says the layout holds in sector 24 or
  45, though the field widths are consistent.
* **The loader is not yet identified.** Something reads `monxNN` into the DS arrays; that
  routine is unnamed. `asm/22D5` is documented as the `monx` read with `CX=0x100`, which
  is 256 bytes — inconsistent with a 160-byte used region and worth re-reading.
* **Dungeon sectors do not use this.** `dungeon-35-live` matched `monx35` at 0/27. They
  have their own encoding — see § 9.
* **The shop is still untriggered.** See § 5.

## 5. Why the shop capture failed

The player was placed at (16,25) in sector 03 beside a letter tile at (15,25) byte `0xC0`,
`T` was sent twice, and **zero executed addresses fell inside `19D7..2027`** — no shop ran.

The reason is visible in the same dump: **no NPC of any kind was within three tiles of
that shop tile.** The two Merchants in the sector were at (50,41) and (39,50), and neither
is adjacent to a letter tile in `mapx03`. So F13's owner report — *"`T` still works with a
shopkeeper standing on the other side of one"* — is not a description of a nicety, it is
the requirement. A shop tile alone does nothing; the transaction needs the shopkeeper
positioned at it, and in sector 03 no merchant is.

**G22 therefore stays open**, and the next attempt has to find a sector where a merchant
actually stands beside a letter tile, or place one there. That is a data question, not a
key-injection question.

## 6. The layout re-checked across the whole corpus — B4, 2026-10-23

`tools/monx_layout_scan.py` reads the sector out of every capture on disk (from `DS:49/4A`,
never from the directory name), loads that sector's `monxNN`, and scores each array over
**only the slots the file actually authors** — a slot with `monx[96+i] == 0` holds no
placed monster, so whatever lives there came from a wandering spawn and is not the file's
business. That distinction is the whole reason this re-scan disagrees with a naive one:
scoring unauthored slots is what makes sector 20 look like a total layout failure when it
is actually an empty file.

| sector | captures | x | y | HP | type | kind | flag | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 00 | 4 | 85/124 | 85/124 | **124/124** | **124/124** | 107/124 | **124/124** | holds except `kind` |
| 03 | 5 | 78/160 | 89/160 | 158/160 | **160/160** | **160/160** | 144/160 | holds; HP misses are damage |
| 20 | 40 | — | — | — | — | — | — | **`monx20` authors nothing**; untestable |
| 31 | 12 | 207/384 | 275/384 | **384/384** | **384/384** | 349/384 | **384/384** | holds except `kind` |
| 32 | 1 | 7/32 | 10/32 | 31/32 | 31/32 | 29/32 | 24/32 | one capture; slot 16 is a killed monster (`type` cleared to 0) |

**`type` and `HP` hold everywhere they can be tested.** Every miss is explained: HP below
the file value is damage, `type` cleared to zero is a killed and cleared slot. No capture
shows HP *above* the file value, which is the property that would have broken the model.

**`kind` does not hold, and that is a finding.** Sector 03 matches 160/160, but sector 00
mismatches 17 slots (file `0x08`, live `0x0C` at slots 28–31), sector 31 mismatches 35
(file `0x70`, live `0x08` at slot 6), sector 32 mismatches 3. So `DS:0x1B7` is **not**
faithfully loaded from `monx[128:160]` — something rewrites it after the load. That is
consistent with § 9, where the dungeon spawner is proven to write `slot >> 1` into the
same field, and it means the earlier "`kind` matches 32/32" on sectors 00 and 03 was a
lucky sample rather than a rule.

### The sixth array, and what it looks like

`monx[160:191] → DS:0x1D7` matches live exactly in sectors 00 and 31, and drifts only
where a wandering spawn overwrote a slot in 03 and 32. Its values are `01`, `02`, `03`
and `81`–`85`. The high-bit set on `81..85` is the Apple II convention the `tlkx` files
use, so this array plausibly holds two things: a small group value `1..3` and a flagged
marker `1..5`. **What it means is not established**, but it now has readers, and one of
them is decisive enough to narrow the field a lot.

`asm/40C0` (a standalone 8-byte fragment, **not** part of `asm/4050_cmd_T.asm` — it is
reached from there by `jne L_0042c0` at `asm/4050_cmd_T.asm:103`) does
`mov 0x1d7(%di),%al ; cmp $0x80,%al ; jb ...`, i.e. it **branches on bit 7**. Found
2026-10-23 while decoding the TRANSACT path.

**`asm/3C2E_cmd_O` (OFFER GOLD) is the reader that gives the field its shape, and it
contradicts the aggro reading.** It reads `[DI+0x1D7]` with `DI = [DS:1F]` (the cursor
slot), and where bit 7 is **clear** it falls through to the generic `THANK YOU VERY
MUCH!`; where bit 7 is **set** it requires `[DS:4A]==2 && [DS:49]==3` (site kind/index)
and then **enumerates the individual high values**, each with its own scripted outcome
(observed bytes, `asm/3C2E_cmd_O.asm:94-150`):

| value | branch | cited consequence |
| --- | --- | --- |
| `0x81` | `L_003eed` | needs `[DS:27] >= 5`, prints `ENILNO IS YOURS!`, sets `[DS:7F]=1` |
| `0x82` | `L_003f10` | with `[DS:6C]==0` prints `EARN THE RING!`, else `THE RING IS YOURS!` and sets `[DS:D6]=1` — the **RING slot** from C5 |
| `0x83` | `L_003f52` | third scripted recipient |

`asm/2C38_sub_2c38` writes the constant `0x84` into `[DI+0x1D7]` immediately before
printing `SHE'S GONE!!!` — a departure transition, not a mood. And `asm/102B_sub_102b`
tests the slot for **zero**, which is what an empty-slot check looks like.

**Reading, graded `inferred` from the above `observed` bytes:** `DS:0x1D7+i` is a
per-slot **role / scripted-identity** byte — `0` empty, `1..3` ordinary classes, `0x81..0x85`
named recipients — and bit 7 means "this slot has a scripted response", not "awake".
**The owner's aggro-marker hypothesis is not supported by any consumer found so far**, and
this is the second field where the bytes declined to cooperate with it. It stays a
hypothesis about some *other* byte, not a dead one.

**Update, same day — the low nibble moves, and § 16 identifies what moves it.**
Two live runs showed an **attacked** Fighter (`type 0xF0`) go `0x03 -> 0x01`. A baseline
taken seconds after load shows guards are born `0x03`/`0x00`, so `0x01` is not a class value:
**it is an alarm.** Landing a blow on *anything* in a town flips every guard in the loaded
sector to `0x01` at once — including slots 46 tiles from the player — while a missed swing
flips nothing, and 65 s of idle on a fresh load flips nothing either. Distance from the
player did not predict which guards flipped, so for the guard class the alarm is
sector-wide.

So the handoff's instinct about this array was right and the bit was wrong: the aggro state
lives in the **low nibble**, not bit 7. What bit 7 still means (scripted recipients
`0x81/0x82/...`) is unchanged. Evidence: `docs/spice86-live-findings.md` § 16, capture
`captures/spice86/cp5-g25-aggro/`. Open: whether `0x01` is read back as a *cause* of
attacking (everything here set it by hitting things), and what distinguishes `0x00` from
`0x03` at load.

Writer attribution also corrected: the writer is `asm/114A_sub_114a` (`mov %al,0x1d7(%di);
dec %di; jns` — a downward fill loop at runtime `114A..1150`), which **precedes**
`asm/1151_dungeon_turn` (`1151..12DA`) and is not part of it. An earlier note here
cited `1151`.

### Which sectors author monsters at all — and it is the site-kind scheme again

Scanning all 33 `monxNN` files directly:

* **16 sectors author monsters**: `00`, and every sector whose ones digit is 1, 2 or 3 —
  `03 11 21 22 23 31 32 33 41 61 71 81 82 92 93`.
* **17 author nothing** (all five arrays zero): every ones-digit 0 wilderness sector
  (`10 20 30 40 50 60 70 80 90`), every `X4` tower (`24 34 44`) and every dungeon
  (`15 25 35 45 85`).

So **`monxNN` is non-empty exactly where `tlkxNN` exists, plus sector 00.** That is a third
independent channel — after map signs and street dialogue (`docs/place-names.md` § 2c) —
agreeing with the ones-digit site-kind scheme, and sector 00 is again the exception: the
compound has authored monsters but no street dialogue.

It also explains a standing confusion in this file. § 1 says sector 20 matches the spawn
arms "because `monx20[64:96]` is all zeros" — the scan confirms it authors **nothing at
all**, so all 40 sector-20 captures are pure wandering spawns and the arms were the only
possible answer there.

### The `monx` tail is a pack-wide pattern

Every `monxNN` is 384 bytes, and in every one checked:

```
monx[256]          == 0x1A
monx[257:384]      == monx[129:256]      (byte-identical)
monx[192:256]      == all zeros
```

The `0x1A` at offset 256 is the same marker the `player` file carries at `0x100`, and
`mapxNN` carries one at the head of its own 128-byte tail — so `0x1A` at the start of a
file's tail region is a pack-wide convention, not a per-file oddity. **What the shifted
duplicate means is not known.** It is not a second monster table (it duplicates the
`kind` and `flag` arrays offset by one), and nothing has been found that reads it. The
open question in `docs/phase4-state.md` § Deferred decisions — "what is the `0x1A` tail"
— is narrower than it looked, and this is the shape of the answer: a marker plus a
shifted copy of the last two arrays.

## 7. Follow-ups opened by § 6

| item | note |
| --- | --- |
| Name the `flag` array (`monx[160:191]` / `DS:0x1D7`) | **narrowed 2026-10-23, not finished.** § 6 now reads it as a per-slot role/scripted-identity byte (`0` empty, `1..3` ordinary, `0x81..0x85` named recipients) from `cmd_O`'s enumerated branches. Remaining: prove the *low* group `1..3` means something, and find who writes the per-slot values — `asm/114A` is a constant fill, not a per-slot load |
| Find what rewrites `DS:0x1B7` after the `monx` load | `kind` mismatches in 3 of 4 testable sectors; the dungeon spawner is one such writer, the overworld writer is unnamed |
| G20 — enter a tower sector | `monx24/34/44` author nothing, so a tower is expected to behave like wilderness for spawning. Testable in one launch |
| Read the `monx` loader | § 4 notes `asm/22D5` reads it with `CX=0x100` = 256 bytes, which covers arrays 1–5 and **not** the sixth at `160..191`. Either the read is larger than recorded or the sixth array arrives another way |

## 8. Curiosity, unproven

The wizard's "gibberish" in `asm/19D7_magic_shop_menu` reads as **Turkish**:
`BIR IKI UC` (one two three), `LUTFEN EVET` (please yes), `GULE GULE` (go smiling /
goodbye), `UGURLA OLSUN` (may it be auspicious). Recorded as a curiosity, **not a
measurement** — it is one agent's inference from strings and wants a native speaker's
eye before it becomes a claim. The interesting version, if it holds, is that the magic
shop's "mystical language" is a real language rather than invented noise.

## 9. The dungeon encoding — A4, measured

`tools/dungeon_spawn_check.py` reads the monster record out of every capture on disk,
selects dungeon-shaped ones **by encoding rather than by directory name**, and checks
them against what `asm/147F_dungeon_monster_spawn.asm` computes. Result:

> **54 of 54 monster slots across both dungeon captures satisfy the formula exactly.**
> The captures are `dungeon-35-live` and `airport32`, and both are sector 35
> (`DS:49=3`, `DS:4A=5`) — the second is the stale name `docs/monster-spawn.md` § 5
> warns about, and its monster record is identical to the first.

### What `0x01`–`0x06` are

A dungeon `type` is **not** an overworld type byte and **not** an index into the same
creature table. It is drawn as `type = (rand & 7) + 1` with the value 7 rejected, so the
domain is **1..7** — seven dungeon creature slots, which is exactly the size of the
dungeon set the third-party table carries. Six of the seven appear in the captures; 7
needs a slot with `slot>>1 >= 12` and the draw to land on it.

### `[DI+0x1B7]` in a dungeon is the slot number, not a kind

`asm/147F` at `L_0016d1..L_0016d6` does `mov %di,%ax ; clc ; rcr $1,%al ; mov %al,0x1b7(%di)`.
`DI` is the slot index, so **`[slot+0x1B7] = slot >> 1`**, and the check confirms it on
54 of 54 slots. The values seen are `0x02..0x0F`, which is the range `slot>>1` produces
for the occupied slots 4..31.

This matters because `0x1B7` was re-labelled from "sprite" to "small kind index" on
2026-10-22 after a live read found `0x08`/`0x0C`. That reading was right for the
overworld and **wrong here**: in a dungeon the field holds no creature information at
all. It is a slot-derived operand the HP formula consumes.

### The slot index is the difficulty ladder

The accept test at `L_0016f0..L_0016f8` is `cmp 0x1b7(%di),%al` against `2*(type-1)`,
accepting on equal-or-below. So a monster is only admitted when

```
2 * (type - 1) <= slot >> 1        i.e.   type <= (slot >> 1) / 2 + 1
```

Slot 4 admits types 1–2; slot 31 admits 1–8, capped at 7 by the draw. **Higher-numbered
slots hold stronger creatures, and HP rises with the slot too**, because the same
`slot>>1` is an operand of the HP expression. That is the mechanism the overworld lacked
and the reason "area can raise HP" looked true from one sector: in a dungeon, strength is
a function of slot, not of place.

### The HP formula, and the third-party table

```
HP = ((2*type + (slot>>1)) * 8) | 0x10
```

— `L_001706..L_001714`: `add %al,%al` for `2*type`, `adc 0x1b7(%di),%al` to add the slot
term, three more `add %al,%al` for `*8`, then `or $0x10,%al`. The carry out of the `adc`
was 0 in all 54 observations, so its contribution is untested rather than proven absent.

Taking each type at its cheapest legal slot and at slot 31 gives the full envelope, and it
reproduces the third-party dungeon table **exactly, one lower, on both bounds and with
the same pairing**:

| type | our min | third-party min `17+32k` | Δ | our max | third-party max `153+32*(k//2)` | Δ |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 16 | 17 | −1 | 152 | 153 | −1 |
| 2 | 48 | 49 | −1 | 152 | 153 | −1 |
| 3 | 80 | 81 | −1 | 184 | 185 | −1 |
| 4 | 112 | 113 | −1 | 184 | 185 | −1 |
| 5 | 144 | 145 | −1 | 216 | 217 | −1 |
| 6 | 176 | 177 | −1 | 216 | 217 | −1 |
| 7 | 208 | 209 | −1 | 248 | 249 | −1 |

The previous status of that fit was *"measured against third-party numbers, which is not
the same as measured against the binary"* (`docs/bestiary.md` § 6). It now is: the shape,
the step of 32, the min/max pairing and the type ordering all fall out of the instruction
sequence, and the only disagreement is a uniform −1. **Grade: measured, with a named
systematic offset against the fan table.** The offset is not explained — the likeliest
reading is that the fan table quotes hits-to-kill plus one, but that is a hypothesis and
it is recorded as one.

### Where dungeon objects live, and what is still missing

The spawner stamps the type into the sector buffer at `L_00171b..L_001729`:
`mov 0x8,%bx ; mov (%bx,%si),%al ; or 0x197(%di),%al ; mov %al,(%bx,%si)` — a read-modify-
write that **ORs** the type into a cell, so a cell can carry a wall flag and a creature at
once. `word[DS:08]` is `0x183D` in the capture and `word[DS:472]` is `0x1800`.

The population claim holds: in `dungeon-35-live` the 4 KB buffer at `DS:1800` holds
exactly **27** bytes in `0x01..0x07`, and their per-value histogram
`{1:6, 2:10, 3:2, 4:4, 5:1, 6:4}` is **identical** to the per-type histogram of the 27
monster records. So the objects are in the grid, one stamp per monster.

**What is not established is the coordinate transform.** The monster record's `x` and `y`
are odd values in 1..15 — `and $0x7` then `add %al,%al` then `or $0x1` — a 16-wide crawl
space, while the stamps sit at rows 8..61 and columns 19..63 of a 64-wide buffer. No
linear map of the tested shapes (`y*16+x`, `y*64+x`, `x*16+y`, with or without a page
offset from `DS:25`) reproduces the cell from the record coordinates: the best attempt
matched 2 of 27. The stamp address comes from `%si`, which is set by a routine the tree
calls `L_00164c` — a five-byte fragment (`nop ; mov 0x1b7(%di),%al`) that cannot be
computing it, so **the fragment boundary is wrong there and the real index computation is
outside the declared range**. That is the same class of defect as G22, and it is where the
dungeon geometry question picks up.

### Correction to § 2 of `docs/sector-rulesets.md`

That table lists the dungeon `kind` column under the same record layout as the
overworld. The layout is the same but the **meaning** is not: `0x1B7` is `slot>>1` in a
dungeon and a kind index on the overworld. Anyone re-deriving this should read this
section rather than infer from that table.

## 10. A false alarm against § 2, raised and retracted the same day (2026-10-23)

Graded **retracted**. § 2 stands. This section exists because the error was made, was
committed, was reported to the owner as a defect in the reconstruction, and is the most
expensive kind of mistake this project can make: **a confident wrong finding that stops
correct work.**

### What was claimed

That `monxNN`'s six arrays were not at the offsets § 2 gives, that the file held
`hp = 0` in all 32 slots and `x = 112` (off a 64-wide grid), that the region was "one
128-byte block stored twice with a one-byte skew", and that `engine/src/game/monsters.c`
therefore found zero creatures in every sector. The CP5 creature stamp was compiled out
behind a `U2_MONSTERS_LAYOUT_PROVEN` guard on the strength of it, `docs/needs-gameplay.md`
gained item **G26** to re-derive a layout that had never been wrong, and W2, W4, W5 and W6
were declared blocked.

### What was actually wrong

**The analysis script, in one place.** The evidence was gathered as
`blk = b[128:128+256]` — slicing the file from offset 128 — because § 2's *table* lists
`128..159` as a row and the prose was read as "the arrays start at 128". § 2 says, in the
first row of that same table, `0..31 | x | DS:0x137+i`. The arrays start at **0**.

Sliced from 128, the six fields land four rows out of position, which is exactly why
`hp` and `type` read as all zeros (they were reading the file's zero tail) and why `kind`
looked like `x` shifted by one. Every "structural impossibility" in the original version
of this section was an artefact of that one constant.

The engine was never wrong. `world.c` reads `monxNN` from offset 0 with no seek, and
`monsters.c` uses `MON_OFF_X 0`, `Y 32`, `HP 64`, `TYPE 96`, `KIND 128`, `ROLE 160` —
which is § 2, correctly transcribed.

### How it should have been caught, and the two rules that came out of it

**1. Check what the code reads before declaring the code wrong.** The claim was "the
engine finds zero creatures in every sector", and that is a one-line check against
`world.c`'s `fread`, which was sitting in the same working tree. Instead the claim was
built entirely from a hand-written script whose slicing was itself an unverified
transcription of the prose. A derived number is only as good as the derivation, and a
script is a derivation — it does not get a pass because it produced a table.

**2. A negative result from a new script needs a positive control run the same way.**
The script never printed anything known to be true. Re-running it with the correct slice
reproduced § 2 exactly on the first try: against a live capture of sector 31 with `G23MAX`
loaded, `mon_hp`, `mon_type` and `mon_role` each match `monx31` at **32/32**, `mon_kind`
at 30/32, and `x`/`y` under-match only because creatures walk after spawn. That is a
fourth independent capture agreeing with § 2, and it is what should have been the first
thing run.

### Corrected on the way through

The original version of this section also asserted that the live reads were suspect,
because an earlier session had seen all twelve sector-31 creatures at `x = 25, y = 29`.
That read was taken before the world had loaded, not from a wrong offset. The live table
is scattered and sane: player at `(50, 21)`, `mon_x` spanning `0x06..0x3d`, `mon_y`
`0x06..0x38`, `mon_hp` a mix of `0xff` and smaller values. The DS offsets in § 2 are
confirmed as written.

### Status

`U2_MONSTERS_LAYOUT_PROVEN` is gone; the stamp is live. `docs/needs-gameplay.md` G26 is
closed as not-a-question. The engine now reports `sector 31 via mapx, tiles from cache,
32 creatures` and a scripted attack against a real creature from `monx31` lands:
`CMD: ATTACK--EAST--HIT!!!`. (The note used to read `via monx`, which was not a claim about
the creature file: it printed one shared "last load" prefix field, and monsters always load
last. That field is per-kind now — see `docs/phase4-state.md` § OPEN DEFECT, closed
2026-10-29 — so the note names the sector file it actually means.)

## 11. The overworld spawner is `0C9A..0E00` — found by reading, not sampling (2026-10-23)

Graded **observed** for the instruction sequence (bytes are CP0-proven) and
**inferred** for the routine's name and for `DS:60`'s role. Opened because CP5 needed
the creature *AI* and went looking for the code that writes `mon_x`/`mon_y` — and found
this instead, which is a placement dispatcher, not a decision maker. Recording the
negative is the point: it moves the AI hunt off this region.

### Why this was invisible before

The bulk clean wave cut the region into 4-to-8 byte fragments (`0C9A`, `0C9B`, `0C9C`,
`0CA2`, `0CAA`, `0CAD`, `0CB0`, `0CB9`, `0CBD`, `0CC0`, `0CD2`, `0CD4`, `0CD9`, `0CDC`,
`0CEE`, `0D06`, `0D09`, ... `0D4F`), each headered `behavior not yet reviewed`, each
carrying a `jmp` into a neighbour. Read one at a time they are meaningless. Concatenated
in address order they are one routine. **This is the concrete cost of fragment-scoped
reading**, and it is the same failure mode that produced the truncated read behind the
retracted § 10.

### The routine, reassembled

Runtime address = file address + `0x200`.

| runtime | instruction | reading |
| --- | --- | --- |
| `0E9C` | `mov bh,0` / `mov bl,0x1f` / `mov di,bx` | `DI = 31`. **The operand is the immediate `0x1f`, not a read of `DS:1F`** — see the trap below |
| `0EA2` | `mov al,[di+0x197]` / `or al,al` / `je 0EB0` | read `mon_type[slot]`; a zero means the slot is free |
| `0EAA` | `dec di` / `jne 0EA2` | walk slots **31 down to 1** looking for a free one |
| `0EB0` | `mov bx,di` / `mov [0x1f],bl` | **`DS:1F` = the free slot just found** — this is the writer of the slot cursor |
| `0EB9` | `cmp al,0x3f` / `jb 0EC0` | x bounds check: store only if `al < 64`, else exit via `0DAF` |
| `0EC0` | `mov bl,[0x1f]` / `mov di,bx` / `mov [di+0x137],al` / `mov [0x23],al` | **store `mon_x[slot]`**, mirror to staging `DS:23` |
| `0CD2` | `js 0ED9` | negative y exits via `0DAF` |
| `0CD4` | `and al,0x3f` | **wrap y into 0..63** — the sector is a torus, and this is where that is enforced |
| `0EDC` | `mov [di+0x157],al` / `mov [0x24],al` | **store `mon_y[slot]`**, mirror to `DS:24` |
| `0EEE` | `mov [di+0x1b7],al` | **store `mon_kind[slot]`** |
| `0EFA` | `incb [0x60]` | bump a per-spawn counter |
| `0F01..0F21` | `clc` / `rcr al,1` / `jae …` chain | **bit-test dispatch on `DS:60`** — each bit selects a species branch |
| `0F4F` | `mov [di+0x197],0x30` / `mov [di+0x177],0x10` | the Orc branch: `type=0x30`, **`hp=16`** |
| `0DAF` | (common exit) | every bounds failure and every species branch ends here |

The Orc HP of `0x10` = 16 is an independent cross-check on the whole reading: the
bestiary lists Orc HP as 16 from a third-party source, and this branch writes 16. Two
different kinds of evidence agreeing on a number that neither was derived from.

### The trap that had to be stepped in first

`0E9E` is `mov $0x1f,%bl`. Read as `mov 0x1f,%bl` it becomes "load the slot cursor", and
the whole cluster then looks like a per-entity accessor API driven from outside — which is
exactly what I wrote in the same breath as calling it "the AI's accessors". It is an
immediate. The cluster does not *read* a slot cursor on entry; it **searches for a free
slot and then writes the cursor**, which makes it a spawner. One `$` character, and the
search target changes completely.

### What this rules out

* **This is not the creature movement AI.** Nothing here computes a new coordinate; every
  store takes `AL` from a caller or a species branch. The AI is whoever feeds it.
* **`asm/152E` is not an overworld pursuit primitive.** Its only caller is
  `asm/1151_dungeon_turn`. It was flagged as "a vector from a creature to the player"
  and that is true, but it is dungeon code. The lead was wrong.
* **No `call` reaches this cluster** — `grep 'call L_000ec0'` and friends return nothing,
  because entry is by fall-through from `0E9A` (itself a split artefact: `jmp 0xe9a`).
  A caller search that only looks for `call` will conclude the region is dead code. It is
  not; it is spawn traffic, and § 2's stamps come from here.

### Consequence for the breakpoint experiment

A `MEMORY_WRITE` breakpoint on a **fixed** slot offset was the wrong instrument, which
explains a result that looked like tooling failure: during a live session 16 creatures
moved, including slot 26 (`x` 48 → 52), and a write-breakpoint on `mon_x[26]` never
fired. If those writes came through `0EC0`, the address written is
`DS:137 + DS:1F` where `DS:1F` is whatever the free-slot scan returned — a moving target,
not slot 26. The correct instrument is an **execution breakpoint on `0EC0`/`0EDC`**
(runtime `0EC0` → linear `0x8BE0 + 0xEC0 - 0x200`), reading `DS:1F` and the return address
off the stack at each hit.

That also demystifies the "11-cell jump" flagged as a data-quality problem: slot 4 going
`(29,40) → (40,29)` between two snapshots is consistent with spawn traffic writing a
different slot's coordinates, not a creature teleporting. Still to confirm, but it has
moved from *suspect measurement* to *expected behaviour*.

### Still open, named so it does not get lost

* **Who calls the spawner, and how often.** No static caller is known. The `DS:60` bit
  chain implies it is invoked repeatedly with a counter, not once per sector load —
  § 2's captures show live tables differing from the authored file, which is consistent
  with ongoing spawn activity, but that is inference.
* **What `DS:60` actually is.** Read as "spawn counter" from `incb` plus the bit dispatch.
  Could equally be a bitmask of remaining spawns, or a seed. Not established.
* **Why the free-slot scan stops at 1.** `dec di ; jne` never examines slot 0, so slot 0
  cannot be allocated here — the same off-by-one already noted in
  `asm/23E6_find_creature_at_cursor.asm`. Two routines, same blind spot, which is
  probably deliberate in a game that uses slot 0 as "none".
* **The AI itself.** Still unfound. The search now has a positive shape to look for: code
  that reads a creature coordinate, adds a delta, and calls `0EC0`/`0EDC` — or writes
  `mon_x` directly without this cluster at all.

### Live check contradicts the semantic reading — corrected the same day

The bytes above are CP0-proven and were re-verified by disassembling the loaded image.
**The interpretation of what the bytes mean is a different grade, and it did not survive
one live session.** Recorded immediately, not filed for later.

Test: headless run, sector 71, execution breakpoint on the `mon_x` store entry (linear
`9360`, disassembly-verified as `mov BH,0 / mov BL,[DS:1F] / mov DI,BX /
mov [DI+0x137],AL`), then 40 turn-generating keys.

| observation | value | consequence |
| --- | --- | --- |
| turns occurred | player moved to `(51,26)`, food fell `153,148,89` | the session was live and processing turns |
| breakpoint hits on the `mon_x` store | **zero** | **overworld creature movement does not go through this code** |
| `DS:1F` after the session | **120** | cannot be a slot index — the table has 32 slots. `DS:1F` is a shared scratch byte with 20+ writers, and this cluster is one of its users, not its owner |
| `DS:60` after the session | **0** | the `incb [0x60]` path never ran, so "spawn counter" is unsupported by any observation |

So the claims to downgrade are: **"this is the overworld spawner"** (unproven — it is a
placement-shaped routine that did not run during ordinary play), **`DS:1F` = the spawner's
slot cursor** (contradicted), and **`DS:60` = a spawn counter** (unsupported). The free-slot
scan shape at `0E9C..0EB6` and the per-species HP branch at `0F4F` are still exactly what
the bytes say; what was wrong was promoting a plausible reading of them into a named
subsystem.

This is the third time in one day that a static or derived reading was promoted past its
evidence — the `monx` slice error, the castle movement metric, and this. The pattern is
identical each time: the local structure is real, and the naming outran it. The fix that
actually works is the one this project already has as a rule — **run the negative check
before naming**, here a single breakpoint on the address, which cost two minutes and
returned "never executed".

### The address model, which was wrong in the notes and is worth having

**Linear address = file offset + `0x17D0`.** Verified by disassembly: file `0x0CC0` →
linear `9360`, which decodes as exactly `asm/0CC0_sub_0cc0.asm`'s body. The CFG note's
"segment `017D`" is that same base expressed as a segment, and `read_disassembly` with
segment `0x249` (585) offset `0` lands on the same linear address.

Two traps this walked into, both worth not repeating: `0x17D:0xEC0` is **string data**
(`ARMS P…`), so the intuitive `runtime = file + 0x200` from the asm label numbering is
wrong for address arithmetic — the `L_000ec0` label numbering and the linear address
space are not the same thing. And `search_memory` for a hand-assembled multi-instruction
pattern returned nothing while a 6-byte fragment matched eight times, because the
accessor idiom `mov bl,[0x1f] / mov di,bx` is **widespread** — which is itself the hint
that `DS:1F` is general scratch, not a dedicated cursor.

## 12. The creature AI region — `0x2122..0x2260`, found by following a third-party claim into the bytes

Graded **observed** for the instruction sequence (CP0-proven bytes, read in address order
the way § 11 should have been) and **inferred** for the behaviour names below. Opened by
`docs/u2-game-meta-analysis.md`, a third-party synthesis, which asserted:

> When an overworld monster's Hit Points drop to 15 HP or lower, its AI behavior toggles
> from aggression to retreat mode. The entity moves away from the player and regenerates
> Hit Points each turn. Once its HP recovers to 16, aggressive pursuit resumes.

A falsifiable claim with two hard numbers in it. The bytes say:

```
2122  mov  al,[di+0x177]      ; mon_hp[slot]
2126  cmp  al,0x10            ; compare against 16, exactly as claimed
2128  jae  232e               ; HP >= 16 -> go straight to the move
212a  incb [di+0x177]         ; HP < 16 -> REGENERATE, +1 per turn, as claimed
212e  clc / mov al,[di+0x137] / adc al,[0x19] / and al,0x3f / mov [0x23],al
      clc / mov al,[di+0x157] / adc al,[0x1a] / and al,0x3f / mov [0x24],al
      call L_0052b3           ; candidate: passability / occupancy probe
```

`DS:19` and `DS:1A` are the movement delta, added to the creature's cell and wrapped by the
same `and 0x3f` torus mask seen in § 11. **`asm/152E` sets `DS:19/1A` to `player - creature`
— a vector toward the player — and `asm/0B0F` negates them in place (`stc; cmc; sbb` from
zero). Toward is pursuit; negated is retreat.** That closes the loop § 11 left open about
what writes those bytes, and it was sitting in a routine I had dismissed as dungeon-only.

### The attack trigger, which answers a question watching could not settle

```
2201  mov al,[0x0] / cmp al,[0x23] / jne 2416   ; is the new x the PLAYER's x?
220a  mov al,[0x1] / cmp al,[0x24] / jne 2416   ; and the new y the player's y?
2213  jmp 238b                                  ; both equal -> the contact path
```

A creature finishing its step **on the player's cell** branches to `238b`, which tests
`DS:4A` and jumps to `0x1240`. Contact is the trigger: monsters attack by walking onto you,
not by a separate ranged decision. Consistent with every live capture, where damage only
ever arrived after a creature closed the last cell, and with the measured rarity of hits
(8 HP changes in 2999 turns).

### The move-rate gate, which explains the breakpoint that would not fire

`2199` writes `DS:1F` from `AL`; `21a6..21d8` dispatch on `mon_type` and compare `DS:1F`
against a ladder — `0x5c`, `0x10`, `4`, `0`, `0x14`, `0x70` — where `0x23fc` returns `0xFF`
(refuse) and `0x23f7` returns `0` (accept).

**This is why § 11's breakpoint returned zero hits while creatures moved.** `DS:1F` read 120
(`0x78`) in that session; `0x78` matches none of the ladder, so the routine exited at `23fc`
with a refusal and never reached the coordinate stores. So the sentence immediately above
this section — "`DS:1F` is general scratch, not a dedicated cursor" — is itself now too
hasty in one direction and wrong in the other: it is a per-creature value feeding a rate
gate. Two confident claims about `DS:1F` in one day, both wrong.

### Also present

* `21e2..21f0` reject a move onto `(0,0)` on either axis, and `21e2` branches on `DS:4A`
  — the same byte `cmd_A` gates on, so site kind affects creature movement too.
* `2216` saves pre-move coordinates into staging `DS:27/28`; `2233` commits them back into
  `mon_x/mon_y` and writes `mon_kind` into the 6-byte-stride table. **§ 11's "placement
  API" is the commit half of this move routine**, not a separate spawner — which is exactly
  why a breakpoint on `0CC0` saw nothing while this region did the work.
* `21a6` special-cases `mon_type` `0x2c` (Sea Monster) and `0x48` (Frigate) and sends
  anything `>= 0x78` elsewhere. Species-specific movement, which the diagonal-versus-axis
  split in the g28 capture independently predicted.

### Method note, because it is the actual lesson

§ 11 was a static reading promoted past its evidence and killed by one breakpoint. This is
the same kind of static reading — but it was **aimed by a falsifiable third-party claim that
named two numbers**, and it independently explains three observations that had no
explanation: the `DS:19/1A` writer, the silent breakpoint, and the 7% move rate. Third-party
data is a hypothesis the bytes get to veto. Here the bytes agreed, and what differed from
§ 11 is that the hypothesis named something checkable.

Still to confirm, none of it load-bearing for the retreat threshold or the contact trigger:
that `L_0052b3` is a passability/occupancy probe; that `238b -> 0x1240` is the attack and
not another contact event; and the exact semantics of the `DS:1F` ladder.

**All three are answered in § 13 (2026-10-27), and two of them were wrong.** `L_0052b3` is
the map-tile probe, `L_001240` is a Minax proximity check and not the attack, and `DS:1F`
is the candidate tile byte feeding a per-species passability rule, not a rate ladder. The
sentence above is left as written because the reasoning in § 12 was sound at the time; the
correction lives in § 13 rather than being edited backwards.

The ladder itself is now fully decoded (2026-10-28): the tile→verdict table, the two
water-only species clauses, the `mon_type >= 0x78` clause, the `DS:26A` total freeze and the
`DS:4A` edge guard are in `docs/contract-creature-tick.md` § "The step gate", and the routine
is named on disk as `asm/2199_creature_step_gate.asm`.

## 13. The `0x1240` blocker was an address-space slip, and what the bytes actually say (2026-10-27)

`docs/handoff-cp5-2026-10-24.md` § 2 named one thing to do first: "identify what
`238b → 0x1240` is", noting that no `asm/` file begins in `0x1230..0x1250` and that the
target must therefore sit inside another fragment's span. It does not. **The number was a
label, not an address.**

The tree has two numbering spaces and they differ by exactly `0x200`:

| space | where it appears | example |
| --- | --- | --- |
| file / filename | the `asm/` filename and the `# func` range | `asm/1040_sub_1040.asm`, `# func 1040..1047` |
| label | the `L_%06x` anchors in the body, and every jump target printed by a disassembler over the exe | `L_001240` |

`label = file + 0x200`, which is the same `FILE_BIAS = 0x200` `engine/tools/extract/`
already uses. So `jmp L_001240` means **file `0x1040`**, and `asm/1040_sub_1040.asm` has
existed all along. § 12 quoted instruction addresses in file space (`2122`, `212A`, `2201`
all match filenames exactly) and jump *targets* in label space (`L_001240`, `L_000ca1`),
because the targets came from disassembly output. Mixing the two is what made the target
look like it lived in a gap.

The same slip is already baked into a header: `asm/0AD3_combat_special_attack_dispatch.asm`
says "the `cmp $0x40` at file 0x1244 in `asm/0FCD_combat_thief_steal`". `L_001244` is file
`0x1044`, which is in `asm/1040`, not in `asm/0FCD`. `asm/0FCD`'s own `cmp $0x40` is at
`L_0011d1` and compares a **random byte** — which is what CP5 ticket C6 already concluded.
So that header's "0x40 is the EMPTY sentinel" reading is wrong twice over, and the empty
sentinel is not `0x40` at all: the slot loop head at `asm/0A74` tests `or %al,%al; je
next_slot` on `mon_type`, so **an empty slot is type `0x00`**.

### What the contact path actually is

Read as file addresses, the chain § 12 left open is:

```
2201  al := [DS:00]; cmp al,[DS:23]; jne 2216      ; candidate x == player x?
220a  al := [DS:01]; cmp al,[DS:24]; jne 2216      ; and candidate y == player y?
2213  jmp 218b                                     ; contact -> 218b
2216  ...                                          ; NOT contact -> commit the move
218b  al := [DS:4A]; cmp al,0; je 2195
2192  jmp L_001240   -> asm/1040                   ; sector second digit non-zero
2195  jmp L_000ca1   -> asm/0AA1                   ; else: next slot
```

and `asm/1040` is:

```
1040  al := [di+0x197]                             ; mon_type
1044  cmp al,0x40 ; jne <next slot>
1048  al := [di+0x137]; sbb al,[DS:00]; call abs(50EF); cmp al,4; jae <next slot>
1059  al := [di+0x157]; sbb al,[DS:01]; call abs(50EF); cmp al,4; jae <next slot>
106a  call L_00118e  -> asm/0F8E_combat_minax_attack
```

**It is not the general attack. It is a Minax check.** `mon_type == 0x40`, within four
cells on both axes, and the callee is the routine already named from its own string,
`MINAX CRIES: DIE FOOL!`, which subtracts 1000 HP and jumps to the death handler on
borrow. The type and the art agree without any extra hypothesis: the art index is
`type >> 2` (W2's stamp, `docs/art-index.md`), and `0x40 >> 2 == 0x10`, which is the tile
the bestiary note in `asm/0F8E` identifies as Minax by bitmap identity. Third-party data
again, and again the bytes were the ones that got the veto.

**Contact does not produce an attack at all.** When a creature's candidate cell is the
player's cell, the routine skips the commit and goes to the next slot. The creature
refuses to walk onto the player; it does not strike for standing there. That is the same
rule W4 found from the other side — a creature blocks the tile — and it means the engine's
occupancy test and the original's contact test are two views of one rule.

### Where the creature's attack really lives

The outer slot loop is `asm/0A6D..0AB5`, and per slot it does:

```
0A6D  di := 31                                     ; slot cursor, counts DOWN to 0
0A74  al := mon_type[di]; if al == 0 -> next slot  ; empty
0A7C  filters on [DS:13] (the vehicle byte) and [DS:26] (the torch counter):
      on a plane, sea monsters, frigates and sleep-casters are skipped entirely
0AA7  call L_005136  -> asm/4F36: DS:19 := player_x - mon_x ; DS:1A := player_y - mon_y
0AAA  if abs(DS:19) >= 3 -> next ; if abs(DS:1A) >= 3 -> next
0AC4  if rand() >= 0x20 -> next                    ; 32/256 = 12.5% per creature turn
0AD3  dispatch on mon_type: 0x34 legs, 0x38 arms, 0xF8 missile, 0x3C sleep
0AA1  dec di ; jne 0A74
```

So the attack is **proximity plus a 12.5% roll, not contact**, and the proximity test is
the same `DS:19/1A` pair the move routine uses as its pursuit vector — `asm/4F36` computes
it as a plain `sub`, `asm/152E` computes the same delta with the torus wrap through
`asm/50D8`. `asm/0AAA` was already named and already documented as this gate, with the
`< 3` thresholds and the `0x20` roll proven and its provenance recorded as a live castle
fight; what was missing was that its caller is the per-slot loop and not the contact path.

The move routine (`asm/2122`, reached by `jmp` from `asm/0B36`, `asm/0B4A` and `asm/107F`)
is a **separate pass** over the same slots, and `DS:1F` is not a rate ladder:

* `2122` regenerates `+1` HP per turn below 16, as § 12 found.
* `212e` builds a candidate cell `(mon_x + DS:19, mon_y + DS:1A) & 0x3f` — the **diagonal**
  step — probes the tile with `L_0052b3` (`asm/50B3_map_tile_ptr`), and asks `asm/2199`.
* `asm/2199` stores the tile byte into `DS:1F` and **refuses outright unless `DS:26A == 0`**
  — the fourth unnamed counter, which is therefore a global creature freeze, not a
  cosmetic tick.
* On refusal it retries `(mon_x, mon_y + dy)` and then `(mon_x + dx, mon_y)`. Three
  candidates, diagonal first. That is where the measured 2-cell steps come from: a
  diagonal is two cells of Manhattan distance in one turn.
* The accept test is a **per-species passability rule on the tile byte**, not a rate:
  sea monster (`0x2c`) and frigate (`0x48`) accept only tile `0x00`; everything else
  refuses `0x10` (mountain), `0x04` and `0x00`, accepts anything below `0x14`, and accepts
  `0x5c` and `0x70` specially. Tile `0x5c` is `92`, and `92` is one of the four town
  second-digits in `docs/sector-rulesets.md` § 1 — worth a look, but not claimed here.
* `218b` is reached both by fallthrough (all three candidates refused) and by the contact
  jump, which is why the contact path leads to a Minax check and a `next slot`: a creature
  that cannot move and a creature that would stand on the player take the same exit.

### What this changes for CP5

* **W6 is no longer blocked.** The trigger is `|dx| < 3 && |dy| < 3` and a 12.5% roll per
  creature turn, dispatching the four specials that `engine/src/game/combat.c` already
  implements (C1/C2/C4/C6). No new mechanics, and no inferred "strike on adjacency" rule.
* **W6b's 7% is explained rather than needing to be encoded.** Most turns are no-ops
  because the candidate tile fails the species passability test or `DS:26A != 0`. The
  engine should implement the rule, not the measured percentage.
* **Melee damage is still not located.** The dispatch covers the four specials; the types
  `asm/0AD3` calls plain melee attackers (`0x30`, `0xF4`, `0xFC`) have no damage site in
  this region. The measured `9999 -> 9796` is unexplained by anything in this section, and
  saying "creatures hurt you" is not a substitute for finding where.

## 14. `mon_role` is a behaviour selector, and the alarm is eight slots (2026-10-27)

§ 13 left the per-slot loop described as "pursuit, gate, special, move for every
non-empty slot". That is the role-1 path only, and the gate that decides which path a
slot takes is the array this project has been calling `mon_role` since W1 without ever
saying what the values mean. They mean this, and it is all in one fragment chain at
`asm/101C..0x1082`:

```
101C  if mon_type[di] == 0 -> next slot
102B  al := mon_role[di] ; if al == 0 -> next slot
1033  if sign(al) -> next slot                 ; bit 7 set: excluded
1035  if al >= 3  -> L_001286 (asm/1086, the wander)
1039  if al == 2  -> L_00127b: call asm/4F11 ; jmp asm/2122   ; move, nothing else
103D  else        -> L_001282: jmp L_000ca7 (asm/0AA7)        ; the full pass
```

| role | what the slot does | what the sector files actually ship |
| --- | --- | --- |
| `0x00` | nothing at all | 6–11 slots in a town, and **every slot** in `monx00` |
| `0x01` | the full pass: proximity gate, special, unit step, retreat, move | 7–11 slots |
| `0x02` | the unit step toward the player and the move routine. No gate, no special, **no retreat test** | 4–6 slots |
| `0x03` | `asm/1086_creature_wander_step`, a wander off a per-slot step vector at `DS:1F7`/`DS:217` — **vector semantics measured 2026-10-26; the writer and the move site still not found** | 10–24 slots, usually the majority (census of all 33 shipped `monx`: 179 role-3 slots) |
| `0x81..0x85` | excluded by the `js` at `asm/1033`; these are the scripted townsfolk `asm/3C2E_cmd_O` enumerates one by one | exactly 4–5 slots, always the same low values |

### Which sweep the gate belongs to — a correction (2026-10-28)

Everything above is true of the **town** sweep only. `asm/0A38`, the tail every completed
command falls through to, dispatches on the sector's second decimal digit:

| digit | where the tail goes | what gates it |
| --- | --- | --- |
| `0` — the world map | the sweep at `asm/0A6D` | **nothing**: `asm/0A6D..0AB5` reads `mon_role` nowhere in its span |
| `1`, `2`, `3` — towns and interiors | runtime `1213`/`1214`/`1215`, i.e. the `asm/1013/1015` chain above | `mon_role` |
| `4`, `5` — dungeons | runtime `135E`/`135F`, inside `asm/1151`'s span | the dungeon creature turn. **Read in full 2026-10-29 for G54** — this row said "unread, and parked per `AGENTS.md` § 3" for ten days after that, which is why `asm/1151` was renamed from `dungeon_darkness` to `dungeon_turn`: the darkness tail is only the last third |
| `6..9` | falls through to `asm/0A6D` | nothing, as digit 0 |

On the ungated sweep the ring at `asm/0AAA`/`0AB6`/`0AC0` still applies, but its escape is a
jump to runtime `0AFC` — the unit step and the move — not to the loop-back at `0AA1`. So
outside three cells a world-map creature skips the special and still walks; the move itself
is unconditional. The gates that really exist there are the `DS:13`/`DS:26` filters, the ring
for specials, and the species/tile ladder inside the move routine.

The consequence is what the owner caught by playing: a wandering spawn on the world map lands
in a slot whose `mon_role` is 0 — `monx20` ships 0 in all thirty-two slots and the spawner
writes no role — and the engine applied the town gate to the world-map sweep, so every
overworld creature stood frozen while the player walked past it. `gated_sweep` in
`engine/src/game/ai.c` is the dispatch, and `engine/tests/ai_check.c` pins both halves.

Three things follow, and all three were previously assumed the other way.

**Role 2 is the alerted state and it is not a caster.** It walks in and does not cast,
and because `asm/107B` jumps straight to the move routine it also skips the retreat test
at `asm/0B07`. An alerted creature at 1 HP keeps coming. The guard that chases you is
therefore not the guard that paralyses you, and a single "hostile" boolean cannot express
that.

**The stillness in the 7% measurement was mostly the role byte, not the tile rule.**
§ 13 attributed it to tile refusals and the `DS:26A` freeze. Both are real, but in
`monx21` twenty-four of thirty-two slots are role 3 and four are `0x81..0x84`, so before
any tile is consulted, twenty-eight slots have already been told to stand still. The
engine now reproduces `0`, `1` and `2` and deliberately does **nothing** for role 3:
`asm/1086` is unread, and inventing a gait for the majority of the population is the
exact failure mode AGENTS.md § 5 warns about. Decoding `asm/1086` is the open item, and
it is the reason the engine currently under-moves rather than over-moves.

### The alarm, which was W5 and is now bytes

`asm/113A`, reached from `asm/2BE9` and from `asm/3F5D_steal_attempt`:

```
113a  if [DS:4A] == 0 -> ret              ; the world map has no guards
1142  di := 7 ; al := 1
114a  mon_role[di] := al ; dec di ; jns 114a
```

Eight slots, fixed indices, one value. And `asm/2BE9` is three bytes — `call L_00133a`
— sitting immediately before the `--HIT!!!` printer at `asm/2BEC`, which is why a landed
blow alerts the sector and a missed swing alerts nothing. That is § 16's live measurement
of 2026-10-23 reproduced from the static tree eight days later, with the mechanism: the
towns ship `0x00` in exactly those slots so this write has something to switch on.

One arm is not reproduced. The loop falls through into `asm/1151`, which writes the same
`1` into `mon_role[DS:20]`, and `DS:20` is the damage byte on the attack path and a poll
counter elsewhere — so which slot that names here is not established, and writing a slot
on an unproven index is worse than not writing it. Recorded in
`engine/src/game/ai.c:u2_ai_alarm`.

## 15. Overworld wandering spawn — identification in progress (2026-10-27)

Opened because the owner walked the starting map in the engine and met nothing. That is
correct behaviour for the data and a gap in the engine at the same time: `monx20[64:96]`
is all zeros, so sector 20 has **no placed monsters** and everything in it must arrive as
a wandering spawn, which `u2_monsters_spawn()` does not implement — it only `memcpy`s the
five arrays out of `monxNN`.

**What is now identified, by bytes rather than by guess:**

- The per-kind arms are a `cmp/je` chain on **`[DI+0x1B7]`**, the kind selector.
  `asm/0D44_sub_0d44.asm` is `mov 0x1b7(%di),%al; cmp $0x8,%al; je <orc arm>`, so kind 8
  is the Orc arm. Note § 9's finding that in a *dungeon* `[DI+0x1B7]` is the slot number,
  not a kind — the same field carries different meanings per sector kind, so any engine
  model that reads it one way everywhere will be wrong in one of the two.
- The dispatch above the arms is a **binary search, not a linear chain**: `0D06..0D43` are
  three-to-five-byte `jae`/`jmp` stubs fanning out to arm entries at file `0F44`, `0F64`,
  `0F84`, `0FA4`, `0FC4`, `0FD1`, `0FD4`, `1050`. `asm/0D21_sub_0d21.asm` is a separate
  four-way `rcr`/carry probe on `AL` with five far targets, semantics still unknown.
- Every arm ends in the shared tail `asm/0BAF_monster_hp_epilogue.asm`, which is
  `call L_005cb1` and nothing else. **What `0x5CB1` does is still unknown**, and it is the
  obvious candidate for "stamp the creature into the sector grid", because the arms
  themselves only write type and HP into the slot. It is called from at least six other
  places (`38B0_cmd_L`, `0864`, `08E6`, `268C`, `26CC`), so it is shared machinery, not a
  spawn-specific helper.

**What is NOT identified, and is the actual blocker:**

1. **The trigger.** Nothing calls the dispatch chain by label, so it is reached by
   fallthrough or a jump table. Until the caller is found there is no evidence for *when*
   a wandering spawn happens — per step, per sector entry, on a random roll, or on the
   status tick. Implementing a plausible rule without this is exactly the inference the
   project keeps getting punished for, so the engine gets no spawn logic until it is found.
2. **The kind source.** What writes `[DI+0x1B7]` before the chain runs.
3. **`L_005cb1`.** — **answered 2026-10-28, see § 15.2.** It is a viewport-origin setter that
   branches on `DS:12 == 0xFF` (world map vs interior), not spawn machinery. Why the spawn
   arms all end by calling it is a separate question and is not answered here.

**How to close it, cheapest first.** A capture, not a read: break at runtime `0x0D44` in a
headless sector-20 session, dump `DI`, `[DI+0x1B7]`, the slot index and the CS:IP return
address, and let the run walk. The return address names the trigger directly and settles
(1) and (2) in one shot. `tools/bp_read.py` stops the CPU at a runtime address and dumps
memory there, which is the existing tool for this — do not hand-roll a new one.

### 15.1 Capture result — the dispatch IS reached while walking (2026-10-27)

Headless sector-20 session, `tools/bp_read.py --ip 0D44 --cs 17D --dump 137:160`, owner's
`G23MAX` at (20,20), arrow keys patched.

- **13 steps: no hit.** Monster type slots all zero. So a spawn is not per-step.
- **~100 further steps: `STOP at 017D:0D44`.** The dispatch is reached from ordinary
  walking, which is the question § 15 left open. The trigger is in the walk/turn path, not
  at sector entry.
- At the stop, `DS:137..1D6` was populated: the type region held `34 30 FC 30` (Daemon,
  Orc, Thief — exactly the kinds the named arms assign), the HP region held `40 10 20 10`,
  and the array after type held `0C 0C 0C 08 0C`. So slots were live at the moment of the
  break, and the arms are doing what their names say.
- Reading `DS:197` again after the run resumed showed all zeros. Do not read that as
  "the monsters vanished": `bp_read` continues the target by default, so the second read is
  a different game state, not a later observation of the same one.

**Two hazards hit, both worth recording because they cost the run:**

1. `bp_read --selftest` **poisons the run**. Spice86 has no command to remove a breakpoint
   (monitor list: breakCycles/breakCsIp/breakStop/callStack/peekRet/state/ramx/dumpAll),
   so the trap armed by the selftest keeps firing on the live idle address. The tool says
   this in its own output; the run must be restarted after a selftest, and I armed the real
   capture on a poisoned run before noticing.
2. `--dump` takes `OFF:LEN` with **LEN in decimal**. `--dump 137:A0` raises
   `ValueError: invalid literal for int() with base 10`. It fails immediately and loudly,
   which is the good shape, but it wasted a full arm/walk cycle.

**Still not captured: the return address**, which is the actual prize — it names the
trigger outright. `peekRet` and `callStack` are **GDB monitor** commands on the RSP port
(20190), not HTTP API paths; `curl /api/peekRet` returns an empty body, which is not an
error and is easy to misread as "no data". Next attempt: talk RSP to the gdb port and send
`monitor peekRet` while the target is stopped at 0x0D44, or extend `bp_read.py` to do it —
extending the existing tool is the right move, not writing a second one.

## 15.2 The caller is not a call: the spawner is the tail of the per-command creature sweep (2026-10-28)

Item 1 of `docs/handoff-2026-10-27-night.md`. `tools/bp_read.py` was extended rather than
duplicated — it now takes `--state`, `--ret`, `--ret-type`, `--stack N` and `--callstack`,
all spoken over RSP on the gdb port while the target is stopped. The prize turned out to be
a **null result, and the null result is the answer**: there is no return address, because
nothing calls this code.

### The stop, and why the return address does not exist

Armed at runtime `0x0CEE` (the kind store, which every spawn passes through and the Orc arm
at `0x0D44` does not), owner's `G23MAX`, sector 20, arrow keys patched:

```
STOP at 017D:0CEE
state:  ... EAX=0x50C EBX=0x12 EDI=0x12 EBP=0x8 ESP=0x100 SS=0xBCE DS=0x8BE ...
peekRet: 017D:0000
stack check: SS=BCE SP=0100 -> word 0x0000, peekRet AGREES
stack at SS=BCE:SP=0100, +00..+26: all zero
stack below SP, 00C0..00FE: 34 nonzero bytes, stale values (0x5212, 0x0A0D, 0x171D, ...)
```

`SP` is exactly `0x100` at every stop and `SP..SP+0x40` is entirely zero, which is what an
*empty* call stack looks like: the two independent derivations agree on "no return address
is pending", and `peekRet` answering `017D:0000` is Spice86 reporting the same nothing, not
a failed read. The `callStack` monitor confirms the shape from the other side — it is
degenerate here, hundreds of repeated `unknown_017D_4FDF_067AF` frames under "From external
interrupt", because the program is not well-behaved enough to track.

So the plan in § 15.1 could not have worked, and would have kept failing quietly: a probe
whose answer key is a return address has no answer key on code reached by `jmp`.

### What reaches it instead, established statically

The chain is fall-through and one `jmp`, all of it in bytes:

| runtime | file | what it does |
| --- | --- | --- |
| `0A1F` | `0C1F` | `cmd_dispatch`: `sbb $0x41,%al; add %al,%al; mov %cs:0x730(%si),%bx; jmp *%bx` — the A–Z command jump table |
| `0A33` | `0C33` | `cmd_done`: every command handler returns here by `jmp`; prints CR via `char_print` and **falls through**, it does not `ret` |
| `0A38` | `0C38` | clears `DS:22/20/21`, then `mov 0x4a,%al; or %al,%al; je L_000c6d` — dispatches on the sector's low digit, see below |
| `0A6D` | `0C6D` | sweep head: `DI = 31` (immediate, the § 11 trap again) |
| `0A74` | `0C74` | `mov 0x197(%di),%al; or %al,%al; je L_000ca1` — free slot, skip it; live slot, run the AI |
| `0AA1` | `0CA1` | `dec %di; jne L_000c74` — next slot down |
| `0AA4` | `0CA4` | `jmp L_000e9c` — **the only branch in the binary into the spawner cluster**, reached when `DI` walks off the bottom |

`L_000e9c` is referenced nowhere else (`grep -rn L_000e9c asm/` returns that one line), and
the AI bodies return to the sweep rather than leaving it — `L_000ca1` is jumped to from
`asm/0BAB`, `asm/2195` and `asm/2272` too. So **every completed command, with `DS:4A == 0`,
ends with exactly one wandering-spawn attempt.** Not per step, not per sector entry, not per
clock tick.

Renamed in the same commit, filename and header only, labels untouched: `0A38_post_command_tail`,
`0A6D_monster_sweep_head`, `0A74_monster_sweep_slot_test`, `0AA1_monster_sweep_next`,
`0AA4_monster_spawn_entry`, `0CEE_spawn_tile_store` (renamed again 2026-10-28, same rule).

### Measurements that came with it

- **`DS:4A` is the sector's second decimal digit, and that turns "the gate" into a
  dispatch.** `docs/map-format.md` and `engine/src/core/state.h` record `DS:49/4A` as the two
  digits patched into the `MAPXFF` filename, and a live read in sector 20 gave `DS:49=0x02
  DS:4A=0x00` — 2 and 0, matching. So `or %al,%al; je L_000c6d` is not a flag test: digit 0
  takes the sweep, digits 1..5 branch away (`asm/0A4A..0A5F` jump to `L_001213`, `L_001214`,
  `L_001215`, `L_00135f`, `L_00135e`), and digits 6..9 fall through to the same sweep from the
  last compare. Sector 20 measured 0, which is why the sweep ran in every capture here. What
  the 1..5 arms do, and why a sector digit selects on them, is not established — and it is now
  the load-bearing unknown for the cadence, not the RNG.
- **Cadence is not the limiter, the free slot is.** After a long walk, all 32 `mon_type`
  slots were occupied (`30 FC 30 34 30 FC 30 F0 …`) and `DS:60` moved only 22 → 23 across
  80 arrow steps. The spawner opens by scanning `31..1` for a zero `mon_type` and exits via
  `L_000daf` when there is none, so an attempt on a full field is a no-op. That is also why
  the first stops came fast on a fresh sector and then stopped coming: `bp_read` armed at
  `0x0CEE` ran 190 s with no stop once the field was full.
- **`DS:60` is not a spawn counter.** § 11 read `incb [0x60]` here as one and § 15's live
  check then read 0 and downgraded it to unobserved; both were wrong in different
  directions. It is incremented here and **decremented at `asm/0DC4`, which is the shared
  reject tail the species arms jump to when their `cmp` on `[DI+0x1B7]` does not match**
  (`decb 0x60 ; jmp L_000daf`). So the increment is rolled back on a mismatched arm: `DS:60`
  is a rotating selector feeding the `clc; rcr al,1; jae` chain, not a count of anything.
  That closes the § 11 loose end and opens a different one, which is what the chain actually
  selects on.
- **The A–Z table is confirmed as a by-product.** Reading the table at file `0x930`
  (= runtime `0x730`, the `mov %cs:0x730(%si),%bx` displacement) gives 26 words whose 21st
  is `0x42F1` — `asm/42F1_cmd_U.asm`. Index 20 = 'U'. Independent confirmation that the
  table, the `+0x200` file/runtime mapping and the handler naming all line up.

### The selector, as far as the bytes allow (inferred, untested)

Reading the chain at `0D01..0D43` against the arms gives a model, and it is a prediction
rather than a finding:

```asm
0CFE: mov 0x60,%al
0D01: clc ; rcr $1,%al ; jae 0D09                    ; CF = bit0 of DS:60
0D06: jmp L_000f44                                   ; bit0 set -> Orc arm
0D09: clc ; rcr $1,%al ; jae 0D11                    ; tests original bit1
0D0E: jmp L_000f64
0D11: clc ; rcr $1,%al ; jae 0D19                    ; tests original bit2
      ...
```

`jae` is `jnc`, so each step branches when the bit it just rotated out is **clear**, and
falls through to an arm when it is **set**. The chain therefore walks the bits of `DS:60`
from bit0 upward and takes the arm belonging to the lowest set bit — and each arm opens
with `mov 0x1b7(%di),%al ; cmp $K,%al ; je <stamp> ; jmp L_000fc4`, where `L_000fc4` is the
`decb 0x60 ; jmp L_000daf` reject. So the counter picks **which species is allowed to spawn
this time**, the kind drawn from `L_005417` has to agree, and a disagreement rolls the counter
back and leaves the field untouched. That is a coherent reason for the measured rarity, and
it is exactly the kind of elegant-but-unverified story this project has been burned by, so it
stays labelled inferred until paired `(DS:60, [DI+0x1B7], arm taken)` samples either confirm
it or kill it.

### `L_005cb1`, which § 15 left open, and what `DS:12` really is (handoff item 6)

Chasing the return address meant stopping inside the spawner, and the epilogue every arm ends
in is `call L_005cb1` — so the same run that produced the null result also walked into the
callee. It is `asm/5AB1_view_origin_by_mode`, and it is not spawn machinery at all:

```
5AB1: cmpb $0xff,0x12 ; jne 5ABB        ; DS:12 == 0xFF -> world map
5AB8: jmp L_005d98                       ; -> asm/5B98_view_origin_wrapped
5ABB: stc; mov 0x1,%al; cmc; sbb $0x5,%al; cmc; mov %al,0x3   ; DS:03 = y - 6
      stc; mov 0x0,%al; cmc; sbb $0x9,%al; cmc; mov %al,0x2   ; DS:02 = x - 10
      (and DS:04, DS:05, SI, DI = 0)
5B98: the same two lines, each followed by `and $0x3f,%al`     ; wrapped into 0..63
```

The two branches differ **only** by the `and $0x3f`. So `DS:02/03` are a viewport origin,
the overworld wraps it and an interior does not — the byte-level version of the seamless-
scroll result the owner reported and the viewport-wrap fix chased from the other end.

Which means **`DS:12` is not a tile and never was.** It is a mode byte, 0xFF on the world
map and 4 in an interior, written by `asm/0C1F_status_tick_branch` and `asm/0C2B` on the
`DS:4A` branch — and `DS:4A` is the sector's low decimal digit (§ above). Live cross-check:
`DS:12` read **0xFF in sector 20** (digit 0) and **4 in sector 32** (digit 2). Two
independent bytes agreeing on the same branch is worth more than either alone, and it is a
second confirmation of the `DS:4A` reading that came from a completely different question.

`engine/src/core/state.h` now calls it `view_mode` (renamed 2026-10-28b, owner-approved; it was
`tile_offsector`) and `overworld.c` writes 4 on a site entry and 0xFF in-world, which is what
the live reads say. Nothing in the engine reads the field, so the rename is bookkeeping that
keeps the state block honest rather than a behaviour change. The unreconciled detail, stated
rather than smoothed: `state.h` cites `asm/5AE1` for a window origin of `(x-9, y-5)`, while
these two branches compute `(x-10, y-6)`. Different routines, and nobody has checked whether
one of the readings is wrong.

### What this does and does not license the engine to do

Proven now, and therefore implementable: **the cadence** — one spawn attempt per completed
command in a sector whose second digit is 0 (or above 5), requiring a free `mon_type` slot.

Still not proven, and still a guess if implemented: **which kind** (the `rcr` chain over
`DS:60` plus per-arm `cmp` on `[DI+0x1B7]` — the arms and their HP are proven, the selector
that picks an arm is not), and **where it lands** (the coordinate roll at `L_005417` and its
bounds exits). Per the handoff's own rule — a plausible wrong spawn rule is worse than an
empty field — the engine keeps no wandering-spawn logic. The closing measurement is
agent-drivable and cheap: stop at `0x0CEE` repeatedly, and at each stop log `DS:60` before
and after, `[DI+0x1B7]`, the `L_005417` result and the arm taken. One arm per capture, so
budget a restart per sample; twenty samples would settle the selector.

### Tool findings, because they cost the most time and are not about this binary

1. **An RSP attach halts the VM and it stays halted.** Opening port 20190 freezes execution;
   closing the socket without a `continue` leaves `/api/status` reporting
   `isCpuRunning: true, isPaused: true` with a frozen cycle count, and every key sent
   afterwards is swallowed. A frozen run looks exactly like an idle game. `bp_read` now
   resumes on every exit path including "no stop", and `reject_trap.RSP` gained
   `drain_pending` for the same reason.
2. **On a halted target the monitor reply lags the command.** Traced directly: `qRcmd,state`
   answered nothing for 20 s; a bare `?` four seconds later produced the register dump
   immediately; `continue` produced the answer to the `peekRet` sent before it. Sent one at
   a time, every reply is attributed to the wrong command — which is how `callStack` came
   back holding a register dump and looked like plausible evidence. `collect_monitors` now
   fires all commands and files each packet by content signature, so ordering cannot lie.
3. **`peekRet` needs a type to be interesting and still told us nothing here.** Valid types
   are `NEAR16 NEAR32 FAR16 FAR32 INTERRUPT EXTERNAL_INTERRUPT MACHINE` (read out of
   `Spice86.Core.dll`, `monitor help`). On this binary the honest answer is `017D:0000`
   because there is no frame; the raw word at `SS:SP` is the number to trust, which is why
   `--ret` prints both and says whether they agree.
4. **`--dump` and `--stack` read while halted are fine** — the HTTP memory API answers a
   halted target, so the register dump and the stack window describe the same instant. The
   § 15.1 warning about a post-resume read still stands; it is the resume that changes the
   state, not the read.
5. **`breakCsIp` is multi-shot, now measured rather than inherited.** The handoff's hazard 5
   said a selftest trap keeps firing, which was a belief about a tool limitation nobody had
   timed. Test: arm once at `0x0CEE`, wait for the stop, resume, then wait again with no
   re-arm. A second stop arrived at `0x0CEE`. So a trap is permanent for the life of the run
   and re-arming the same address just stacks duplicates — which is what several of the
   captures in this section accidentally did, and why a capture that must see *every* pass
   through an address should arm once and drive the resume itself rather than relaunching
   `bp_read`.

## 15.3 The selector, sampled — what it confirmed and what it did not (2026-10-28)

`tools/bp_read.py` gained `--samples N` and `--post-dump` for this, on the strength of the
multi-shot finding above: one arm plus a resume loop replaces a restart per sample, and 20
samples came off one run in 90 seconds.

**The chain is now fully enumerated statically**, which is the part that was missing. Reading
`0D01..0D21` end to end (the last of it was already cleaned in `asm/0D21_sub_0d21.asm` with
local labels, which is why it had resisted the fragment-scoped view):

| lowest set bit of the chain value | arm | kind it wants | type | HP |
| --- | --- | --- | --- | --- |
| bit0 | `0F44` | `0x08` | `0x30` | `0x10` Orc |
| bit1 | `0F64` | `0x08` | `0xFC` | `0x20` Thief/Daemon |
| bit2 | `0F84` | — | | |
| bit3 | `0FA4` | `0x00` | | |
| bit4 | `0FD4` | `0x08` | `0xF0` | `0x80` Fighter |
| bit5 | `0FCB -> 0FF3` | `0x00` | `0x48` | `0xA0` Frigate |
| bit6 | `0FCE -> 1012` | `0x08` | `0x38` | `0xC0` Devil |
| bit7 | `0FD1 -> 1031` | `0x08` | `0xF8` | `0xE0` Wizard |
| none (chain value 0) | `1050` | `0x08` | `0x3C` | `0xFF` Balron |

Two arms want kind `0x08` and stamp different creatures, so the kind alone cannot pick the
arm — the bit chain must, which is the model's whole point and is now visible in the bytes
rather than inferred. Note the chain runs on `DS:60` **after** the `incb`, so a stop at
`0x0CEE` reads one less than the chain sees.

**What the samples confirmed.** Over 20 stops at `0x0CEE` in a fresh sector, `DS:60` stayed at
2 then 3, and later 9 — it creeps, it does not march, which is what increment-plus-rollback
looks like and not what a spawn counter looks like. The kind drawn into `AL` was `0x00` or
`0x08`, exactly the two values the arms compare against. And the arm actually taken tracks
`DS:60`: with `DS:60 = 9` the chain sees `10 = 0b1010`, bit1, the Thief/Daemon arm — and a
trap on the Orc stamp site `0x0D4F` then sat out 80 seconds without firing. That last one is
a prediction of the table holding, not a measurement of the mapping.

**What it did not confirm, and a method warning.** The per-sample `--post-dump` reads are
**not** attributable to the stop they follow: half a second of game time contains other spawn
attempts, so a stamped slot in the post window may belong to a different command entirely.
Two apparent Orc stamps in the first sample set dissolved once the pre-increment offset was
accounted for. Anyone re-testing this must stop **inside the arm** (e.g. `0x0D4F`) and read
`DS:60` there, not infer the arm from a post-resume dump. `--post-dump` is still fine for
anything a busy main loop does not touch in half a second; it is wrong for exactly the thing
it was added for here.

So the engine still gets no wandering spawn. The cadence is proven, the arm table is proven,
the bit-to-arm mapping is enumerated statically and consistent with every sample but not yet
pinned at runtime, and the coordinate roll is unread. The remaining measurement is narrow and
cheap now: trap each stamp site in turn and read `DS:60` at it.

## 15.4 The selector is pinned at runtime, and the byte the arms compare is terrain (2026-10-28)

§ 15.3 left one gap: the bit-to-arm mapping was read off the bytes but never observed. It is
observed now, and the measurement was cheap because the input is pokable. `DS:60` has exactly
two writers in this cluster (`incb` at runtime `0CFA`, `decb` on the reject tail), so forcing
it is forcing the selector. `tools/spice86_type.py` grew `--poke`/`--peek` for it, since that
tool already owns the only write path (the MCP endpoint).

**Trap the arm entry, not the stamp site.** The stamp site is reached only when the arm's
comparison also passes, which is the rare half; the arm entry is reached whenever the chain
selects that arm, whatever the comparison decides. One arm-entry trap plus a resume loop
samples three to four times as often, and the register dump at the stop carries the compared
byte in `AL`, so the arm entry gives strictly more information than the stamp site did.

Forced `DS:60` (the chain reads it *after* the `incb`, so poke `N-1`), one command per attempt:

| forced chain value | arm entered (runtime) | `DS:60` at the stop | verdict |
| --- | --- | --- | --- |
| `0x01` | `0D44` Orc | `01` | as predicted |
| `0x04` | `0D84` Daemon | `04` | as predicted; `AL=00` so the arm rejected |
| `0x10` | `0DD4` Fighter | `10` | as predicted; `AL=00` so the arm rejected |
| `0x00` | `0E50` Balron (no bits set) | `00` | as predicted |

and at the stamp sites, where the comparison also passed:

| forced chain value | stamp site | `DS:60` at the stop | `AL` | spawned |
| --- | --- | --- | --- | --- |
| `0x01` | `0D4F` | `01` | `08` | Orc |
| `0x02` | `0D6F` | `02` | `08` | Thief |

and by outcome census (diff the type array `DS:197` before and after a forced-value run,
counting only slots that were empty at the baseline):

| chain value | bits | predicted arm | creature that appeared |
| --- | --- | --- | --- |
| `0x08` | bit3 | Sea serpent | `0x2C` sea serpent x3 |
| `0x20` | bit5 | Frigate | `0x48` frigate x2 |
| `0x40` | bit6 | Devil | `0x38` devil |
| `0x80` | bit7 | Wizard | `0xf8` wizard |
| `0x12` | bits 1 **and** 4 | Thief, because bit1 is lower | Thief x2 at slots `18`,`19` |

That last row is the one that makes the model a model rather than a reading: with two bits
set the chain took the **lowest**, not the highest and not either. **The selector is the
lowest set bit of `DS:60` after the increment, and a zero byte falls through to the Balron
arm.** Nine arms, all accounted for.

### The compared byte is the terrain tile at the rolled coordinate, not a drawn kind

§ 15.3 called it "the kind drawn from `L_005417`". It is not drawn. Walking the bytes in
address order: `0CEB` is `call L_0052b3`, which is `asm/50B3_map_tile_ptr` — it computes
`y*64 + x + [DS:0472]` from the staging bytes `DS:23`/`DS:24`, leaves the cell pointer in
`DS:06` and returns the **tile byte** in `AL`. Nothing between there and the store touches
`AL`, so `mov %al,0x1b7(%di)` at runtime `0CF6` files the terrain byte of the cell the
coordinate roll picked. The arms then compare that byte, and the coordinate roll's staging
bytes are the same `DS:23`/`DS:24` the tile lookup reads.

Two independent confirmations:

* **11/11.** Every spawn this session and the last has a slot record whose compared byte equals
  the original `mapx20` byte at the coordinates stored in that slot's `x`/`y` — sea serpents
  at (4,16), (19,25), (6,18) all `0x00`; frigates at (22,28), (23,26) all `0x00`; orcs,
  thieves, the devil and the wizard all on `0x08`. Checked in code, not by eye.
* **the two arms that want `0x00` are the two sea creatures.** Sea serpent (bit3) and Frigate
  (bit5) compare against `0x00`, which is water and is 2770 of the 4096 cells of sector 20.
  Every other arm compares against `0x08`. A random index would not sort that way.

So the spawner is: find a free slot, roll a coordinate, look up its terrain, then let the
selector decide *which species is allowed this tick* and refuse unless the terrain it wants
is the terrain that was rolled. That is why the observed rate is low — the gates multiply:
`rand < 0x3F` (~25%), a second roll whose sign bit must be clear (~50%), the tile lookup,
and then the terrain/selector agreement. Roughly one spawn per few hundred commands, which
matches what the walk loops produced.

The slot's tile byte also mutates after the spawn (Orc slots were seen going `08` -> `0c`
between reads), so **a census must diff against a baseline and count only slots that were
empty in it**. Comparing whole slot records flags a moving monster as a new spawn, and that
false positive cost a re-run of the Daemon row.

### What this licenses the engine to do

The § 15.2 sentence "still not proven, and still a guess if implemented: which kind" is now
withdrawn. Proven and implementable: the cadence, the free-slot scan (31 down to 1), both
coordinate rolls and their bounds, the terrain lookup, the nine-arm table, the
lowest-set-bit selector, and the `decb` rollback on a mismatch. Not proven, and still a
guess if implemented: nothing structural — the remaining unknowns are the exact distribution
of `rand_byte` and whether a door opened in a sector survives a reload.

## 16. Creature state is re-derived on entry — HP, roster and aggro (2026-10-29)

Owner-driven live session, sector 31, read over the Spice86 HTTP API. The owner attacked a
guard to draw aggro, was chased out, left the town and re-entered. Afterwards the guards were
back at their authored cells and no longer hostile. The bytes say more than that:

| array | live DS vs authored `monx31` | reading |
| --- | --- | --- |
| `mon_hp` | **32/32 identical** | the damaged guard is back at his authored 255 |
| `mon_type` | 32/32 | the roster is exactly the authored one |
| `mon_role` | 32/32 | no scripted state carried over |
| `mon_x` | 17/32 | drifted since entry — expected, monsters wander |
| `mon_y` | 19/32 | same |
| `mon_kind` | 29/32 | three slots differ; **unexplained, not guessed at** |

`mon_hp` is the decisive column. HP is precisely the field one would persist if creature
state were persisted, and it came back equal to the authored byte on every slot. Combined with
§ 10 of `docs/map-format.md` — `monx31` on disk unchanged — the mechanism is a plain reload
from the pristine file, not a restore from a shadow copy.

What is **not** explained here: the three `mon_kind` differences, and where aggro itself lives.
The session observed aggro behaviourally (a guard chasing) and observed its disappearance
behaviourally, but nobody has read a byte that says "this creature is hostile". Anyone
implementing aggro should find that byte rather than invent one, and should re-run this
comparison as the control: if aggro has a home in DS, it must be a field that resets on entry
for the right reason and not one that happens to look clean.
