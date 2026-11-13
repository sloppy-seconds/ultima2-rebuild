# FM Towns Dungeon Rendering — Problem Brief (input to a decompile effort)

> **RETIRED 2026-11-14 (owner ruling).** The enhanced dungeon takes the
> procedural route: our own raycaster and our own generated art, shipped in
> `engine/src/game/dungeon3d.c` and `engine/src/game/dungeon_assets.c` — see
> `docs/enhanced-dungeon-3d.md`. This brief and the bank decodes it fed are
> preserved on branch `archive/fmtowns-dungeon-route`. Nothing here is current
> intent on main.

**Status:** assembled 2026-11-13. This is a *problem statement + evidence pack*, not a
solution. Its purpose is to hand a focused reverse-engineering effort everything it needs
to produce the **dungeon rendering codex** — the exact spec of how the FM Towns Ultima II
dungeon view is drawn from the GRAPH banks — which we then implement in
`engine/src/game/dungeon3d.c`.

---

## 1. Why we want this (the economic argument)

The enhanced-mode 3D dungeon can be built two ways:

- **Our design (shipped, `d78ad75`):** a flat-shaded perspective corridor that samples one
  brick tile across trapezoid walls. It looks good but is *not* what the FM Towns port
  does, and it forces a **cleanroom redraw of every dungeon sprite** — monsters, chests,
  ladders, doors — because we would be inventing the look, not reusing it.
- **Faithful blit (this brief):** the FM Towns port blits **pre-projected tiles** that
  already exist on the disc, one per (side, depth). Reusing them means the monsters,
  chests, ladders and doors come from the disc for free — no cleanroom art wall.

The owner ruled the art route is **disc-supplied** (reuse the FM Towns art), so the
faithful blit is the economical path. The blocker is that we do not yet know the exact
tile-selection and blit-geometry rules. That spec is the codex.

## 2. The model (owner's decode, 2026-11-13, browsing the banks)

The FM Towns dungeon does **not** texture-map a flat tile. It blits pre-projected
perspective segments:

- **DUNG1** = wall segments (raw 32x32, 64 tiles, all ink=1.00 - solid brick, no
  transparent background, so the shading/wedge shape is the signal), graded **inferred**
  (visual read). A vertical column of the bank is one wall placement stepping through
  depth: tiles **27, 35, 43, 51, 59** (column 3) recede, then **36, 44** (column 4) and
  **45** (column 5) as further depth pieces; the **horizontal mirror** of each is the
  right-wall tile at that depth. Groupings measured 2026-11-13: **flat frontal brick**
  (end-caps / dead-end walls facing you) #1,2,4,5,6,7,39,40; **bright near side-wall**
  segments (closest depth, lit cyan) #8,9,16,17,32,48; **receding side-wall wedges**
  (blue wall against the black corridor void, stepping through depth) #18-31, 33-38,
  41-63; **arched / corner end-caps** #62,63; **dark unlit variants** #0,3,10-15. The
  exact `index -> (placement, depth)` table is for the codex to make authoritative.
- **DUNG2** = doors, at different sizes and sides (pre-projected per depth). Measured
  2026-11-13 from the decoded bank (raw 32x32, 64 tiles, all ink=1.00 - it is the solid
  ornate wall bank, no transparent background), graded **inferred** (visual read): doors
  are pre-projected for **placement** and **depth**. **Front-facing** archways (symmetric,
  at corridor's end): #2 (red handle dot), #3, #10, #11, #40, #41, #46, #47. **Side-wall**
  doors (set into a receding wall, the blue wedge being the perspective-projected side
  surface, left/right as mirror pairs): #12-15, #22-31, #32-39, #56-63. The same door/arch
  recurs small->large across the bank (far->near). Extras: #20/#21 framed wall panels, and
  #18/#19/#49/#54 have a figure in the doorway. The exact `index -> (front/left/right,
  depth)` table is for the codex to make authoritative.
- **CHEST** = chests and ladders, at different sizes (per depth). Measured 2026-11-13
  from the decoded bank (raw 32x32, 64 tiles), graded **inferred** (visual read + ink
  coverage, not a decoded table): chests ramp by size across **#1 (tiny) -> #2-10
  (medium) -> #11-23 (full-frame biggest)**; ladders and perspective frame pieces
  (blue rails + orange rungs, the blue *diagonals* #48-60 being the receding-depth
  projections) fill **#24-60**; blanks are **#0, 30, 39, 44-47, 53, 61, 62**; **#31** is
  an unexplained white/cyan blob. Unlike the monsters, this size progression lives in
  one bank's indices, not three LOD tiers. The exact `index -> (object, depth)` table is
  still for the codex to make authoritative.
- **DUNMON1 / DUNMON2 / DUNMON3** = the **same 26 monster sprites at three level-of-detail
  tiers**: 128x128 (near), 64x64 (mid), 32x32 (far). Verified 2026-11-13: bank 0 of each is
  the same knight sprite at 128/64/32. The renderer picks a tier by the monster's on-screen
  size (depth), so DUNMON3 is the *distant* tier, not poor art. Each bank is 26 sprites,
  4bpp, TIFF-wrapped (see § 4).

This is corroborated by the lossless FM Towns screenshots (§ 4): first-person corridor,
textured receding walls, sprite monsters scaled by distance.

## 2b. The dungeon map files differ (DOS vs FM Towns) — measured 2026-11-13

The two versions do **not** share a dungeon map format. This reframes what the codex must
answer.

| | DOS (what the engine reads today) | FM Towns |
|---|---|---|
| container | embedded in `mapxNN` (sector whose ones-digit is 4/5) | 7 separate named files: `DARDIN/FIRE/M/MINE/P/PERINIAN/TIME.DG3` |
| size | 4096 B = 16 pages of 16x16 (`floor*256+y*16+x`, `asm/68C0`) | 2048 B = 8 pages of 16x16 |
| cell encoding | bitmask: `0x80` rock, `0xC0` doorway, `0xE0` fake wall, `0x40` chest, `0x20` ladder bit | small integers 0..15, dominated by `0` and `2` (histogram of `M.DG3`: 0x00 x898, 0x02 x804, then 4/1/7/12/3/6/15/14/5/9) |

Consequences:

- **We do not adopt their grid format.** Our engine already reads the DOS `mapx` bitmask and
  already resolves each cell to a *feature* (wall / doorway / chest / ladder / monster /
  depth). What we need from the FM Towns binary is therefore **not** "how it parses `.DG3`"
  but "given a feature at a depth, which tile + which screen rectangle."
- **The small-int grid is a hint the FM Towns renderer is a direct value->tile lookup**
  (`tile = table[cell]`), a simpler structure to find and read than bit-testing. So the codex
  narrows to a compact **feature -> tile-index -> geometry** table we apply to our own
  already-decoded DOS features.
- **The named `.DG3` files are a cleaner tower-vs-dungeon discriminator** than sector numbers:
  compare a tower file (likely `M` or `P`) against a dungeon file to settle § 3.7.

## 3. What the codex must specify

A complete answer lets us render a frame from the dungeon grid + creature list with no
guessing. It must give, for each of the ~3-7 depth steps:

1. **Wall:** which DUNG1 tile for the left wall at depth d, and the mirror rule for the
   right wall; the screen rectangle (x, y, w, h) to blit it into at that depth.
2. **End cap / dead end:** which tile fills the far wall when the corridor stops.
3. **Door:** which DUNG2 tile per (side, depth, open/closed) — note the FM Towns port has
   closed doors, which the DOS original does not (`docs/enhanced-dungeon-3d.md` § 0c).
4. **Chest / ladder:** which CHEST tile per (kind, depth).
5. **Monster:** how a creature type maps to a sprite index (0..25), and the **depth -> LOD
   tier** rule (which of DUNMON1/2/3 at which depth), plus the y-offset for placing it.
6. **Ceiling / floor:** flat colour or tiles? (The screenshots show a dark ceiling and a
   blue floor; whether those are tiles or fills is unresolved.)
7. **Tower vs dungeon:** the owner's hypothesis is two art sets. DUNG1 (blue brick) vs
   DUNG2 (yellow/olive, ornate) are two sets; which is the tower is unconfirmed.
8. **Draw order / occlusion:** far-to-near, and how nearer segments overwrite farther ones.

## 4. Evidence available

**Disc:** `references/fmtowns/Ultima Trilogy 1-3 (1990)(Origins)(Jp-En).img` (141,649,200
B, ISO-9660 over raw MODE1/2352 sectors). Readable with the bespoke reader in
`tools/fmtowns_xwalk.py` (`find_disc`, `disc_files`, `decode_bank`); 7z cannot parse it.

**FM Towns Ultima II program (the decompile target) — CONFIRMED:** `ENCHANT.EXP`
(1,072,398 B). Its own strings read `Ultima ][  - Revenge of Enchantress -` and
`Roster data:ULTIMA II for FM-TOWNS`; the trilogy mapping is confirmed by the siblings'
strings too (`DARKNESS.EXP` = "Ultima I - The First Age of Darkness", `EXODUS.EXP` =
"Ultima III - EXODUS"). The file starts with magic `4D 50` ("MP"), the FM Towns executable
format. **The FM Towns CPU is a Motorola 68020 (68k), not Z8000** — Ghidra (which REA drives)
has first-class 68000 support, so route B is a normal native RE task, not an exotic-ISA
gamble. The MP header means it must be loaded as a raw 68000 image at its base, not
auto-parsed.

Not the renderer: `DUNGEON.EUP` (9,466 B) is **Euphony music** (magic `EUPHONY`), not code.
The seven `*.DG3` files (2048 B: DARDIN, FIRE, M, MINE, P, PERINIAN, TIME) are **dungeon
level data** (the grid the renderer reads), not the renderer. See § 2b.

**GRAPH banks (decode recipe) — two formats, measured 2026-11-13:**

- **Raw tile banks** (`DUNG1`, `DUNG2`, `CHEST`): header starts `00 00 …`, then 32x32 tiles
  at 4bpp packed, **high nibble = left pixel**, indices 0..7, palette `k_pal` (index 0 =
  black = transparent pen). 64 tiles each.
- **Monster banks** (`DUNMON1/2/3`): header starts `49 49 2a 00` = the **little-endian TIFF
  magic**. The IFD describes one sprite (BitsPerSample=4, FillOrder=2 = **low nibble = left**,
  one strip at byte 512), but the file holds **26 sprites** concatenated after it, at
  128x128 / 64x64 / 32x32 respectively. **`tools/fmtowns_xwalk.py:decode_bank` is wrong for
  these** — it assumes 32x32 high-nibble tiles, so it mis-slices DUNMON1/2 and reverses the
  nibble order for all three. Any faithful monster render must special-case the TIFF magic
  and the per-bank sprite size. (The earlier "DUNMON3 = viper/chest/blobs" tile reading was
  this bug producing garbage, not real sprites.)

**Bank browser:** `/var/tmp/dng_browser.py` serves labelled, indexed grids of the dungeon
banks (`--serve --port N`), k_pal palette. Used to produce the owner's tile reading.

**Lossless FM Towns screenshots** (1280x960, 4-bit palette PNG, from the live emulator via
`/var/tmp/ju2s.sh` on jarvis): `/var/tmp/u2-chamber.png` (clean straight corridor, best
for geometry), `u2-thief.png`, `u2-viper.png`, `u2-orc.png`, `u2-cyclops.png`,
`u2-bat-chest.png`, `u2-skeleton.png`, `u2-shot-161521.png` (closed door),
`u2-door-open.png` (same door open), `u2-shot-160402.png` (ladder to overworld).

**Live FM Towns emulator:** on jarvis; `/var/tmp/ju2s.sh <out.png>` screenshots the window.
This is the instrument for the empirical route (§ 5A).

## 5. Two routes to the codex

**A — Empirical capture (tractable, no exotic ISA).** Drive the live FM Towns dungeon to
known positions/facings, screenshot each, and match bank tiles to the screen rectangles
they fill. The straight-corridor frames (`u2-chamber.png`) already show the receding wall
segments; a handful of controlled frames at known depths pins the per-(side,depth) tile
table and the blit geometry. Downside: it infers the table from behaviour, so edge cases
(doors, towers, unusual creatures) need targeted captures.

**B — Decompile the FM Towns binary (authoritative, now tractable).** Disassemble
`ENCHANT.EXP` and read the dungeon-drawing routine directly. The FM Towns CPU is a
**Motorola 68020**, which Ghidra supports well, so this is a large but ordinary native
reverse-engineering task. Load `ENCHANT.EXP` as a raw 68000 image at its MP base. If it
lands, it answers § 3 exactly, including the tower discriminator.

Recommendation: **B is now the primary route** (68020 is Ghidra-friendly, target confirmed),
with **A** as corroboration for anything static analysis cannot pin (e.g. the tower
discriminator if it turns out to be data-driven rather than code).

## 6. Current engine state (what the codex replaces)

`engine/src/game/dungeon3d.c` — flat-shaded corridor, viewport-relative depth frames
(`dg3_frame`, pinhole `s(d)=1/(d+1)`, `U2_DG_DRAW_DEPTH=7`), trapezoid fills, one sampled
wall tile from `ctx->dng_wall`. The faithful renderer replaces the trapezoid fills with
per-(side,depth) tile blits driven by the codex; the `u2_dg_scan` topology (which faces are
solid) is reused unchanged. The theme already bakes `DNGWALL.TILE` (`fmtowns.c`); the codex
will grow that into a dungeon tile set (walls/doors/chests/ladders/monsters).

## 7. Open questions for the owner / capture

- Which bank is the **tower** vs the **dungeon** (needs a known-tower capture).
- Are ceiling/floor **tiles** or **flat fills**?
- The **closed-door** state: the FM Towns port has it, DOS does not — do we mirror the port
  (owner earlier ruled open-archway for the flat renderer; a faithful blit may revisit this).
- Why do FM Towns dungeons have **8 pages** (2048 B) where DOS has 16 (4096 B)? Fewer floors,
  or a denser encoding? Affects whether the depth model transfers.
- Which `.DG3` is the **tower** (likely `M` or `P`)? Cross-check against the DUNG1-vs-DUNG2
  bank question in § 3.7.
- The **depth -> LOD tier** rule: at what on-screen size does the renderer switch between
  DUNMON1 (128), DUNMON2 (64) and DUNMON3 (32)?
