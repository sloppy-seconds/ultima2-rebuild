# Tile Art Specification — original substitute art

**Status: specification only. No art has been produced from this document yet.**
**Purpose: to commission independent, original tile art for the Ultima II engine
reconstruction, so the project can ship a usable art set without distributing the
copyrighted 1983 assets.**

This document is written by an agent that has examined the original binary. That is
the first half of a cleanroom process. The implementer is a different agent, in a
different session, that must not examine the original. See § 2.

---

## 1. What this is, and what it is not

This is a specification for **substitute art**, not a reproduction. The goal is a
tile set that communicates the same thing at a glance, in the same palette, at the
same size. Where the result differs from the 1983 art, that difference is the
point and the requirement — it is what makes the set independently created.

**Do not attempt to match the original sprites.** Do not seek them out to "check
your work". If you find yourself comparing your output against a reference image
of the game, stop; that comparison is the act this process exists to prevent.

Nothing in this file is a pixel transcription. Descriptions are impressions of
function and character. Two competent artists following it will produce visibly
different sets, and both sets are correct.

## 2. Cleanroom protocol — read this before doing anything

The value of this exercise depends entirely on the wall being real.

**The implementer must not open, read, list, render, or search any of the
following, in this repository or anywhere else:**

| forbidden | why |
| --- | --- |
| `original/` | the copyrighted game files |
| `references/` | a second copy of the same |
| `tmp/tiles/` | extracted renderings of the original tile art |
| `tmp/tiles/tiles.json` | contains ASCII renderings of the originals |
| `disasm/tile-art.tsv` | descriptions written by someone who had the originals in view |
| `asm/`, `build/`, `captures/` | derived from the copyrighted binary |
| `tmp/*.png`, `tmp/screens/` | screenshots of the running game |
| `tmp/*.html` | the map viewer and tile review sheet embed the extracted originals as base64 |
| `docs/tile-art-evidence.md` | **the answer key**: measured structure of every original |
| `docs/dungeon-stroke-art.md` | **another answer key**: measured descriptions of the seven dungeon creature shapes, plotted from the original's own stroke data |
| `tools/dungeon_strokes.py` | the renderer that produces those shapes, and the shipped `monsters` file it reads |
| `tools/tile_rules.py` | **also the answer key**: the gate's bands were fitted to the originals — density ranges, periods, component sizes, diagonal runs |
| `bestiary/cache/` | **original art in bulk**: copyrighted rips of the game's tiles in seven platform variants, plus the FM Towns animation frames split from them. Collected for *naming*, not for drawing. |
| `bestiary/view.html` | embeds those rips and our extracted tiles as data URIs |
| `bestiary/bestiary.tsv`, `bestiary/matches.md` | they record which of our art indices a community bitmap matched, which is a description of the originals written with the originals in view |
| `tools/tile_review.py`, `tools/tile_atlas.py`, `tools/monster_shapes.py` | load or render the extracted originals |
| `engine/assets/tiles/cleanroom/PROVENANCE.md` | legal record; discusses the originals and cites forbidden paths |
| `/var/tmp/tile-review-ledger.md`, `/var/tmp/game-knowledge-deltas.md` | review working notes containing ASCII pixel dumps of the originals |
| `/var/tmp/tile_probe.py`, `tile_show.py`, `validate_rule.py`, `gem_*` | tools that read or describe the originals |

**Why the answer-key rows matter as much as the art rows.** A rule fitted to one example
encodes that example. `tools/tile_rules.py` holds bands such as "diagonals of at least 4",
"density 26–38%%" and "five hollow components of equal size" — enough to reconstruct the
original's structure arithmetically without ever seeing it. Reading it would satisfy the
letter of this protocol and destroy its purpose, which is why the gate is run **against** the
finished art and never handed to the person making it.

**Also be aware of two non-obvious leaks in this environment:**

1. **context-mode session memory.** Prior sessions in this project indexed ASCII
   renderings of the original tiles. A `ctx_search` in this project can surface
   them. Do not use `ctx_search`, `ctx_index` or `ctx_batch_execute` against this
   project while doing this work.
2. **`AGENTS.md` and the docs directory** name the asset paths above. Reading them
   for orientation will point you at forbidden material.

**Recommended working arrangement:** do the implementation in a separate
directory outside this repository, containing only this specification. If you are
an agent, state in your first reply that you will not read the paths listed above,
and let the requester confirm.

**Record keeping.** When the work is done, write a short provenance note next to
the output recording: the date, who authored this spec, who implemented it, which
inputs were available to the implementer (this file, and nothing else), and a
statement that the forbidden paths were not read. Keep it. That note is the
cleanroom claim.

## 3. Technical requirements

- **Format:** SVG, one file per tile.
- **Canvas:** `viewBox="0 0 16 16"`, no fixed `width`/`height`, so consumers can
  scale freely.
- **Rendering:** `shape-rendering="crispEdges"`; this is pixel art and must not
  blur when scaled.
- **Grid:** build from 1×1 unit rectangles (or paths aligned to integer
  coordinates). Nothing may straddle a pixel boundary.
- **Colours — three inks on a black backdrop, no others, no gradients, no alpha:**

  | role | name | hex |
  | --- | --- | --- |
  | ink | cyan | `#55FFFF` |
  | ink | magenta | `#FF55FF` |
  | ink | white | `#FFFFFF` |
  | backdrop | black | `#000000` — **never emitted in a tile** |

  This is the CGA composite mode-4 palette. Black is the field the engine paints
  before drawing (`u2_frame_clear(..., U2_CGA_BLACK)`), not a colour a tile owns.
  Emitting black would make a tile opaque in some cells and transparent in others,
  and on a black backdrop that inconsistency is invisible to any proof sheet - it
  can only be caught by asserting that `#000000` does not appear in the set.

  Consequence: a tile whose appearance is "all black" is expressed as an **empty
  tile**, not as a tile full of black rectangles. Art `1D` is exactly that case and
  is correct, not missing. A backend that needs opaque tiles composites onto black
  itself rather than each tile carrying its own background.
- **Naming:** `tile_<NN>.svg`, `NN` = the two-digit uppercase hex **art index**
  from the table below. Not the map byte — see § 4.
- **Every tile must be legible at 16×16.** Test by rendering at native size, not
  zoomed.

## 4. Indexing

The engine stores one byte per map cell. The art index is:

```
art_index = map_byte >> 2
```

So map byte `0x00` selects art `00`, byte `0x04` selects art `01`, byte `0xFC`
selects art `3F`. Art indices run `00`–`3F`, 64 tiles. The byte column is given
below only so the mapping is unambiguous; it is not otherwise relevant to drawing.

## 5. Functional classes

The 64 entries are **not** 64 terrain types. They fall into five groups, and the
group tells you how much care a tile needs:

| class | meaning | risk |
| --- | --- | --- |
| ground | walkable or blocked surface, tiles the map in bulk | low — geometric, few expressive choices |
| structure | a built thing: walls, gates, towers, docks | medium |
| creature | a person or animal standing on the map | **high — these are the expressive ones** |
| letter | the capital letters A–Z, used as shop fronts and signs | low — it is a font |
| block | border and fill pieces for framing | low |

**The creature tiles are where the copyright risk actually lives.** A figurative
drawing is creative expression in a way that a dot field is not. For those, favour
the most generic, schematic figure that still reads, and deliberately avoid
reproducing any particular pose, silhouette or costume detail you may have seen.

## 6. Colour assignment

Colour is specified per tile. This is not a contradiction of the cleanroom rule:
colour is a **fact about the program's behaviour**, not expression, and a spec is
allowed to carry facts. What stays off-limits is the drawing - which pixels are lit
and how. Knowing that grass is cyan tells the implementer nothing about how to draw
grass.

The table below records what each colour must **do** in the set. Its derivation is a
spec-author concern and is kept in `docs/tile-art-evidence.md`, which § 2 forbids to the
implementer; the requirements themselves are all you need.

| ink | art | meaning |
| --- | --- | --- |
| **cyan** | `02` `03` | vegetation - grass, forest |
| **magenta** | `00` `1C` | liquid - water, marsh |
| **white** | everything else: `04`-`0A`, `0C`-`16`, `18`-`1B`, `1E`-`3F` | solid and man-made - mountains, structures, creatures, letters, blocks |
| cyan + white | `01` | rough water |
| magenta + white | `0B` `0E` | arch in a field; figure with a staff |
| cyan + magenta + white | `17` | the checker border |
| **none** | `1D` | solid black - an empty tile per § 3 |

55 of 64 tiles use white alone. The pattern is worth understanding rather than
copying blindly: **cyan means vegetation, magenta means liquid, white means
solid.** Graded *inferred* - it is a reading of the pairing, and the pairing is
tight, the only two cyan tiles being grass and forest and the only two magenta
tiles being water and marsh.

**This deliberately pins the substitute set close to the original's appearance.**
An earlier revision of this spec said the opposite, arguing that a visibly different
fallback made it obvious which art was loaded. That is dropped: for a reconstruction,
fidelity is the point, and a fallback set that looks different on purpose is a
strange thing to ship. Recorded here so the change is visible rather than silent.

### Verification constraints

The assignment above is fixed, so these are checks on the drawing, not choices.

| # | constraint | why |
| --- | --- | --- |
| V1 | Letters (`20`-`39`) must stay legible against **every** ground tile they can sit on. | Signs are placed on varied ground; a letter that merges with its backdrop fails at its only job. White ink on cyan grass and on magenta water both need checking. |
| V2 | Creatures must not be confusable with any ground tile. | Creatures are stamped into the map at runtime, so they are seen *on* ground, not beside it. |
| V3 | The seven ground tiles must remain distinguishable at 16x16 **on black**. | This is the real viewing condition. A set that separates on white can collapse on black. |

## 7. Per-tile specification

### Ground

Must tile seamlessly when repeated, and must stay quiet under the other classes.

| art | byte | name | must read as |
| --- | --- | --- | --- |
| `00` | `0x00` | water | open water; thin regular dashes so a field of these tiles reads as one continuous surface in motion, not static per tile (see § 7a) |
| `01` | `0x04` | shallow / rough water | horizontal wave lines with **white foam dominant over cyan**; sparser than `00`, not busier (see § 7a) |
| `02` | `0x08` | grass | open grassland; sparse light speckling on dark ground, the calmest tile in the set |
| `03` | `0x0C` | forest | woodland; a few large crowns, not many small marks — clearly "more stuff on the ground" than grass |
| `04` | `0x10` | mountain | high ground; two families of crossing diagonal lines reading as zigzag peaks or stacked diamonds, fine strokes, all-over coverage |
| `09` | `0x24` | mountain pass | the mountain texture **plus an arch** open at the bottom centre. The opening is the whole point — do not seal it |
| `1C` | `0x70` | marsh / reeds | vertical strokes on a dark field; distinct from water by direction, not colour. Naming contested — see § 7a |

### Structures

| art | byte | name | must read as |
| --- | --- | --- | --- |
| `05` | `0x14` | ring formation | five rings of equal size arranged like the five on a die, slightly crooked. Every one must actually be a ring — a stray single pixel among them is a defect |
| `06` | `0x18` | emblem (command-key square) | one symmetric mark: a square frame with four equal corner loops and a hole at the centre, inset from the edges so tiled copies stay separate. Marks a settlement — see § 7a |
| `07` | `0x1C` | tower | a **solid** upright rook silhouette, two prongs on top, flaring to the base. Outlining it and filling it with windows is a misreading |
| `08` | `0x20` | gate | a gateway frame: two posts under a beam, blocky tabs at all four corners, feet flaring, bottom centre open. A thick slab on two posts is not a gate |
| `0A` | `0x28` | signpost | a post carrying ONE heavy crossbar high up, on a lighter base. A second bar near the foot turns it into a different kind of cross |
| `0B` | `0x2C` | sea monster in water | a creature rising from sparse water: body low-left, neck and head with an open mouth on the right. The water is the quiet part; the creature is the subject. NOT a doorway — see § 7a |
| `12` | `0x48` | ship | a boat: hull with a vertical mast |
| `13` | `0x4C` | plane | an aircraft the player can board with `B` — see amendment A1 |
| `14` | `0x50` | rocket | a tall tapering form on a wider base, i.e. a rocket on a pad — see amendment A1 |
| `15` | `0x54` | dungeon mouth | an enclosed opening; the "go down here" tile. Naming contested — see § 7a |
| `16` | `0x58` | marker | a small, sparse diagonal mark with a cross at its foot. Naming contested — see § 7a |
| `30` | `0xC0` | moongate | an arch open at the bottom: solid top, side borders, no bottom line, interior stripes. It is a teleporter, so it must read as an entrance, not as masonry — see § 7a |

### Amendment A1 — 2026-10-22, arts `13` and `14` renamed

Arts `13` and `14` were originally named **signpost** and **spire** by structural guess,
before the vehicles had been identified. They are the **plane** and the **rocket**.

The proof is byte-level and is recorded in `docs/tile-art-evidence.md`. That file is **not**
part of this specification and is listed as forbidden in § 2 — an implementer does not need
it, and reading it would breach the wall. What you need is stated here: art `13` is an
aircraft the player boards, art `14` is a rocket, and arts `11` and `12` were already
correct.

**The art set was deliberately not regenerated by this amendment.** Renaming a row is a
spec-author act and may use evidence the implementer may not see. Redrawing `tile_13` is an
implementer act, and must be done from this document alone. Until that happens `tile_13`
still depicts a notice board, which is a known and recorded mismatch, not an undiscovered
one.

Art `14` needs no redraw: "a tall tapering form on a wider base" already reads as a rocket
on a pad. Only its name was wrong.

### Monster sprites

These eleven are the overworld enemy set, **not** non-player characters — the old names
(person, swordsman, archer) were shape guesses. See § 7a for why. Six have no known name; do
not invent one.

They must read as creatures that stand up and fill their cell: one figure, one dominant
colour, upright, substantial — not a small icon floating in the middle, and not scattered
pieces.

| art | byte | name | must read as |
| --- | --- | --- | --- |
| `0C` | `0x30` | monster (unnamed) | an upright creature, arms at sides, horned or crested head |
| `0D` | `0x34` | monster (unnamed) | same family, arms out and up |
| `0E` | `0x38` | monster (unnamed) | a creature beside a thin vertical staff |
| `0F` | `0x3C` | monster (unnamed) | a broad, heavy creature |
| `10` | `0x40` | monster (unnamed) | a cloaked figure, triangular silhouette |
| `18` | `0x60` | monster (unnamed) | upright creature, arms angled down and out |
| `1A` | `0x68` | monster (unnamed) | a slight, tall creature |
| `3C` | `0xF0` | monster (unnamed) | upright creature, broad stance |
| `3D` | `0xF4` | monster (unnamed) | creature holding a blade up to one side |
| `3E` | `0xF8` | monster (unnamed) | creature beside a tall vertical bar |
| `3F` | `0xFC` | monster (unnamed) | creature holding a curved bow |

### Other figures

| art | byte | name | must read as |
| --- | --- | --- | --- |
| `11` | `0x44` | horse | a four-legged animal, low and wide, head at one side |
| `19` | `0x64` | figure, arms up | figure with arms forming a V above the head |
| `1B` | `0x6C` | crouching figure | humanoid with arms drawn inward |

### Letters A–Z

Art `20`–`39` are the capital letters A to Z in sequence, with one exception:
art `30`, where **Q** would fall, is the wall tile above and is not a letter.
So the set is A–P, then R–Z, and Q is absent:

| art | letter | art | letter | art | letter |
| --- | --- | --- | --- | --- | --- |
| `20` | A | `28` | I | `31` | R |
| `21` | B | `29` | J | `32` | S |
| `22` | C | `2A` | K | `33` | T |
| `23` | D | `2B` | L | `34` | U |
| `24` | E | `2C` | M | `35` | V |
| `25` | F | `2D` | N | `36` | W |
| `26` | G | `2E` | O | `37` | X |
| `27` | H | `2F` | P | `38` | Y |
| `30` | *not a letter* | | | `39` | Z |

There is no `Q` tile. If a place name needs one, the engine has no glyph for it,
which is a fact about the original data and not something to fill in here.

Requirements: a single bold capital glyph centred in the cell, drawn from **any
public-domain or self-drawn bitmap letterform** — do not trace a reference. These
tiles are used as shop fronts and notices in towns, so they must be legible at
16×16 above all else. A horizontal rule above and below the glyph is **required**: every letter tile is a sign
board, not a floating glyph. See § 7a — an earlier revision of this document called the rules
optional and every letter tile came out wrong as a result.

Note that in some sectors these same indices are used as structural fill rather
than as letters. That is the map's business; draw them as letters.

### Blocks and borders

| art | byte | name | must read as |
| --- | --- | --- | --- |
| `17` | `0x5C` | checker border | a small repeating checkered band |
| `1D` | `0x74` | solid black | fill, black |
| `1E` | `0x78` | solid white | fill, white |
| `1F` | `0x7C` | edge strip | white at one edge, dark elsewhere |
| `3A` | `0xE8` | dark fill A | near-black with a small light motif |
| `3B` | `0xEC` | dark fill B | near-black, motif clearly different from `3A` |

## 7a. Amendment record — 2026-10-22 tile review

The owner reviewed all 64 entries against the extracted originals and re-identified several.
This section states **what each tile must communicate**. It deliberately does not state
measured structure — no coordinates, no densities, no periods — because those are traces and
belong in `docs/tile-art-evidence.md`, which § 2 forbids to you.

A machine gate (`tools/tile_rules.py`, run by `make test`) checks the finished art against
these requirements. **Do not read the gate.** It is the answer key: its thresholds were fitted
to the originals, so satisfying it by reading it would tell us nothing.

### Re-identifications — these tiles are not what the old names said

| art | was | is | what it must communicate |
| --- | --- | --- | --- |
| `06` | monument | **emblem** | one symmetric mark: a square frame with four equal corner loops and a hole at the centre, inset from the edges. Marks a settlement. |
| `0A` | signpost | **signpost** (moved here from `13`) | a post carrying ONE heavy crossbar high up, on a lighter base. A second bar near the foot turns it into a different kind of cross. |
| `0B` | arched doorway | **sea monster in water** | a creature rising from sparse water: body low on the left, neck and head with an open mouth on the right. The water is the quiet part; the creature is the subject. |
| `30` | wall | **moongate** | an arch open at the bottom — solid top, side borders, no bottom line, interior stripes. It is a teleporter, so it must read as an entrance, not as masonry. |
| `0C` `0D` `0E` `0F` `10` `18` `1A` `3C` `3D` `3E` `3F` | people | **monster sprites** | see the note below. |

**The eleven monster sprites.** The old spec named these person, robed figure, swordsman,
archer and so on. They are the overworld enemy set, not non-player characters — one of them
is the only monster in the game that drops items. So the brief changes: they must read as
**creatures that stand up and fill their cell**, not as small icons floating in it, and not as
scattered pieces. One figure, one dominant colour, upright, substantial. Six of the eleven
have no known name; do not invent one, draw the silhouette class and leave the identity open.

### Requirements the old spec left too loose

* **`00` water** — the dashes must be thin and regular enough that a field of these tiles
  reads as one continuous surface in motion. Chunky or irregular specks read as static per
  tile, which is the current substitute's failure.
* **`01` shallow water** — horizontal wave lines, with **white foam dominant over cyan**. The
  previous substitute inverted this ratio and buried the foam. Sparser than `00`, not busier.
* **`03` forest** — a few large crowns, not many small marks. Scattered confetti is the
  failure mode.
* **`04` mountain** — two families of crossing diagonal lines, reading as zigzag peaks or
  stacked diamonds. Fine strokes, all-over coverage. Solid blobs or a single diagonal family
  do not read as rock.
* **`05` ring formation** — five rings of equal size arranged like the five on a die, slightly
  crooked. Every one must actually be a ring: a stray single pixel among them is a defect.
* **`07` tower** — a **solid** silhouette, two prongs on top, flaring to the base. Outlining
  the tower and filling it with windows is a misreading of "upright tower".
* **`08` gate** — a frame with blocky tabs at all four corners and an opening at the bottom
  centre. A thick slab on two posts is not a gate.
* **`09` mountain pass** — the mountain texture **plus an arch**. The opening is the entire
  point; sealing the bottom turns a pass into a wall.
* **`1D` solid black** — the original carries no ink at all. Since black is the backdrop the
  engine paints and must never be emitted, **the correct substitute draws nothing**. That is
  intentional; do not "fix" it by adding texture.
* **`1E` solid white** — a completely filled white cell, uniform.
* **`17` checker border** — must repeat exactly on both axes so it tiles seamlessly.

### The letter tiles: the rules are now mandatory

The old text said *"A horizontal rule above and below the glyph is optional decoration, not a
requirement."* That option was taken and every letter tile came out wrong as a result.

**Changed:** every letter tile is a **sign board** — a full-width rule along the top edge, a
clear gutter, the glyph, a clear gutter, and a full-width rule along the bottom edge. The
rules are required, not decorative. Without them a letter reads as a floating glyph rather
than a shop front, which is what these tiles are used for.

### What is still contested

`15` (dungeon mouth or sign), `16` (marker or sword) and `1C` (marsh or something else) have
competing readings and no settled answer. Their rows keep their current names; the gate checks
only their structure. Do not treat an unresolved name as permission to redraw freely — draw
what the row says and leave the question open.

## 8. Acceptance criteria

1. 64 files, `tile_00.svg` … `tile_3F.svg`, each a valid `16×16` SVG.
2. Only the three ink colours appear anywhere in the set, and **no `#000000` is
   emitted** - black is the backdrop (§ 3). Art `1D` is an empty tile.
3. Every tile is distinguishable from its neighbours in the same class at 16×16.
4. Ground tiles tile seamlessly when repeated on a grid.
5. No file was produced by tracing, importing, or converting original game art.
6. A provenance note exists per § 2, naming the implementer, the date, and the
   inputs used. **It must record which version of this spec was implemented** - the
   commit hash or blob SHA, not just the filename. Without that the record cannot be
   checked later, because the spec changes.
7. Constraints V1-V3 in § 6 hold, and the ink assignment matches the § 6 table.
8. **Proof sheets are composited on black, not white.** Tiles are transparent and
   the engine clears to black before drawing, so any tile whose ink is white is
   invisible on a white sheet. A checker that renders on white silently cannot see
   those tiles and will report success while two ground types render blank. If a
   near-blank or white-ink tile exists, the proof must show it.

## 9. What happens to the output

Place the result in the engine's own asset directory, clearly labelled as original
substitute art, and reference it from `LICENSE`/`NOTICE` as such. The engine should
prefer the player's own extracted art when present and fall back to this set
otherwise, so the shipped repository contains no copyrighted asset at any point.

This document and any art produced from it are Apache-2.0, consistent with the rest
of the project.
