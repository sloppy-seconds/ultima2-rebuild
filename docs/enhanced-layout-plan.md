# Enhanced-mode screen layout — plan, not yet implemented

Status: **plan**. No engine code has been changed for this. The owner asked for the
FM Towns side-by-side layout to become our enhanced-mode layout, and asked that the
layout be validated against the reference screenshots first. It has been, and the
measurements are below.

> **Corrected 2026-11-17, in part.** "No engine code has been changed" is no longer true of the
> question of *when* the layout turns on: D4 is ruled and implemented, enhanced is the engine's
> default mode, and `--classic` plus `config.ini [game] mode = classic` are the opt-out. What is
> still plan is the FM Towns side-by-side arrangement itself — the enhanced layout that ships is
> the earlier one, not this document's § 3. Read D4 as settled and the rest as unbuilt.

> **Added 2026-11-15 — a fifth item, owner-ruled, not a decision: enhanced mode should address
> the player according to gender in its speech.** Tracked in `docs/outstanding.md` § 1. It lives
> here because it is enhanced-only work, and it is a deliberate divergence rather than a fidelity
> target: the DOS original's only gendered line is the cleric shrine's `BROTHER`/`SISTER`, and
> that branch is dead code (`DS:46` holds the typed letter while the code compares against 1 —
> `asm/1869_cleric_shrine.asm` header, G80b). There is no original behaviour to copy. Classic mode
> must keep saying `BROTHER`, because matching the original there means the branch never fires.

Four decisions need a ruling before any of this is built (§ 4). Two of them change
what the player can see, which makes them gameplay decisions rather than cosmetic
ones, and one of them changes a string's provenance, which makes it a licensing
decision. Everything else is mechanical.

---

## 1. The reference layout, measured

Source: `references/fmtowns-screens/`, third-party photographs of the FM Towns
version running, captioned in `_CAPTIONS.tsv`. Grade: **measured**, from six frames
(`FMtownsu210`, `211`, `214`, `U2FMTowns1`, `19`, `32`) plus a grid overlay check.
These are camera photographs of a CRT, so read every number as ±2 px and do not
over-fit to one of them.

Frame is 640 × 480.

| region | x | y | size | note |
| --- | --- | --- | --- | --- |
| banner | 0..639 | 3..20 | 640 × 18 | yellow; title left, `FUJITSU` right |
| map | 0..~360 | 21..~405 | **11 × 12 tiles at 32 px** | grid overlay aligns at 32 px pitch (autocorrelation peak, corr 0.64) |
| panel | 362..638 | 25..~424 | 276 wide | three boxes, blue borders |
| — box 1 | | 47..98 | 52 tall | vitals + avatar |
| — gap | | 99..110 | 12 | blue |
| — box 2 | | 111..292 | 182 tall | stats + equipment |
| — gap | | 293..302 | 10 | blue |
| — box 3 | | 303..~424 | 122 tall | command window |
| bottom bar | 18..621 | 434..449 | 604 × 16 | red, **constant** |

The panel geometry was **identical in all six frames**, which is what makes it a
spec rather than one screenshot's accident.

The bottom bar is not a health bar. It spans x 18..621 with fill 0.94 in every
gameplay frame including one at `H:258` and one at `H:4922`, and it is absent only
in the four full-screen cutscenes. It is chrome. (The 0.78 readings are frames
where a `Press SPACE to read more` line overlaps the band.)

## 2. What each box holds

Read off the frames at 3–4× zoom, not guessed.

**Box 1 — vitals.**
```
PIX
 H:  258   E:   34
 F:  383   G:  313
```
plus a small avatar figure at the right. The label reads `PIX` on Earth, in a
dungeon, **and on Planet X**, so it is not a world or sector code; the simplest
explanation is the character's own name, which we carry at `DS:0x36 name[16]`.
`H F E G` are HP, Food, Experience, Gold — the same four values our classic status
band prints as `H.P.= FOOD= EXP.= GOLD=`, in a 2 × 2 grid instead of a column.

**Box 2 — stats and equipment.**
```
STRENGTH :  35
AGILITY  :  35
INTELLI  :  15
WISDOM   :  10
CHARISMA :  10
STAMINA  :  20
WEA:Dagger
ARM:Cloth
```
Two things to notice. The **order is not ours**: FM Towns runs STR, AGI, INT, WIS,
CHA, STA, while our creation form and status code run STR, AGI, STA, CHA, WIS, INT.
And the labels are **not our strings**: `INTELLI` is `INTELLIGENCE` truncated to fit
the leader column, and the equipment lines are `WEA:` / `ARM:`, whereas the DOS
binary we reconstruct uses `WEAPON-` / `ARMOUR-`. See decision D3.

**Box 3 — command window.** Echoed commands, NPC replies in yellow, and the input
line:
```
West
Transact
Direct?West
Guard says:
Pay your taxes!
@
```
`▶` appears as a line marker on several frames. What it means is an open question
(§ 6), not a decoration to copy blindly.

## 3. Our current layout, and the gap

`fm-full.png` is our engine with FM Towns art at 640 × 400: the map fills the top
as 20 × 10 tiles, and the bottom band holds `CMD:` at the left with a right-aligned
`H.P.= / FOOD= / EXP.= / GOLD=` column. No boxes, no borders, no stats, no
equipment, no avatar, no banner.

So the gap is not "add a panel". It is three separate changes: the **viewport
shape**, the **frame size**, and the **panel itself**. The first two are the hard
ones and they are the ones that need a ruling.

## 4. Decisions needed

> **Update:** all four were ruled on 2026-11-05. See § 9 for the rulings and § 9a for
> the one that remains a taste call. The text below is kept as asked, because the
> reasoning behind each ruling is the part worth keeping.

**D1 — does enhanced mode adopt the FM Towns viewport of 11 × 12?**
Our 20 × 10 is not a preference; it is derived from the DOS original's staging
buffer (`asm/4ABB`, the 6400-byte span the game flushes to video). Adopting 11 × 12
shrinks the visible world from 200 cells to 132 and moves the player's centre from
(col 9, row 5) to about (col 5, row 6). That is a gameplay-visible change, and it
makes the engine's view no longer the DOS view. The FM Towns binary has its own view
size and we have **not** analysed it — we crosswalked its art, nothing more — so
this layout would be reimplemented from photographs, with no byte-level authority
behind it.
Options: (a) adopt 11 × 12 in enhanced mode and keep 20 × 10 in classic; (b) keep
20 × 10 and fit the panel beside a narrower map, accepting that the layout is then
ours and not the reference's; (c) adopt the panel arrangement only, below the map,
which is a smaller change and further from the reference.

**D2 — frame size.** The measured layout needs 640 × 480. Our frame is capped at
`U2_FRAME_MAX_W/H = 320×2 × 200×2 = 640 × 400`, and the SDL window is derived from
`U2_SCREEN_W/H × frame_scale × SDL_SCALE`. Adopting 480 means raising that cap and
touching the staging buffer, the texture and the window — all three of which the
640 × 400 ruling was written to keep from drifting per theme. Alternative: compress
the layout into 640 × 400 (banner 16 + map 12 × 32 = 384 + bar 16 = 416, already
over, so the map drops to 11 rows).

**D3 — where the panel's strings come from.** `STRENGTH`, `AGILITY`, `WISDOM`,
`CHARISMA`, `STAMINA` and `INTELLIGENCE` all exist in the binary we reconstruct, in
the creation form. `WEA:` and `ARM:` do not; ours are `WEAPON-` and `ARMOUR-`. The
project's line has been that shipped text comes from the bytes we reconstruct, and
the FM Towns abbreviations live in a binary we have not reconstructed. My
recommendation is to use **our** strings and let the layout be faithful while the
text stays sourced; the alternative is to declare the FM Towns labels a second
provenance and say so in a comment at each site.

**D4 — when the layout turns on.** Candidates: whenever `--ega` is active; only for
v2 32 px themes (a 16 px theme in an 11 × 12 map is 176 × 192 and looks stranded);
or an explicit `--layout classic|enhanced` with a `config.ini` default. Note the
precedent already set: frame scale is a property of the **art mode**, not the theme,
specifically so a Ctrl-T cycle cannot resize the window. A layout that changes with
the theme would reopen exactly that problem, which argues for tying it to the mode or
to an explicit flag, not to the theme.

**D4 is ruled, 2026-11-17, and it went further than any of the three candidates.** The
layout belongs to the *mode*, and the mode now defaults to enhanced with `--classic` as
the one-word opt-out for the whole presentation; `config.ini` carries `[game] mode =
classic` for a tree that should always open the original way. No `--layout` flag was
added, because with the mode owning it there is nothing left for a second flag to say.
The art axis stayed separable, and the measured consequence is the third canvas: classic
with fan art is 640x400, the EGA mode the art pack was drawn for, not 320x200. Enhanced is
1024x512 whatever the art. See `docs/phase4-implementation.md`, 2026-11-17.

## 5. What already exists to reuse

The panel's data is all in `state.h`, with offsets proven and `_Static_assert`
guards: `name[16]` (0x36), the six stats (0x4B..0x50, charisma and wisdom both
proven by capture), `hp` (0x51), `food`/`food_hi`/`food_lo` (0x53..0x55),
`exp_lo`/`exp_hi` (0x56/0x57), `gold_lo`/`gold_hi` (0x58/0x59), `readied_weapon`
(0x61), `worn_armour` (0x62).

`status.c` already renders the four vitals with the packed-BCD convention and the
byte-order trap documented (a swapped pair reads 3800 instead of 0038), so box 1 is
a re-layout of existing drawing code, not new number formatting. The Z screen
already resolves equipment names through `Z_NAME(U2_T_WEAPONS, …)` and
`U2_T_ARMOUR`, so `WEA:Dagger` is a lookup we already do. The box-1 avatar is the
themed player tile, which is exactly the art-index-to-pool mapping fixed today in
`u2_frame_draw_view_ega` — that fix is a precondition for drawing it correctly in a
v2 theme.

## 6. Open questions that need evidence, not opinion

1. **What is `▶`?** It prefixes several lines in box 3. Copying it without knowing
   what it marks is the kind of plausible-but-wrong detail this project has been
   bitten by. Needs a live FM Towns session or the FM Towns binary.
2. **Does box 3 scroll, and how deep?** Frames show 5–7 lines. Our vram log is a
   40 × 25 grid; a 276 px box at 16 px glyphs is 17 characters wide, so the log
   would have to re-wrap. Whether it wraps or clips changes what a player can read
   back.
3. **Is the panel identical in every mode?** Dungeon frames show the same three
   boxes with a level label (`L13`..`L16`) above the map, and the space frame shows
   the panel too. Town needs confirming from a frame that is unambiguously a town.
4. **Does the box-1 avatar vary by race or class?** The FM Towns `PLAYER` bank is
   entirely unmapped in our crosswalk precisely because it varies by race and class,
   an axis the DOS atlas does not have. If the box-1 figure comes from that bank,
   we cannot draw it faithfully from what we have mapped.

   > Ruled 2026-11-19, and the FM Towns bank turned out to be the wrong question. The
   > DOS atlas already varies by class: the on-foot stamp is `0x78 + 2*pclass`, so the
   > map figure is art 60..63 for Fighter/Cleric/Wizard/Thief, and the vehicle tile
   > while mounted. The owner ruled that the avatar mirrors the map, so it shows the
   > boat and the horse too. Nothing is being reproduced faithfully here — the original
   > has no status-screen sprite at all — so the standard is internal consistency, and
   > the panel now passes the same expression the map does rather than the constant 60.
5. **Is the banner part of the game?** It carries `Ultima Trilogy` and `FUJITSU`,
   which is packaging and hardware branding rather than game output, and we would
   be inventing our own text for it either way.

These belong on `docs/needs-gameplay.md` when the plan is accepted, with a
who-can-do-it verdict, and several of them need the FM Towns disc rather than a
DOS session.

## 7. Proposed slices, in order

The ordering is deliberate: S1–S4 are additive and cannot change what a classic-mode
player sees, so they can land and be tested while D1 and D2 are still open. S5 and S6
are the invasive ones and should not start until they are ruled.

| # | work | gate |
| --- | --- | --- |
| S1 | `core/layout.h` — the panel geometry as data, with the measured numbers and their source named in comments | a check asserting the boxes tile the panel without overlap and match the measured extents |
| S2 | panel primitives: box border, dotted leader, right-aligned value | unit check on each primitive |
| S3 | box 1 and box 2 from `state` | values must match state; BCD order asserted against the proven convention, including the swap control |
| S4 | box 3 fed by the existing vram log | every message visible in classic must be visible in enhanced — the no-silent-mutation rule, asserted rather than eyeballed |
| S5 | *(needs D1)* view dimensions at runtime instead of `U2_VIEW_COLS/ROWS` | CP1/CP5 view tests unchanged for classic; new test for the enhanced shape |
| S6 | *(needs D2)* frame size, texture, window, scale caps | frame check plus a render at the new size |
| S7 | box-1 avatar through the theme map | reuses today's overlay fix; assert it draws the mapped tile |
| S8 | docs, and the § 6 questions onto `needs-gameplay.md` | — |

## 7a. Owner-requested enhancement, 2026-11-11 — the helm automap

*(numbered 7a because it is a slice request that sits on top of the slice table above;
§ 8 and § 9 keep their numbers, since other documents point at them.)*

**Status: requested, not ruled, not started.** Recorded here rather than built, and it
is an *enhancement*, so it lives behind `--enhanced` and must not change what a
classic-mode player sees. Classic keeps what the original does: press `V`, spend one
HELM, see the sector drawn, press a key, back to the game (`CP5 H1`,
`captures/spice86/vhelm-sector-map-live`).

The request, as the owner put it: in enhanced mode, **while the player holds one or
more helms**, the current sector map is drawn persistently in the side panel — no key
press, no charge per look. And in a **dungeon or tower** the map is not given at all
up front: it starts blank and every step fills in the section you have walked, so the
automap is earned by moving rather than by holding the item.

Why it is cheap to want and not free to build:

- **The panel does not exist yet.** This whole document is the panel, and S1–S4 are
  unstarted. The automap is a box in that panel, so it is downstream of S1 and S2 at
  minimum, and of whatever the § 4 font ruling picks, because the box's pixel width is
  what sets how much of a 64x64 sector can be shown.
- **The sector half is nearly free.** `helm_map_draw` in `game/commands.c` already
  renders a whole sector from `u2_world_tile_at` at 4x2 px per cell with a density
  ladder measured from the frames. A panel box is the same draw at a different scale
  into a different rectangle. The density ladder is fitted to five art values, so
  widening it is a measurement task, not a guess.
- **The dungeon half is the real work, and it is a new state.** A sector map is static;
  a fog-of-war automap is *accumulated*, and nothing in the engine currently holds
  "cells the player has seen". That means a new per-save structure, and it raises
  questions the bytes cannot answer because the original has no such feature:
  what a step reveals (the cell only, a radius, or the lit radius the dungeon light
  already computes), whether the map survives leaving and re-entering the site, whether
  it survives death, and whether it is written to the save overlay under `saves/<slot>/`
  alongside the mapx overlay or kept in memory only.
- **Dungeon geometry is not sector geometry.** A dungeon is 16 pages of 16x16 with its
  own byte semantics — `0x80` is rock there and something else on a sector map, `0x40`
  is a chest — and `docs/art-index.md` already records that dungeons do not stamp the
  grid the same way. So the automap needs its own per-page grid, not a reuse of the
  sector view.

What it would need before anyone writes code, in order:

1. S1 and S2 landed, so there is a panel with a box of known pixel extent.
2. A ruling on **persistence**: is the explored dungeon map saved, or session-only?
   Saved means it belongs in the save overlay and needs a format decision; session-only
   means it resets on reload and should say so in the UI.
3. A ruling on **reveal rule**: cell-only, fixed radius, or the existing light radius.
   The third is the most defensible, because the dungeon already computes a lit radius
   for drawing, so it would reuse a proven quantity rather than invent one.
4. A decision on whether holding a helm in a dungeon reveals the *sector* map, the
   *dungeon* map, or nothing until you have walked — the request reads as the last, but
   it is worth writing down which, because the item currently costs a charge in the
   original and an automap that also costs charges would be a different feature.

No gate is proposed yet, for the reason this project keeps learning: a gate for a
feature whose shape is undecided gets written against the first implementation and then
argues for it forever. When this is ruled, the assertion should be the visible one — a
panel box whose lit cells match the sector's land pattern, and in a dungeon, a box that
is empty at the door and grows monotonically with steps taken.

## 8. Risks worth stating plainly
- **No byte authority.** The DOS layout is derived from the binary. This layout would
  be derived from photographs of a different binary we have not disassembled. That is
  a lower grade of evidence and every number in § 1 should carry that caveat in the
  code that uses it.
- **View shrink is a gameplay change.** 200 cells to 132 is fewer monsters seen,
  fewer tiles of warning before an attack, and it interacts with the contact and
  sweep code that already assumes the DOS staging shape.
- **Over-fitting a photograph.** Camera geometry is skewed; ±2 px is the honest
  resolution, so the layout should be expressed in tile and character units with the
  pixels as a comment, not the other way round.
- **Two sources of truth for text.** If D3 goes the other way, the engine ships
  strings that did not come from the bytes it reconstructs, and that needs saying at
  each site rather than being left implicit.

---

## 9. Owner rulings, 2026-11-05

**D2 — ruled.** Enhanced mode is 640 × 480, and `--enhanced` is what sets it. It also
makes `--ega` redundant rather than orthogonal: at 640 × 480 there is room for a
32 px theme by definition, so `--enhanced` implies themed art and `--ega` becomes the
classic-layout-with-fan-art path that stays at 640 × 400.

**D3 — ruled.** Enhanced mode may invent its own text. It is a reimplementation of a
presentation, not a byte-matching target, so `WEA:` and `ARM:` are fine and the stat
labels are ours to choose. Each invented string gets a comment saying it is ours and
not from `ultimaii.exe`, so nobody later mistakes it for extracted content.

**D4 — ruled.** `--enhanced` is the trigger. Not the theme, which keeps the
window-resize-on-Ctrl-T problem closed.

**Red bar — ruled.** Keep it, draw it, give it no meaning yet. Candidate future use
is the planet or area the player is standing on, which is a nicer home for that
information than the map, but it stays an empty bar until there is a use for it.

**Z screen — ruled.** Takes the whole screen in enhanced mode for now. Paginating it
into the panel is a later change, and it is worth noting that the Z page is the one
screen whose content is already too wide for a 276 px column, so pagination is not
just a layout nicety.

**Stats order — ruled.** Ours, not the reference's.

### 9a. D1: the arithmetic, and what actually sets 11 × 12

The question was whether a viewport at least as large as the DOS one fits beside a
panel. It does, and the answer turned out to be driven by the panel's **font size**,
not by the frame.

The hard constraint is `tile_px × cols + panel_px ≤ 640`. At 32 px tiles, 20 columns
is 640 px — the entire width — so **20 × 10 beside a panel is arithmetically
impossible**, by 72 px even with a panel only 8 characters wide. That is not a
layout to tune; it is arithmetic.

What fits at 32 px tiles, 640 × 480, banner dropped and the 16 px bottom bar kept:

| map | cells | vs DOS 200 | panel | text cols at 8 px |
| --- | --- | --- | --- | --- |
| 11 × 14 | 154 | −46 | 280 px | 34 |
| 13 × 14 | 182 | −18 | 216 px | 26 |
| **14 × 14** | **196** | **−4** | 168 px | 20 |
| 15 × 14 | 210 | +10 | 144 px | 17 |

So a roughly square **14 × 14** matches the DOS cell count almost exactly, which is
what the owner suspected when he said it would have to go square.

The reason the reference is 11 wide is now visible: FM Towns draws its panel text at
about 16 px, so a panel readable at that size is ~276 px, and 640 − 276 = 364 = 11
columns. **The 11 is not a taste decision, it is the font.** Our font is 8 × 8 and
the only sizes we can render are 8 px native or 16 px doubled, so the choice is
binary:

- **Panel at 16 px** — matches the reference's visual weight next to 32 px tiles,
  needs ~276 px, and the map is 11 × 14 = 154 cells, *smaller than the DOS view*.
- **Panel at 8 px** — half the reference's text size, but a 168 px panel holds 20
  columns and the map becomes 14 × 14 = 196 cells, matching DOS.

There is no middle, because there is no 12 px font. This is the remaining decision
for D1 and it is genuinely a taste call: a faithful-looking panel with a smaller
world, or a world as large as the original with small panel text. My recommendation
is the 8 px panel at 14 × 14, because losing 46 cells of visibility is a gameplay
change and small text is only an aesthetic one — but the reference look is the
reason to want the other.

One more number worth having: the command log wraps at 40 columns in the DOS binary
(`asm/53A0`). Any panel narrower than 40 text columns re-wraps it, so 17–26 columns
means lines like `CMD: ATTACK--EAST--HIT!!!` break across three rows. The reference
lives with 17, so this is normal rather than a defect, but it is the thing that will
feel different in play and it is worth seeing in a prototype before committing.

At 16 px tiles (a v1 theme in enhanced mode) none of this binds: 22 × 29 = 638 cells
fit beside a 34-column panel, so the v1 case has room to spare and the constraint is
specific to 32 px art.

## 10. Future enhancement: a real-3D dungeon (enhanced mode only)

The dungeon renderer is being made art-mode aware so that CGA keeps the byte-faithful
wireframe and EGA draws the same wireframe scaled to fill the map window in the four
CGA colour indices resolved through the active palette. That same per-mode dispatch seam
could later carry a third renderer for enhanced mode that draws a real 3D view (textured
walls, floor and ceiling, with sprite monsters and chests) instead of a wireframe. This is
a feature and not a fidelity target, no shipped original has it, and it is deliberately not
scoped here. Its open question is art: a cleanroom-generated set is allowed and is the
honest default, while the FM Towns route is not ready because `disasm/fmtowns-crosswalk.tsv`
maps overworld and town tiles only and nothing in this project has confirmed that the FM
Towns dungeon is 3D at all, so that path needs the dungeon art extracted, mapped, and its
shape verified before it can be assumed. Treat this as its own plan with its own owner
ruling when someone wants to build it, downstream of the mode-aware dispatch that the EGA
tier introduces.

**Update, 2026-11-13.** The "is the FM Towns dungeon 3D at all" question is now closed:
the screenshots on disk under `references/fmtowns-screens/` (see `_CAPTIONS.tsv`, e.g.
`FMtownsu214.jpg`) show a first-person perspective corridor with textured walls, a
perspective floor, and sprite monsters. The geometry spike and the full design are written
up in `docs/enhanced-dungeon-3d.md` (plan only, not started, owner ruling on the art route
still open). The projection turns out to be the same nested depth rectangles the wireframe
already draws, halving toward the near-frame centre, so the new work is a textured-trapezoid
rasteriser and the dungeon-art decode, not a new projection.
