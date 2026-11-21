# The `Z` status screen — spec for wiring it in the engine

Opened by `docs/archive/handoff-2026-10-27-night.md` item 3, which called it "self-contained,
zero unknowns". It is self-contained but it is not zero unknowns, and the two things the
handoff guessed are both wrong: the handler is not `asm/462F_status_keys.asm`, and the
screen has a real handler chain that is already partly readable. This document is the
spec; the implementation is the next step and has not been done.

Everything below is graded. **observed** = read from `original/ultimaii.exe` or a live
session. **measured** = derived by a named tool or read. **inferred** = reasoned from a
partial read, not yet checked.

## 1. The handler, and why the handoff's guess was wrong

The A–Z command table at file `0x930` (runtime `0x730`, read out of the binary 2026-10-28
while chasing `docs/monster-spawn.md` § 15.2) gives entry 25 = `'Z'` → runtime `0x4568`,
which is `asm/4568_cmd_Z.asm`. That is the handler.

`asm/462F_status_keys.asm` prints the string `KEYS-` and belongs to a family of five
status-line fragments — `45C4_status_weapon`, `45E9_status_torches`, `460A_status_armour`,
`462F_status_keys`, `466F_status_tools`. The Z screen **reuses** those fragments, which is
exactly why it looks like they are the handler. They are shared drawing helpers, not the
entry point.

The chain, from the bytes at file `0x4768`:

```
4568 cmd_Z:   e8 b1 04      call L_004c1c      ; screen clear prologue, see § 4
              c6 06 4f 05 01   mov byte [0x54f],1
              c6 06 2e 00 01   mov byte [0x2e],1     ; cursor row = 1
              c6 06 31 00 26   mov byte [0x31],0x26
              e8 80 0c      call ...
              e8 fa 0a      call ...
4580:         call str_out ; db 0x8d,"A LEVEL ",0     ; 0x8d is the cursor escape
458D:         mov 0x56,%al ; call L_0055b5            ; level, packed decimal
4593/4598/459E/45A3/45AC/45B1:  spaces and DS:46 (sex), DS:47 (class), DS:48 (race)
```

`DS:2E/2F` are the shared text cursor row/col (the same pair `engine/src/game/status.c`
saves and restores around `u2_status_draw`). The Z screen drives it the same way.

## 2. The rendered page, transcribed

Captured 2026-10-28 headless: `spice86_start.sh --headless --patch-arrows --player
/var/tmp/u2/novnc-saves/PLAYER --auto-play --name zstats`, then `spice86_type.py --keys Z`
and `spice86_screenshot.py`. 320x200 at 8x8 cells = **40 columns x 25 rows**, and the text
wraps at 40, which is why the item list breaks mid-word.

```
G23MAX
 A LEVEL 99 FEMALE HUMAN FIGHTER

  WEAPON-QUICK SWORD      TORCHES-50
  ARMOUR-POWER            KEYS-20
  SPELL-NONE              TOOLS-20
  STRENGTH-40  STAMINA-40   WISDOM-40
  AGILITY-40   CHARISMA-40  INTELL.-40

WEAPONS: DAGGERS-01 MACES-01 AXES-01 B
OWS-01 SWORDS-01 GREAT SWORDS-01 LIGHT
 SWORDS-01 PHASERS-01 QUICK SWORDS-01
ARMOUR:
SPELLS:


ITEMS: RINGS-01 WANDS-01 STAFFS-01 BOO
TS-01 CLOAKS-01 HELMS-01 GEMS-01 ANKH
S-01 RED GEMS-01 SKULL KEYS-01 GREEN GE
MS-01 BRASS BUTTONS-01 BLUE TASSELLS-01
 STRANGE COINS-01 GREEN IDOLS-01 TRI-L
ITHIUMS-01
```

Note `A LEVEL 99`. The handoff records the owner's screenshot as `A LEVEL 00` for the same
character — either a different character or a different moment. Unresolved, and it matters
because `458D` prints `DS:56`, which `status.h` already identifies as the EXP high byte.
**Do not implement "level" from EXP until this is pinned down**; the field being printed is
`DS:56` either way, which is the honest thing to render.

## 3. The name tables — all of them are in the binary

A NUL-separated table at file `0x4F53..0x50FF` (runtime `0x4D53..`). Extracted by splitting
the region on `0x00`, 2026-10-28:

| group | entries | file range |
| --- | --- | --- |
| weapons (10) | `HANDS DAGGER MACE AXE BOW SWORD GREAT SWORD LIGHT SWORD PHASER QUICK SWORD` | `0x4F53..0x4F9D` |
| armour (8) | `SKIN CLOTH LEATHER CHAIN PLATE REFLECT POWER NONE` | `0x4F9E..0x4FCD` |
| dungeon features | `LIGHT DOWN LADDER UP LADDER PASSWALL SURFACE PRAYER` | `0x4FD0..0x5003` |
| spells | `MAGIC MISSILE BLINK KILL` | `0x5004..0x501C` |
| items (16) | `RING WAND STAFF BOOTS CLOAK HELM GEM ANKH RED GEM SKULL KEY GREEN GEM BRASS BUTTON BLUE TASSLE STRANGE COIN GREEN IDOL TRI-LITHIUM` | `0x501D..0x509F` |
| stats (6) | `STRENGTH AGILITY STAMINA CHARISMA WISDOM INTELL.` | `0x50A0..0x50D0` |
| races (4) | `HUMAN ELF DWARF HOBBIT` | `0x50D1..0x50E7` |
| classes (4) | `FIGHTER CLERIC WIZARD THIEF` | `0x50E8..0x5103` |

Two things the screenshot makes visible and the table alone does not:

* **The lists are the table entry plus a literal `S`.** `DAGGER` prints as `DAGGERS-01`,
  `TRI-LITHIUM` as `TRI-LITHIUMS-01`, `INTELL.` as `INTELL.-40` (stats get no `S`). So the
  plural is a suffix in the print path, not a second table.
* **`BOOTS-01`, not `BOOKS-01`.** The line wraps as `BOO`/`TS-01` and reads as "BOOKS" to
  the eye. The table says `BOOTS`, and `state.h` independently names `items_held[BOOTS]`
  as the paralysis save. Transcribing the screenshot by eye would have invented an item.

The counts come from the save record, already mapped in `engine/src/core/state.h`:
`weapons_owned[10]` at `DS:76`, `items_held[16]` at `DS:D6`, `readied_weapon` `DS:61`,
`worn_armour` `DS:62`, `spell_pick` `DS:63`.

**These strings were not in the generated header when this was written** (90
`U2_S_` defines, none of them an item name), and the extractor had no table mode.
It has one now: `engine/content/gen_content.h` carries the indexed name pool as
`U2_T_*` arrays, generated from the same bytes, so `status.c` builds the screen
without hardcoded text. The path also moved — the header is committed at
`engine/content/`, not built into `engine/build/`.

## 4. Measured behaviour, not assumed

Headless, `G23MAX`, sector 20, before/after `Z` and before/after the dismissing key:

| check | result |
| --- | --- |
| food `DS:53/54/55` | unchanged (`[153,100,2]` before and after) — **`Z` costs no food** |
| HP `DS:51/52` | unchanged |
| player position `DS:00/01` | unchanged |
| `mon_x` `DS:137..140` | unchanged (all zero — the field was empty) |
| `DS:60` | unchanged |
| cursor row `DS:2E` | `0` while the page is up, `5` after the dismissing key |
| dismissal | a key returns to the world; the page is a full-screen overwrite, not an overlay |

So the handoff's acceptance question — "does `Z` cost food?" — is answered: **no**.

**Not measured, and it needs live monsters:** whether the post-command creature sweep runs
after `Z`. Per § 15.2 the sweep follows every command that returns through `cmd_done`, and
`asm/4565` (the fragment immediately before `cmd_Z`) is a `jmp L_000c33`, but no fragment
in the Z chain has been shown to reach `cmd_done`. If the sweep does run, `Z` gives every
creature a turn, which is a gameplay cost even though food does not move. Settle it with
monsters on the field: read `mon_x`, press `Z`, read again.

## 5. Open questions, named

1. **Exact column of every field.** **Closed 2026-10-28** — see § 7, which derives every
   position from the `mov di,bx` / `mov si,bx` pairs in the handler and corrects three
   columns in the § 2 transcription. Kept here only so the § 2 table is read with § 7.
2. **`ARMOUR:` and `SPELLS:` print empty** for a character wearing POWER armour. Either the
   lists show only *spares*, or the owned-armour array is elsewhere than `state.h`'s
   `reserved_80[0x16]`. Unresolved.
3. **`DS:31 = 0x26` and `DS:54F = 1`** are set at the top of the handler and their meaning
   is unknown. `DS:54F` is past the 256-byte save record, in the DS scratch area.
4. **`asm/4A1C_char_out.asm` was misnamed** — renamed 2026-10-28b, owner-approved, to
   `asm/4A1C_screen_clear_mode1.asm`. It is not character output: `mov $0x1,%ax`
   is AX=0x0001, so AH=00 set-video-mode AL=01, and it falls through (no `ret`) into the
   cursor-zero at `4A22`. The engine's `seam_char_out` keeps its name and its behaviour but no
   longer cites this address: the INT 10h AH=0E teletype it was said to stand in for has never
   been located, so the seam is engine-authored. The header and the seam map in
   `engine/src/platform/platform.h` carry the detail.
5. **`A LEVEL 99` vs the handoff's `A LEVEL 00`** for the same character.

## 6. What implementing it actually needs, in order

1. `tools/extract/extractor.py`: extract the six name tables above as arrays, not as
   individual `U2_S_` defines, and add a `content-check` case for them.
2. `engine/src/game/status.c`: a `u2_status_draw_full()` that reproduces § 2 from the state
   record, driving the same cursor pair, with positions taken from the asm per open
   question 1.
3. `overworld.c`: a `case 'Z'` in the command switch (next to `case 'A'` at line 940) that
   draws the page and waits for a key without advancing the turn.
4. `engine/tests/status_check.c`: assert the rendered page against the transcription, and
   assert the measured no-cost properties (food, position, HP unchanged across `Z`).
   **Demonstrate it failing first** — flip one expected byte, watch it go red, restore.

## 7. The layout, derived from the bytes (2026-10-28)

§ 5 open question 1 is closed. The region was re-read with `objdump -D -b binary -mi8086`
synchronised on every `call str_out` site — the bulk wave's fragments could not be trusted
here because the inline strings are data being disassembled as code, and one misaligned byte
shifts everything after it. Runtime = file − 0x200 throughout; `fXXXX` is a file offset.

**The cursor pair is the other way round from § 1.** `asm/51EB_str_out_at` stores **DI into
`DS:2E` and SI into `DS:2F`**, so `DS:2E` is the **column** and `DS:2F` the **row**. `cmd_Z`
opening with `mov byte [0x2e],1` is "column 1", which is where the name line starts. Every
position below comes from a `mov bh,0 ; mov bl,N ; mov di,bx` / `mov si,bx` pair, so none of
it is eyeballed.

| what | col | row | site |
| --- | --- | --- | --- |
| name, then `A LEVEL ` + `DS:56` + sex + race + class | 1 | (inherited) | `f4770`, `f4780` |
| `  WEAPON-` + pool[19 + `DS:61`] | (flows) | | `f47c4`, `f47d1` |
| `TORCHES-` + BCD(`DS:64`) | 29 | 3 | `f47da`, `f47e9` |
| `  ARMOUR-` + pool[29 + `DS:62`] | 1 | 4 | `f47fb`, `f480a` |
| `KEYS-` + BCD(`DS:65`) | 32 | 4 | `f4820`, `f482f` |
| `SPELL-` + pool[36 + `DS:63`] | 4 | 5 | `f483c`, `f484d` |
| `TOOLS-` + BCD(`DS:66`) | 31 | 5 | `f4860`, `f486f` |
| `STRENGTH-` + BCD(`DS:4B`) | 1 | 6 | `f487f`, `f488e` |
| `AGILITY-` + BCD(`DS:4C`) | 2 | 7 | `f489e`, `f48ad` |
| `STAMINA-` + BCD(`DS:4D`) | 16 | 6 | `f48bd`, `f48cc` |
| `CHARISMA-` + BCD(`DS:4E`) | 15 | 7 | `f48dc`, `f48eb` |
| `WISDOM-` + BCD(`DS:4F`) | 30 | 6 | `f48fb`, `f490a` |
| `INTELL.-` + BCD(`DS:50`) | 29 | 7 | `f491a`, `f4929` |
| `WEAPONS: ` list | 1 | 9 | `f4939`, `f4948` |
| `ARMOUR: ` list | 1 | 12 | `f4993`, `f49a2` |
| `SPELLS: ` list | 1 | 14 | `f49eb`, `f49fa` |
| `ITEMS: ` list | 1 | 18 | `f4a44`, `f4a53` |

The transcription in § 2 is wrong in three places this table fixes: `TORCHES-` is column 29
not 26, the stat columns are 1/16/30 on row 6 and 2/15/29 on row 7 rather than the even
spacing the screenshot suggests, and the four list headers sit on rows 9, 12, 14 and 18.

### The four list loops, and what each one actually iterates

Every loop has the same shape: `DI` starts at **1**, the body reads a count byte, skips the
entry when it is zero, prints `pool[base + DI]`, then a separator, then the count as two
packed-decimal digits, then a space; `inc di` and loop while `DI < bound`.

| list | base | bound | count array | separator |
| --- | --- | --- | --- | --- |
| `WEAPONS:` | 19 | 10 | `DS:76 + DI` (`weapons_owned`) | `"S-"` |
| `ARMOUR:` | 29 | 7 | `DS:80 + DI` | `"S-"` |
| `SPELLS:` | 36 | 10 | `DS:B6 + DI` | `"S-"` |
| `ITEMS:` | 46 | 16 | `DS:D6 + DI` (`items_held`) | `"S"` then `"-"`, with the `"S"` skipped when `DI == 3` |

Three things fall out of that table which the screenshot alone did not say.

* **Index 0 is never listed.** Every loop starts at 1, so `HANDS` cannot appear in the weapon
  list and `SKIN` cannot appear in the armour list. That is why the transcription's weapon
  list begins at `DAGGERS-01` even for a character holding one of everything.
* **The plural is a literal `S`, and `BOOTS` is the one exception** (`f4a7a: cmp byte
  [0x1f],3 ; je`), because the entry already ends in S. So `RINGS-01`, `TRI-LITHIUMS-01` and
  `BOOTS-01` all come out of one rule, and the stats get no plural at all because they print
  a bare `'-'`.
* **`ARMOUR:` reads `DS:80`, which `state.h` still calls `reserved_80`.** § 5 open question 2
  is answered: the owned-armour counts really are at `DS:80..0x86`, seven entries, and the
  list printed empty for `G23MAX` because that character's counts are zero — not because the
  array is elsewhere. The `CANDIDATE, NOT PROVEN` note in `state.h` can now be retired with a
  citation, and the `SPELLS:` count array at `DS:B6` is a new finding with no owner yet.

### The pool base, and why `HANDS` is index 19

`asm/508C` takes the entry index in `AL`, and the print sites add a constant: `+0x13` for
weapons, `+0x1d` for armour, `+0x24` for spells, `+0x2e` for items, `+0x44` for races,
`+0x48` for classes, and the six stat names come as literals `0x3E..0x43`. Anchoring on
`HANDS == 19` is what makes `readied_weapon 9` print `QUICK SWORD` and the weapon list start
at `DAGGER`; the byte before `IN THE WATER.` in the image is a second NUL, so the code's
index base counts one entry that carries no text. `engine/tools/extract/extractor.py` now
splits the pool and **asserts** that anchor instead of trusting the split.

### What blocked the screen, and what the answer turned out to be

The engine builds in CI with no `original/ultimaii.exe`, from the `db` lines of `asm/`. The
pool used to be the one piece of text the asm tree could not supply: the bulk wave
disassembled the region after `HANDS` as code, so `asm/4C50_plane_landing_site.asm` had
`db "GGER"`, `db "CE"`, `db "ORD"` where `DAGGER`, `MACE` and `SWORD` live — the strings were
recoverable, because `assemble_clean.sh` rebuilt those bytes exactly, but not by reading the
listing.

**That is fixed as of 2026-10-25.** The region is now `asm/4C55_name_pool.asm`, one entry per
`db` line, each tagged with the index the code uses (`db "DAGGER",0x00   # pool[20]`), and the
split is confirmed twice over: every group base in the table above lands on the right entry, and
`extractor.py` now reads the exe path and the asm path and gets byte-identical 76-entry lists.
The extractor's fallback used to parse one enormous `db` operand and could only hope the split
landed; it now requires the `pool[N]` tags to run 0..N with no gap, so a drift fails the build
instead of putting PHASER where QUICK SWORD belongs.

This was first written up as a decision for the owner between a CI content fixture, a
skipping test, or a committed generated table. **That framing was wrong and those options
are withdrawn.** The rule is that game truth needs the game, the game cannot go on GitHub,
so game truth is checked on a dev box and CI checks structure. Concretely:

* the extractor emits the seven tables when it has the exe, and when it does not it **warns
  and emits none** — never a partial or silently-empty table;
* `status.c` compiles either way (`#ifdef U2_T_WEAPONS_N`), and with no pool the names render
  blank while every position, label, dash and count still draws;
* `status_check.c` asserts the layout in both modes and asserts *which mode it ran in*, via
  `u2_status_has_pool()`, so a silently lost pool cannot pass as green.

`status_check` passes with 44 checks against the pool and 39 without it.

### Two more things the bytes said, found by building it

**`DS:31 = 0x26` is the right margin, not an unknown.** Open question 3 asked what the
`mov byte [0x31],0x26` at the head of the handler was for. It is the right margin, and it is
load-bearing: at the default `0x1d` (col 29) the `TORCHES-` field printed at column 29
overflows and wraps onto the `ARMOUR` row, so the row-4 label came out reading
`RCHES-50-`. `status_check` caught that on its first run.

**The item list starts at index 0; the other three start at 1.** The loop heads are
`mov bl,1` for weapons (`f4955`), armour (`f49ae`) and spells (`f4a06`), but `mov bl,0` for
items (`f4a5e`). So `RING` can be listed and `HANDS` and `SKIN` cannot — which corrects the
sweeping statement above that every loop starts at 1. The plural and the dash are also two
separate prints with only the plural conditional: name, then `S` unless index 3, then `-`
always, then the count. Collapsing them into one separator produced `RINGS01`, and that was
the second bug the test caught.

**`MALE` and `FEMALE` are exe-only locators.** `asm/5400` stores them as a byte list, so the
fallback paints them empty and the sex assertion is conditional on the string being
available. Asserting an empty cell and calling it a pass would have been the third bug.
