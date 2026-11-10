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

## 2. The shots

Each shot lists **what it must settle**. A shot that settles nothing is decoration; cut it.

### Family A — ground

| # | shot | settles | notes for the prompt |
| --- | --- | --- | --- |
| **A1** | Temperate countryside: open water, grassland, woodland, a rocky ridge, all in one frame | the **core palette** — water, grass, forest, mountain; and how *quiet* grass must be | grass is 60,963 instances, the most-seen tile in the game. If grass is busy, the set fails |
| **A2** | Primordial landscape — young earth, harsher, mineral | whether the eras differ by **hue temperature** rather than new colours | era 0; must stay in the same palette family |
| **A3** | A cobbled town street from above, shopfronts along both sides, a sign board over a door | **brick road** paving + **signage in situ** + how much detail a walkable surface can carry | this is where white text has to stay legible |
| **A4** | A castle vault interior: stone floor, weapon racks, armour racks | racks read as **containers**, not doors; stone vs paving | the racks are emptied by `GET`; eight of each per vault |
| **A5** | A cave mouth cut into the side of a rocky hill | the **`09` dungeon entrance** — the dispute against the older "mountain pass" reading | must read as *an opening you go into*, in the same rock texture as impassable mountain |
| **A6** | A reedy, waterlogged bog beside open water, with a path crossing it | **swamp vs water** — walkable-but-hostile vs impassable | swamp costs 5 HP silently; water stops you. The art must make one look enterable and the other not |
| **A7** | An alien planetary surface — one barren, one with strange growths | whether off-Earth worlds need **new palette entries** or reuse with a hue shift | 9 worlds; a new colour per world would blow a 16-colour budget |
| **A8** | A glowing arch standing in an open field, nothing around it | the **moongate** — temporary, unearthly, not a building | it appears and disappears on a timer and moves you through *time*. A solid doorway is already taken by the site door and the castle |

### Family B — figures

| # | shot | settles | notes for the prompt |
| --- | --- | --- | --- |
| **B1** | A roster line-up: a green-skinned brute, an armoured swordsman, a holy warrior with a blade raised, a robed staff-caster, a hooded figure with a curved blade | the **five humanoid enemies** and their silhouette distinctness | the acceptance test is **swordsman vs hooded-blade** at 32×32 — if they confuse, the set fails |
| **B2** | A town line-up: a heavily armoured watch-guard, a motley fool with arms raised, a small merchant, a seated king | **NPCs must not read as threats** | measured HP: guard 500, fool 1, merchant 1, king very high but inert. The fool and merchant are harmless and must look it |
| **B3** | Four figures: a caster with arms raised, a tall figure with a staff, a heavy horned dream-attacker, a sea creature breaching | the **four unnamed ones** — the game never names these, so the art is the definition | the horned one has max HP and *puts you to sleep at range 2* — a mind attacker, not a fire brute. The sea creature lives on water and nowhere else |
| **B4** | A sorceress who has conquered time | **the boss** | the game's own poem: *"AND CONQUER TIME ITSELF TO BATTLE / [her name] THE ENCHANTRESS"*. She deals exactly 100 damage per strike. The only figure that should read as *the reason you are here* |
| **B5** | A shopfront sign board assembled from pieces: left cap, three letter panels, a blank panel, right cap | the **signage construction** — frame rules, caps joining edge-to-edge, and the **absent Q** | the bars above and below a letter are the *frame*, not decoration. An earlier set treated them as optional and every letter came out wrong |
| **B6** | A horse, a sailing ship, a small aircraft, a rocket on a pad — each as a standalone object | the four **boardable** vehicles read as *you can stand on this and enter it* | a vehicle tile you cannot stand on is a vehicle you cannot board; an older pass refused them all |

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
