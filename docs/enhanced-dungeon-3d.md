# Enhanced-Mode 3D Dungeon — Plan (not started)

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

---

## 1. Geometry spike result

The spike asked: can our existing dungeon geometry reproduce the FM Towns perspective,
or does a 3D renderer need a new projection? **Answer: the projection is already fully
determined by the wireframe we ship.** The FM Towns renderer fills the same nested depth
rectangles our wireframe outlines.

### 1a. Our wireframe depth frames (measured, not eyeballed)

Captured with the `U2_DG_LINES` diagnostic seam (`engine/src/game/dungeon.c`) on an
all-open corridor facing east, in 320x200 CGA logical space (origin `DG_ORIGIN` 16):

| depth d | x range | y range | width | height |
| --- | --- | --- | --- | --- |
| 0 (near) | 16..271 | 16..143 | 256 | 128 |
| 1 | 80..207 | 48..111 | 128 | 64 |
| 2 | 112..175 | 64..95 | 64 | 32 |
| 3 | 128..159 | 72..87 | 32 | 16 |
| 4 | 136..151 | 76..83 | 16 | 8 |
| 5 | 140..147 | 78..81 | 8 | 4 |
| 6 | 142..145 | 79..80 | 4 | 2 |

Each depth **halves** the frame toward a fixed point. The exact model:

```
VP          = centre of the near frame = (143.5, 79.5)
frame(d)    = VP + (near_frame - VP) * 2^(-d)
```

Verified: near left x=16, VP.x=143.5 → d1 left = 143.5 + (16-143.5)*0.5 = 79.75 ≈ 80;
near right x=271 → d1 right = 207.25 ≈ 207. The convergence point the lines approach
(≈143,80) is the near-frame centre.

Note the near-frame centre (143.5, 79.5) is **not** the screen centre (160, 100): the
corridor sits high and left because the CGA status/message band owns the bottom of the
screen. An enhanced renderer must place the vanishing point at the **map-window centre**
and scale the near frame to the window, not reuse the CGA offset.

### 1b. The FM Towns view uses the same model

Measured on `FMtownsu214.jpg` (640x480): the map-window interior is roughly x[8..448]
y[30..420], and the ceiling/wall convergence (black-region centroid) lands at ≈(237,222),
which is within a few percent of the window centre (≈228,225). Same centre-converging
nested-rectangle projection, at the FM Towns window size, with the trapezoids between
consecutive depth frames filled with wall texture instead of outlined.

### 1c. What this means for the renderer

The corridor geometry does not need inventing. A 3D renderer is the wireframe's depth
frames with the regions between them **filled**:

- **Wall faces**: the trapezoid between `frame(d)` and `frame(d+1)` on each side, textured
  with a wall pattern. Which side is a wall and which is open comes from the cell scan
  (§ 4), exactly as the wireframe already decides which edges to draw.
- **Far wall**: the innermost frame filled (the end of the visible corridor).
- **Floor / ceiling**: the bands below / above the corridor, textured (the FM Towns floor
  is a perspective grid; ceiling is dark).
- **Sprites**: monster / chest / ladder stamped at the depth frame the object sits in,
  scaled to that frame. Our `dg_sprite_page` is already depth-aware.

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
   measured, not guessed. The straight-corridor projection is already pinned (§ 1).
2. **Dungeon-art extraction (medium).** Locate and decode the FM Towns dungeon/tower
   GRAPH banks (wall textures, floor, ceiling, monster sprites). The 4bpp decode exists;
   the unknowns are which banks hold dungeon art and the sprite geometry/sizes. The
   current crosswalk (`disasm/fmtowns-crosswalk.tsv`) maps overworld and towns only and
   would grow a dungeon section.
3. **The renderer (the big piece).** A new enhanced dungeon renderer: per depth, fill the
   wall trapezoids with a textured column rasteriser, fill floor and ceiling, stamp the
   object sprite at its depth. Reuses `dg_scan` and the depth-frame formula from § 1.
4. **Licensing (a decision, § 6).** FM Towns art is copyrighted and `references/` is
   gitignored, so it cannot ship. Two honest routes, below.
5. **Gate.** Render into an enhanced frame and assert wall quads and a stamped sprite are
   present and no wireframe lines remain; red-controlled by disabling the 3D path.

Rough size: the rasteriser plus art decode is medium-to-large. The topology, layout, and
projection are free.

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
   a known tower if available).
3. A second spike: extract the dungeon GRAPH banks and confirm the wall-sprite set and
   the tower-vs-dungeon question (§ 5) against the real art.
4. Only then write the renderer against § 1's projection and § 4's face set, with the
   § 3.5 gate.

This stays its own plan with its own owner ruling, exactly as `enhanced-layout-plan.md`
§ 10 anticipated. The wireframe (CGA) and the scaled wireframe (classic EGA) are
unaffected; this adds a third renderer behind the existing per-mode dispatch.
