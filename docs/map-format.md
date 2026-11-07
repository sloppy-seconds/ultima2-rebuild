# Sector map files: format, authoring, and the probe harness

How to read, modify, and synthesise Ultima II sector maps, and how to place the
player to test them. Written up because the knowledge is cheap to lose and this
will be needed again for every remaining terrain question.

Related: `docs/passability-findings.md` (F11, F12) for what each byte means,
`docs/tooling-procedures.md` for driving the emulator.

---

## 1. Hard rule

`original/` and `references/` are the ground truth and are **never written to**.
Modified maps go into a per-run copy under `tmp/<timestamp>/`, which is what
`tools/spice86_start.sh` creates each launch. `tools/u2_mkprobe.py` refuses any
output path containing `original/` or `references/`.

## 2. File layout

Sector maps are `original/mapxNN`, one per sector, 33 of them, all exactly
**4224 bytes**:

| range | size | meaning |
| --- | --- | --- |
| `0x000..0xFFF` | 4096 | the `64x64` tile grid, row-major |
| `0x1000` | 1 | always `0x1A` - a header marker, present in all 33 files |
| `0x1001..0x107F` | 127 | mostly zero; 25 distinct variants across the 33 sectors, purpose unproven |

### Cross-checked against the Apple II build

The Apple II reference disks hold the same sector maps as `M/AP.NN`, 4096 bytes each
(`docs/apple2-reference.md` § 8). This is an independent encoding, produced by a different
port, so it is a real gate on the model above rather than a second reading of the same bytes.

* **The `64x64` row-major grid is confirmed byte for byte on 31 of 33 sectors.** Not a
  re-derivation of the same file: a different serializer, same bytes.
* **Sectors `10` and `15` are the two exceptions, and they are content differences, not
  format differences.** Sector `10` differs in exactly one cell, index 543 (`x=31, y=8`),
  where DOS has `0x08` and Apple has `0x24`. Sector `15` differs in 260 cells confined to
  `y=59..63`, the southern edge, where Apple has `0x80` and DOS has grass or forest. Two
  single-tile and edge-only deltas across 31 matching sectors is what a content revision looks
  like, not a layout mismatch. Do not chase them as a decoder bug.
* **The `0x1000..0x107F` tail does not exist on Apple at all**, and the marker is not
  map-specific: `monxNN` (384) and `tlkxNN` (384) are each exactly 128 bytes longer than the
  Apple `ON.NN` and `TLK.NN` (256), and `monxNN[256] == 0x1A` in all 33 files and
  `tlkxNN[256] == 0x1A` in all 15. So the tail is a **DOS-port container artifact appended to
  every per-sector file**, not part of Garriott's map format. That narrows what those 127
  bytes can be: they cannot be original map content, and the per-file-type blocks differ from
  each other, so they are not one shared per-sector structure either. Still unproven; now
  bounded.

Tile lookup, verified against a live frame at 199/200 cell agreement:

    tile(x, y) = data[y * 64 + x]        # x, y in 0..63

The `0x1A` marker is easy to misread as a rare terrain byte, because it appears
exactly once per file. It is not in the grid. Do not treat it as a tile.

## 3. Byte semantics

Every byte that occurs in a grid is a multiple of 4, `0x00..0xFC` - exactly 64
values - and the tile atlas has exactly 64 entries. So:

    art_index = map_byte >> 2

`art_index` indexes `tmp/tiles/tile_<NN>.png` (hex-named `00`..`3F`), produced by
`tools/tile_atlas.py` from the executable. Regenerate with
`python3 tools/tile_atlas.py` if the atlas is missing.

Confirmed terrain, each measured by walking (see F11):

| map byte | art | terrain | walking |
| --- | --- | --- | --- |
| `0x00` | 0 | water, magenta waves | **blocks** |
| `0x04` | 1 | faint sparse dots | passes |
| `0x08` | 2 | grass, sparse cyan dots | passes |
| `0x0C` | 3 | forest, cyan double-dash | passes |
| `0x10` | 4 | mountain, white diamond lattice | **blocks** |
| `0x14`..`0x28` | 5..A | `%`, four-pane, tower, gate, lattice, cross | passes |

Four bytes out of 256 are proven by hand-walk; the rest of the table came from
the probe harness and is less thoroughly cross-checked. The *rule* is still
unknown - only instances of it.

Note `0x08` grass renders as nearly nothing. A "blank" cell in a screenshot is
real terrain, not a missing tile.

### 3a. `0x04` re-proven live, with controls (2026-10-26)

`0x04` came back into question because the engine's default-deny passability table had no row for
art `0x01` and so silently blocked it, contradicting the row above. Rather than argue from that row,
the verdict was re-measured against real cells in `mapx10`, with the tile read back from the live
grid *before* each step so no verdict could be cast against a cell the game does not contain:

| target | cell | step | result | food spent |
| --- | --- | --- | --- | --- |
| `0x04` shallow | (21,12) from (20,12) | East | **PASSABLE** | 19 |
| `0x08` grass (positive control) | (14,4) from (14,5) | North | **PASSABLE** | 19 |
| `0x00` open sea (control) | (14,3) from (14,4) | North | **BLOCKED** | 19 |
| `0x10` mountain (control) | (35,5) from (35,6) | North | **BLOCKED** | 19 |

The controls are the reason the shallow answer is usable. A harness that returns PASSABLE for
mountain and open sea would also have returned PASSABLE for `0x04` and proved nothing, which is
exactly how an earlier probe sweep managed to report forest as impassable. Method: player position
poked to the stand-spot, one arrow press, position and the food odometer read either side.

Two things fall out for free. `0x04` is confirmed passable on foot, and the asymmetry with the
creature step gate stands - `asm/2199` refuses `{0x10, 0x04, 0x00}` for land creatures, so the
player wades `0x04` and a land monster will not, which is what makes it shallows rather than sea.
And every step above spent exactly 19 food including the two refused ones, which independently
re-confirms the charge-before-verdict finding in `asm/2423_passability.asm`.

## 4. The world wraps

The viewport is toroidal, not clamped. With the player at `x=0`, the left edge
shows `x=55..63`. Verified by seeing the atlas `X Y Z` glyphs (map bytes
`0xF0..0xF8`) on the left of the screen while parked at `x=0`.

Practical consequence: scripted walks that assume a flat grid desynchronise
silently near an edge. **Start probe walks near the middle of the map.**

Viewport geometry: 20x10 tiles, 16x16 px each, player at screen cell `(9,5)`,
status band below.

## 5. Authoring a probe map

Real sectors make passability testing slow and incomplete: rare bytes occur 2-7
times and are often walled off behind water or mountains, so you cannot reach
them to step into them. A synthetic map puts every byte next to walkable ground
in one sector, so a single walk measures the whole table.

    python3 tools/u2_mkprobe.py -o /var/tmp/tp/mapx20.probe \
        --probe-y 31 --corridor-y 32

This fills the grid with grass, then lays one probe tile per byte value
(`0x00..0xFC` step 4) along row `y=31` at `x=0..63`, leaving row `y=32` as a
clear grass corridor. Stand on the corridor, step Up into a probe, step back.

Deploy it by overwriting the map inside a **run copy**, after the emulator has
started but before the game loads the sector (the map is read when the character
enters the sector, i.e. after pressing `P`):

    tools/spice86_start.sh --headless --name probe --player <save> --patch-arrows
    # parse "run dir <path>" from its output, then:
    cp /var/tmp/tp/mapx20.probe <run dir>/mapx20

To render a map to a PNG for eyeballing, paste `tile_<byte>>2>.png` per cell -
see the recipe in this file's history, or `tools/map_render.py`.

## 6. Placing the player: edit the save, do not poke memory

Covered properly in `docs/save-format.md`. The two facts that matter for map work:

* **Writing `DS:00/01` live does not teleport.** The write lands and reads back,
  but movement then stops working entirely - it is a mirror, not the source.
  Edit the save and reload instead: `python3 tools/u2_edit.py setpos <save> X Y`.
* **Sector is two decimal digits** at `DS:49/4A` (patched into the filename
  `MAPXFF`), so sector 20 is the pair `2,0`, and a numeric read gives 512.
  `tools/u2_edit.py set <save> sector 71` handles the split. Verified: digits
  `1,0` boots into sector 10.
  **The ones digit is also the site kind** — 0 wilderness, 1/2/3 towns (every `tlkx`
  dialogue file sits in 1/2/3), 4 tower, 5 dungeon/cave. Not a contradiction between
  this note and the site-kind reading in `phase4-implementation.md`; they are the same
  fact. See `docs/sector-rulesets.md` § 1.

Since the save record is `DS:36..135` verbatim, a probe map plus a teleport can
put the character on any tile of any sector without walking there.

**Sector is not stored in the save.** The game tracks it separately, so this
places you within whatever sector you land in; it cannot move you between
sectors. Cross-sector work still requires walking off a map edge.

## 7. Verifying a result

Two independent signals, both cheap:

1. **Screenshot** via MCP `screenshot` (needs the patched vendored build; see
   `docs/tooling-procedures.md`). Copy it out of `/tmp/spice86-mcp-screenshots/`
   into `tmp/screens/` to look at it.
2. **Position read** from `DS:00/01` before and after each key. A move that did
   not change position was blocked.

Cross-check the two. The probe harness reproduced the hand-walk results for
`0x00` and `0x10` independently, and that agreement is what made the table
trustworthy.

## 8. Traps

- **Memory reads pause the CPU.** `WhilePaused` in Spice86 pauses for every HTTP
  memory read and resumes afterwards, but keys sent immediately into that window
  are silently lost. Settle ~0.5-1 s after a read before injecting a key, and
  verify every step rather than assuming it landed.
- **`/api/memory/<addr>/range/<n>` is read-only.** Writes use
  `PUT /api/memory/<addr>/byte` with body `{"value": N}`, one byte at a time.
- **The MCP screenshot tool needs the patched build.** Stock upstream returns raw
  PNG bytes where base64 is required and the client rejects it.
- **`pgrep -f Spice86` matches its own shell.** Check ports instead. The same trap kills a
  `pkill -f gate_watch.py` dead: the invoking shell's own command line contains the pattern, so the
  signal lands on the shell and the command exits 143 with the watcher unharmed. Use the bracket
  trick — `pgrep -f 'gate_watc[h]\.py'` — or kill by PID.
- **The stamped grid at `DS:1800` is the `mapxNN` grid shifted DOWN two rows.** Measured 2026-10-29:
  comparing 4096 live bytes against `mapx20` byte-for-byte gives 1020 disagreements; applying a 2-row
  shift leaves 8, and those 8 are the genuine runtime stamps (creatures, and a time door). Anything
  that diffs live state against the file must apply the shift first or it will report a mostly-fake
  delta. This is also the origin of the `(x,25)` vs `(x,23)` discrepancy in
  `docs/contract-overworld-walk.md`.
- **The grid holds small state values, not art indices.** `0x00`, `0x08`, `0x0c`, `0x10`, `0x20` are
  the common ones. `0xC0` appears only when the game stamps a time door, so a census of the file for
  `0xC0` says nothing about what the live grid will contain.
- **Never run the game in place.** It rewrites its own `player` file; always use
  a run copy.

## 9. mapx has creature art baked into the terrain, and the game clears it

Found 2026-10-29 while looking for the cause of the replay differ's creature-position
divergence, and it is a better answer than the one being chased.

`mapxNN` is not pure ground. At every cell where `monxNN` places a creature, the map byte
**is the creature's own art byte**. Observed in sector 03, all 32 live slots: the raw mapx
byte at the spawn cell equals that slot's `type` (`0xf4`, `0xfc`, `0x3c`, `0x60`, ...). The
map was authored by drawing the creature into it.

The original does not treat that byte as terrain. Once a creature leaves its spawn cell the
cell reads `0x70` - art `0x1C`, town paving, walkable - in every case measured. Measured on
a fresh sector-03 session: 22 of 32 spawn cells, and the set is identical across two
different player key paths and stable across 24 turns.

Two mechanisms fit and this data does not separate them:

* **departure writes `0x70`**, or
* **load clears the baked art to `0x70`** and a creature still standing there re-stamps its
  own type over it.

They are indistinguishable here because the creatures never left the paving: of all cells ever
occupied across 24 turns, **zero** were non-spawn cells whose mapx byte was not already `0x70`
(1886 of 4096 cells in sector 03 are `0x70`). Settling it needs a sector where spawn cells sit
on non-paving ground. Grade: the `0x70` outcome is **measured**; which mechanism produces it is
**open**.

**Closed 2026-11-08, and it is neither mechanism.** The two candidates were framed as a choice
between *departure writes `0x70`* and *load clears to `0x70`*, and that framing is what hid the
answer: **the original restores a per-slot saved terrain byte, and `0x70` was the paving case.**
`asm/2233_sub_2233` is the departure site — `mov 0x1b7(%di),%al` + `mov %al,(%bx,%si)` writes
`DS:1B7[slot]` back into the grid cell — and `asm/0CEE_spawn_tile_store` fills that byte at spawn
from the rolled cell's own terrain, returned by `asm/50B3_map_tile_ptr` one instruction earlier.
No `movb $0x70` into the grid exists anywhere in the corpus, so the constant is ruled out by the
bytes and not by inference, and the Apple II twin (`LDA $4D80,X ; LDY #$0091 ; STA ($06),Y`) agrees
independently.

The shipped data corroborates it. Across all 33 sectors and 511 live `monx` slots, the column at
`monx[128..159]` — which `engine/src/core/state.h` calls `mon_kind` and glosses as "kind index" —
takes exactly two values, `0x70` (355 slots) and `0x08` (156), and for 469 of 502 baked slots it
equals a real unbaked neighbour's terrain. It is a terrain byte, and it is only ever paving or land
because that is where authored creatures stand. Sector 03 is a paving sector, which is exactly why
restoring the slot byte was indistinguishable from writing a constant there, and exactly why § 9
could not separate the two from that sector alone.

Two consequences, neither applied yet. **`mon_kind` is misnamed**: it is the saved terrain, and the
spawn arms' `cmp` on `[DI+0x1B7]` is a terrain test, not a kind dispatch. And
**`u2_monsters_clear_spawn_cells` hard-codes `k_clear = 0x70`** where the original restores the
slot's own byte, so it is right in towns and wrong in the field — the 2026-11-06 "only clear a baked
cell" guard reaches the right outcome for the wrong reason.

**And now observed, not just read off the bytes.** `captures/spice86/g66-departure/` loaded a
character into sector 00 — land, `DS:1B7 = 0x08` for every creature, so the restore and a constant
`0x70` are distinguishable there in a way they were not in paving sector 03 — ran the creature sweep,
and read the live grid either side of it. Over 8 clean departures the vacated cell became the slot's
own terrain byte **8 times out of 8**, with zero `0x70` and zero ghosts. The two engine consequences
above were fixed the same day and shipped.

### Why this matters for the engine

`u2_world_load_sector` reads mapx verbatim into `map_base`, and `u2_monsters_restamp` rebuilds
`w->map` from `map_base`. So the engine keeps the baked creature art as terrain, and every art
index seen at sector-03 spawn cells (`0x0d`-`0x0f`, `0x1a`, `0x3c`-`0x3f`) is **impassable** in
`u2_tile_passable`'s default-deny table.

Two consequences, both real:

1. **The engine leaves a copy of the creature behind.** When a creature walks off its spawn cell,
   the restamp restores the baked art byte, so the sprite reappears on empty ground. This is the
   owner's report "he left a copy of him behind", which was earlier attributed to the per-pass
   stamp bug. That bug was real and is fixed, but it is not this. Grade: inferred from the code
   path, not yet screenshotted.
2. **The spawn cells are walls in the engine and walkable paving in the original.** Creatures
   that wander back over their spawn cell move legally in the original and are blocked in the
   engine - same rolls, different outcome. This is the first concrete mechanism found for the
   replay differ's "same budget, different assignment" symptom.

### Done

`u2_monsters_clear_spawn_cells` (engine/src/game/monsters.c) rewrites each spawn cell in **both**
`map` and `map_base` at sector load, called from both overworld entry paths right after the monx
load. Clearing only `w->map` would be the same mistake in a different place: `u2_monsters_restamp`
rebuilds from `map_base`, so the baked art would come back on the creature's first step.

Gated in `engine/tests/monsters_check.c` with four assertions - fixture starts as baked art, every
in-range spawn cell becomes `0x70`, cells with no creature behind them are untouched, and after a
creature leaves, restamp restores paving rather than its old sprite. Control run: setting the
clear value to `0x00` turns the second and fourth red, restoring turns them green.

The test's own guards had to match the implementation's (`cell < U2_SECTOR_TILES`, `type != 0`,
all 32 slots). It did not at first, and the fixture's slot 31 sits at `(0,0)` with a live type -
the same `(0,0)` shape as the grid phantom fixed earlier - so a loop stopping at slot 31 reported a
legitimate clear as collateral damage. A test that disagrees with the code about what a slot is,
is the test's bug.

### Settled by seeding: `0x70` is written by the game, not read from the file

The generator question had a cheap answer that needed no second sector. `spice86_start.sh
--seed-map` installs a synthetic `mapx03` before launch; the version used here rewrote all 32
spawn cells from their baked creature art to `0x0C` forest, leaving everything else alone. A live
recording on that seeded map then showed **13 of 14 vacated spawn cells reading `0x70`**, not the
`0x0C` the file had been given.

So the clear value is a game constant, not terrain-derived, and not `monx kind` either (sector 21's
`kind` array is all `0x08`; sector 03's is all `0x70`; neither predicts the other sector). That is
what makes the engine fix safe to write from one sector: whichever of the two mechanisms produces
it, the terrain under a creature's spawn cell is `0x70`.

Still open, and now harmless: whether the write happens at load or on departure. `docs/map-format.md`
§ 9's engine fix clears `map_base` once at load, which serves both.

### Attempted and invalid: the sector-21 route

Sector 21 (forrest town, 2394 forest cells, **zero** `0x70` cells in mapx21) is the right
discriminator: if vacated spawn cells there read `0x70`, the clear is a constant; if they read
`0x0C`, it is the surrounding ground. The attempt failed and the failure is worth recording.

`u2_edit.py set <save> sector 21` writes the two decimal digit bytes, and the recorder then
reported `sector: 00 03` - the game loaded sector 03 regardless. Two separate mistakes
compounded:

* the first attempt edited a copy of `original/player`, which is a **blank character** (name all
  spaces), so there was no valid save to honour the field;
* the second used the real character and still landed in sector 03, and the tell was that the
  recorder's final line was **byte-identical across runs with different key counts**
  (`rng 62 62 4f c9 96 68`, `pos 1a 2f`, `sector 00 03` for both a 6-key and a 24-key script).

The invalid recording was compared against `mapx21` anyway and produced a confident-looking
absurdity: 3448 of 4096 cells "differing", and a `mapx byte -> many grid values` mapping that
looked like a broad load-time transformation. It was none of that - it was sector-03 grid data
measured against the wrong terrain file. Caught only because the sector field was checked after
the fact. **Check the recording is in the sector you think it is before analysing it against a
sector's mapx.**

So: no conclusion about sector 21, and the mechanism and the generality of `0x70` remain open.
Whatever method is used to reach a non-paving sector must first be proven to change the sector at
all - the save's sector pair is not sufficient on its own.

## 10. The grid is re-derived on entry; it is written back only by an outdoor save (2026-10-29)

§ 9 established that `mapx` carries baked creature art. The follow-on question was whether the
game ever writes the grid back after mutating it — paving an opened door, clearing baked art,
stamping a creature. **It does, but only on a save, and only outdoors.** Corrected the same
evening it was first written: an earlier draft of this section said the game never writes the
grid, which was measured from transitions and from a `Q` that had silently been refused.

The full shape, from `captures/spice86/save-wilderness` and the noVNC session:

| event | sector files on disk |
| --- | --- |
| leaving or entering a site | untouched — `mapx31`, `monx31`, `mapx30`, `monx30` byte-identical to pristine |
| `Q` outdoors, `DS:6D == 0` | **written**: the stamped grid and the live creature table |
| `Q` inside a town | refused, `ONLY OUTDOORS!` |

What an outdoor save wrote in sector 30 answers § 9 directly: three cells changed, each one a
cell a creature was standing on, written as the creature's **type byte** (`0x0C -> 0x30`,
`0x0C -> 0xFC`, `0x08 -> 0x30`). The saved grid is the stamped grid. So the baked creature art
that § 9 found in the shipped files is not an authoring quirk — it is what a saved game looks
like, and the shipped `mapx` files are somebody's save. That closes the question of why the
files contain art the renderer draws as creatures.

Two things this settles for the engine:

* **Within a visit the grid is durable.** Opened doors survived ~38 cells of creature churn.
  The original does not rebuild the grid from a pristine copy on a creature turn, and the
  engine must not either — which is why `u2_world_terrain_put()` is the correct call for a
  terrain edit that should last the visit (`asm/435E` now uses it).
* **Across a visit nothing survives unless you saved outdoors first.** Re-entering a town
  re-reads the file, so an opening closes. A town can never be saved, so a town can never
  persist — that is the design, and the refusal message is the evidence.

The engine's `map_base` is an engine construct serving § 9's problem (telling baked art from
terrain at load), and it must not be read as a model of the original's memory.

**Resolved 2026-11-08 — the engine no longer rebuilds on a creature turn.** The bullet above was
true of the engine when it was written and the engine was in breach of its own instruction:
`u2_monsters_restamp` rebuilt all 4096 cells from `map_base` on every creature turn, exactly the
thing this finding forbids. That call is now removed (`overworld.c`, step 2c-ii of
`docs/demo-record-playback.md § 13.10`), and the engine patches one cell on departure and one on
arrival, which is what `asm/2233` and `asm/225A` do.

`map_base` itself **stays**, and not for the reason its header gave. The rebuild was its stated
justification and the rebuild is gone; what actually keeps it alive is `u2_world_ground_at` feeding
the EGA compositor at `frame.c:308`, which needs the ground under a sprite. That is an enhancement
the original never needed — DOS draws one art index per cell — so `map_base` is now extra machinery
the bytes are silent about rather than a divergence from them, which is a materially better place to
be. Two rebuild sites remain deliberately: the sector-load paths, and the kill path, where the terrain
byte is zeroed by the record clear before the site runs and a targeted restore would write water into
the cell. Both are documented at their call sites.
