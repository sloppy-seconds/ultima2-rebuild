# Concept shot list — what to generate, and what each shot must settle

**Status: ready to run.** Companion to `docs/tile-brief.md` (identity per tile) and
`docs/theme-art-spec.md` (format, palette, the fidelity decision).

This is a list of **concept** shots. Concept art is not the source of the tiles — see § 5. The
mistake this document exists to prevent is collapsing concept into sprite.

---

## 1. Two prompt families — do not mix them

The game uses a **mixed projection**: the ground is seen from above, the figures on it are seen
from the front. Our own census proves it — the Orc is described as *"arms down, legs apart"* and
Minax as *"a wide cape or robe"*. None of that is visible from directly above.

One prompt covering both produces top-down orcs, which are unrecognisable blobs. So:

### Family A — ground (top-down, tiling)

> `flat top-down orthographic ground texture, seen from directly above, seamless tiling, no
> internal border or frame, no vignette, hand-drawn early-90s pixel art, medieval fantasy,
> limited palette, flat even lighting, no anti-aliasing, single surface filling the frame`

### Family B — figures (front-facing, filling the cell)

> `front-facing game sprite, full figure standing upright and filling the frame, strong readable
> silhouette, hand-drawn early-90s pixel art, medieval fantasy, limited palette, flat even
> lighting, plain dark background, no anti-aliasing, no ground shadow`

**Deliberately absent from both:** any game title, any artist's name, any franchise. Genre and
technical properties only — genre is not copyrightable, a specific work's expression is.

---

## 2. The shots — one self-contained prompt each

Each prompt below is **complete and copy-pasteable**: it carries its own family stem, its subject
and its constraints, so nothing needs to be concatenated from § 1. § 1 explains *why* there are
two families; the prompts here are the finished form.

A shot that settles nothing is decoration. Cut it rather than run it.

---

### Family A — ground (top-down, tiling)

#### A1 — Temperate countryside
**Settles:** the core palette — water, grass, forest, mountain — and how quiet grass must be.
**Tiles:** `00` `01` `02` `03` `04` `09` `1C`

```text
flat top-down orthographic ground texture, seen from directly above, seamless tiling, no internal
border or frame, no vignette, hand-drawn early-90s pixel art, medieval fantasy, limited palette,
flat even lighting, no anti-aliasing — four distinct ground surfaces side by side in one frame:
deep open water with a continuous low-contrast ripple; short grassland that is deliberately quiet
and low-contrast; denser woodland with rounded crowns clearly busier than the grass; and a bare
rocky ridge of crossed strata that reads as impossible to cross. The grass must be the calmest
surface in the image.
```
**Check:** grass is the most-seen tile in the game (60,963 instances). If it competes with the
woodland for attention, the set fails here.

#### A2 — Primordial landscape
**Settles:** whether eras differ by hue temperature rather than by new palette entries.
**Tiles:** the same seven, recoloured.

```text
flat top-down orthographic ground texture, seen from directly above, seamless tiling, no internal
border or frame, hand-drawn early-90s pixel art, limited palette, flat even lighting, no
anti-aliasing — a harsh young world: mineral shallows, sparse tough vegetation, black rock and
pale scree, a hot dry colour temperature. Same kinds of surfaces as a temperate landscape, shifted
in hue rather than given new colours.
```
**Check:** if this needed colours A1 did not have, the era budget is wrong.

#### A3 — Cobbled town street with shopfronts
**Settles:** brick road paving, signage in situ, and how much detail a walkable surface can carry.
**Tiles:** `1C` `28` plus the sign family in use.

```text
flat top-down orthographic ground texture, seen from directly above, seamless tiling, hand-drawn
early-90s pixel art, limited palette, flat even lighting, no anti-aliasing — a narrow medieval
town street paved in worn cobble or brick, quiet and low in contrast, with building fronts along
each side and a painted sign board hung over one doorway. The paving is a surface the player walks
on and white text must stay readable over it, so it must not be busy.
```
**Check:** squint at it. If the paving is the loudest thing in the frame, it is too loud.

#### A4 — Castle vault interior
**Settles:** that racks read as containers, not doors; stone versus paving.
**Tiles:** `15` `16` plus wall and floor.

```text
flat top-down orthographic game environment, seen from directly above, hand-drawn early-90s pixel
art, limited palette, flat even lighting, no anti-aliasing — a stone vault or armoury interior:
cold flagstone floor, and freestanding storage furniture holding weapons on one side and shields
and mail on the other. The racks are open containers that a figure can stand beside and take things
from; they are not doorways and not alcoves.
```
**Check:** an earlier pass mistook the armour rack for a dungeon mouth. If it still reads as an
entrance, it failed.

#### A5 — Cave mouth in a hillside
**Settles:** the `09` dungeon entrance, against the older "mountain pass" reading.
**Tiles:** `04` `09`

```text
flat top-down orthographic game terrain, seen from directly above, hand-drawn early-90s pixel art,
limited palette, flat even lighting, no anti-aliasing — a rocky hill mass of crossed strata with a
dark cave mouth cut into its side. The same rock texture fills most of the frame and reads as
impassable, except for the one opening, which reads unmistakably as a place you walk into.
```
**Check:** the opening must win. If the rock texture dominates and the mouth is a detail, the
dungeon entrance is invisible.

#### A6 — Bog beside open water
**Settles:** swamp versus water — walkable-but-hostile against impassable.
**Tiles:** `00` `01`

```text
flat top-down orthographic ground texture, seen from directly above, seamless tiling, hand-drawn
early-90s pixel art, limited palette, flat even lighting, no anti-aliasing — open deep water on one
side and a reedy waterlogged bog on the other, with a faint trodden path crossing the bog. The
water must read as impassable. The bog must look enterable but wrong to stand in: saturated,
stagnant, slightly hostile, with reed texture rather than water motion.
```
**Check:** the bog is walkable and quietly damages you; the water stops you outright. If the two
read the same way, one of them is wrong.

#### A7 — Alien planetary surfaces
**Settles:** whether nine worlds need new palette entries or a hue shift on existing ones.
**Tiles:** off-world terrain.

```text
flat top-down orthographic ground textures, seen from directly above, seamless tiling, hand-drawn
early-90s pixel art, limited palette, flat even lighting, no anti-aliasing — two alien planetary
surfaces: one barren and dusty with cracked mineral crust, one bearing strange low growths in an
unusual hue. Both must feel clearly off-world while staying inside a small shared palette.
```
**Check:** nine worlds against a 16-colour budget. Count how many new colours this shot demands.

#### A8 — A glowing arch in an empty field
**Settles:** the moongate — temporary, unearthly, not a building.
**Tiles:** `30`

```text
front-facing game sprite on a plain dark background, hand-drawn early-90s pixel art, limited
palette, no anti-aliasing — a free-standing arch of light or energy, nothing around it, no wall,
no building, no frame of masonry. It is a portal that appears in open ground, transports the
traveller somewhere else, and then vanishes. It should look temporary and unearthly rather than
built: luminous edges, an interior that is clearly an opening, and no door, gate or leaf.
```
**Check:** a solid doorway is already taken twice over by the site door and the castle entrance.
If this could be locked, it is wrong.

---

### Family B — figures (front-facing, filling the cell)

#### B1 — The five humanoid enemies
**Settles:** silhouette distinctness among the enemies a player meets most.
**Tiles:** `0C` `3C` `3D` `3E` `3F`

```text
front-facing game sprites, full figures standing upright and filling the frame, strong readable
silhouettes, hand-drawn early-90s pixel art, medieval fantasy, limited palette, flat even lighting,
plain dark background, no anti-aliasing, no ground shadow — a line-up of five enemies: a green-skinned
brute with a simple weapon; a heavily armoured swordsman, broad and upright; a holy warrior in
robes raising a blade; a tall robed caster leaning on a long staff; and a slight hooded figure in
motion carrying a curved blade. Each must be identifiable by outline alone at very small size.
```
**Check:** shrink to 32×32. The armoured swordsman and the hooded curved-blade figure must never
be confusable. If they are, the set fails and nothing else here matters.

#### B2 — The town figures
**Settles:** that non-combatants do not read as threats.
**Tiles:** `18` `19` `1A` `1B`

```text
front-facing game sprites, full figures standing upright and filling the frame, strong readable
silhouettes, hand-drawn early-90s pixel art, medieval fantasy, limited palette, flat even lighting,
plain dark background, no anti-aliasing, no ground shadow — a line-up of four town figures: a
watch-guard in heavy plate, imposing and authoritative; a motley fool with arms thrown up, comic
and slight; a small merchant, plainly dressed and unthreatening; and a seated king, crowned,
dignified and still. Only the guard is meant to be feared.
```
**Check:** measured HP is guard 500, fool 1, merchant 1, king very high but completely inert. The
fool and the merchant must look harmless at a glance.

#### B3 — The four the game never names
**Settles:** the four creatures with no name anywhere in the game's own text — the art is the
definition.
**Tiles:** `0B` `0D` `0E` `0F`

```text
front-facing game sprites, full figures standing upright and filling the frame, strong readable
silhouettes, hand-drawn early-90s pixel art, dark fantasy, limited palette, flat even lighting,
plain dark background, no anti-aliasing, no ground shadow — a line-up of four creatures: a robed
caster with both arms raised outward as if summoning; a tall figure holding an upright staff with
a bright head; a heavy horned brute radiating menace, the most physically dangerous thing in the
set, whose power is of the mind rather than the arm, so give it a dominating gaze or head motif
rather than a bigger weapon; and a scaled sea beast breaching with an open mouth, clearly a creature
of open water and unable to move on land.
```
**Check:** the horned one has maximum HP and puts enemies to sleep at range two — a mind attacker,
not a fire brute. The sea creature lives on the water tile and nowhere else.

#### B4 — The sorceress who conquered time
**Settles:** the boss.
**Tiles:** `10`

```text
front-facing game sprite, full figure standing upright and filling the frame, strong readable
silhouette, hand-drawn early-90s pixel art, dark fantasy, limited palette, flat even lighting,
plain dark background, no anti-aliasing, no ground shadow — an enchantress who has conquered time
itself. Regal, robed in a wide cape, commanding, and the only figure in the set that should read as
the reason the player is here. Not a monster and not a warrior: an immortal sorceress. Suggest
mastery of time through her bearing and her ornaments rather than through size or muscle.
```
**Check:** she deals exactly 100 damage per strike and appears only inside a site. She should look
inevitable, not merely strong.

#### B5 — A sign board assembled from pieces
**Settles:** signage construction — frame rules, caps joining edge-to-edge, and the absent Q.
**Tiles:** `1F` `20`–`2F` `31`–`39` `3A` `3B`

```text
a horizontal row of adjacent square game tiles that together form one painted wooden sign board
hanging over a shopfront, hand-drawn early-90s pixel art, limited palette, flat even lighting, no
anti-aliasing — from left to right: a left end cap, three panels each bearing one bold capital
letter, one blank panel with no letter, and a right end cap. Every panel carries a full-width rule
along its top edge and another along its bottom edge; those rules are the frame of the board and
are required, not decorative. The panels must join edge-to-edge with no visible seam so the board
reads as one object assembled from tiles.
```
**Check:** there is no Q in this alphabet — one slot in the range is a portal instead. An earlier
set treated the top and bottom rules as optional and every letter tile came out wrong.

#### B6 — The four boardable vehicles
**Settles:** that each reads as something you stand on and enter.
**Tiles:** `11` `12` `13` `14`

```text
front-facing game sprites, each filling its own square frame, strong readable silhouettes,
hand-drawn early-90s pixel art, limited palette, flat even lighting, plain dark background, no
anti-aliasing, no ground shadow — four vehicles as standalone objects: a saddled horse; a small
masted sailing ship; a light single-engine aircraft; and a rocket standing upright on a pad. Each
must read as something a person can board and operate, not as scenery or decoration.
```
**Check:** a vehicle tile you cannot stand on is a vehicle you cannot board. An earlier pass
refused all four as impassable, which made them unusable.

---

**14 shots.** A1, A3, A5, A6, A8, B1, B4 and B5 are the eight that settle the most; if budget is
tight, run those first.

---

## 3. What each shot must produce

Not just an image — three extractable decisions:

1. **A palette.** Pull the dominant colours from the finished shots and count them. *That* is the
   answer to the 16-vs-256 question, empirically rather than by argument. If the shots need ~40
   colours to survive, the 16-colour plan is wrong and the loader work is justified.
2. **Silhouette tests.** B1 and B2 must be checkable at 32×32: shrink them and confirm Fighter ≠
   Thief, Guard ≠ King. Record the failures — they are the brief for the sprite stage.
3. **Read failures.** Any shot where the intended thing is not obvious in two seconds is a shot
   that did its job: it found the problem while it was still cheap.

---

## 4. Provenance and review — two cheap rules

**Save the prompts.** Once the prompt is the provenance, it is evidence. A `PROMPTS.md` beside the
outputs recording prompt text, date, model and seed costs nothing and is what makes the record
defensible later.

**Review is not optional.** A genericised prompt *reduces* the risk of reproducing a specific
copyrighted sprite; it does not remove it. Something that looks like a known sprite from any game
gets rejected, whatever prompt produced it. The check is the outputs, not the wording.

---

## 5. What happens after — the stage this list deliberately does not cover

```
concept shots (free camera)      → identity + palette
        ↓
SPRITE SHEET                     → two rows: ground tiles (top-down, tiling)
                                   and figures (front-facing, filling the cell)
        ↓
TILES at 32×32, drawn natively   → not downscaled from the concept
```

The sprite sheet is a separate artefact and it is where the projection and tiling constraints
actually bite. **Do not downscale concept art into tiles.** Terrain will not seam, creature
silhouettes collapse, and a full-colour source quantised to a small palette goes muddy. The read
has to be designed at target size, not discovered afterwards.

That stage is not specified here. It needs the palette decided, which needs the shots run.

---

## 6. Open

- **16 or 256 colours** — deferred to § 3.1 deliberately. Do not decide it before the shots.
- **The four unnamed creatures** — B3 is where they get defined. If the owner has a memory of
  what these looked like, that outranks the wiki and should be written into the shot first.
- **Dungeon and tower interiors are not here.** First-person wireframe, different byte semantics
  (`0x80` is rock there and a different thing on a sector map). Separate commission.
- **Era count for A2/A7.** Five Earth eras plus nine worlds is fourteen surfaces; the shot list
  samples two. Whether each needs its own look is a decision the first pass should inform.
