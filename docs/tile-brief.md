# Tile Brief — what each tile *is*, for the artist

**Status: draft for owner review.** Derived from `disasm/census/tile.tsv` plus the evidence
sources named per entry. This document describes **identity and purpose**. It deliberately does
not describe the 1983 artwork.

Companion to `docs/theme-art-spec.md` (palette, format, the fidelity decision) and
`docs/tile-art-spec.md` (the older 16×16 commission, pinned). Where this document contradicts
the pinned one, § 8 lists it.

---

## 0. What we are making

**The game.** *Ultima II* is a **top-down, tile-based 2D role-playing game** (1983). The world
view is a grid seen from directly above; the player walks one cell at a time, and the camera is
locked to the grid — there is no rotation, no perspective, no depth. Everything in this brief is
drawn for that view: a tile is always seen flat-on, from directly above, at a fixed size, next to
other tiles of the same size.

Two exceptions, both out of scope here: towns and castles are also top-down but denser, and
dungeons and towers are a **first-person wireframe** with completely different byte semantics.
This brief covers the overworld and town tiles only.

**What the tiles have to survive.** A tile is never seen alone. It is seen in a field of hundreds
of copies of itself, beside other terrains, with creatures stamped on top of it and white text
placed over it. Judge every design decision against that, not against a single tile on a blank
background. This is the single most important thing in this document:

> **A tile is a unit of a surface, not an illustration.**

**The format.** 32×32 pixels, one image per art index, 64 indices. Tiles are laid edge-to-edge
with no gaps and no overlap, so any internal border, frame or shading gradient that does not meet
its own edge cleanly will show as a grid across the whole map.

**The palette — and this is the part to read carefully.**

The original ran on **CGA: four usable colours** — black, cyan, magenta, white. That is why its
water is magenta and its grass is cyan. It was not a design choice, it was a hardware limit, and
nothing about it is worth preserving.

**We are not bound by it.** But there is a real constraint in its place, and it is ours rather
than the 1983 machine's:

| option | colours | cost | status |
| --- | --- | --- | --- |
| **A** | **16**, chosen freely | none — the engine loads this today | **default** |
| **B** | **256** | a new unpack path, a palette format change (48 → 768 bytes), and rewriting a deliberate refusal gate that currently asserts 4bpp is the only packing | needs a decision + work |

`engine/src/core/tiles_ega.c` parses `bpp` and refuses anything but 4, with a named control
asserting that refusal. So **B is not a config change.** It is a loader feature.

**The argument for A being enough:** 16 freely-chosen colours is four times what the original had,
and it is what the entire EGA era of games shipped on. What makes a set feel alive is not colour
*count* but whether the colours are **related** — a shaded ramp per material family gives
volume, and volume is what reads as "alive". The palette in `docs/theme-art-spec.md` § 4 is built
that way: four three-step ramps (water, vegetation, terrain, structure) plus black, white, two UI
accents and a gold highlight. Blue water, green land, warm timber, cold iron, a gold for magic.

**The argument for B:** 32×32 is four times the area of 16×16, and at that size a 16-colour
budget shows banding on gradients — sky, water and cloth especially. If the goal is genuinely
"stunning" rather than "good EGA", 16 colours may be the ceiling that matters.

**Decision needed from the owner.** A is free and today; B is a feature. The rest of this brief is
written to work either way — nothing in it depends on the colour count.

**What the palette does not change:** legibility. White text sits on ground tiles, so ground must
stay dark enough to hold it (see `theme-art-spec.md` § 5). Richer colour does not license a pale
floor.

---

## 1. How to use this

Every tile gets: **what it is** → **what it must communicate** → **what it must not be confused
with**. Nothing here tells you what pixels to light. If a line reads like a drawing instruction,
it has gone wrong.

Two numbers identify each tile and you will see both:

- **art** — the index the engine selects by (`art = map byte >> 2`), `00`–`3F`
- **map byte** — the value stored in the world file, because behaviour keys on `map byte >> 1`

They are not interchangeable and confusing them has caused every passability bug in this project.

---

## 2. Lore provenance — read this before trusting a name

A name in this document came from one of four places, and they are not equally trustworthy. Each
entry below carries its grade.

| grade | meaning | weight |
| --- | --- | --- |
| **byte-text** | the name is a string inside `ultimaii.exe` | strongest — the game itself says it |
| **byte-behaviour** | the name follows from what the game *does* with the tile — a routine, a gate, a cost | strongest, and often better than a name |
| **owner** | the owner's knowledge of playing the game | outranks agent inference; every past conflict resolved this way |
| **third-party** | a community wiki or bestiary, joined by bitmap identity | a hypothesis the bytes get to veto. Never promote one into a byte-name |

**The important consequence:** eight of the thirteen creature names are **byte-text** — the game
literally prints them. Four are **third-party only**, and for those the name is a guess wearing a
picture, so the brief has to carry lore instead.

Measured against the binary: `MINAX` ×3, `ENCHANTRESS` ×2, `FIGHTER`/`CLERIC`/`WIZARD`/`THIEF` ×5
each, `ORC` ×5, `KING` ×3, `GUARD` ×2, `MERCHANT` ×1, `JESTER` ×1.
**Absent from the binary entirely: `BALROG`, `BALRON`, `DAEMON`, `DEVIL`, `SEA MONSTER`,
`MOONGATE`, and any string meaning door/portal/gate.**

---

## 3. Terrain

Walkable or blocking ground that tiles the map in bulk. **The hard requirement is seamless
tiling**: a field of one terrain tile must read as one continuous surface, not as a grid of
separate 32×32 pictures. Anything that draws an edge inside the tile will tile badly.

| art | map | name | grade | what it is | must communicate | must not be confused with |
| --- | --- | --- | --- | --- | --- | --- |
| `00` | `0x00` | **water** | byte-behaviour | open sea. Impassable on foot — 22 live walk rejects | a continuous moving surface. Motion, not texture | anything walkable |
| `01` | `0x04` | **swamp** | byte-behaviour + owner | walkable bog. **Costs 5 HP on entry, silently** (`asm/2495`) | wet, treacherous, slightly wrong to be standing on. It is *not* shallow water — you walk on it and it hurts you | water (it is walkable); grass (it damages) |
| `02` | `0x08` | **grass** | byte-behaviour | the most common tile in the game, 60,963 instances | **quiet.** It is the background everything else sits on. A busy grass tile is the classic way a set fails | forest (denser) |
| `03` | `0x0C` | **forest** | owner + spec § 7 | walkable woodland | "more stuff on the ground" than grass, at a glance | grass |
| `04` | `0x10` | **mountain** | byte-behaviour | impassable high ground, blocked live at (11,20) | mass and height. Blocks the player, so it must read as *impossible to cross* | the dungeon entrance `09`, which is also mountain-textured |
| `09` | `0x24` | **dungeon entrance** | byte-behaviour + owner | `site_for_tile` case 9 → dungeon. Walkable | **a cave mouth or door cut into the side of a hill.** The owner's words: "looks like a cave or door in the side of a mountain" | `04` mountain — same texture, but this one has an opening and this one *lets you in* |
| `1C` | `0x70` | **brick road** | byte-behaviour + owner | town paving, 19,433 instances, walkable | worked, laid surface — the ground inside a settlement. **Letters and shopfronts sit on this**, so it must stay dark and quiet | marsh. An older note called it marsh/reeds; the byte evidence says paving |

**Tiling rule for all of the above:** edges must be continuous with themselves. A terrain tile
placed next to a copy of itself must show no seam. Water and mountain form the largest contiguous
regions in the game (mean same-tile orthogonal neighbours 3.62 and 2.81 of a possible 4), so their
tiling is the most visible failure in the set.

---

## 4. Site entrances — five tiles that are doors, not scenery

These are the tiles the player steps onto to *enter another world*. `site_for_tile()` in the
engine dispatches on `art`, and the return value is the sector's ones digit — the ones digit of a
sector number **is** the site kind. That is why the art index and the site kind line up.

| art | map | enters | kind digit | arrival cell inside |
| --- | --- | --- | --- | --- |
| `05` | `0x14` | **village** | 1 | street cell `0x1f,0x3e` |
| `06` | `0x18` | **town** | 2 | street cell `0x1f,0x3e` |
| `07` | `0x1c` | **tower** | 4 | `5,5` |
| `08` | `0x20` | **castle** | 3 | street cell `0x1f,0x3e` |
| `09` | `0x24` | **dungeon** | 5 | `5,5` |

**What they must communicate: "walk here to go in."** Each must read as an *entrance of a
particular kind*, and the five must be distinguishable from each other, because a player navigates
by them — the place-name index is built from these tiles.

Village < town < castle is a scale progression of settlement. Tower and dungeon are the two
adventure entrances and both put the player at `5,5` inside.

**The trap:** `07` looks like a rook, `08` looks like a gate frame, and `05`/`06` look like an
abstract mark and a lattice. Judging them by appearance gets you "ring formation", "emblem",
"tower", "gate" — which is exactly what the older spec did, and it missed that they are one
coherent family of doorways.

---

## 5. The signage family — 27 tiles, one construction

**This is the family that needs the most special handling, and it is the one an artist is most
likely to get wrong.**

These are not letters. Each is a **sign board** — a shop front or a place name, built as a panel.
The bars above and below the glyph are the *frame*, not decoration.

| art | map | part | what it is |
| --- | --- | --- | --- |
| `20`–`2F` | `80`–`BC` | glyph | letters **A–P** |
| `31`–`39` | `C4`–`E4` | glyph | letters **R–Z** |
| `1F` | `7C` | counter | a blank panel — a space, or plain counter face |
| `3A` | `E8` | border right | right end cap of a sign board |
| `3B` | `EC` | border left | left end cap of a sign board |

**There is no Q.** `30` sits in the middle of the range and is the moongate (§ 7). The missing Q
is the proof that this range is a font and not a coincidence, and it is also a trap: an artist
who "completes the alphabet" has drawn a tile the game never uses.

**Construction rule (mandatory, from the pinned spec's amendment):** every glyph tile is a full
panel — a full-width rule along the top edge, a clear gutter, the glyph, a clear gutter, a
full-width rule along the bottom. The earlier text called the rules optional decoration; that
option was taken and **every letter tile came out wrong.** Without the frame a letter reads as a
floating glyph instead of a shop front.

**Legibility rule:** signs sit on town paving (`1C`) and grass (`02`) and must stay readable on
both. White ink on the palette's ground colours measures 4.01:1 worst-case (see
`docs/theme-art-spec.md` § 5). The end caps must join a glyph tile edge-to-edge with no visible
seam — a sign is assembled from multiple tiles at runtime.

**`28` is two things at once.** Map byte `0xA0` is the **site door** — the locked door inside a
town or castle that `UNLOCK` opens — and `0xA0 >> 2 == 0x28`, which is the letter **I**. So the
door genuinely *is* the letter-I glyph reused. It carries both names in the census and neither is
hidden. An artist drawing `28` is drawing a door that must also read as an I.

---

## 6. Creatures and enemies — 13 tiles

Two sub-families, and the difference matters to an artist:

- **NPCs** — do not fight, or fight only if attacked. The player talks to and trades with these.
- **Enemies** — attack the player. Threat is legible in HP and behaviour.

**HP is an art direction tool.** It is measured from the game's own spawn tables, and it tells you
how dangerous something is supposed to look:

| art | map | name | grade | HP | behaviour | what it must read as |
| --- | --- | --- | --- | --- | --- | --- |
| `0C` | `30` | **Orc** | byte-text ×5 | 16 | common enemy | the weakest humanoid foe. Green-brained brute work, but *weak* — do not make it look like a boss |
| `3C` | `F0` | **Fighter** | byte-text ×5 | 128 | enemy | armoured, heavy, upright, weapon ready. Bulk |
| `3D` | `F4` | **Cleric** | byte-text ×5 | — | enemy | robed, holy, blade up. The opposite of `3C` on the bulk axis |
| `3E` | `F8` | **Wizard** | byte-text ×5 | 224 | enemy | robed, staffed, the highest-HP of the four classes |
| `3F` | `FC` | **Thief** | byte-text ×5 | 32 | enemy | slight, hooded, curved blade, agile. **Set against `3C`: a player must never confuse them** |
| `18` | `60` | **Guard** | byte-text ×2 | **500** | enemy | the toughest thing in the set by HP. Heavily armoured, authoritative. Found in castle sectors alongside merchants |
| `19` | `64` | **Jester** | byte-text ×1 | 1 | non-combatant | **1 HP — harmless.** Motley, arms raised, comic. Drawing this as a threat is wrong |
| `1A` | `68` | **Merchant** | byte-text ×1 | 1 | non-combatant, trades | **1 HP — harmless.** Small stature. This is who you buy from |
| `1B` | `6C` | **King** | byte-text ×3 | very high | **does not move or fight back** | seated dignity. High HP but inert — a figure you approach, not fight |

### The four that need lore, because the game never names them

These names are **third-party only** — joined by bitmap identity against a community bestiary, not
found in the binary. Treat the name as a label and the *behaviour* as the real brief.

| art | map | label | grade | HP | measured behaviour | what to draw from that |
| --- | --- | --- | --- | --- | --- | --- |
| `0D` | `34` | Daemon | third-party | 64 | humanoid, arms raised outward | a summoner or caster pose — arms up, active |
| `0E` | `38` | Devil | third-party | 192 | humanoid beside a tall staff with a coloured top | staff-bearer, higher threat than `0D` |
| `0F` | `3C` | **Balron / Balrog** | third-party | **255 — the maximum** | **puts enemies to sleep at range 2** | the most dangerous non-boss in the game. Sleep-inducing, so something with a *head* or *gaze* motif, not a brute with a bigger sword. HP maxed means it should look near-unkillable |
| `0B` | `2C` | Sea Monster | third-party | 64 | **travels on water** | the only creature that lives on the water tile. Must read as belonging to `00`, not to land |

**On Balrog specifically:** the name is not in the binary and the game gives no description. The
brief that *is* earned by evidence is: maximum HP, puts you to sleep from two cells away. That is
a dream or mind attacker, not a fire demon. Anything beyond that is outside this game and should
be labelled as borrowed if used.

### The boss

| art | map | name | grade | what the game says |
| --- | --- | --- | --- | --- |
| `10` | `40` | **Minax** | **byte-text ×3** | the intro poem: *"AND CONQUER TIME ITSELF TO BATTLE / MINAX THE ENCHANTRESS"* |

Minax is the only enemy the game names *and* gives a story line. She is an **enchantress who
conquered time**, and the game's whole premise is travelling through time to fight her. Her
attack is measured: **exactly 100 damage per strike**, subtracting from the hundreds byte of HP,
killing by underflow (`asm/0F8E`). She appears only inside a site. 1,264 instances of her tile
across the shipped maps.

So the brief is not "a big monster". It is **a sorceress who owns time** — robed, regal, and the
only figure in the set that should read as *the reason you are here*.

---

## 7. Hazards, vehicles and the special cases

| art | map | name | grade | what it is | must communicate |
| --- | --- | --- | --- | --- | --- |
| `17` | `5C` | **force field** | byte-behaviour | **costs 1000 HP on entry unless a ring protects** (`asm/24B1`). Messages: `FIELD CAUSES 1000 DAMAGE!` / `RING PROTECTS FROM FIELD!` | a barrier that will kill you. It is walkable in the engine's terms, which means it *looks* passable and is not — that trap is the whole point of the tile |
| `30` | `C0` | **moongate / time door** | byte-behaviour, **observed live** | see below | |
| `11` | `44` | horse | byte-behaviour | boardable (`asm/2FAD`) | a mount you stand on and `B`oard |
| `12` | `48` | ship | byte-behaviour | boardable; ships travel on water only | |
| `13` | `4C` | plane | byte-behaviour | boardable | |
| `14` | `50` | rocket | byte-behaviour | boardable; needs an ankh and tri-lithium to launch | the spaceflight vehicle — four exist in the whole game |
| `15` | `54` | armour rack | byte-behaviour | `cmd_G` empties it: `rand & 3` → CLOTH..CHAIN | a stored-arms rack. **It is a container, not a door** — an older note called it a dungeon mouth |
| `16` | `58` | weapon rack | byte-behaviour | `cmd_G` empties it: `rand & 7` → DAGGER..LIGHT SWORD | ditto. Eight of each per castle vault |
| `1D` | `74` | solid black | byte-behaviour | impassable fill (`asm/361C` stamps it as land) | flat fill. Used for the ending repaint |
| `1E` | `78` | solid white | byte-behaviour | impassable fill | flat fill |

### The moongate, in full — the tile most likely to be drawn wrong

This is **observed**, from a live owner session (`captures/spice86/moongate-cycle`), not inferred:

- A gate is **placed at runtime**. It does not exist in the world file — it appears as map byte
  `0xC0` in the live stamped grid.
- It **transits the player the instant they step onto it.** No prompt, no `E`, no animation gate.
- It is **consumed on use** — the gate list goes empty — and **a new gate appears somewhere else**
  a moment later.
- There are **four gates per era, at fixed cells**, and they rotate on a countdown measured in
  turns. Stepping on gate *k* takes you to era *k* if *k* is below your era, else *k+1* — so gate
  *k* is the two-way link between era *k* and era *k+1*.
- **Earth only.** Off Earth the countdown does not run.
- The five eras are: Pangea, 1423 B.C., 1990 A.D., 2112 A.D. Aftermath, and the Time of Legends.

**What the artist must draw: a magical portal that is not a building.** It is an opening with no
wall behind it, that appears and disappears on a timer and moves you through *time*, not space.
It must read as temporary and unearthly. A solid arch or a doorway in a wall is wrong — those are
`28` (site door) and `08` (castle entrance), and both of those are permanent.

**Neither name is in the binary.** No string meaning door, portal, gate or moon refers to it.
"Moongate" is the community's name and the owner's is "time door". Both are correct as labels;
neither is canonical.

---

## 8. Corrections this brief makes against the pinned spec

Recorded rather than silent, because the pinned document is a legal record for a different
commission.

| art | pinned spec says | this brief says | evidence |
| --- | --- | --- | --- |
| `09` | mountain pass | **dungeon entrance** | `site_for_tile` case 9; owner: "cave or door in the side of a mountain" |
| `1C` | marsh / reeds, "naming contested" | **brick road / town paving** | `asm/2423` paving on map `0x70` |
| `17` | checker border | **force field** | `asm/24B1`, −1000 HP on map `0x5C` |
| `1F`, `3A`, `3B` | edge strip, dark fill A, dark fill B | **sign counter, sign border right, sign border left** | owner, 2026-11-09 |
| `05`–`08` | ring formation, emblem, tower, gate | **village / town / tower / castle entrance** | `site_for_tile` cases 5–8 |

---

## 9. Open — what this brief still cannot say

- **The four third-party creature names.** Daemon, Devil, Balron and Sea Monster have no name in
  the binary. Their briefs above are built from measured behaviour instead, which is honest but
  thin. If the owner has a memory of what these looked like or were called, that outranks the
  wiki and should replace them.
- **`15` and `16`** are still contested in the pinned spec (dungeon mouth or sign; marker or
  sword). The byte evidence says racks, and `cmd_G` empties them — but the art shows an S-curve
  and a diagonal stroke, which is why it was ever ambiguous.
- **Dungeon and tower interiors are not in this brief at all.** They are a first-person wireframe
  over a 16-page grid with completely different byte semantics — `0x80` is rock there and a
  different thing on a sector map. Different commission, different rules.
- **No tile here describes the original's appearance**, deliberately. If you need to know what the
  1983 art looked like, this document is not where you should be looking.
