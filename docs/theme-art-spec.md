# Theme Art Specification — original 32×32 EGA-palette tile set

**Status: DRAFT, not commissioned.** This document exists so the decisions below are
reviewed before any artist is pointed at anything. Nothing in `engine/assets/` is
produced from it yet.

**Relationship to `docs/tile-art-spec.md`.** That file is the specification for the
existing 16×16 cleanroom substitute set, and it is **pinned** — `PROVENANCE.md` names
it as the sole legal input for that work. It is deliberately **not edited by this
document.** Amending a pinned spec would silently change the record of what the
existing artist was given, which is the one thing the provenance note exists to
prevent. Where this spec reverses a decision that file records, the reversal is
stated here, in § 2, with its reason, and the older file is left standing as the
record of the older commission.

---

## 1. What this is, and what it is not

A **new, original tile set** for the engine, shipped as a v2 theme, at 32×32 and
16 colours.

It is **not** a substitute for the 1983 art and it does not claim to be. The existing
cleanroom set's goal is fidelity: it tries to look like the original, and
`tools/tile_rules.py` measures how well it did. This set's goal is to be **good art
that fits the engine**. A thief here is not a rendering of the original's thief; it
is a thief, drawn well, inside the constraints of a 32×32 16-colour tile.

That difference is the whole document. Everything below follows from it.

The cleanroom wall still applies in full, and it applies **more** strictly, not less.
See § 6.

---

## 2. Amendment — reopening the fidelity-pinning decision

`docs/tile-art-spec.md` § 6 currently states:

> "**This deliberately pins the substitute set close to the original's appearance.**
> An earlier revision of this spec said the opposite, arguing that a visibly different
> fallback made it obvious which art was loaded. That is dropped: for a
> reconstruction, fidelity is the point, and a fallback set that looks different on
> purpose is a strange thing to ship."

**That decision stands for the set it was written for.** It was correct: a fallback
for a reconstruction should look like the thing being reconstructed.

It is **not adopted here**, for two reasons that did not apply to the 16×16 set:

1. **This set is not a fallback.** It is a theme, selectable alongside `fmtowns`,
   `default`, `EGATHEME.ALT` and the others. A theme is chosen for how it looks. A
   theme that aspires to be invisible is a strange thing to ship, which is the same
   argument the earlier revision made, pointing the other way.
2. **Fidelity is already available.** A player who wants the original's appearance
   supplies the original's art. This set competes with `fmtowns`, not with `pic*`.

**Consequence for the gate.** `tools/tile_rules.py` measures fidelity — density
bands, row and column periods, component counts, aspect ratios, all fitted to the
original's pixels. It is the correct gate for the 16×16 set and it stays wired into
CP4 unchanged. **It must not be applied to this set.** Art that is not trying to
resemble the original will fail those bands as a matter of course, and a gate that
fails the thing it was never meant to measure is not a gate. See § 7.

---

## 3. Technical contract

All of this is supported by the engine today. No loader change is required for this
set, which is the reason for the two choices in § 3a.

| field | value | note |
| --- | --- | --- |
| `tile_w` / `tile_h` | `32` / `32` | parsed and used: `tbytes = tw*th*bpp/8` |
| `bpp` | `4` | **the only value the loader accepts** |
| `pixel` | `nibble-high-left` | high nibble = left pixel |
| `transparent_index` | `0` | index 0 is the transparent pen |
| `palette` | `PALETTE.RGB` | exactly 16 × 3 bytes, 8-bit RGB |
| `identity` | `yes` | art index == tile index, as in `default` |
| `scope` | `overworld,towns` | see § 3c |
| tile bytes | 2048 per tile, 131,072 for 64 | |

### 3a. Why 4bpp and not 256 colours

`engine/src/core/tiles_ega.c` parses `bpp` and then refuses anything but 4:

```c
/* bpp 3 is the control case: a theme that asks for a packing this loader
 * does not implement must not be drawn with a 4bpp guess. */
if (bpp != 4) { bad_reason(... "only 4bpp packed tiles are implemented" ...); }
```

That refusal has a named control. Moving to 8bpp means a new unpack path, a palette
format change (16 entries → 256, 48 bytes → 768), and rewriting a deliberate gate.
**It is deferred, not rejected** — but the burden is on a specific tile to
demonstrate it needs more than 16 colours, not on the palette to provide them.

Sixteen well-chosen colours at 32×32 is a rich medium. It is the constraint that will
make the set feel coherent rather than generated.

### 3b. Why the palette is ours and not fmtowns'

`themes/fmtowns/PALETTE.RGB` is:

```
 0 #000000   4 #00ff00
 1 #0000ff   5 #00ffff
 2 #ff0000   6 #ffff00
 3 #000000   7 #ffffff
 8–15 #000000 (zero-filled)
```

`fmtowns.c:417` is explicit: *"Only 0..7 are measured; 8..15 are zero-filled."* It is
a transcription of a 1990 port's eight-colour mode plus padding, not a designed
palette. Half of it is black, and index 3 — which the text layer assumes is white —
is a second black, which already caused invisible status text once
(`theme-format.md` § 5). There is nothing worth taking.

### 3c. Scope

`overworld,towns` only. Dungeon and tower art is a first-person wireframe with
different primitives and different legibility needs; it is a separate commission and
is not in this document.

---

## 4. The palette

Designed to four constraints, in priority order:

1. **Index 0 is the transparent pen** (`transparent_index = 0`) — reserved.
2. **Index 3 must be the brightest entry.** The text layer is written as index 3 on
   index 0, and the loader adopts the brightest palette entry as text ink. Putting
   white at 3 makes the existing CGA assumption true instead of worked around.
3. **Ground must stay dark enough for white glyphs** (V1) and be mutually
   distinguishable on black (V3).
4. **Creatures must not be confusable with ground** (V2) — so creature hues are kept
   out of the ground ramps.

| idx | hex | name | role |
| --- | --- | --- | --- |
| 0 | `#000000` | black | transparent pen; text background |
| 1 | `#2a7fff` | azure | UI accent; water highlight family |
| 2 | `#ff5f9e` | rose | UI accent; blood, danger, magic |
| 3 | `#ffffff` | white | **text ink**; bone, steel, cloth, letters |
| 4 | `#14512b` | forest | vegetation dark |
| 5 | `#2f9e57` | grass | vegetation mid |
| 6 | `#8fe0a0` | leaf | vegetation light |
| 7 | `#10305e` | deep | water dark |
| 8 | `#2f6fb5` | sea | water mid |
| 9 | `#9fd8f0` | foam | water light |
| 10 | `#4a3826` | soil | terrain dark |
| 11 | `#8f6f45` | earth | terrain mid |
| 12 | `#d8c9a8` | sand | terrain light — **fails V1, see § 5** |
| 13 | `#5c5c6e` | iron | structure dark |
| 14 | `#a8a8bc` | steel | structure mid |
| 15 | `#ffd84d` | gold | treasure, magic, torchlight, the player marker |

Ramp logic: four three-step families (vegetation, water, terrain, structure) plus
black, white, two UI accents and one highlight = 16, with no slot left idle.

**This table is a proposal for review, and § 5 shows it does not yet satisfy its own
constraint.** The contrast figures below are computed (WCAG relative luminance,
`(L_hi+0.05)/(L_lo+0.05)`), not eyeballed.

---

## 5. The open problem: light ground versus white letters

V1 requires letters to stay legible against **every** ground tile they can sit on.
The original solved this almost for free: its ground tiles were dark and its letters
were white.

Measured, white ink (index 3) against each ground candidate in § 4, at a 3:1 floor
for large glyphs:

| idx | colour | ratio | verdict |
| --- | --- | --- | --- |
| 4 | `#14512b` forest | 9.37:1 | pass |
| 5 | `#2f9e57` grass | 3.42:1 | pass |
| 6 | `#8fe0a0` leaf | **1.57:1** | **fail** |
| 7 | `#10305e` deep | 13.07:1 | pass |
| 8 | `#2f6fb5` sea | 5.17:1 | pass |
| 9 | `#9fd8f0` foam | **1.55:1** | **fail** |
| 10 | `#4a3826` soil | 11.14:1 | pass |
| 11 | `#8f6f45` earth | 4.64:1 | pass |
| 12 | `#d8c9a8` sand | **1.64:1** | **fail** |

V3, the same nine tiles against a black backdrop, all pass — the tightest is `#10305e`
at 1.61:1, so the dark end of the palette is only just distinguishable from the
background it sits on.

**The failure is structural, not incidental.** All three failures are the *light step
of a ramp* — leaf, foam, sand. A three-step ramp inevitably puts its top step above
the legibility line, so this is not a matter of nudging three hex values. Three of
nine ground candidates fail, and they are the three that make the ramps read as three
steps.

Three ways out:

| option | what it means | cost |
| --- | --- | --- |
| **A. Cap ground lightness** | no ground tile lighter than index 11 | deletes the light step from three of four ramps — a third of the palette's shading, given up |
| **B. Letters get an outline** | glyphs drawn in index 3 with a 1px index 0 surround | a renderer change, and letters stop being a pure palette-index lookup |
| **C. Promote V1 to a gate** | compute every (letter, ground) pair and refuse the set if any fails | none on its own; it makes A or B *enforced* rather than hoped for |

**Recommendation, revised by the measurement: B + C.** My first draft said "A now, C
as the gate". The numbers argue against that — A does not cost one tile, it costs the
shading strategy that made the palette worth designing in the first place. Outlined
glyphs are what every later game in the series did for exactly this reason, and the
alternative is shipping a palette whose best colours cannot be used for ground.

B is a renderer change and should be argued on its own merits — but it should be
argued now, because choosing A means quietly accepting a smaller palette than § 4
claims. Either way **C is free and should happen regardless**: an unenforced V1 is
prose, and prose is how a set passes review and then ships unreadable letters.

---

## 6. Cleanroom protocol for this set

**The wall is unchanged and this set is more exposed to it, not less**, because the
brief is "draw a good thief" rather than "draw that thief", and the tempting
reference is exactly the material that is off-limits.

The implementer may read this document and nothing else from this repository.

Explicitly forbidden, in addition to everything in `tile-art-spec.md` § 2:

| forbidden | why |
| --- | --- |
| `themes/` | third-party art, including `EGATHEME.ALT`. **ALT is not a style brief.** |
| `themes/fmtowns/` | art derived from copyrighted FM Towns media |
| `docs/tile-art-evidence.md`, `tools/tile_rules.py` | the answer key for the other set |
| `disasm/tile-art.tsv`, `bestiary/` | descriptions and rips made with the originals in view |

The specific hazard this set creates: **"make it look like ALT" is a trace request**,
and it is the natural thing to ask for, because ALT is the theme we like. It must not
be asked. If a hue family in § 4 is inspired by a theme, that is a spec-author
determination recorded as a fact, in the same way § 6 of the older spec is allowed to
record that grass is cyan. The implementer receives colours, not a picture to match.

Provenance: this set needs its own `PROVENANCE.md` before any art is commissioned,
naming this document as its sole input and recording the roles. Do not reuse the
existing one.

---

## 7. Gate plan

| check | status |
| --- | --- |
| `tile_rules.py` fidelity bands | **not applied to this set.** Stays wired to CP4 for the 16×16 set, unchanged. |
| V1 letter/ground contrast | **to be written.** Machine check over every (letter, ground) pair. |
| V2 creature/ground confusability | **to be written.** Silhouette or histogram distance between each creature and each ground tile it can appear on. |
| V3 ground distinguishable on black | **to be written.** Pairwise ground separation measured against a black backdrop, not a white one. |
| palette slot usage | **to be written.** Refuses a theme that leaves entries idle — the failure mode `fmtowns` demonstrates. |
| `theme_v2_check.c` | applies unchanged: geometry, palette, map indirection, refusals. |

The three V-checks exist today as prose in `tile-art-spec.md` § 6. Promoting them is
the cheap half of this work and it is the half that makes "visually stunning"
falsifiable rather than a matter of taste.

---

## 8. Per-tile briefs — format, and a start

Intent only. A brief says what the tile must **communicate at a glance**, never which
pixels are lit. No densities, no periods, no component counts, no measurements of any
existing tile.

Format: `art index — name — what it must read as — constraints`.

| art | name | must read as |
| --- | --- | --- |
| `00`–`03` | water family | open water; calm to rough. Motion, not texture. |
| `04` | mountain | mass and height. Must not read as a building. |
| `08` | grass | walkable ground, the most common tile in the game. **Quiet.** It is the background everything else sits on; a busy grass tile is the classic way an art set fails. |
| `0C`–`0F`, `18`–`1B` | creatures | see below |
| `1D` | empty | nothing. Solid black, per the existing contract. |
| `20`–`39` | letters A–Z | legible on every ground (§ 5). Place names are spelled with these. |

### The creature family — the brief Paul actually asked for

> **`3C` Fighter.** A humanoid that reads as **armoured and heavy** at a glance:
> broad, upright, plate or mail silhouette, weapon held ready. It must be
> distinguishable from a ground tile at the rendered size (V2) and from the other
> three classes by shape alone, before colour. Nothing here specifies the original's
> figure and the artist should not want it to.
>
> **`3F` Thief.** A humanoid that reads as **agile and hooded**: slight, forward,
> crouched or turned, a curved blade. The contrast with `3C` is the point — set the
> two side by side and a player should never confuse them.
>
> **`3D` Cleric**, **`0D`/`0E` wizard-family**: reads as robed and staffed, the
> opposite of `3C` on the bulk axis.

These four are the test case for the whole approach. If a fighter and a thief drawn
to intent cannot be told apart at 32×32, the approach is wrong and it is better to
learn it on four tiles than on sixty-four.

### Remaining work

The other ~50 briefs are not written. They should be written by a spec author who has
looked at the originals and is recording **function** — what the tile is for, what it
must not be confused with — and not appearance.

---

## 9. Deliberately not done here

- **No art.** This is a proposal. Commissioning is a separate decision.
- **No 8bpp.** Deferred with a stated bar (§ 3a).
- **No dungeon/tower scope.** Different medium (§ 3c).
- **No edit to `tile-art-spec.md`.** It is pinned; the reversal is recorded here (§ 2).
- **No answer to § 5.** The measurement says B+C, but B is a renderer change and that
  is a separate decision with its own cost, not a palette footnote.
