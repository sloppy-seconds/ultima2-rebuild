# Concept brief — the dungeon: walls, floor, ceiling, door, ladder

**Status: ready to run, but gated on a one-shot A/B first (see § 5).** Companion
to `docs/concept-dungeon-brief.md` (the creatures and chest — same two-stage
model, same cleanroom rule, read that one first), and `docs/enhanced-dungeon-3d.md`
(the renderer these are drawn by). The current look is procedural in
`engine/src/game/dungeon_assets.c`; this brief is the option to replace it.

This covers the **surfaces** — the things that tile across a corridor — plus the
ladder, which is the one structural element that is really an object.

---

## 0. Two stages, palette open, cleanroom absolute — same as the creature brief

Nothing here changes § 0 of `docs/concept-dungeon-brief.md`. Concept is full
colour and unrestricted; the sprite stage is separate; the palette is decided
empirically from the finished shots, not argued here; the cleanroom rule is
absolute (genre and technical properties only, no game title, no artist, no
ripped sprite from any version — the FM Towns *look* is authorised as intent,
its pixels are not).

What is **different** about surfaces is stated in § 1 and § 2, and it is the
whole reason this is a separate brief: a wall is not a billboard.

---

## 1. What the renderer actually does with these (read before prompting)

Creatures and the chest are **billboards** — one subject, seen once, transparent
background. Walls, floor and ceiling are **tiled textures** — the same square is
repeated many times across a long corridor by the raycaster. That single fact
drives every requirement in § 2, and it is why diffusion art is a *worse* fit for
a wall than it was for an orc, not a better one.

The proven facts that shape the art:

- **Falloff darkens everything.** The renderer shades through four distance bands
  (`u2_dga_shade`), and the procedural textures deliberately use a **light-grey
  base** because the bands darken it — a dark base crushes to black one band out.
  Any concept inherits that constraint: a texture with a baked dark corner or a
  baked light source will read wrong once the engine darkens it again.
- **The door is see-through.** `0xC0` draws as a framed opening with a
  **transparent interior** (the `U2_DGA_TRANSPARENT` sentinel) so the room beyond
  shows through. The door art must therefore mark *where the opening is* so the
  sprite stage knows what to make transparent. It is not a closed door.
- **The secret door is the wall.** `0xE0` renders *exactly* like ordinary rock —
  finding it by walking is the mechanic. So there is **no secret-door art**: it is
  the wall texture, byte for byte, with no tell. Do not make a "secret door" shot.
- **The ladder is one image, not two.** The up- and down-ladder use the *same*
  vertical ladder art; the difference is which horizontal plane has the hole and
  which end the ladder is anchored to (ceiling for up, floor for down). A ladder
  is vertically near-symmetric, so **one concept serves both** — the engine flips
  and re-anchors it. The black hole is a separate flat void on the floor/ceiling
  texture, not part of the ladder.

Current procedural look, as the *intent* to match or beat (not to copy):

| surface | today | must communicate |
| --- | --- | --- |
| wall | grey stone blocks, running-bond courses, dark mortar, sparse pale chips | dressed dungeon masonry, cold, hard |
| floor | 16px flagstone slabs, dark seams, worn chipped faces, light base | walkable stone, worn, light enough to survive falloff |
| ceiling | rough pitted grey rock, darker than the floor | raw cave roof, rougher than the dressed floor |
| door | stone wall + timber-framed opening, transparent interior | *go this way* — an open passage, not a closed door |
| ladder | two brown timber rails, pale rungs, vertical | *climbable*, a way up / a way down |

---

## 2. Hard requirements for the tiled surfaces (wall, floor, ceiling)

These are not style preferences; a texture that fails one of them will look broken
in the corridor even if it looks beautiful on its own.

1. **Seamless / tileable.** The square must wrap edge-to-edge with no visible
   seam. Either prompt for `seamless tileable texture` **and** verify, or run a
   seamless-tiling pass (offset-and-patch) after generation. A raw generation
   almost never wraps clean — assume you will have to make it wrap.
2. **No single dominant motif.** One big distinctive brick or a conspicuous crack
   will repeat visibly down a corridor (the classic tiling tell). Aim for **even,
   low-frequency grain** so repetition is invisible. Uniformity is the goal, not
   interest.
3. **Flat orthographic, no baked lighting.** Front view for walls, top-down for
   floor/ceiling. No perspective, no vignette, no directional light — the engine
   applies its own torch falloff, and a baked light source fights it.
4. **Neutral mid-tone base.** Light enough to survive four darkening bands, not so
   bright it blows out near the torch. Match the procedural "light-grey base on
   purpose" reasoning.
5. **Square 1:1.** The sampler treats textures as squares.
6. **No text, no watermark.** And check the corners after generation — modern
   models hallucinate crisp fake handles and monograms (this bit the creature
   pass). A watermark on a tiled surface repeats across the whole wall.
7. **Style-match the creatures.** The approved creatures are painterly
   dark-fantasy. A photoreal stone wall next to a painterly orc looks like two
   games. Aim for the same painted, torch-lit register.

The **door** is exempt from "seamless" — it is a **single wall-cell panel**, not a
repeating tile. It still owes requirements 3, 4, 6, 7, plus: mark the opening with
a **flat pure magenta `#FF00FF` fill** so the sprite stage knows exactly which
pixels become the see-through interior.

---

## 3. The shots

Each has what it must communicate and a paste-ready prompt. The shared stem for
the three tiled surfaces:

```text
seamless tileable texture, dark-fantasy video game, flat orthographic view,
even diffuse lighting with no shadows or highlights, square 1:1, full colour,
subtle even grain with no single dominant motif, no text, no watermark
```

### S1 — Wall (dressed stone)
Cold dressed dungeon masonry: grey stone blocks in running-bond courses with dark
mortar joints, worn but solid. It is the surface the player sees most, so it must
tile invisibly and stay quiet behind the creatures.

```text
seamless tileable texture, dark-fantasy video game, flat orthographic front view,
even diffuse lighting with no shadows or highlights, square 1:1, full colour,
subtle even grain with no single dominant motif, no text, no watermark — weathered
grey stone blocks in running-bond courses with dark mortar joints, cold dressed
dungeon masonry, worn and solid, evenly lit so it tiles without a visible repeat.
```

### S2 — Floor (flagstone)
Worn square stone slabs with dark seams and chipped faces. Light base on purpose
(falloff darkens it). Reads as *walkable stone*, not dirt, not tile mosaic.

```text
seamless tileable texture, dark-fantasy video game, flat orthographic top-down
view, even diffuse lighting with no shadows or highlights, square 1:1, full
colour, subtle even grain with no single dominant motif, no text, no watermark —
worn square grey flagstone slabs with dark seams and chipped, weathered faces, a
dungeon floor you walk on, light enough in tone to survive being darkened at
distance.
```

### S3 — Ceiling (rough cave rock)
Raw pitted grey rock, rougher and a touch darker than the dressed floor — a cave
roof, not cut stone. Same light-base caveat.

```text
seamless tileable texture, dark-fantasy video game, flat orthographic view, even
diffuse lighting with no shadows or highlights, square 1:1, full colour, subtle
even grain with no single dominant motif, no text, no watermark — rough pitted
grey cave rock, a natural cavern roof, rougher and less regular than dressed
masonry, evenly lit so it tiles without a visible repeat.
```

### S4 — Door (single panel, not seamless)
One wall-cell of the same stone, with a timber-framed **open** passage in the
middle — the way through, not a closed or locked door. The opening interior is a
flat magenta fill so the see-through region is unambiguous.

```text
dark-fantasy video game wall panel, a single square stone wall cell, flat
orthographic front view, even diffuse lighting, square 1:1, full colour, no text,
no watermark — weathered grey dungeon stone blocks with a timber-framed arched
opening in the centre that is an open passage you walk through, not a closed door
and not locked; the opening interior is a flat pure magenta #FF00FF fill marking
the see-through area.
```

### S5 — Ladder (one image; the engine flips it for down)
A single vertical wooden ladder: two timber rails, evenly spaced pale rungs,
weathered wood, seen straight on as a flat vertical strip. **Generate one.** The
up- and down-ladder are the same art; the engine anchors it to the ceiling (up) or
floor (down) and flips it. The black hole it pokes through is a flat void on the
floor/ceiling texture, not part of this shot.

```text
dark-fantasy video game prop, a single wooden ladder, flat orthographic front
view, full colour, two vertical weathered timber rails with evenly spaced pale
wooden rungs, seen straight on as a flat vertical strip, plain near-black
background, no text, no watermark
```

### S6 — Secret door (no shot)
Deliberately nothing. `0xE0` is the wall texture with no tell; finding it by
walking is the mechanic. If a "secret door" concept ever looks different from S1,
it is wrong.

---

## 4. Review — three tests a surface must pass

Not just "does it look nice" — three pass/fail checks, run on the concept:

1. **Tiling test.** Tile the texture 4×4 with **no** post-processing. If you can
   see the grid, it failed requirement 1 and must go through a seamless pass.
2. **Repetition test.** Tile it across a long corridor mock. No conspicuous motif
   may repeat. If your eye catches "there's that crack again," it failed
   requirement 2.
3. **Falloff + clash test.** Darken it through the four bands (it must keep
   reading, not crush to black), then composite an approved creature (the orc) in
   front of it. Two questions: does the wall survive the darkening, and do the
   wall and the creature look like the **same game**? A clash here is a style
   failure, not a texture failure.

**Provenance.** Same rule as the creature brief — save prompt, date, model, seed
in a `PROMPTS.md` beside the outputs.

---

## 5. The honest caveat, and the gate before committing all five

**Procedural is a legitimate final answer here, not a placeholder.** The creature
brief had no choice — you cannot procedurally draw an orc with character. A grey
stone wall is exactly the case where procedural noise is a *good* tool: it tiles
cleanly, has no repetition tell, and takes the falloff predictably. Diffusion
textures have to clear a high bar (seamless + no repeat + style-match) to beat it.

So do **not** commission all five up front. Run the A/B from the last conversation
first: generate **one wall and one floor**, tile them in a corridor mock, and put
them side-by-side with the current procedural render.

- If the diffusion tiles clearly look better and pass § 4 → commission the set and
  wire them in (the sampler already does bilinear + a 256 palette, so textures can
  be higher than 32px now — bump `U2_DGA_TEX_EDGE` and keep them at 64 or 128 to
  push the repeat further out).
- If they show seams, repetition, or clash → **keep procedural and move on.** That
  is a real result, not a failure.

The ladder (S5) is the one worth doing regardless — it is an object, it is small,
and a real wooden ladder reads better than the procedural rails even at low
effort. It is cheap to test alongside the wall/floor A/B.