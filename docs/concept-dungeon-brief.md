# Concept brief — the dungeon: creatures, chests, and loot

**Status: ready to run.** Companion to `docs/concept-shot-list.md` (the overworld
figures), `docs/tile-brief.md` (identity per tile), `docs/theme-art-spec.md`
(format and palette), and `docs/enhanced-dungeon-3d.md` (the renderer these
sprites are drawn by).

The overworld shot list deliberately excluded the dungeon — `concept-shot-list.md`
§ 6 calls it "a separate commission." This is that commission.

---

## 0. What this covers, and the one thing that changed

The dungeon is **not** the top-down grid the overworld brief is drawn for. In the
enhanced build it is a **first-person 3D view**: you look down a corridor, and a
creature is a **front-facing billboard standing on the floor at some distance,
lit by a torch that falls off with range.** That single fact changes every rule
below, and it is why this is a separate brief rather than more shots bolted onto
the overworld list.

Covers:

- the **seven dungeon creatures** (Family C),
- the **chest** and the **loot it yields** (Family D),
- the **structural objects** — ladders, the door, the secret door (Family E,
  lower priority; these are already drawn procedurally).

Does **not** cover: the corridor walls, floor and ceiling textures (already
procedural in `dungeon_assets.c`), or the overworld, which is the other brief.

**The cleanroom rule is unchanged and absolute.** These prompts carry genre and
technical properties only — no game title, no artist, no franchise, and no
reference to any ripped sprite from any version of the original. The wiki's
textual notes (a creature "blows out your torch") are *intent* and are fair
game; a picture of the original's sprite is a *trace* and is not. Review is the
outputs, never the wording: anything that looks like a known sprite from any
game gets rejected, whatever prompt made it.

---

## 1. What the bytes actually say (read this before naming anything)

The engine has **no monster name table** and, for the dungeon, no proven
stamp→creature identity either. What *is* proven is behaviour, and the behaviour
is the most useful thing here because it tells us what each creature must
*communicate*.

**The dungeon's danger is light, food, and consciousness — not hit points.**
asm/12DB is the contact routine: when a creature steps adjacent to you it rolls,
acts only one time in four, and switches on the creature's type. Only three of
the seven types do anything, and none of them hit you:

| type | what it does (byte-proven) | the string it prints |
| --- | --- | --- |
| 2 | blows out your torch, but only if it is already lit | `YOUR TORCH IS BLOWN OUT!` |
| 5 | steals one unit of food | `A GREMLIN STOLE SOME FOOD!` |
| 7 | puts you to sleep for a few turns | `YOU FEEL A STRONG MAGIC!` |
| 1,3,4,6 | nothing at all | — |

That is the whole contact system, and it is graded **proven** (the strings and
arms are in `asm/12DB_dungeon_events.asm`; the gremlin arm is gated live by CP7
§ 6d).

**Where the names come from.** The third-party Ultima Codex lists exactly seven
dungeon creatures, each with a level range and, for three of them, a special:

| # | name (third-party) | levels | special (third-party) | matches a proven type? |
| --- | --- | --- | --- | --- |
| 1 | Orc | 1–16 | — | — |
| 2 | Ghost | 3–16 | blows out torch / light | **yes — type 2** |
| 3 | Carrion creeper | 5–16 | — | — |
| 4 | Viper | 7–16 | — | — |
| 5 | Gremlin | 9–16 | steals 100 meals | **yes — type 5** |
| 6 | Daemon / Minotaur | 11–16 | — | — |
| 7 | Balron | 13–16 | puts enemies to sleep | **yes — type 7** |

Lay the wiki's level-ordered list onto the seven stamps 1–7 and the three
third-party specials land exactly on the three stamps that have byte-proven
specials. **Grade that honestly:** it is *corroboration*, not proof. Nothing in
the binary pairs a stamp with a word, and the HP/level numbers are third-party
too — the engine computes dungeon HP from the floor (asm/147F), it does not read
those ranges. So:

> **The three creatures with a proven special (Ghost, Gremlin, Balron) are the
> safe identities — two independent lines agree. The other four names are a
> labelled hypothesis, and the art should stand on its own silhouette even if a
> later capture reorders them.**

Design consequence: the three proven creatures should visibly *do* their thing
(a ghost that reads as cold and immaterial, a gremlin that reads as a thieving
vermin, a balron that reads as a mind, not a muscle). The other four just need
to be distinct and to fit a dark dungeon.

---

## 2. Family C — the rules for a dungeon creature sprite

Paste-ready stem (every creature prompt below is self-contained; this is the
shared part, kept here so the *why* is written down once):

```text
front-facing game sprite, full figure standing upright on the ground and filling
the frame, strong readable silhouette, hand-drawn early-90s pixel art, dark
fantasy dungeon, limited 16-colour palette, flat even lighting, plain near-black
background, no anti-aliasing, no ground shadow
```

What makes the dungeon different from the overworld figures, and what each shot
must therefore survive:

- **It is seen against a dark stone corridor, not a plain background.** The
  silhouette must hold against mottled grey-brown rock. A dark creature on a dark
  wall disappears — give every creature a light rim, a pale core, or a bright
  accent so it separates from the wall at a glance.
- **It is seen at a distance and under torch falloff.** The renderer darkens a
  sprite through four shade bands as it recedes. A sprite whose only contrast is
  mid-tone greys goes to mud at range. Build each one around **one high-contrast
  read** (a pale face, a glowing eye, a bright weapon) that survives being
  darkened.
- **Two sizes only.** Creatures are **tall** — they stand on the floor and fill
  most of the cell height. The chest is **low** — it sits on the floor and is
  squat. Do not draw a creature that floats or that is half the height of a
  companion; the renderer scales them to a common cell.
- **16 colours, shared.** Everything in the dungeon draws from one EGA palette.
  A creature that needs forty colours will not survive quantisation.

**The test that matters for the whole family:** shrink the line-up until each
figure is a few dozen pixels tall and squint. No two of the seven may be
confusable. If two read the same small, the set has failed and nothing else here
rescues it.

---

## 3. The seven creatures

Each shot: the third-party name, what it must communicate (with the proven
special, if any, woven in), and a paste-ready prompt. Embellishment is welcome —
this is concept art, not a sprite sheet — but keep each one identifiable by
outline alone.

### C1 — Orc  *(the common brute; stamp 1, no proven special)*
The rank and file. There are more of them than anything else, so it is the face
of the dungeon for most of the game. Green-skinned, heavy-browed, hunched
forward, a crude notched blade or club. It should read as *cheap and numerous*
— dangerous in a corridor only because there is always another one behind it.

```text
front-facing game sprite, full figure standing upright on the ground and filling
the frame, strong readable silhouette, hand-drawn early-90s pixel art, dark
fantasy dungeon, limited 16-colour palette, flat even lighting, plain near-black
background, no anti-aliasing, no ground shadow — a green-skinned humanoid brute,
heavy brow, jutting jaw, hunched forward, gripping a crude notched blade. Cheap
and numerous, the rank and file of a dungeon. A pale highlight on the face and
blade so it separates from a dark stone wall.
```

### C2 — Ghost  *(stamp 2 — PROVEN: blows out your torch)*
The one that plunges you into darkness. Immaterial, cold, a shape that is almost
but not quite a body — a hooded or robed silhouette that fades to nothing at the
bottom, with two points of cold light where a face should be. It must read as
*not solid* and as *the reason your light goes out*: pale, faintly luminous, the
one figure that seems to give off its own dim cold glow rather than reflect the
torch.

```text
front-facing game sprite, full figure filling the frame, strong readable
silhouette, hand-drawn early-90s pixel art, dark fantasy dungeon, limited
16-colour palette, flat even lighting, plain near-black background, no
anti-aliasing, no ground shadow — the hooded, robed shape of a ghost that is
almost but not quite a body, its lower half fading into nothing, two points of
cold pale light where a face should be. It gives off its own dim cold glow
rather than reflecting light. Immaterial, silent, the thing that snuffs out your
torch. Pale enough to read against dark stone.
```

### C3 — Carrion creeper  *(stamp 3, no proven special)*
Low, many-legged, wrong. A bloated or segmented body on too many legs, the kind
of thing that scuttles and that you would rather not touch. It should read as
*vermin and rot* — a dungeon's filth given legs. Give it a wide, low stance so
its silhouette is clearly different from the upright humanoids around it.

```text
front-facing game sprite, full figure filling the frame, strong readable
silhouette, hand-drawn early-90s pixel art, dark fantasy dungeon, limited
16-colour palette, flat even lighting, plain near-black background, no
anti-aliasing, no ground shadow — a low, wide, many-legged vermin-creature: a
bloated or segmented body on too many legs, something that scuttles. Vermin and
rot given form. A wide low stance so its outline is unmistakably unlike an
upright figure. A pale or sickly accent so it reads against dark stone.
```

### C4 — Viper  *(stamp 4, no proven special)*
A snake — coiled and reared, head raised, the strike pose. No limbs, just a
sinuous body and a hood or a wedge of a head. It must read as *venom and a quick
strike* and, above all, as a shape with no legs, so it never confuses a human
silhouette at a distance.

```text
front-facing game sprite, full figure filling the frame, strong readable
silhouette, hand-drawn early-90s pixel art, dark fantasy dungeon, limited
16-colour palette, flat even lighting, plain near-black background, no
anti-aliasing, no ground shadow — a snake reared to strike, coiled body, head
raised with a hood or a wedge of a head, tongue or fangs suggested. No legs, a
purely sinuous outline. Venom and a quick strike. A pale marking on the head so
it reads against dark stone.
```

### C5 — Gremlin  *(stamp 5 — PROVEN: steals your food)*
The thief of the dungeon. Small, slight, in motion — crouched and reaching, hands
already busy. It should read as *quick, mean, and a scavenger*, the opposite of
the brute: not the thing that breaks down your door but the thing that empties
your pack while you look past it. Give it a bright, busy hand or a stolen
bundle so the thieving reads even small.

```text
front-facing game sprite, full figure filling the frame, strong readable
silhouette, hand-drawn early-90s pixel art, dark fantasy dungeon, limited
16-colour palette, flat even lighting, plain near-black background, no
anti-aliasing, no ground shadow — a small, slight, wiry creature crouched and in
motion, reaching forward with busy hands, a snout or pointed ears, mean and
quick. A scavenger and a thief, not a brute. One bright hand or a small stolen
bundle so the thieving reads at a glance.
```

### C6 — Daemon / Minotaur  *(stamp 6, no proven special)*
The heavy of the dungeon — big, broad, horned, the thing you meet deep down that
you are glad the ghost is not. Where the balron (C7) is a mind, this one is
*muscle and mass*: a hulking horned brute, wide shoulders, a heavy weapon. Make
it the largest, heaviest silhouette in the set so the deep floors feel heavier.

```text
front-facing game sprite, full figure standing upright and filling the frame,
strong readable silhouette, hand-drawn early-90s pixel art, dark fantasy
dungeon, limited 16-colour palette, flat even lighting, plain near-black
background, no anti-aliasing, no ground shadow — a hulking horned brute, broad
shoulders, immense and heavy, a heavy weapon held low. The most physically
imposing thing in the set, power in the arm and shoulder. The largest silhouette
of the seven. A pale highlight on the horns so it reads against dark stone.
```

### C7 — Balron  *(stamp 7 — PROVEN: puts you to sleep with magic)*
The deep-floor mind attacker. It does not hit you; it reaches into your head and
turns off your consciousness. So it must read as *menace of the mind, not the
arm* — a dominating gaze or a head motif (horns swept back from a wide brow,
glowing eyes, an arcane crest), arms raised or held in a casting gesture rather
than gripping a weapon. The most dangerous thing in the dungeon, and it never
touches you.

```text
front-facing game sprite, full figure standing upright and filling the frame,
strong readable silhouette, hand-drawn early-90s pixel art, dark fantasy
dungeon, limited 16-colour palette, flat even lighting, plain near-black
background, no anti-aliasing, no ground shadow — a horned demonic figure whose
menace is of the mind, not the arm: a dominating glowing gaze or an arcane head
motif, arms raised in a casting gesture rather than gripping a weapon. The most
dangerous creature in the dungeon and it never touches you. A bright eye or crest
so the "mind" read survives distance and darkness.
```

### C8 — The line-up  *(silhouette test — run this as one image)*
All seven standing together, so the distinctness test is one glance.

```text
front-facing game sprites in a single row, seven full figures standing upright
and filling the frame, strong readable silhouettes, hand-drawn early-90s pixel
art, dark fantasy dungeon, limited 16-colour palette, flat even lighting, plain
near-black background, no anti-aliasing, no ground shadow — left to right: a
green-skinned hunched brute with a crude blade; a pale hooded shape fading at the
bottom with two cold lights for eyes; a low wide many-legged vermin; a reared
snake with no legs; a small wiry crouching thief with busy hands; a hulking
horned brute with a heavy weapon; a horned demonic caster with raised arms and a
glowing gaze. Each identifiable by outline alone at very small size.
```

---

## 4. Family D — the chest and the loot

The chest is the **only object that sits on a dungeon floor** besides creatures
(terrain bit `0x40`). Opening it yields, per asm/363B (proven by capture
2026-11-09): **gold**, or on the deepest floor **TRI-LITHIUM**, or a **weapon**
(on an even floor) or a piece of **armour** (on an odd floor). Those are the
items that "appear in the dungeon," and they are what this family draws.

Paste-ready stem for objects:

```text
front-facing game sprite, a single object filling its frame, hand-drawn early-90s
pixel art, dark fantasy dungeon, limited 16-colour palette, flat even lighting,
plain near-black background, no anti-aliasing, no ground shadow
```

### D1 — The chest  *(closed, and open)*
The reward you walk toward. A squat, banded strongbox — iron-banded wood, a hasp,
a lock — clearly *a container you open*, not a wall feature and not a doorway
(an earlier overworld pass mistook an armour rack for a dungeon mouth; do not let
the chest read as an entrance). Draw it **closed** and **open** (lid up, faint
inner glow) as one shot, so the open/closed pair is decided together.

```text
front-facing game sprite, a single squat object filling its frame, hand-drawn
early-90s pixel art, dark fantasy dungeon, limited 16-colour palette, flat even
lighting, plain near-black background, no anti-aliasing, no ground shadow — a
treasure chest shown twice, closed and open: an iron-banded wooden strongbox with
a hasp and a lock, squat and clearly a container you open, not a doorway and not
a wall feature. The open one has its lid raised and a faint warm glow inside.
```

### D2 — Gold
The common reward. A small hoard — a few coins and a short stack — that reads as
*pile of coins*, warm and bright, unmistakable at a glance and at a distance.

```text
front-facing game sprite, a single object filling its frame, hand-drawn early-90s
pixel art, dark fantasy dungeon, limited 16-colour palette, flat even lighting,
plain near-black background, no anti-aliasing, no ground shadow — a small hoard
of gold coins, a loose pile with one short stack, warm and bright, unmistakable
as money at a glance and from a distance.
```

### D3 — TRI-LITHIUM  *(the deepest-floor prize)*
The rare one — it only appears on the deepest floor of a dungeon and is the sole
way to obtain it, so it should feel like *the* find. A single crystalline gem or
shard, distinctly not a coin and not an ordinary jewel: faintly self-lit, an
unusual hue, cut like nothing else in the set. It is a quest object, and it
should look like one.

```text
front-facing game sprite, a single object filling its frame, hand-drawn early-90s
pixel art, dark fantasy dungeon, limited 16-colour palette, flat even lighting,
plain near-black background, no anti-aliasing, no ground shadow — a single
crystalline gem or shard, faintly self-lit in an unusual hue, cut like nothing
else, clearly a rare and singular object rather than a coin or an ordinary jewel.
The prize found only at the very bottom of a dungeon.
```

### D4 — The weapons  *(line-up)*
The chest's weapon arm draws from this set (the game's own names): **dagger,
mace, axe, bow, sword, great sword, light sword**. Each must read as itself at
small size and as *something you hold in a hand*. A phaser and a quick sword also
exist in the game's weapon list but are not chest drops; include them only if a
wider weapon set is wanted later.

```text
front-facing game sprites in a single row, each a single object filling its own
frame, hand-drawn early-90s pixel art, dark fantasy dungeon, limited 16-colour
palette, flat even lighting, plain near-black background, no anti-aliasing, no
ground shadow — a line-up of hand weapons, left to right: a short dagger, a
studded mace, a broad axe, a simple bow, a straight sword, an oversized great
sword, and a slender glowing light-sword. Each unmistakable as itself at small
size and clearly held in one hand.
```

### D5 — The armour  *(line-up)*
The chest's armour arm yields the low tiers (the game's own names): **cloth,
leather, chain**. Each should read as *a thing you wear*, and the three should
read as a visible ladder of protection (flimsy → sturdy → metal), so a player
instantly ranks them.

```text
front-facing game sprites in a single row, each a single object filling its own
frame, hand-drawn early-90s pixel art, dark fantasy dungeon, limited 16-colour
palette, flat even lighting, plain near-black background, no anti-aliasing, no
ground shadow — a line-up of body armour shown as wearable pieces, left to right:
a rough cloth shirt, a boiled-leather cuirass, and a ringed mail hauberk. A clear
ladder of protection from flimsy to metal, each readable as something you put on.
```

---

## 5. Family E — structural objects  *(optional; already procedural)*

These already render in the enhanced build (procedural textures/sprites in
`dungeon_assets.c`), so concept art here is an *upgrade*, not a gap. Lower
priority than Families C and D. If run, keep the same front-facing, dark-wall,
falloff-safe rules.

- **E1 — The ladders.** An up-ladder (climb out through a hole in the ceiling)
  and a down-ladder (descend through a hole in the floor): wooden rails, pale
  rungs, a black opening in the horizontal plane. They must read as *climbable*
  and as *a way up vs a way down* at a glance.
- **E2 — The door.** A framed wooden opening set in a stone wall, an open
  passage you walk through (not locked, not a wall). It must read as *go this
  way*.
- **E3 — The secret door.** A fake wall: it looks *exactly* like the ordinary
  rock it is embedded in, because finding it is the mechanic. The concept here is
  deliberately "indistinguishable from the wall" — the only note is that it must
  match the corridor rock texture perfectly, with no tell.

---

## 6. What each shot must produce, and how to review

Not just an image — three extractable decisions, same discipline as the
overworld list:

1. **A palette.** Pull the colours the finished dungeon shots actually use and
   count them. The dungeon draws on the 16-colour EGA palette; if the creatures
   need materially more to survive falloff, that is a real finding, not a
   suggestion.
2. **Silhouette tests.** C8 (the line-up) is the test: shrink it and confirm no
   two of the seven are confusable, and that the legless snake and the low
   carrion-creeper never read as an upright humanoid. Record the failures — they
   are the brief for the sprite stage.
3. **Falloff tests.** Darken each creature through four steps toward black (the
   renderer's four shade bands). Any creature whose *only* read is mid-tone grey
   has failed: it must keep one high-contrast element (pale face, glowing eye,
   bright weapon) all the way down.

**Provenance.** Save the prompt text, date, model and seed beside the outputs (a
`PROMPTS.md`). Once the prompt is the provenance, it is evidence, and it is what
makes the record defensible later.

**Review is not optional.** A genericised prompt reduces the risk of reproducing
a specific copyrighted sprite; it does not remove it. Something that looks like a
known creature from any game gets rejected, whatever prompt produced it.

---

## 7. Open questions and honest caveats

- **Identity is a hypothesis, not a measurement.** The stamp→creature mapping is
  unproven. The three creatures with byte-proven specials (Ghost, Gremlin,
  Balron) are the safe identities; the other four names ride on the level-order
  alignment and could be reordered by a future capture. The art must stand on its
  silhouette regardless.
- **HP and level ranges are third-party.** The engine computes dungeon HP from
  the floor (asm/147F); it does not read the wiki's numbers. Do not treat those
  ranges as proven.
- **The FM Towns and EGA rips are not the reference.** They are copyrighted
  traces. This brief is intent-only by design; the moment a prompt or a review
  starts pointing at a ripped sprite, it has left the cleanroom.
- **Concept is not the sprite.** Same as the overworld list: do not downscale
  these into 32×32 dungeon sprites. The sprite stage is separate and must be
  designed at target size, under the falloff and dark-wall constraints in § 2.
