# Enhanced-Mode 3D Dungeon — procedural route (shipped round 1)

Status: **round 1 shipped 2026-11-14.** Owner ruling that day, on rethinking the
enhanced build: rather than reuse FM Towns art, the engine draws its **own
procedurally generated art** with its **own raycaster**. The FM Towns
faithful-blit decode effort is retired to branch `archive/fmtowns-dungeon-route`
(kept in case it is ever picked back up); the DUNG1/DUNG2/DUNMON bank decodes
and the blit tile/rect rules live there, not on main.

What shipped (gated by `make test-cp7`, 30 checks in `tests/dungeon3d_check.c`):

* `engine/src/game/dungeon3d.c` — a DDA raycaster (per-column walls, textured
  floor/ceiling cast, torch falloff bands, z-buffered billboard sprites) that
  renders into `u2_frame` like every other pixel the engine makes, so the
  terminal backends, the demo recorder and the headless tests all still see it.
  Movement stays instant, like the original.
* `engine/src/game/dungeon_assets.c/.h` — 32x32 palette-indexed textures and
  sprites generated at startup from intent, deterministic, Apache-2.0 ours.
  The generator must never read the forbidden list in `docs/tile-art-spec.md` § 2;
  that cleanroom wall is what makes this shippable where an FM Towns rip is not.
* Solidity follows the render-stop rule, not the walk rule: bit 7 draws solid
  except the open doorway `0xC0`; the fake wall `0xE0` draws exactly like rock,
  because finding it by walking is the mechanic.

Round 1 art is agent-gated placeholders good enough to explore by. Round 2 is the
owner's call after exploring in-game: better generators, or commissioned art
(ComfyUI or similar) dropped behind the same `dungeon_assets` interface.

The rest of this document is the original plan and the FM Towns observations that
motivated a 3D view; where it discusses the FM Towns banks as the art source,
read that section as archived context, not current intent.

---

## Old status (superseded 2026-11-14, kept for the reasoning)

Status: **plan only, nothing built.** This is a feature, not a fidelity target: no
shipped DOS original draws a textured 3D dungeon, so nothing here is byte-proven and
none of it may be cited as evidence about `original/ultimaii.exe`. It is downstream of
the art-mode-aware dungeon dispatch that the EGA tier introduced (see
`docs/enhanced-layout-plan.md` § 10), and it needs an owner ruling on scope and the
art-licensing route before any code is written.

What this document is: the design, the geometry spike result, the workstreams, the
licensing decision, and the open questions. It deliberately does not quote the FM Towns
binaries as a source of truth about the DOS game; the FM Towns port is a different
program that reads the same data files.

---

## 0. What the FM Towns dungeon actually is (observed)

The FM Towns port (1990, FLAIR) reimplemented the dungeon as a **first-person
perspective corridor with textured walls, a perspective floor and ceiling, and monster
sprites stamped at depth** — Dungeon-Master / Eye-of-Beholder style pseudo-3D. This is
no longer an assumption: it is visible in the screenshots already on disk under
`references/fmtowns-screens/` (third-party, gitignored, captions in `_CAPTIONS.tsv`):

| file | shows |
| --- | --- |
| `FMtownsu214.jpg` | straight corridor, blue textured walls converging to a vanishing point, perspective floor grid, dark ceiling, a green coil monster sprite at depth, `HEAD: North` compass |
| `FMtownsu215.jpg` | corridor with a golden humanoid sprite |
| `U2FMTownsSS10.jpg` | L14 corridor, blue-grey figure |
| `U2FMTowns5.jpg` / `U2FMTowns7.jpg` | L13 / L15 corridors, yellow and winged-white creature sprites |
| `U2FMTowns13.jpg` | a **flat** room ("P TEST LAB" / "CONTROL", green floor, red striped walls, cone objects) — not a perspective corridor |
| `U2FMTowns34.jpg` | Minax's castle interior (brick halls, a blue pool) — a third wall art set |

Two things fall out of this that shape the whole plan:

1. The normal dungeon is a **perspective corridor**, and the HUD is essentially our
   enhanced layout already (map window left, stats top-right, message log bottom-right,
   compass, HP bar). The enhanced layout was modelled on this screen.
2. There is **more than one wall art set** (dungeon stone, the flat lab room, Minax's
   brick castle), which supports the owner's suspicion that towers and dungeons differ
   (§ 5).

### 0b. Lossless captures (2026-11-13, live FM Towns emulator)

A set of **lossless 1280x960 4-bit-palette PNGs** was captured from the running FM Towns
build (window grab, exact palette, no JPEG dithering). These are the correct reference for
the art inventory; they are on disk only (not committed — copyrighted, and `references/`
is gitignored). Frames seen: a ladder back to the overworld, an orc at a ladder wall, a
straight corridor with a green door on the left and a thief at depth, a coiled viper at
depth, a long corridor opening into a chamber, a closed door then the same door opened, a
bat with a chest, a skeleton, and a cyclops.

**The palette** (4-bit, ~10 entries): 0 black (ceiling), 2/4 blue (wall bricks / border /
floor), 8 cyan (bright wall bricks), 6 white (text + brick highlights), 3 red, 7 yellow,
9 green, 5 teal (HEAD box), 1 pale-yellow (title). Walls are blue/cyan brick on a black
ceiling with a blue floor.

**Monster / object sprites observed** (distinct art per creature, each a full-colour sprite
scaled to its depth ring and standing on the floor plane): orc, thief, viper, bat, skeleton,
cyclops. Objects: chest = red/green striped box on the floor plane; ladder = orange runged
sprite on a dead-end wall; door = green panel (closed) / open frame. (Per-sprite pixel
footprints were *not* measured: the wall bricks carry white/yellow highlights from the same
palette, so colour segmentation cannot separate a sprite from wall chrome without a
transparent-background source. Footprints come from the decoded bank, § 3.2, not from these
frames.)

### 0c. The FM Towns dungeon has closed doors; the DOS original does not

Frame-proven: after stepping forward into a closed door the message log reads
`Opened the door!` and then the creature behind it acts (`Cyclops attacked!`). So in the FM
Towns port a dungeon door is a **closed cell that forward opens** — a two-step: face it,
forward opens it, forward again walks through.

**This is a port addition, not DOS behaviour.** The DOS dungeon grid (`docs/map-format.md`
§ 3c) has no closed-door state: `0xC0` is an open doorway the walk gate passes in one step,
and `0xE0` is a fake wall that also passes (it only *looks* closed). Nothing in
`original/ultimaii.exe` opens a door inside a dungeon — the only door the DOS game opens is
the overworld site door `0xA0` via `cmd_O`/UNLOCK (`asm/435E`), which paves it with the
player's own ground at the cost of a key. The owner's recollection of the original (no
closed dungeon doors) is correct and matches the bytes.

Consequence for this plan: the closed-door mechanic is a **design choice** for the enhanced
renderer, not a fidelity requirement. If enhanced mode adopts FM Towns closed doors it must
add a door-open state the DOS grid does not have; if it stays DOS-faithful it renders `0xC0`
as an open archway and walks through in one step. This is an owner decision alongside § 6,
and it is the kind of port-vs-original divergence the plan exists to surface rather than
absorb silently.

---

## 0d. Dungeon banks located and decoded (2026-11-13, disc-supplied route)

Owner ruled the art route **disc-supplied (FM Towns)** (§ 6, route A). The dungeon GRAPH
banks were enumerated off the disc (`tools/fmtowns_xwalk.py`'s ISO reader) and decoded with
the existing GRAPH format — 512-byte header, 32x32 tiles at 4bpp packed, high nibble left.
No new decoder was needed; the overworld reader's format is identical.

| bank | tiles | content (rendered, `k_pal` palette) |
| --- | --- | --- |
| `DUNG1` | 64 | dungeon walls — blue/cyan brick on black; matches the corridor screenshots |
| `DUNG2` | 64 | a **second wall set** — yellow/olive walls with ornate doors and archways |
| `CHEST` | 64 | red/green striped chest sprites + blue/yellow perspective frame pieces |
| `DUNMON1` | 416 | dungeon monster sprites (large set) |
| `DUNMON2` | 104 | monster sprites |
| `DUNMON3` | 26 | monster sprites (viper, chest, blobs, wizard, …) |

Two things settled and one trap recorded:

* **Palette.** The banks decode correctly with the reader's `k_pal` (indices 0..7; index 0
  is black and acts as the sprite transparent pen, matching the overworld `transparent_index
  = 0`). The trap that cost a first pass: a lossless dungeon *screenshot*'s PNG colormap order
  is **not** the bank index order (the capture tool reorders its colormap), so mapping bank
  indices through the screenshot palette produced saturated red/yellow noise. `k_pal` is the
  correct palette; the screenshot palette is not.
* **Two wall banks exist.** `DUNG1` (blue brick) and `DUNG2` (yellow/olive with doors) are
  visibly different sets, which is the first hard evidence behind the tower-vs-dungeon
  hypothesis (§ 5). Which bank is the tower is still unconfirmed — it needs a known-tower
  capture or the FM Towns binary's bank-selection table, not a guess.

Still open in this workstream: the per-depth wall geometry and convergence rate are not read
off these banks yet (they are flat 32x32 texture tiles, not pre-rendered perspective
columns), so `r` (§ 1b) is not settled by them either. The renderer will texture-map these
flat tiles onto the viewport-relative frames rather than blit pre-projected columns.

**Model correction (owner, 2026-11-13, browsing the banks).** The line above is wrong. The
FM Towns dungeon does **not** texture-map a flat tile. It blits **pre-projected wall
segments, one per depth step**, and mirrors them for the right wall. Reading DUNG1: a
vertical column (tiles 27,35,43,51,59) is the left wall receding through depth steps, then
36,44 and 45 are further depth pieces; ~3 steps of corridor per view. DUNG2 is **doors**
at different sizes and sides (pre-projected per depth). CHEST is **chests and ladders** at
different sizes (per depth). So a faithful renderer blits a per-(side,depth) tile table, it
does not fill trapezoids with a sampled texture. The flat texture-map shipped in step 3
(`d78ad75`) is therefore **our design**, not FM Towns-faithful — it stays valid as a
look, but it is not what the port does. Pinning the exact tile->(side,depth,position) table
needs either the FM Towns binary's dungeon renderer (Z8000, a large decode) or an empirical
capture: walk the live FM Towns dungeon to known depths, screenshot, and match bank tiles to
screen rectangles.

**Correction after labelling the bank (2026-11-13).** DUNG1 is not all flat tiles. Tiles
~1-9 are flat brick (blue and bright-cyan variants); tiles ~10-23 are the same brick
**pre-skewed into perspective** (receding diagonals baked into the tile). So the FM Towns
renderer may blit pre-projected per-depth wall segments rather than texture-map a flat
tile. That is a genuine step-3 fork, not settled: (a) texture-map a flat brick tile across
the wall quads (fits the current trapezoid-fill architecture, Wolfenstein columns), or
(b) blit the pre-projected tile for each depth/face (matches what the bank seems to carry,
but reworks the fill into a blit). Confirming which the original does needs the FM Towns
binary's tile-index table, not a guess. Floor/ceiling tiles in DUNG1 are not yet identified.

---

## 1. Geometry: viewport-driven, not wireframe-driven

**Correction to an earlier spike.** A first pass derived the projection from the CGA
wireframe we ship and concluded the geometry was "already determined." That was wrong, and
the owner caught it. The wireframe's near frame (256x128 inset at origin 16) and its
off-centre vanishing point (143.5, 79.5) are **CGA artifacts**: the corridor was squeezed
into the top of the 320x200 screen because the status/message band owns the bottom, so the
vanishing point drifted up and left and the rings were crammed into a 128px-tall band.
Enhanced mode has a clean 448x448 map window with nothing inside it, so the corridor should
fill that window with the vanishing point at its centre. The projection is a design choice
parameterised by the viewport, not a quantity to inherit from the wireframe.

### 1a. What the original actually fixes

Only one geometry constant is inherited from the original: the game draws **7 depth frames**
(`U2_DG_DRAW_DEPTH = 7`, `engine/src/game/dungeon.h`). How far you can see down a corridor
is seven cells. Everything else — vanishing point, near-frame size, convergence rate — is
ours to choose for the viewport.

### 1b. The viewport-relative model

In the enhanced map window (448x448 physical px at `U2_ENH_MAP_X/Y` = 0,0):

```
VP        = map-window centre = (224, 224)
near      = the window inset by a margin m   # m keeps the nearest wall edges off the border
frame(d)  = VP + (near - VP) * s(d)          # d = 0..6
```

`s(d)` is the depth scale, `s(0)=1` (the near frame fills the window) shrinking to a small
far frame at `d=6`. Two candidate forms:

- **Geometric**: `s(d) = r^d`. Simple; `r` is one knob. The CGA wireframe used `r=0.5`, but
  that value was chosen to fit 7 rings into 128px and is *not* the right value for a 448px
  window — with a taller window you can afford a gentler `r` (more depth visible at a
  readable size).
- **Pinhole**: `s(d) = 1/(d+1)`. A true perspective (linear in 1/z), which recedes more
  realistically than a fixed ratio and is probably closer to what the FM Towns corridor
  looks like.

The convergence rate (`r`, or the pinhole choice) is a **design parameter to fit to the FM
Towns reference**, not inherited from the wireframe. The screenshots on disk are dithered,
upscaled JPEGs and are too lossy to measure it reliably — a ceiling/wall-edge line fit on
`FMtownsu214.jpg` gave an inconsistent vanishing point (the black ceiling and the dark floor
gaps both read as "black"), so no number is quoted here. Settling `r` needs a cleaner
reference: a lossless FM Towns capture, or reading the depth spacing straight off the
decoded dungeon bank (§ 3.2). This is a capture/extract task, not a guess.

**Update (2026-11-13): the lossless captures did not settle `r` either, and no number is
quoted.** The vanishing point is confirmed at the **map-window centre** (the corridor
converges to the middle of the box — visually unambiguous, and consistent with the model
above). But none of the captured frames is a clean empty corridor running to a far wall: the
chamber shot opens into a room, and the others have a monster, a door, or a chest at the far
end, so there is no near-frame-plus-far-frame pair to take a ratio from. A wall-edge line fit
on the lossless frames gave ceiling-fit and floor-fit vanishing points hundreds of pixels
apart — the signature of a bad fit (the brick texture puts cyan at varying heights, so the
cyan min/max per column is not the wall/ceiling boundary), not a real number. **`r` stays
open.** It should come from the decoded dungeon bank's depth spacing (§ 3.2) or a purpose-
captured empty corridor, not from these frames. A plausible-but-wrong `r` is worse than an
open parameter.

What the wireframe measurement *does* legitimately confirm (kept as a hint, not a spec):
the original renders the corridor as **nested rectangles converging to a single point**
(a geometric perspective), and it does so over 7 rings. That shape is worth keeping; the
CGA numbers are not.

### 1c. What this means for the renderer

Given the viewport-relative frames, the 3D view is those frames with the regions between
them **filled**:

- **Wall faces**: the trapezoid between `frame(d)` and `frame(d+1)` on each side, textured
  with a wall pattern. Which side is a wall and which is open comes from the cell scan
  (§ 4), exactly as the wireframe already decides which edges to draw.
- **Far wall**: the innermost frame filled (the end of the visible corridor).
- **Floor / ceiling**: the bands below / above the corridor, textured (the FM Towns floor
  is a perspective grid; ceiling is dark).
- **Sprites**: a monster / chest / ladder at depth `d` is stamped at the size of `frame(d)`,
  centred on that frame's floor line. Our `dg_sprite_page` is already depth-aware; only the
  placement math changes to the window-relative frames.

The genuinely new code is a **textured-trapezoid rasteriser** (fill a quad between two
depth frames with a wall pattern, columns scaled by depth — Wolfenstein-style vertical
strips are the simplest correct approach). That, not the geometry, is the cost of the
feature.

---

## 2. What we already have

- **Topology**: `dg_scan` samples left / centre / right cells per depth, and the dungeon
  grid bitmask (`0x80` rock, `0xC0` doorway, `0xE0` fake wall, `0x40` chest, `0x20`
  ladder bit — `docs/map-format.md` § 3c) says which face is solid. A sprite renderer
  reads the same scan the wireframe does; no new world model.
- **Layout + frame**: the enhanced 1024x512 frame, the 448x448 map window, and the panel
  already exist. The fit primitives added for the EGA dungeon (`u2_frame_pixel_fit` /
  `u2_frame_line_fit`) show the pattern for mapping logical geometry into the window.
- **A GRAPH-bank reader**: `engine/src/core/fmtowns.c` already opens the FM Towns disc,
  skips the 512-byte bank header, and unpacks 4bpp-packed tiles to palette indices. It
  currently pulls only the overworld/town banks (MAP2_1, ENEMY2, EXTRA); the dungeon
  banks are unread but the decode path is proven.
- **The mode dispatch seam**: `dg_set_transform` already branches CGA / classic-EGA /
  enhanced. A third renderer plugs into that same branch.

---

## 3. What it would take (workstreams)

1. **Evidence (cheap, owner-assisted).** A handful more screenshots to pin the geometry
   and inventory the art: a corner/turn, a T-junction, a ladder, a chest, and the same
   corridor at 2-3 depths so the wall-sprite set and the floor/ceiling treatment are
   measured, not guessed. Prefer **lossless** captures if you can get them — the dithered
   JPEGs on disk are too lossy to measure the convergence rate (§ 1b), and a clean frame
   would let the depth spacing be read directly.
2. **Dungeon-art extraction (medium).** Locate and decode the FM Towns dungeon/tower
   GRAPH banks (wall textures, floor, ceiling, monster sprites). The 4bpp decode exists;
   the unknowns are which banks hold dungeon art and the sprite geometry/sizes. The
   current crosswalk (`disasm/fmtowns-crosswalk.tsv`) maps overworld and towns only and
   would grow a dungeon section.
3. **The renderer (the big piece).** A new enhanced dungeon renderer: per depth, fill the
   wall trapezoids with a textured column rasteriser, fill floor and ceiling, stamp the
   object sprite at its depth. Reuses `dg_scan` and the viewport-relative frame model
   from § 1b (VP at window centre, 7 depths, convergence rate fit to the reference).
4. **Licensing (a decision, § 6).** FM Towns art is copyrighted and `references/` is
   gitignored, so it cannot ship. Two honest routes, below.
5. **Gate.** Render into an enhanced frame and assert wall quads and a stamped sprite are
   present and no wireframe lines remain; red-controlled by disabling the 3D path.

Rough size: the rasteriser plus art decode is medium-to-large. The topology and layout are
free; the projection is a small parameterised model but its convergence rate still needs a
clean reference to fit.

---

## 4. Wall-sprite set (hypothesis, to be confirmed by § 3.1)

The corridor is built from a small library of faces selected by the cell bitmask at each
depth, mirroring how the wireframe picks edges:

| cell condition (per side, per depth) | face drawn |
| --- | --- |
| rock (`0x80`) | solid wall trapezoid |
| open (`0xC0` doorway / corridor continues) | no wall (see through) |
| fake wall (`0xE0`) | wall that reads solid but is walkable |
| corner (side solid, centre open, next depth side open) | angled corner face |
| chest (`0x40`) | chest sprite at that depth |
| ladder bit (`0x20`) | ladder sprite / floor opening |
| closed door (FM Towns only, § 0c) | green door panel; **no DOS grid byte** — only if enhanced adopts the port's open-on-forward mechanic |

The exact set (straight, left corner, right corner, T-junction, end-cap, doorway) and
whether FM Towns uses per-depth pre-rendered sprites or a texture-mapped column
rasteriser is what the extra screenshots in § 3.1 must settle before the renderer is
written. Do not hardcode a set from one straight-corridor frame.

---

## 5. Towers vs dungeons (owner hypothesis, open)

The owner suspects towers and dungeons use different sprite sets. Supporting signals,
none conclusive:

- The FM Towns changelog (`original/updated/History.txt`) and the screenshot captions
  speak of "dungeon/tower" as a pair, and the screenshots show at least three distinct
  wall art sets (dungeon stone, the flat lab room, Minax's brick castle).
- **Now on record (2026-11-13, § 0d):** the disc carries two distinct dungeon wall banks,
  `DUNG1` (blue brick) and `DUNG2` (yellow/olive with doors). Two sets exist; which is the
  tower is still unconfirmed, so the claim is upgraded from *recalled* to *observed that two
  sets exist*, not yet *observed which is which*.
- The DOS side already distinguishes site kinds and carries `site_floor`, so a per-kind
  art selection has somewhere to hang.

**Not confirmed**: which screenshot is a tower, and whether the FM Towns art actually
differs by site kind or just by area. Closing this needs either (a) identifying a known
tower screenshot, or (b) extracting the dungeon/tower banks and seeing whether the wall
sets differ. Until then, treat "towers differ from dungeons" as a graded *recalled*
claim (owner memory) with a capture to convert it to *observed*. The renderer should key
wall-art selection on site kind from the start so a tower set can drop in without a
rewrite.

---

## 6. Art-licensing route (owner decision needed)

FM Towns art is not ours and cannot be committed. Two honest routes:

- **A. Disc-supplied (matches the existing pattern).** The engine reads the dungeon art
  from the player's own FM Towns disc at runtime, exactly like `--fmtowns-media-dir`
  already does for the overworld theme. Ships no art. Downside: the 3D dungeon only works
  for players who own and mount the disc.
- **B. Cleanroom substitute set.** Someone draws a wall/floor/ceiling/monster set from
  intent only, under the cleanroom wall (`docs/tile-art-spec.md`; the evidence files are
  forbidden to the artist). Ours to ship, works for everyone. Downside: real art labour,
  and it will not look like FM Towns.

Recommendation: **A for the first cut** (cheapest, reuses the disc path, and the feature
is enhanced-only and opt-in), with **B** as the eventual default so the feature is not
gated on owning a 1990 Japanese disc. This is the owner's call.

---

## 7. Recommended next steps

1. Owner rules on § 6 (art route) and confirms scope: enhanced-only, opt-in, feature not
   fidelity.
2. Owner grabs the § 3.1 screenshots (corner, T-junction, ladder, chest, 2-3 depths, and
   a known tower if available), lossless where possible so the convergence rate can be
   measured rather than guessed (§ 1b). **Partly done 2026-11-13** (§ 0b): ladder, chest,
   several monster types and a door pair are captured lossless. Still missing: a clean empty
   corridor to a far wall (for `r`), a corner/T-junction, and a known tower.
2b. Owner rules on the **closed-door question** (§ 0c): does enhanced mode adopt the FM Towns
   open-on-forward door (a new state the DOS grid lacks) or stay DOS-faithful (open archway,
   one-step walk)? This is a port-vs-original divergence, not a fidelity requirement.
3. A second spike: extract the dungeon GRAPH banks, confirm the wall-sprite set and the
   tower-vs-dungeon question (§ 5) against the real art, and fit the depth convergence rate
   (`r` or pinhole) to the bank's depth spacing — the lossless frames did not yield `r`
   (§ 1b update), so the bank is now the primary route to that number.
4. Only then write the renderer against § 1's viewport-relative model and § 4's face set, with the
   § 3.5 gate.

### 7a. Build order (owner-ruled 2026-11-13: disc-supplied route, open archway)

Owner rulings that fix the scope: art route = **disc-supplied (A)**; doors = **open
archway** (DOS-faithful, no new door state; the open-door screenshots give the archway
graphic to key on `0xC0`); convergence = **pinhole `s(d)=1/(d+1)`** as the design default
(the banks are flat texture tiles, so there is no FM Towns `r` to extract — it is ours).

Build order, gate-first, each step committed green:

**Status (2026-11-13): steps 1, 2 and 3 are shipped and gated; step 4 still needs a
known-tower capture.**

**Palette-context finding (resolves the step-2 question).** Under a v2 theme the SDL
uploader (`renderer_sdl.c` `sdl_upload`) maps every frame index through
`ctx->theme_palette` when `ctx->theme_palette_set` is true, and for the fmtowns theme that
palette *is* `k_pal` (PALETTE.RGB). So a DUNG1 tile's indices drop into the dungeon frame
**verbatim** — no `k_pal`→engine remap — provided the corridor textures only while the
fmtowns theme is active. The corollary the renderer must honour: its flat ceiling/floor/
end-cap indices are CGA-semantic today (1/2/3); under the fmtowns theme those same indices
read as blue/red/black, so the flat fills need to switch to `k_pal`-semantic indices when
`theme_palette_set`, exactly as the status-text ink already does (`frame.h` note on index 3
being black in the FM Towns palette). Step 2 is therefore: at startup, when enhanced +
fmtowns, pick the flat-brick DUNG1 tile, hand it to `u2_dungeon3d_set_wall_tex` (indices
verbatim), and make the flat fills palette-context-aware. The open pick is *which* DUNG1
tile is the wall (tiles ~1-9 are flat brick) and which bank is the tower — § 5.

1. **Geometry + flat-shaded corridor** (SHIPPED, `dd1f985`). New `engine/src/game/dungeon3d.c`: the
   viewport-relative frames (VP = map-window centre, 7 depths, `s(d)=1/(d+1)`), fill
   ceiling / floor / wall quads from `dg_scan`, stamp the depth sprite. Flat palette colours
   first (no texture yet) so the geometry and the dispatch are proven before art is wired.
   Wired behind `ctx->enhanced_layout` in `u2_dungeon_render`; CGA and classic EGA keep the
   wireframe untouched. Gate `dungeon3d_check.c`: render a lit corridor into an enhanced
   frame and assert large filled regions (ceiling + floor + at least one wall quad), the
   corridor converges toward the centre, and ink stays inside the map window (x < 448).
   Red-controlled by disabling the 3D path (falls back to wireframe → sparse lines → fails).
2. **Bank loader.** (SHIPPED, `63f645f`) Bake the wall tile into the theme at build time
   (owner ruling: bake, not a runtime ISO read). `fmtowns.c` reads `DUNG1` directly and
   decodes tile 1 (a flat brick) into `DNGWALL.TILE` (1024 k_pal indices) and adds
   `dng_wall = DNGWALL.TILE` to THEME.INI; `tiles_ega.c` parses the key and copies the
   tile into `ctx->dng_wall`; `dungeon3d.c` samples it verbatim (no remap — the uploader
   already maps frame indices through `theme_palette`). `fmtowns_theme.py` emits the
   identical bytes so the C<->Python byte-identity check stays green. Gate:
   `fmtowns_check` carries `DUNG1` in the synthetic ISO and asserts `DNGWALL.TILE` is
   DUNG1 tile 1 decoded. The floor/ceiling tiles and the tower-vs-dungeon bank choice
   (§ 5) are still open — step 4.
3. **Texture the quads.** (SHIPPED, `d78ad75`, Option A) Replace flat fills with the wall/floor/ceiling tiles from the
   loaded banks (vertical-strip column mapping, Wolfenstein-style). Gate: a wall quad shows
   bank texture, not a flat fill. *Shipped as a texture-mapping rasteriser that samples any
   32x32 indexed tile via `u2_dungeon3d_set_wall_tex`; the disc loader (step 2) supplies the
   real DUNG1 brick. Gate proves a checkerboard texture varies the wall column and flat does
   not.*
4. **Face set + tower art.** Key wall-art selection on site kind (§ 4, § 5) so `DUNG2` (the
   second set) drops in for towers once a known-tower capture confirms which bank is which.

The `dungeon_art_neutral.py` gate (CP7) asserts the dungeon draws no theme art; it stays
green through step 1 (flat colours are not theme art) and must be revisited at step 3 when
the corridor starts consulting the theme atlas.

---

This stays its own plan with its own owner ruling, exactly as `enhanced-layout-plan.md`
§ 10 anticipated. The wireframe (CGA) and the scaled wireframe (classic EGA) are
unaffected; this adds a third renderer behind the existing per-mode dispatch.
