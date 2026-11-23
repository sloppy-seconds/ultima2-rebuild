# Helm Automap — Specification

**Status: specified, not built.** Owner rulings of 2026-11-23 are recorded below and are
final unless he reopens them. This document is the home of the design;
`docs/enhanced-layout-plan.md` § 7a keeps its number and points here, because other
documents cite § 7a.

Everything here is an **enhancement**, not a fidelity target. Nothing in the original
has a persistent automap, so almost none of this can be byte-proven, and the grade column
says which parts are ours. Where a claim about the original *is* grounded, it names the
routine or the capture.

---

## 1. The two artefacts, which are not the same thing

The owner's rulings split this feature in two, and conflating them is the main way to get
it wrong:

| | **helm map** | **dungeon automap** |
| --- | --- | --- |
| surfaces | overworld sectors, town/site sectors | dungeon and tower levels |
| made when | a helm is held | a helm is held |
| revealed by | the viewport — what is on screen now | what the renderer actually drew this frame |
| persisted | **yes** | **no** — session only |
| stored | a catalogue of recorded sectors, not pixels | in memory, dies on reload |
| survives death | yes (it is in the save) | no |
| survives reload | yes | **no, and the UI must say so** |

The asymmetry is the owner's ruling, not an oversight: *"dungeon maps do not get
persisted, but the helm maps would, same for overworld and town maps, our helm maps will
need to be persisted, also maps are only made when there's a helm."*

### The recorder fiction

> the helm is a magical or technological device with a recorder function that builds maps

That is not flavour, it is the mechanism, and it has three consequences worth stating
because each one is a decision someone would otherwise re-litigate later:

1. **No helm, no recording.** A character with zero helms never gains a map, never
   reveals a dungeon cell, and writes nothing to the save.
2. **Recording is passive.** Holding a helm records; nothing is spent. The original's
   `V` still costs one HELM in both modes and is unchanged — the enhancement adds a
   display, it does not replace the original's behaviour.
3. **Losing the last helm stops recording; it does not erase what was recorded.** A map
   that was made exists. This is the cheaper and more forgiving reading, and it is the
   one adopted here. The alternative — maps vanish with the helm — was rejected: it
   punishes the player for an event they may not have caused, and it makes a helm a
   liability rather than a tool.

---

## 2. What "persisted" means, and what must not be persisted

A sector is 64 × 64 tiles (`U2_SECTOR_EDGE`, `U2_SECTOR_TILES` = 4096, proven at
`asm/50B3` and `asm/2290`). Its terrain comes from the player's own `mapxNN`. So
**persisting a rendering of a sector map would store 4096 bytes of data the engine can
already derive from content the player supplied** — a second copy of the world, which is
exactly what the save overlay rules exist to prevent.

What is *not* derivable is **which sectors this character recorded**. So:

> **Persist the catalogue, not the pixels.** A recorded sector is re-rendered from
> `mapxNN` on demand, exactly as `helm_map_draw` does today.

### Format

One new file in the save slot: `saves/<slot>/helmmap.bin`.

| bytes | content |
| --- | --- |
| 0–3 | magic `U2HM`, and version byte 1 in byte 3 |
| 4 | count of entries that follow |
| 5+ | 3 bytes per entry: `world`, `tens`, `ones` — the same triple the engine already uses to name a sector |

At most 50 sectors exist per world and the engine knows the whole set, so the file is
under 160 bytes and needs no compression, no index and no pagination.

**Why a triple and not a sector number.** `DS:49 * 10 + DS:4A` is the sector, and the
tens byte carries two different axes depending on `DS:6D` — era on Earth, world off it.
Storing the resolved number would bake in that ambiguity; storing the triple keeps the
file readable by anything that knows the world model, and `docs/planet-maps-galaxy-patch.md`
§ 3a/§ 3b is the reason nobody should re-derive that axis from a directory listing.

### This amends a ruled decision, and says so

`engine/src/core/slot.h` D3 rules that the slot overlay is *not* a content root and that
only `mapx`/`monx` overlay it. `helmmap.bin` is a third file class. That is an
**explicit amendment to D3**, not a violation of it: D3's purpose is to stop the slot
from becoming a shadow copy of the game tree, and a 160-byte record of what the character
mapped is character state, not content. The amendment must be written into `slot.h` in
the same commit as the writer, or the next reader will treat the file as a bug.

Consequences that come free from existing rulings:

- **Slot rotation archives it.** `u2_slot_rotate()` renames the whole slot on character
  creation, so a new character never inherits the last one's catalogue. No new code.
- **Archived slots are never scanned.** The existing rule that an archive must not be
  read as a slot covers this file too.
- **Cross-load is unaffected.** The original has no such file and never will, so
  `tools/save_cross_load.py` has nothing to disagree about. It still must be run when the
  writer lands, because save handling changed.

---

## 3. Reveal rule: "what I can see right now"

The owner's framing — *"the lit radius makes sense, and for overworld/town I guess it
would be the viewport, so in all cases it's roughly what I can see right now"* — is the
rule. Per surface:

| surface | revealed set | source |
| --- | --- | --- |
| overworld | the cells inside the live viewport | the view module already knows them |
| town / site sector | same | same |
| dungeon / tower | the cells the wireframe actually drew this frame | `u2_dungeon_probe` |

**One correction to the phrase "lit radius", and it matters.** The dungeon's light is not
a radius. It is a counter (`DS:26`) that a torch or the LIGHT spell refills and each step
burns down; `u2_dungeon_light_step()` returns whether there is light to draw with, and
when there is, the renderer samples cells through `u2_dungeon_probe`. So the honest
implementation of "what I can see" is **the set the probe sampled while there was light**
— which is depth- and wall-limited, not circular. A fixed radius would be an invention
that shows you through walls, and would be *less* faithful to the owner's intent, not
more. Adopted: reveal = what the probe drew.

Two free and correct consequences:

- **In darkness nothing is revealed.** No light, no probe, no new cells. That is right,
  and it needs no special case.
- **Turning reveals.** The wireframe's visible set depends on facing, so a turn can
  reveal cells without movement. That is consistent with "what I can see".

Dungeon geometry is not sector geometry: a dungeon is 16 pages of 16 × 16 with its own
byte semantics (`0x80` is rock there, `0x40` is a chest), and `docs/art-index.md` records
that dungeons do not stamp the grid the same way. So the automap needs **its own per-page
grid**, allocated on entry and freed on exit. It must not alias the sector view.

---

## 4. Display

- **Enhanced mode only.** Classic shows what the original shows and nothing more. This is
  a hard constraint, not a preference: classic is the fidelity surface.
- One panel box, extent asserted before anything draws into it.
- Terrain only. The original's `V` view is a density ladder over terrain
  (`helm_map_draw`, fitted from frames); creatures are not shown, and adding them would
  be a second invention on top of the first.
- A dungeon automap that is empty **because the session just started** must be visually
  distinguishable from one that is empty because the player has not walked anywhere.
  Session-only persistence is otherwise a silent lie, and the no-silent-mutation rule
  applies to omissions as much as to state changes.

---

## 5. Gates, in the order they should be written

A feature with no way to go red is the failure this project names constantly, and the
enhanced panel has already been sitting that way. The gates come first, not last.

| # | gate | what it proves |
| --- | --- | --- |
| G0 | panel box extents tile without overlap and match the measured rectangle | the § 7 gate that never landed; nothing draws into an unasserted box |
| G1 | classic mode draws no automap and `V` still costs one HELM | the enhancement did not leak into the fidelity surface |
| G2 | catalogue round-trips: record, save, reload, still recorded | persistence works |
| G3 | zero helms: no map made, no `helmmap.bin` written, no dungeon cell revealed | the recorder gate holds |
| G4 | dungeon automap is gone after reload, and the UI said so | session-only is real, not aspirational |
| G5 | shrinking the viewport reveals fewer cells; a dungeon turn reveals the probe's set and no more | the reveal rule is the drawn set, not a guess |
| G6 | rotating the slot archives the catalogue with the character | the free consequence is actually free |

G5 is the one that would have caught the "lit radius" misreading, which is why it is in
the list rather than left to review.

---

## 6. Open questions

| question | grade | who |
| --- | --- | --- |
| Does recording a sector happen on first entry while holding a helm, or continuously? | inferred — the original's `V` draws the whole sector at once (`captures/spice86/vhelm-sector-map-live`), so "record the whole sector once" is the faithful reading | decidable now, no capture needed |
| Do sector stamps (creature and object nibbles) appear in a recorded map? | inferred — stamps are live state, not terrain; recommend terrain only, matching `V` | owner, if he cares |
| Does a helm map show the sector the player is *in*, or every sector ever recorded, browsable? | open — the panel box is small; browsing is a second feature | owner |
| Is a town a separate entry from its surrounding sector? | measured — a town is a site sector with its own number, so it is its own entry | settled by the data |

---

## 7. Non-goals

- No change to classic mode, in any respect.
- No change to `V`, its cost, or its rendering.
- No persisted dungeon fog.
- No creature, monster or NPC display on any automap.
- No new art. The density ladder and the existing tile art are reused; if substitute art
  is ever wanted for the automap, it goes through the cleanroom procedure and may not
  cite `asm/`.
