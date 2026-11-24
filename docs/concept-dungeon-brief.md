# Concept brief — the dungeon: creatures, chests, and loot

**Status: ready to run.** Companion to `docs/concept-shot-list.md` (the overworld
figures), `docs/tile-brief.md` (identity per tile), `docs/theme-art-spec.md`
(format and palette), and `docs/enhanced-dungeon-3d.md` (the renderer these
sprites are drawn by).

The overworld shot list deliberately excluded the dungeon — `concept-shot-list.md`
§ 6 calls it "a separate commission." This is that commission.

---

## 0. Two stages — and the palette is NOT decided here

Read this first, because it governs every prompt below.

**Stage 1 — CONCEPT (this brief).** Make the **strongest possible creature and
object art you can.** Full colour, rich, dramatic, detailed — whatever style
reads best. Do **not** restrict the palette, do not flatten the lighting, do not
force a pixel grid. The job of a concept is to nail *identity*: silhouette,
character, mood, colour direction. A timid, palette-starved concept tells you
nothing.

**Stage 2 — SPRITE (separate, not specified here).** The final dungeon sprite is
a 32×32 billboard on a small shared palette, seen down a corridor under torch
falloff. img2img from an approved concept is a good way to **seed** that stage,
but the sprite is *finished* at target size and palette — it is not a raw
downscale of the concept. A full-colour concept quantised straight down goes
muddy and a silhouette that read at 512px collapses at 32px; the read has to be
designed at target size, not discovered afterwards. (This is the overworld
brief's § 5 lesson, and it is the one place "downsample it" is a trap.)

So the palette question is **deliberately open.** § 6 makes it empirical: count
the colours the finished concepts actually need, and *that* is the 16-vs-more
decision, made from evidence rather than argument.

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
| 6 | Minotaur | 11–16 | — | — |
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

**Sharpened 2026-11-23: one of those three is stronger than the other two, and the grades
should not be lumped.** The gremlin arm is `cmp $0x5,%al` in `asm/12DB_dungeon_events.asm`, and
the message it prints is `A GREMLIN STOLE SOME FOOD!` — **the binary's own string names the
creature on the type byte**. That is not corroboration between a wiki and a formula, it is the
executable pairing a stamp with a word, which is the thing the paragraph above says "nothing in
the binary" does. So the honest split is three grades, not two:

| stamp | name | grade | why |
| --- | --- | --- | --- |
| 5 | Gremlin | **proven** | `asm/12DB` keys on `cmp $0x5` and prints `A GREMLIN STOLE SOME FOOD!` — the string names it |
| 2 | Ghost | corroborated | proven special (torch blown out) matches the third-party special for Ghost; the message does not name it |
| 7 | Balron | corroborated | proven special (sleep) matches the third-party special for Balron; the message does not name it |
| 1, 3, 4 | Orc, Carrion creeper, Viper | **hypothesis** | position in the third-party level-ordered list, and nothing else |
| 6 | Minotaur | **inferred, differential** | the dungeon gives a contact special to exactly three stamps and Balron proves the mechanism carries a creature's signature effect across domains; stamp 6 has none, and the Daemon's whole identity is leg paralysis while the Minotaur has no special anywhere. See *The special asymmetry settles stamp 6* in `docs/dungeon-stroke-art.md` |

**What settled the remaining four is the stroke data, and it is now looked at.** The original ships
a file named `monsters` — 8 blocks of 256 bytes of stroke data, read by `asm/07F3` as one 0x800
block, and indexed by `asm/68ED` as `(stamp & 7) * 256` — so the binary contains a *shape* per
stamp. `tools/dungeon_strokes.py` renders them; **`docs/dungeon-stroke-art.md`** records what each
one is. The result, briefly: stamp 4 plots as a **limbless sinuous serpent** and stamp 3 as a
**domed body with many splaying legs**, and neither shape has an alternative candidate on this
list, so Viper and Carrion creeper move from "position in a wiki list" to corroborated. Stamp 7 is
the **largest figure with a wide fan of wings** — a winged demon whose proven special is the sleep
the guide attributes to a balron, so it agrees twice over. Stamp 2 is a **legless mass drawn
entirely in one translucent half-dither**, which is a plausible way to draw a ghost in four colours
with no alpha. **Stamp 1 stays the weakest row**: a generic humanoid, which is the one shape that
would survive any reordering and therefore proves the least. **Stamp 6's shape does not settle its
name** — a horned humanoid fits both Daemon and Minotaur, and the third-party source itself cannot
choose — but the *specials* do, and they choose Minotaur: see *The special asymmetry settles stamp 6*
in `docs/dungeon-stroke-art.md`.

Design consequence: the three proven creatures should visibly *do* their thing
(a ghost that reads as cold and immaterial, a gremlin that reads as a thieving
vermin, a balron that reads as a mind, not a muscle). The other four just need
to be distinct and to fit a dark dungeon.

---

## 2. Family C — what a dungeon creature concept must achieve

The dungeon is **first-person 3D**: a creature is a front-facing figure standing
on the floor at a distance down a torch-lit corridor. That shapes what a *good
concept* has to deliver, even at full colour and full detail:

- **A silhouette that survives being small.** The final sprite is tiny and dim.
  The concept can be lush, but the *idea* must be readable as an outline alone —
  if it only works because of surface detail, it will not survive to 32px.
- **One high-contrast read.** A pale face, a glowing eye, a bright weapon, a
  cold aura — one element that pops. At runtime the renderer darkens the sprite
  through four torch-falloff bands; a design whose only contrast is mid-tone
  greys vanishes at range. Bake that "one bright thing" into the concept now.
- **Distinct from the other six.** No two of the seven may be confusable when
  shrunk. The legless snake and the low carrion-creeper must never read as an
  upright humanoid.
- **It reads against dark stone, not a void.** The corridor wall is mottled
  grey-brown. A dark creature on a dark wall disappears, so give each one a
  light rim, a pale core, or a luminous accent.
- **One subject per image.** Generate a single figure (or single object) per
  prompt. Asked to draw a row, a model averages faces, fuses silhouettes, and
  botches spacing — a group prompt is asking for weird output. Any comparison
  line-up is assembled afterward in an editor, never generated.

Paste-ready stem (every prompt below is self-contained; this is the shared part,
written down once so the *why* is here):

```text
detailed dark-fantasy video-game creature concept art, full figure standing on
the ground and filling the frame, strong readable silhouette, full colour,
dramatic torchlight against a dark dungeon, plain near-black background, single
figure, no text, no watermark
```

> If you want a concept that is a closer starting point for the pixel sprite, add
> `chunky pixel art, limited palette` to a prompt — but that is a *style choice
> for the concept*, not a requirement, and it trades away some of the "strong."

---

## 3. The seven creatures

Each shot: the third-party name, what it must communicate (with the proven
special, if any, woven in), and a paste-ready prompt. Embellish freely — this is
concept art — but keep each one identifiable by outline alone.

### C1 — Orc  *(the common brute; stamp 1, no proven special)*
The rank and file. There are more of them than anything else, so it is the face
of the dungeon for most of the game. Green-skinned, heavy-browed, hunched
forward, a crude notched blade or club. It should read as *cheap and numerous*
— dangerous in a corridor only because there is always another one behind it.

```text
detailed dark-fantasy video-game creature concept art, full figure standing on
the ground and filling the frame, strong readable silhouette, full colour,
dramatic torchlight against a dark dungeon, plain near-black background, single
figure, no text, no watermark — a green-skinned humanoid brute, heavy brow,
jutting jaw, hunched forward, gripping a crude notched blade. Cheap and numerous,
the rank and file of a dungeon. A pale highlight on the face and blade so it
separates from a dark stone wall.
```

### C2 — Ghost  *(stamp 2 — PROVEN: blows out your torch)*
The one that plunges you into darkness. Immaterial, cold, a shape that is almost
but not quite a body — a hooded or robed silhouette that fades to nothing at the
bottom, with two points of cold light where a face should be. It must read as
*not solid* and as *the reason your light goes out*: pale, faintly luminous, the
one figure that seems to give off its own dim cold glow rather than reflect the
torch.

```text
detailed dark-fantasy video-game creature concept art, full figure filling the
frame, strong readable silhouette, full colour, dramatic torchlight against a
dark dungeon, plain near-black background, single figure, no text, no watermark
— the hooded, robed shape of a ghost that is almost but not quite a body, its
lower half fading into nothing, two points of cold pale light where a face should
be. It gives off its own dim cold glow rather than reflecting light. Immaterial,
silent, the thing that snuffs out your torch.
```

### C3 — Carrion creeper  *(stamp 3, no proven special)*
Low, many-legged, wrong. A bloated or segmented body on too many legs, the kind
of thing that scuttles and that you would rather not touch. It should read as
*vermin and rot* — a dungeon's filth given legs. Give it a wide, low stance so
its silhouette is clearly different from the upright humanoids around it.

```text
detailed dark-fantasy video-game creature concept art, full figure filling the
frame, strong readable silhouette, full colour, dramatic torchlight against a
dark dungeon, plain near-black background, single figure, no text, no watermark
— a low, wide, many-legged vermin-creature: a bloated or segmented body on too
many legs, something that scuttles. Vermin and rot given form. A wide low stance
so its outline is unmistakably unlike an upright figure. A sickly pale accent so
it reads against dark stone.
```

### C4 — Viper  *(stamp 4, no proven special)*
A snake — coiled and reared, head raised, the strike pose. No limbs, just a
sinuous body and a hood or a wedge of a head. It must read as *venom and a quick
strike* and, above all, as a shape with no legs, so it never confuses a human
silhouette at a distance.

```text
detailed dark-fantasy video-game creature concept art, full figure filling the
frame, strong readable silhouette, full colour, dramatic torchlight against a
dark dungeon, plain near-black background, single figure, no text, no watermark
— a snake reared to strike, coiled body, head raised with a hood or a wedge of a
head, tongue or fangs suggested. No legs, a purely sinuous outline. Venom and a
quick strike. A pale marking on the head so it reads against dark stone.
```

### C5 — Gremlin  *(stamp 5 — PROVEN: steals your food)*
The thief of the dungeon. Small, slight, in motion — crouched and reaching, hands
already busy. It should read as *quick, mean, and a scavenger*, the opposite of
the brute: not the thing that breaks down your door but the thing that empties
your pack while you look past it. Give it a bright, busy hand or a stolen
bundle so the thieving reads even small.

```text
detailed dark-fantasy video-game creature concept art, full figure filling the
frame, strong readable silhouette, full colour, dramatic torchlight against a
dark dungeon, plain near-black background, single figure, no text, no watermark
— a small, slight, wiry creature crouched and in motion, reaching forward with
busy hands, a snout or pointed ears, mean and quick. A scavenger and a thief, not
a brute. One bright hand or a small stolen bundle so the thieving reads at a
glance.
```

### C6 — Minotaur  *(stamp 6, no special in either domain)*
The heavy of the dungeon — big, broad, horned, the thing you meet deep down that
you are glad the ghost is not. Where the balron (C7) is a mind, this one is
*muscle and mass*: a hulking horned brute, wide shoulders, a heavy weapon. Make
it the largest, heaviest silhouette in the set so the deep floors feel heavier.

```text
detailed dark-fantasy video-game creature concept art, full figure standing on
the ground and filling the frame, strong readable silhouette, full colour,
dramatic torchlight against a dark dungeon, plain near-black background, single
figure, no text, no watermark — a hulking horned brute, broad shoulders, immense
and heavy, a heavy weapon held low. The most physically imposing thing in the
set, power in the arm and shoulder. The largest silhouette of the seven. A pale
highlight on the horns so it reads against dark stone.
```

### C7 — Balron  *(stamp 7 — PROVEN: puts you to sleep with magic)*
The deep-floor mind attacker. It does not hit you; it reaches into your head and
turns off your consciousness. So it must read as *menace of the mind, not the
arm* — a dominating gaze or a head motif (horns swept back from a wide brow,
glowing eyes, an arcane crest), arms raised or held in a casting gesture rather
than gripping a weapon. The most dangerous thing in the dungeon, and it never
touches you.

```text
detailed dark-fantasy video-game creature concept art, full figure standing on
the ground and filling the frame, strong readable silhouette, full colour,
dramatic torchlight against a dark dungeon, plain near-black background, single
figure, no text, no watermark — a horned demonic figure whose menace is of the
mind, not the arm: a dominating glowing gaze or an arcane head motif, arms raised
in a casting gesture rather than gripping a weapon. The most dangerous creature
in the dungeon and it never touches you. A bright eye or crest so the "mind" read
survives distance and darkness.
```

### C8 — Compose the seven  *(review step, not a prompt)*
Do **not** ask the model for a group shot — it averages faces, fuses silhouettes,
and botches spacing. Generate C1–C7 one at a time (each prompt above is a single
figure), then drop the seven cut-outs side by side in an editor and shrink the
row. That composite is the silhouette test in § 6; it is assembled afterward,
never generated.

---

## 4. Family D — the chest and the loot

The chest is the **only object that sits on a dungeon floor** besides creatures
(terrain bit `0x40`). Opening it yields, per asm/363B (proven by capture
2026-11-09): **gold**, or on the deepest floor **TRI-LITHIUM**, or a **weapon**
(on an even floor) or a piece of **armour** (on an odd floor). Those are the
items that "appear in the dungeon," and they are what this family draws.

Paste-ready stem for objects:

```text
detailed dark-fantasy video-game prop concept art, a single object filling its
frame, full colour, dramatic torchlight against a dark dungeon, plain near-black
background, no text, no watermark
```

### D1 — The chest  *(closed, and open)*
The reward you walk toward. A squat, banded strongbox — iron-banded wood, a hasp,
a lock — clearly *a container you open*, not a wall feature and not a doorway
(an earlier overworld pass mistook an armour rack for a dungeon mouth; do not let
the chest read as an entrance). Draw it **closed** and **open** (lid up, faint
inner glow) as one shot, so the open/closed pair is decided together.

```text
detailed dark-fantasy video-game prop concept art, a single squat object filling
its frame, full colour, dramatic torchlight against a dark dungeon, plain
near-black background, no text, no watermark — a treasure chest shown twice,
closed and open: an iron-banded wooden strongbox with a hasp and a lock, squat
and clearly a container you open, not a doorway and not a wall feature. The open
one has its lid raised and a faint warm glow inside.
```

### D2 — Gold
The common reward. A small hoard — a few coins and a short stack — that reads as
*pile of coins*, warm and bright, unmistakable at a glance and at a distance.

```text
detailed dark-fantasy video-game prop concept art, a single object filling its
frame, full colour, dramatic torchlight against a dark dungeon, plain near-black
background, no text, no watermark — a small hoard of gold coins, a loose pile
with one short stack, warm and bright, unmistakable as money at a glance and from
a distance.
```

### D3 — TRI-LITHIUM  *(the deepest-floor prize)*
The rare one — it only appears on the deepest floor of a dungeon and is the sole
way to obtain it, so it should feel like *the* find. A single crystalline gem or
shard, distinctly not a coin and not an ordinary jewel: faintly self-lit, an
unusual hue, cut like nothing else in the set. It is a quest object, and it
should look like one.

```text
detailed dark-fantasy video-game prop concept art, a single object filling its
frame, full colour, dramatic torchlight against a dark dungeon, plain near-black
background, no text, no watermark — a single crystalline gem or shard, faintly
self-lit in an unusual hue, cut like nothing else, clearly a rare and singular
object rather than a coin or an ordinary jewel. The prize found only at the very
bottom of a dungeon.
```

### D4 — The weapons  *(one per image)*
The chest's weapon arm draws from this set (the game's own names): **dagger,
mace, axe, bow, sword, great sword, light sword**. Generate **one weapon per
image** — a single hand weapon, unmistakable as itself at small size. A phaser
and a quick sword also exist in the game's weapon list but are not chest drops;
add them only if a wider set is wanted later. Compose the comparison row
afterward if you want it.

Per-weapon prompt (substitute the weapon):

```text
detailed dark-fantasy video-game prop concept art, a single <WEAPON> filling its
frame, full colour, dramatic torchlight against a dark dungeon, plain near-black
background, no text, no watermark — clearly a hand weapon held in one hand,
unmistakable as itself at small size.
```

### D5 — The armour  *(one per image)*
The chest's armour arm yields the low tiers (the game's own names): **cloth,
leather, chain**. Generate **one piece per image**, each a wearable thing you put
on; laid side by side afterward they should read as a visible ladder of
protection (flimsy → sturdy → metal) so a player instantly ranks them.

Per-piece prompt (substitute the piece):

```text
detailed dark-fantasy video-game prop concept art, a single piece of body armour
filling its frame, full colour, dramatic torchlight against a dark dungeon, plain
near-black background, no text, no watermark — a wearable torso piece (a rough
cloth shirt / a boiled-leather cuirass / a ringed mail hauberk), readable as
something you put on.
```

---

## 5. Family E — structural objects  *(optional; already procedural)*

These already render in the enhanced build (procedural textures/sprites in
`dungeon_assets.c`), so concept art here is an *upgrade*, not a gap. Lower
priority than Families C and D. If run, keep the same full-colour concept rules
and the same "reads against dark stone, one high-contrast element" goals.

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

Not just an image — three extractable decisions:

1. **A palette, measured.** Pull the colours the finished concepts actually use
   and count them. This is the empirical answer to the palette question from
   § 0 — if the creatures need ~40 colours to keep their read, the small-palette
   plan is wrong and the loader work is justified; if 16 survive, it is not.
   Decide from the shots, not before them.
2. **Silhouette tests.** Composite the individual C1–C7 outputs side by side
   (per C8 — assembled in an editor, never generated) and shrink the row: confirm
   no two of the seven are confusable, and that the legless snake and the low
   carrion-creeper never read as an upright humanoid. Record the failures — they
   are the brief for the sprite stage.
3. **The "small and dim" test.** Shrink each creature and darken it toward black
   (the renderer's four falloff bands). Any creature whose *only* read is mid-tone
   grey has failed: it must keep one high-contrast element (pale face, glowing
   eye, bright weapon) all the way down. This is a test of the *design*, run on
   the concept, even though the falloff itself is applied later at the sprite
   stage.

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
- **Concept is not the sprite.** Same as the overworld list: img2img from an
  approved concept is a fine way to *seed* the 32×32 sprite, but the sprite is
  finished at target size and palette under the falloff and dark-wall
  constraints — not shipped as a raw downscale. That stage is separate and is
  specified elsewhere.
