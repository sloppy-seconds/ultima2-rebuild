# Bestiary — naming the creature tiles from third-party data

Status: **tools built, first join run**. The pipeline is `fetch → match → view`;
the machine-readable result is `bestiary/bestiary.tsv` and a rendered summary is
`bestiary/matches.md`.

## 1. Why this exists

The executable has **no monster name table**. `tools/monster_shapes.py` records
this: the string `THIEF` at file `0x50FE` is a *player class*, not a creature, and
nothing in the binary pairs a monster type with a word. That is why
`disasm/tile-art.tsv` could only say "white humanoid, arms down, legs apart" about
art `0x0C`. A creature could be given a face but not a name.

Two community sources carry names, stats and per-platform art, and they are
independent of each other and of this binary:

| source | shape | what it adds |
| --- | --- | --- |
| [Ultima Codex](https://wiki.ultimacodex.com/wiki/Ultima_II_monster_data) | four tables (land, townsfolk, dungeon, NPCs), one column per shipped variant from Apple II 1982 to FM Towns 1990 | names, HP, specials, item drops, art in 7 populated variants |
| [Gamer Corner](https://guides.gamercorner.net/ultimaii/creatures/) | one page per creature, sortable table | names, HP, where it appears, prose behaviour notes, one DOS tile image |

The move that makes them useful rather than decorative: a table cell delivers a
**name and a bitmap together**. Where a source's DOS bitmap is pixel-identical to
one of our 64 tiles, the name attaches to that art index by measurement.

## 2. Evidence grades

Be precise about what is proven, because the interesting part is not proven by the
binary.

| claim | grade | basis |
| --- | --- | --- |
| "this community bitmap and art index *NN* are the same 256 pixels" | **measured** | `tools/bestiary_match.py` against `original/ultimaii.exe`, same decode as `tools/tile_atlas.py` |
| "art index *NN* is called *Orc*" | **third-party** | inherited from the codex or Gamer Corner; the bytes never say |
| "Daemon has 64 HP" | **measured** | `asm/0D6F_monster_hp_thief_daemon.asm` stores `mov $0x40,%al` into `[DI+0x177]` for type `0x34`; the field is proven HP by `asm/2E3D_combat_attack_flow.asm`. The *name* Daemon is still third-party |
| "Daemon paralyzes legs at range 2" | **third-party, corroborated** | two independent sources agree, and `asm/0E70_combat_paralyzed_legs.asm` proves the leg-paralysis event exists |
| "monster type byte *T* draws art index *NN*" | **hypothesis, 4 arithmetic agreements** | the arms store type and HP as plain immediates (`mov $0x30,%al; mov %al,0x197(%di)`) — **no arm contains a shift or a `0xFC` mask**, so nothing in the spawn path performs `>>2`. It happens that `type >> 2` equals the matched art index on the four special-casting types. That is agreement, not mechanism. See **G18** |

Where a third-party claim contradicts the binary, the binary wins, and the
contradiction is written down here rather than resolved silently. See § 6.

## 3. Method

```
python3 tools/bestiary_fetch.py      # two pages + their images -> bestiary/cache/
python3 tools/bestiary_match.py --self-test
python3 tools/bestiary_match.py --md # -> bestiary/bestiary.tsv, matches.md
python3 tools/bestiary_view.py       # -> bestiary/view.html
```

`bestiary_fetch.py` keeps `cache/manifest.json` with a sha256 and timestamp per
file, so `--offline` re-parses what is already on disk and makes no requests. It
splits the animated FM Towns GIFs into numbered frames **and** a single strip, so
the animation is usable with and without a GIF decoder. It also handles the codex's
ragged tables explicitly (§ 8), because a flat column map there ships a plausible
wrong stat on most rows.

`bestiary_match.py` refuses to be generous:

* an image is eligible only if it reduces to 16x16 by an **exact integer factor**.
  The codex mixes 16x16 rips, 32x32 upscales and 224x142 screenshot crops; a crop
  is reported `not-1:1` and never resized into a vote.
* a match is equality of all 256 colour indices. Near-matches are printed as
  `near-N` and never promoted to a name.
* animated images are ineligible as a whole; they are intent evidence, not pixels.

### The controls, and the proof that they bite

AGENTS.md: *a gate that cannot go red is worse than a tool that lies.* Six controls
run under `--self-test`:

1. our own art, round-tripped through the image path, matches its own index
2. the same bitmap with **one pixel flipped matches nothing**
3. an all-black image matches no figure
4. an animated FM Towns GIF — a different art set — matches nothing
5. a flat image can only ever be classified `trivial`, never `exact`
6. a 24x24 crop is ineligible; a clean 2x upscale is eligible and still matches

Controls 5 and 6 exist because of mistakes the first run made, not as decoration.
Three of our art indices are blank or solid (`0x1D` has zero ink, `0x1E` is fully
solid). The first version of the matcher reported **Kings, Minax and Sea serpent**
as names for art `0x1E`, because three codex C64 images reduce to a flat field and
a flat field equals a solid tile. A plausible wrong name is exactly the failure mode
the rule warns about.

The gate was then demonstrated failing, by mutating one guard at a time in a copy of
the module: loosening the comparison to a Hamming distance of 6 turns control 2 red;
disabling the trivial-tile guard turns control 5 red; disabling the resize guard
turns control 6 red. All three mutations go red, and the unmutated tool passes 6/6.

## 4. What the join produced

26 of 120 image comparisons named a tile, across 15 distinct art indices, with no
art index claimed by two different names:

| art | name | corroborated by our bytes |
| --- | --- | --- |
| `0B` | Sea Monster | **disputed**, see § 6 |
| `0C` | Orc | — |
| `0D` | Daemon | `asm/0E70_combat_paralyzed_legs.asm` — leg paralysis exists |
| `0E` | Devil | `asm/0EC2_combat_paralyzed_arms.asm` — arm paralysis exists |
| `0F` | Balron | `asm/0F48_combat_sleep_spell.asm` — sleep event exists |
| `10` | Minax | `asm/0F8E_combat_minax_attack.asm` — and the string `MINAX CRIES: DIE FOOL!` |
| `12` | Frigate | `asm/2FAD` BOARD dispatch already separates ship from horse/plane/rocket |
| `18` | Guard | — |
| `19` | Jester | — |
| `1A` | Merchant | — |
| `1B` | King | — |
| `3C` | Fighter | — |
| `3D` | Cleric | — |
| `3E` | Wizard | `asm/0F12_combat_magic_missile.asm` — magic missile event exists |
| `3F` | Thief | `asm/0FCD_combat_thief_steal.asm` — `A THIEF STOLE SOMETHING!` |

The four specials in the middle of that table are the useful result. Two
third-party sources, independently, say Daemon paralyses legs at range 2, Devil
paralyses arms at range 2, Balron sleeps at range 2 and Wizard casts magic missile
at range 2 — and the binary contains exactly one routine per event, each named from
a string that executed in a live capture. The sources did not have to be right about
which creature did which, and they agree on all four. That is corroboration, not
proof, and it is recorded as a `# corroboration:` line in those six `asm/` headers.

Nine names matched nothing, and the reason is known rather than mysterious: the
codex's dungeon creature images are 224x142 screenshot crops, not rips. Ghost,
Carrion creeper, Viper, Gremlin and Goblin stay unnamed until a real DOS dungeon
sprite is found or one is extracted from a live session.

## 5. Three creature sets, and the boundary of this join

The sources do not describe one monster list. They describe three, and the overlap
between them is not one kind of overlap:

| set | count | members |
| --- | --- | --- |
| Creatures of the land | 10 | Orc, Goblin, Thief, Daemon, Sea serpent, Fighter, Pirate ship, Devil, Wizard, Balron |
| Townsfolk | 6 | Jester, Merchant, Thief, Cleric, Wizard, Guard |
| Creatures in Dungeons | 7 | Orc, Ghost, Carrion creeper, Viper, Gremlin, Daemon, Balron |

Overlaps: land ∩ dungeon = **Orc, Daemon, Balron**; land ∩ town = **Thief, Wizard**;
town ∩ dungeon = **none**.

Those two overlaps are different things wearing the same word:

* **Town reuses overworld creatures as NPCs.** Thief and Wizard carry the *same* DOS
  image (`U2Thief.PNG`, `U2Wizard.PNG`) and the same HP in both tables. Same
  creature, two contexts.
* **The dungeon creatures are different creatures with the same names.** Dungeon Orc
  is `U2-OrcDung.png` with HP `17-153`; overworld Orc is `U2Orc.PNG` with HP `16`.
  Same for Daemon (`177-217` vs `64`) and Balron (`209-249` vs `255`).

**This is the hard boundary on everything above.** The join named **zero** dungeon
arts, and that was the eligibility rule working, not a gap to close later: every
codex dungeon image is a `64x41` screenshot crop rather than a 16x16 rip. And the
dungeon is a wireframe renderer whose outlines `tools/monster_shapes.py` already
suspects live in `original/monsters` (64 records x 34 B of coordinate pairs, still
open as T9) — so dungeon creatures may never be drawn from the tile table at all, and
no tile-based join can name them. Filed as **G19**.

So: a name earned from the overworld tile table must never be carried into a dungeon
row. The bestiary names the overworld and town set and is silent about the dungeon.
One more source smell in the same direction: the codex's dungeon Daemon image is
filed as `U2DungeonMinotaur.png`, so the source disagrees with itself about what
that creature is called.

## 6. Monster HP: immediates for the overworld, computed for the dungeon

**This section was wrong when first written and has been corrected the same day.**
The original conclusion, "HP is computed, not stored", was half right: it is true of
the dungeon and false of the overworld, and the overworld case is not a computation
at all.

`tools/hp_table_scan.py` searched for a per-creature HP *table* and found none across
13 encoding x indexing combinations (raw byte, 16-bit LE, and the hundreds-first BCD
pair the player's own HP uses at `DS:51/52`), match bar "all but one". That negative
was correct and it was the useful part: **there is no table.** The values are
immediates scattered through a chain of compares on the monster type byte, which no
table scan could find and which is exactly what a hand-coded 1983 binary does.

The field is `[DI+0x177]` in the monster record, and it is proven by
`asm/2E3D_combat_attack_flow.asm`, which at runtime `0x30BD` does:

```
mov 0x177(%di),%al    ; load monster HP
stc; cmc              ; clear CF for a no-borrow subtract
sbb 0x20,%al          ; HP -= the damage byte just stored at [DS:20]
cmc                   ; re-expose the borrow (the BCD idiom AGENTS.md warns about)
jae -> store reduced HP, monster survives
    -> HP=0, type=0, sprite=0, whole record cleared
```

With the field known, the arms are trivial to read. `tools/monster_hp_dispatch.py`
extracts them and cross-checks them, and **exits non-zero on a disagreement**:

| type | HP | art | creature | third-party | verdict |
| --- | --- | --- | --- | --- | --- |
| `0x30` | 16 | `0x0C` | Orc | 16 | match |
| `0xFC` | 32 | `0x3F` | Thief | 32 | match |
| `0x34` | 64 | `0x0D` | Daemon | 64 | match |
| `0x2C` | 64 | `0x0B` | Sea serpent | 64 | match |
| `0xF0` | 128 | `0x3C` | Fighter | 128 | match |
| `0x48` | 160 | `0x12` | Frigate | 160 | match |
| `0x38` | 192 | `0x0E` | Devil | 192 | match |
| `0xF8` | 224 | `0x3E` | Wizard | 224 | match |
| `0x3C` | 255 | `0x0F` | Balron | 255 | match |

**9 for 9, zero disagreements.** So the overworld HP figures are now **measured**,
not third-party — and they arrived by a third independent route: the binary's own
immediates, the community bitmap, and the community stat table all name the same
creature. Balron is literally `mov $0xff`, the byte ceiling, which is why the
sources report 255 where a 32-step progression predicts 256.

### Live check: the arm value is the base for wandering spawns — and there is a second source

The first headless capture (seeded STRANGER, sector 0 at 30,32 — the walled
compound) read **HP = 255 for every active monster**, including types whose arms
store 64/192/224/32. That was written up as a refutation of the arms. It was half
right, and the correction over-corrected: **the existing capture corpus already
answered this.** Sixty-plus dumps were sitting in `captures/spice86/`, and AGENTS.md
literally says to list the corpus before harvesting runtime data.

Reading the monster record out of every historical dump settles it:

| captures | active | HP seen | verdict |
| --- | --- | --- | --- |
| `baseline-fight`, `probe`, `probe2`, `scrollfix`, `u2-vendored`, `bandhash`, `f9-capture`, `tile-look`, `sweep*` … | 1–8 | 16, 32, 64 | **zero mismatches across ~40 captures** |
| `play-test` (the landbridge session) | 6 | 16, 32, 44 | 44 is a Sea serpent at 44 of 64 — mid-fight damage |
| `castle-03-live` | 32 | 1, 25, 30, 40, 50, 60, 100, 150, 187, 200, 250, 255 | damaged **and** buffed |
| `autoplay`, `e2e`, `passtab` (sector-0 compound) | 31 | 255 only | everything saturated |

So the model is: **the arm immediate is the base HP of an ordinary wandering spawn;
placed monsters get their HP from the sector's `monxNN` file instead; damage lowers
whatever it was.** Ordinary spawns match exactly. In `castle-03-live` a Thief (base 32)
appears at 100, 150, 200 and 250 and a Devil (base 192) at 255 — not an override, but
`monx03[64 + slot]` holding those values — while a Fighter (base 128) sits at 100 and a
Devil at 187, which are plainly damaged. The sector-0 compound reads 255 everywhere
because `monx00[64:96]` is all `0xFF`. See `docs/monster-spawn.md` § 1.

The same dump **confirms the townsfolk have HP and validates the third-party ranges**,
which is the strongest cross-validation in this document:

| type | art | creature | third-party | **live observed** |
| --- | --- | --- | --- | --- |
| `0x60` | `0x18` | Guard | codex 255 / Gamer Corner 500 | **255** |
| `0x68` | `0x1A` | Merchant | codex 5–50 | **30, 50** |
| `0xF4` | `0x3D` | Cleric | codex 10–60 | **25, 30, 40** |

The codex's Townsfolk "ranges" are not spawn randomisation — they are the
area-dependent HP this model predicts, and the live values land inside them. Guard
reads 255, so the codex is right and Gamer Corner's 500 is wrong, and this time that
is an observation rather than a field-width argument.

Still open, and now narrow: the rule by which an area raises HP above the base
(castle vs compound vs overworld), and why the sector-0 compound saturates
everything. Neither demotes the nine arm values, which are **measured and live** for
ordinary wandering spawns.

What is *not* explained by the arms: **the townsfolk have no arm at all.** Guard,
Jester, Merchant, Cleric and King are assigned no HP anywhere in the monster-kind
dispatch, and this is a measured negative, not a shrug: the nine arms are the whole
chain (a bit-tree on a small kind index, guards at runtime `0xF06`–`0xF21`, arms
`0xF44`–`0x106F`, shared epilogue `asm/0BAF_monster_hp_epilogue`), and there is no
`mov $0x60/0x64/0x68/0x6C/0xF4,%al` — the type values those arts fold back to —
anywhere in `asm/`. So town NPCs are not created by the wandering-monster spawner.

**A lead I promoted and then retracted.** `asm/3611` looked like the townsfolk path
because it is the only routine in the tree that tests the site kind
(`cmpb $0x4,[DS:4A]`, tower — and note `DS:4A` is the ones digit of the sector number, so
that test is really "are we in sector X4"; see `docs/sector-rulesets.md` § 1). It is not. It is reached by fall-through from
`asm/35F3` — which stores a slot index to `[DS:20]`, rolls `rand | 0x20` into
`[DS:1F]` at `asm/3606`, and jumps into the HP subtract — so it *applies* a random
32–63 of damage to a chosen monster slot, and its site-kind test gates a map stamp of
`0x8`, not NPC creation. Renamed `3611_monster_damage_apply` with the retraction in
its header, because a wrong name left in place propagates.

The static search is now exhausted on this question, and that is a finding rather
than a failure: **every write site of `[DI+0x177]` is accounted for** — nine spawn
immediates, the dungeon computation, the two subtract paths, and the kill clear.
There is no tenth site where town NPC HP could be assigned. So either town NPCs
reuse one of these paths in a way the bytes alone cannot show, or they have no HP
record at all. Only a live read of a town NPC's `[DI+0x177]` distinguishes them.
That is **G17**'s live step, and it is the last move on this question.

The dungeon really is computed, and the routine is now identified.
`asm/147F_dungeon_monster_spawn.asm` scans the 32 slots for an empty one, rolls the
RNG three times for x and y as **odd values in 1..15** — the dungeon coordinate
range, not the overworld's 0..63 — and derives HP from the drawn sprite byte
(`rcr/adc` into the type, then `add %al,%al` four times around an `adc 0x1B7(%di)`,
then `or $0x10,%al` into HP). Its only caller in the tree is
`asm/1151_dungeon_darkness.asm`, which is what makes it the dungeon spawner rather
than a guess. That is consistent with the third-party dungeon ranges being two exact
arithmetic progressions in dungeon-table order:

```
min = 17 + 32k          max = 153 + 32*(k//2)
k=0 Orc 17/153   k=1 Ghost 49/153     k=2 Carrion 81/185  k=3 Viper 113/185
k=4 Gremlin 145/217    k=5 Daemon 177/217   k=6 Balron 209/249
```

14 data points, four constants, 7/7 on both. Grade that pair of formulas carefully:
the fit is **measured against third-party numbers**, and although the routine is now
identified by its caller, its arithmetic has not been validated against a live
dungeon monster. See **G17**.

One consequence of the field width, which settles a conflict without any capture:
**monster HP is a single byte.** Every immediate is `<= 0xFF`, the arithmetic is
byte-width `sbb`, and Balron is literally `mov $0xff`. So Gamer Corner's Guard HP of
**500 is impossible** and the codex's 255 stands — refuted by the width of the field,
not by an argument about which wiki is likelier.

## 7. What the join did **not** buy

* **No monster type byte is named, and the `>>2` transform is still a hypothesis.** Two
  record fields were candidates: `DS:0x1B7+i`, called "sprite stamped into the map cell",
  and `DS:0x197+i`. The first is **retracted** — a live read of all 32 slots shows `0x1B7`
  holds a small kind index (`0x08`/`0x0C`), which is what the spawn guards compare against,
  not a sprite. So the earlier "eight agreements across two fields" was really **four
  agreements from one field relabelled**, and they are arithmetic coincidences: the arms
  themselves contain no shift, only `mov $imm,%al; mov %al,0x197(%di)`. What remains is to
  confirm `>>2` against a real drawn tile — **G18**. See also the `CONFLICT` note in
  `asm/0AD3_combat_special_attack_dispatch.asm`. This is the deliberate line from the
  project plan: art names yes, type names not yet.
* **No asm file was renamed.** The tile-art table lives inside
  `asm/72A0_sub_72a0.asm`, which spans `72A0..8386` and is a mixed region of code,
  strings and data — calling it `tile_art_table` would be a wrong name, and a wrong
  name is worse than none. The names went to `disasm/tile-art.tsv` and the headers
  that own the behaviour instead.
* **No HP value is verified against a live session.** § 6 promoted the nine
  overworld figures to *measured* from the binary's own immediates, which is stronger
  than a capture of the same bytes — but the townsfolk (Guard, Jester, Merchant,
  Cleric, King) have no arm, so their HP is still third-party and the Guard
  255-vs-500 conflict is still open. See **G17**.

## 8. Conflicts on the record

* **art `0B` — CLOSED 2026-11-23, and it was a creature all along.** The dispute above read it as
  "a doorway set in a magenta field" and `disasm/tile-art.tsv` carried it as `kind: object`. The owner
  read the tile and said what it is: **a sea serpent, mostly submerged.** The magenta speckles are
  water, not decoration; the white is the back surfacing, then the neck, the head, and an open jaw.
  Grade **recalled (owner)**, and the bytes agree with it independently, which is the part worth
  writing down:
  * it is placed **only in harbour sectors** — `monx31` Port Bonifice (3 slots), `monx32` New San
    Antonio (2), `monx41` Pirates Harbour (2), and nowhere else in the pack;
  * every one of those 7 slots is **exclusively kind `0x08`** with **HP fixed at 255** and flag `0x01`.
    Every other placed creature shows both `0x08` and `0x70`; Minax and King show only `0x70`. So `0x2c`
    sits apart from the land creatures in exactly the way a water creature should;
  * the type byte `0x2c` **is** art `0x0b`, because `art = map_byte >> 2`. The first pass at this
    missed that and reported `0x2c` as an unidentified creature type, because the lookup filtered the
    tile table on `kind=creature` and `0x0b` was misclassified as `object`. **A wrong classification in
    a generated table hides the fact it contradicts.**
  The name is now `Sea serpent` with `Sea Monster` as an alias. **HP remains an open conflict**: the
  game's own byte is 255, the third-party figure is 64, and the game wins. Do not "fix" the engine to
  match the wiki.
* **"Sea Monster" and "Sea serpent" are one creature, and the join was counting case variants as two
  names — ruled 2026-11-23.** The Codex's "Sea serpent" sat in the never-matched list while Gamer
  Corner's "Sea Monster" held the matched tile, so the correct name and the correct tile were in
  separate rows and neither looked complete. `Carrion Creeper` and `Carrion creeper` were likewise
  counted as two names because the comparison was case-sensitive. **Fold case before treating names
  as distinct.** The consequence is a headline number: the tool's `9 names never matched a tile` is
  really **7** — Carrion creeper, Ghost, Goblin, Gremlin, Kings, Pirate ship, Viper.
* **Those seven are not failures of the join, and four of them cannot match by construction.** Owner,
  2026-11-23: Carrion creeper, Ghost, Goblin, Gremlin and Viper are **dungeon-only** creatures. Dungeon
  sectors do not use `monx` at all (`dungeon-35-live` matched `monx35` at 0/27) and the dungeon view is
  first-person wireframe, so **there is no sector-map tile for them to match against** — the bestiary
  join was matching against sector art and could never have named them. `Kings` is not unmatched
  either, see the next bullet. That leaves `Pirate ship` as the one genuinely open name, and it is
  filed as a decode, not a capture.
* **Kings: four placements, three castles, and the king/queen pair is in the bytes.** Owner recalled
  kings in a few castles, mostly Lord British, with one other king/queen pair. Measured, all HP 255:
  `monx23` Castle of Lord British (Earth 1423 BC) slot 8; `monx33` Castle of Lord British (Earth 1990
  AD) slot 8; **`monx93` CASTLE BARATARIA (Planet X) slots 10 and 11 — two King-sprite creatures side
  by side**, which is the pair. **A shared sprite can be two creatures**, which is the same shape as
  the pirate-ship claim below and the reason "same graphic" is not evidence of "same creature".
* **The creature count, settled: 15 creature tiles, 14 placed monster types, and the difference is the
  horse.** After `0x0b` was reclassified, `tile.tsv` has 15 `kind=creature` rows and the `monx` type
  bytes give 14 distinct creature arts. The extra is **art `0x11`, "quadruped, head at left, four legs
  and a tail — horse candidate"**, which is never placed as a monster because it is placed as a
  **vehicle** (`census.py --what vehicle` lists the horses). Every placed type now has a creature tile,
  and the one creature tile without a placed type has an explanation. That is the invariant worth
  keeping: **placed creature arts ⊆ creature tiles, with the difference explained row by row.**
* **Mounts are the reason "creature" and "vehicle" are not disjoint.** Owner: the frigate is a mount;
  a **pirate ship** is the same picture used by a creature that roams the water and fires cannon, and
  can be boarded with a tassel, which takes it over, or fought. The bytes say the frigate's map byte
  `0x48` is **never** a monster type in any real `monx` file, and all 64 tiles have **distinct**
  bitmaps, so "same graphic" cannot mean two art indices with identical pixels — it has to mean one
  art index used two ways. The reconciliation is already in this project's own findings: `monx` holds
  **placed** monsters, and `docs/monster-spawn.md` § 3 closed the townsfolk question by establishing
  that **wandering spawns come from spawn arms instead**. A roaming creature would therefore have no
  `monx` type at all, which is exactly what is observed. **Filed as a decode of the spawn arms in
  `asm/`, not a capture.**
* **Guard HP — RESOLVED by observation, 2026-10-22.** Codex 255, Gamer Corner 500.
  `castle-03-live` has type `0x60` (Guard) at HP **255**, so the codex is right and
  500 is wrong. Note the route here: first "resolved" by a byte-width argument, then
  reopened when a compound run read 255 for everything, then settled properly by the
  existing capture corpus.
* **Merchant HP.** Codex 5–50, Gamer Corner 1. Unresolved.
* **`bestiary.tsv` contains byte-identical duplicate rows, and `creature.tsv` inherits them —
  found 2026-11-23.** `art=0B` "Sea Monster" appears **three times, identical in all thirteen
  columns**: same `art_index`, same source, same `image`, same `sha256`. Nothing in the pipeline
  checks for it, so every table generated from it re-imports the defect. Measured on
  `disasm/census/creature.tsv`: 120 rows, **16 distinct `art` values**, 24 distinct names, 36
  distinct `(art, name)` pairs, and **no combination of up to three columns is unique**. `source`
  and `grade` are constant across all 120 rows, so they carry no information at all.
* **What that actually means: `creature.tsv` is not a creature table.** Of the eight upstream rows
  matching "Sea Monster", three are the duplicates above and **five have no `art_index` at all** —
  they are Codex rows for the Sea Serpent on `c64`, `dos`, `dos-fan-upgrade`, `fm-towns` and
  `pc88-starcraft`, each with its own bitmap and a `verdict` like `near-49` or `animated (2 frames)
  — not a single tile`. Those are **art-matching candidates**, the evidence trail for which
  platform's sprite best matches a DOS tile index. `tools/census.py` copies bestiary rows verbatim
  and drops `section`, `variant`, `image`, `sha256` and `note` — the only columns that distinguished
  them — so five pieces of evidence arrive looking like five creatures. The 120 rows are roughly 36
  creatures multiplied by their matching evidence.
  **Ruled 2026-11-23:** the fix is to separate the two things that are currently one table — a
  creature table with one row per art index, keyed by `art`, and an art-variant table keyed by
  `art + variant`, which is what this data actually is. De-duplicating `bestiary.tsv` and giving it
  a uniqueness assertion comes first, because a source file with silent duplicates re-infects every
  table generated from it. See `docs/plan-record-index.md` § 7, slice S-1.
  **S-1 landed the same day.** `disasm/census/creature.tsv` is now **15 rows, one per art index,
  keyed by `art`**, and `disasm/census/art-variant.tsv` is **118 rows keyed by (name, section,
  source_variant, variant, image)** holding the platform sprites, including the 94 that matched no
  tile at all. `tools/bestiary_match.py` collapses byte-identical rows at generation and **refuses
  to write the file if any rows remain that differ only in a column the table does not carry**; six
  writer controls prove that refusal can fire. A range whose endpoints agree is now normalised
  (`224-224` is 224), which resolved one conflict as pure notation and left five genuine ones visible
  in a `conflicts` column instead of hidden in duplicate rows: Daemon and Devil `special`, Guard `hp`
  255 vs 500, Jester `hp` 1 vs 1-255, Merchant `hp` 1 vs 5-50, Wizard `special`.
* **The silent pick was propagating a value a capture had already disproved — found while splitting.**
  `census.py` built its art index with `{c['art']: c for c in crows}`, so for art `0x18` the last
  duplicate won and `tile.tsv` published Guard HP as **500** — the Gamer Corner figure, while the two
  bullets above record that `castle-03-live` observed **255** and that the Codex is right. Neither the
  capture nor the resolution was at fault; the join simply never looked at the disagreement. The tile
  table now reads `255 | 500` with the field named in `conflicts`, which is honest rather than right,
  and closing it properly means the capture, not a rule. **A dict comprehension over duplicate keys is
  a conflict resolver that never announces itself.**
* **`census.py` had no `__main__` guard, so importing it rewrote the committed census.**
  `bot/launch.py` does `import census` to read the rocket pad, and the bare `main()` at the bottom of
  the module fell through to `write_tables()` with the default `--out`. Every bot launch regenerated
  all eleven tracked tables as a side effect — and a `--check-fresh` gate run after such an import
  compares a fresh build against a fresh build, so **the freshness gate could be disarmed by import
  order**. Guarded now: importing is a read, only the CLI writes. `census.py --selftest` is wired into
  `check_all` and asserts uniqueness for all eleven tables through a declared `KEYS` table, replacing
  the single hand-written sector check that had been the only uniqueness assertion in the tool.
* **The codex HP/DMG columns are not HP and DMG in two of the four tables.** Those
  tables carry a second header row (`Min`, `Max`) written without `colspan`, so in
  the raw HTML the two sub-labels land under *Name* and *Apple II* and cannot be
  positioned from markup. Reading the pair as HP+DMG is a guess, and the guess is
  wrong: Orc is `17` and `153` in those columns, and Gamer Corner's single HP column
  reads `17-153` for the same creature. So in Townsfolk and Creatures in Dungeons the
  pair is an **HP range** and DMG is absent — and the rows ship **one more cell than
  the header has columns**, because HP occupies both of those cells. A flat
  header→cell map therefore mislabels every column after HP: Balron's level range
  arrived as its `Special`. The row width is what settles it — `sum(spans) ==
  len(cells)` only closes if HP spans two — and Gamer Corner agrees independently.
  `bestiary_fetch.py` now detects the extra cell from row width and spans HP, so
  dungeon Balron reads `HP 209-249`, `Location 13–16`, `Special Puts enemies to
  sleep`. Two readings of this table were wrong in two different directions before
  that, which is the argument for measuring row width instead of trusting a header.
* **`0x1D` and `0x1E`.** Blank and solid art respectively. They are not creatures and
  cannot name anything, whatever a naive comparison says.

## 9. Licensing and the cleanroom wall

Every image in `bestiary/cache/` is a copyrighted rip of game art, in seven platform
variants, and `bestiary/view.html` embeds all of it plus our extracted tiles. Both
are gitignored and must never be committed — same rule as `original/` and
`tmp/tiles/`. What is committed is our own derived table and this document: facts plus
attribution, no bitmaps.

This is also the largest single pile of original art the project has ever held, which
makes it a cleanroom hazard. `bestiary/cache/`, `bestiary/view.html`,
`bestiary/bestiary.tsv` and `bestiary/matches.md` are all added to the forbidden list
in `docs/tile-art-spec.md` § 2 and `docs/cleanroom-procedure.md`. Whoever draws
substitute art may not read any of them. The bestiary answers *what is this creature
called*; the spec answers *what must the tile communicate*; they must not mix.

## 10. Next

1. **Townsfolk HP needs a live read, not more static work.** Every write site of
   `[DI+0x177]` is enumerated and none of them can be a town NPC spawner (§ 6),
   including the `asm/3611` lead that was raised and retracted. Read `[DI+0x177]` for
   a Guard or Merchant in a town and the question closes either way.
2. Validate `asm/147F_dungeon_monster_spawn.asm`'s arithmetic against a live dungeon
   monster and check it against the `17 + 32k` / `153 + 32*(k//2)` fit (**G17**).
3. Settle whether `[DI+0x1B7]` (sprite) and `[DI+0x197]` (type) are always equal —
   at spawn they take the same immediate, which is why both fold to the right art
   (**G18**). Correct the "thief is monster type `0x40`" claim in
   `docs/spice86-live-findings.md`; the thief is `0xFC`.
4. Decode the dungeon stroke table and identify dungeon creatures from their drawn
   outline (**G19**, also T9).
5. Find real DOS sprites for the five dungeon creatures whose codex images are crops.
6. Cross-check the item-drop claims (Fighter drops torches and helms, Guard drops 2
   keys, Wizard drops staves and wands) against the kill-reward code in
   `asm/2D8C_combat_kill_reward.asm` and friends.
