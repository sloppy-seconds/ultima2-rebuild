# FM Towns Dungeon Rendering — Problem Brief (input to a decompile effort)

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

- **DUNG1** = wall segments. A vertical column of the bank is the left wall receding
  through depth steps: tiles **27, 35, 43, 51, 59** (column 3, rows 3-7), then **36, 44**
  (column 4) and **45** (column 5) as further depth pieces — about **3 steps of corridor**
  per view. The **horizontal mirror** of each is the right-wall tile at that depth.
- **DUNG2** = doors, at different sizes and sides (pre-projected per depth).
- **CHEST** = chests and ladders, at different sizes (per depth).
- **DUNMON1 / DUNMON2 / DUNMON3** = monster sprites (416 / 104 / 26 tiles), presumably
  indexed by creature type and drawn at a depth-dependent scale.

This is corroborated by the lossless FM Towns screenshots (§ 4): first-person corridor,
textured receding walls, sprite monsters scaled by distance.

## 3. What the codex must specify

A complete answer lets us render a frame from the dungeon grid + creature list with no
guessing. It must give, for each of the ~3-7 depth steps:

1. **Wall:** which DUNG1 tile for the left wall at depth d, and the mirror rule for the
   right wall; the screen rectangle (x, y, w, h) to blit it into at that depth.
2. **End cap / dead end:** which tile fills the far wall when the corridor stops.
3. **Door:** which DUNG2 tile per (side, depth, open/closed) — note the FM Towns port has
   closed doors, which the DOS original does not (`docs/enhanced-dungeon-3d.md` § 0c).
4. **Chest / ladder:** which CHEST tile per (kind, depth).
5. **Monster:** how a creature type maps to a DUNMON* tile, and the depth→(scale, y-offset)
   rule for placing it.
6. **Ceiling / floor:** flat colour or tiles? (The screenshots show a dark ceiling and a
   blue floor; whether those are tiles or fills is unresolved.)
7. **Tower vs dungeon:** the owner's hypothesis is two art sets. DUNG1 (blue brick) vs
   DUNG2 (yellow/olive, ornate) are two sets; which is the tower is unconfirmed.
8. **Draw order / occlusion:** far-to-near, and how nearer segments overwrite farther ones.

## 4. Evidence available

**Disc:** `references/fmtowns/Ultima Trilogy 1-3 (1990)(Origins)(Jp-En).img` (141,649,200
B, ISO-9660 over raw MODE1/2352 sectors). Readable with the bespoke reader in
`tools/fmtowns_xwalk.py` (`find_disc`, `disc_files`, `decode_bank`); 7z cannot parse it.

**Likely FM Towns Ultima II program (the decompile target):** `ENCHANT.EXP` (1,072,398 B).
The disc is "Ultima Trilogy 1-3" and the three large `.EXP` programs map to the trilogy by
subtitle — `DARKNESS.EXP` (U1, First Age of Darkness), `ENCHANT.EXP` (U2, Revenge of the
Enchantments), `EXODUS.EXP` (U3). **This mapping is a hypothesis to confirm** (strings /
entry point), not measured. Also relevant: `DUNGEON.EUP` (9,466 B) and seven `*.DG3` files
(2048 B each: DARDIN, FIRE, M, MINE, P, PERINIAN, TIME) which look like dungeon level data.

**GRAPH banks (decode recipe):** 512-byte header, then 32x32 tiles at 4bpp packed, high
nibble = left pixel, indices already in 0..7. Palette `k_pal` (index 0 = black =
transparent pen). Counts: DUNG1 64, DUNG2 64, CHEST 64, DUNMON1 416, DUNMON2 104,
DUNMON3 26.

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

**B — Decompile the FM Towns binary (authoritative, hard).** Disassemble `ENCHANT.EXP`
(and `DUNGEON.EUP`) and read the dungeon-drawing routine directly. FM Towns is **Z8000**
(16-bit Zilog); Ghidra's Z8000 support is limited, so this is a substantial, uncertain
effort. If it lands, it answers § 3 exactly, including the tower discriminator.

Recommendation: start with **A** (cheap, reuses the emulator we already drive), fall back to
**B** for the parts A cannot pin (tower discriminator, exact creature index).

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
- Confirm `ENCHANT.EXP` is the U2 program before committing to route B.
