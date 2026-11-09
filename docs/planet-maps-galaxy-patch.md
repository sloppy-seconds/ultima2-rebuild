# Planet maps and the Galaxy patch

Status: **finding recorded, engine behaviour implemented 2026-11-04.** The custom-placeholder
map is parked (see § 5). Player-facing summary: README.md, "Missing or cloned planet maps";
the same *Ultima II Upgrade* package also carries the EGA art (`docs/ega-graphics.md`).

## 1. The problem

Ultima II shipped on three floppies: a Program Master disk (Earth and its time periods), a
Player Master disk, and a Galactic disk (the other planets). MS-DOS had no directories, so
Garriott reused the same map filenames on the Earth and Galactic disks. On floppies that was
fine — you physically swapped disks when you went into space.

When the game was later collapsed into a single folder for the CD collections (Ultima
Collection, GOG, the EA re-releases), the copy step overwrote the Galactic planet maps with
Earth's era maps, because they shared names. The result: in an unpatched modern copy, flying
to Mercury, Venus, Mars or Jupiter lands you on a clone of an Earth time period. Planet X
(sector 90) survived because its name did not collide, and because X is the only planet needed
to finish the game, nobody noticed before shipping.

The fan-made **Ultima II Upgrade Patch** (Pix's Ultima Patcher) restores the galaxy maps and
also upgrades CGA to EGA. Our pack ships that patch, present but **not applied**.

## 2. Measured against our own bytes

`original/` is the fan pack. It carries the root `mapxNN` files (the merged, partly-broken
set) and an `original/updated/` directory holding the Galaxy patch: `MAPG10..MAPG93`,
`MONG10..`, `RENGAL.BAT`, and a patched `ultima2.com`. `run.bat` offers "1 = original,
2 = updated"; option 2 `cd updated` and runs the patched binary.

Comparing each root `mapxNN` to the patch's `updated/MAPGNN` (md5 of the 4224-byte file):

| sectors | world(s) | root vs patch |
| --- | --- | --- |
| 10, 15, 20, 30, 32, 40, 41, 45 | Mercury, Venus, Mars, Jupiter (1-4) | **DIFFERENT** — root is the Earth-era clone, MAPG is the real planet |
| 44, 50, 60, 61, 70, 71, 80, 81, 82, 85, 90, 92, 93 | Saturn..Pluto, X (5-9) | **IDENTICAL** — no collision, survived the overwrite |

The boundary is exactly the collision: Earth's five time periods occupy sectors 00-49 (the
era is the tens digit, `asm/3546`), so they collided with worlds 1-4 (sectors 10-40). Worlds
5-9 (sectors 50-90) had no Earth counterpart and were never overwritten.

Grade: **measured** — `md5sum original/mapxNN` against `original/updated/MAPGNN`.

The monster-layout files behave identically: `monx`/`mong` mirror `mapx`/`mapg`, and root
`monx10–45` differ from `updated/MONG10–45` while 44/50–93 are identical. One nuance from
`RENGAL.BAT`: it *creates* `mong10/15/30/40/45` by copying `mong20`, so the inner planets
share Venus's creature table rather than each shipping a distinct one.

The third family is the sector **dialogue**: `tlkx`/`tlkg` (the NPC lines asm/22D5 reads into
`DS:476`). `RENGAL.BAT` deletes six of them — `tlkx61/71/81/82/92/93`, the outer-planet site
sectors — because it ships corrected `TLKG` counterparts and the loader tries `tlkx` first.
All six differ from their `TLKG` (measured), so the delete is content-correcting, not
cosmetic. Sectors 32 and 41 also have a differing `TLKG` but are **not** deleted: they are
ambiguous (Earth-era-3/4 *and* Mars/Jupiter sites), so `tlkx` stays for Earth and `tlkg` is
added for the planet, disambiguated at runtime like the maps.

## 3. The disambiguation is the world byte, not the filename

A sector number is genuinely ambiguous: **sector 40 is both Earth-2112 (era 4) and Jupiter
(world 4)**, and sector 20 is both Earth-1423 (era 2, the starting area) and Venus. The
filename alone cannot tell them apart; the world byte `DS:6D` can. `0` means Earth (use the
`mapx` era map); `1..9` means a planet (use the `mapg` planet map). This is why the patched
binary reads `mapg` in space and `mapx` on Earth, and why `RENGAL.BAT` renames the surviving
outer `mapx` files to `mapg` rather than deleting them.

### 3a. The sector model, in one table

`sector = DS:49 * 10 + DS:4A`. Two bytes, and the tens byte carries two different axes:

| `DS:6D` (world) | `DS:49` (tens) means | maps | reached by |
| --- | --- | --- | --- |
| `0x00` Earth | era index, 0..4 | root `mapx00..45` | time doors |
| `0x01..0x08` Mercury..Pluto | world index | `MAPG` in `updated/` | **rocket only** |
| `0x09` Planet X | world index | root `mapx90/92/93` **survived** | rocket |
| `0x0A` deep space | — | — | — |

`DS:4A` is the ones digit and is the **site kind**: `0` is the wilderness map you walk on,
`1..5` are sites. `cmd_G` branches on `cmpb $0x4,[DS:4A]` and `cmd_O` gates on
`DS:4A==2 && DS:49==3`.

**The collision only touched sectors 10-40.** Earth's eras occupy tens 0..4, so only
Mercury, Venus, Mars and Jupiter (worlds 1..4) shared names with something. Saturn, Uranus,
Neptune, Pluto and Planet X are tens 5..9, never collided, and their real maps are still the
root `mapx50/60/61/70/71/80/81/82/85/90/92/93`. That is why Planet X survived and is `mapx`,
not `mapg` — and it is the whole reason nobody noticed the loss before shipping: the planet
you need to finish the game was never missing.

### 3b. Anti-pattern: do not read the sector model off the root file list

This project has drawn the wrong conclusion from the root `mapx` set more than once, and the
reason is that the set is *evidence of the damage, not of the design*. The tempting argument
is:

> "every tens group has exactly one wilderness `X0` plus its own sites, and five time periods
> do not explain ten groups, so the tens digit must be a world."

It is invalid. Ten groups is exactly what **five Earth eras plus a name collision** looks like
from the outside: the merge copied Earth's era maps into the planet filenames, so each
affected group still presents one `X0` and its sites — they are just the wrong world's.

The rule: **the sector model comes from the bytes and from live reads, never from a directory
listing.** `DS:6D` versus `DS:49` across a time door and across a landing is what settled it
(`docs/moongates.md` § 4, `captures/spice86/g39-rocket-flight`), and both `asm/3546` and
`asm/7030` now carry that resolution in their headers. A listing cannot tell you which layer
won a 1996 folder merge.

## 4. Engine behaviour (implemented)

`engine/src/core/world.c` is now world-aware for all three per-sector families
(`mapx`/`mapg`, `monx`/`mong`, `tlkx`/`tlkg` — each collides with Earth eras the same way):

- **On Earth (`world == 0`)**: unchanged — `mapx`/`monx`, then `MAPX`/`MONX`, then
  `mapg`/`mong`, `MAPG`/`MONG`.
- **On a planet (`world != 0`, not deep space `0x0A`)**: prefer the patched map, in this
  order — `updated/MAPG`/`updated/MONG`, `updated/mapg`/`updated/mong`, `MAPG`/`MONG`,
  `mapg`/`mong` — then fall back to the unpatched `mapx`/`monx` bytes when no patched file
  exists.

So the reconstruction reflects the *real* planets when the Galaxy patch is present (in the
content root or in `./updated`), and degrades to the unpatched bytes when it is not. A map
read from `./updated` records a bare `MAPG`/`mapg` prefix so write-back stays legal (it
writes the root spelling, never an `updated/` path).

Gated by `engine/tests/world_check.c`: on Earth sector 40 loads the era map; on Jupiter it
prefers `updated/MAPG40`; with the patched copy removed it falls back to `mapx40`; and the
same three cases hold for the monster layout (`updated/MONG40` over `monx40`) and the
dialogue (`updated/TLKG40` over `tlkx40`, via `u2_world_load_tlkx`).

## 5. Parked: a placeholder map when the patch is absent

If a player's copy has neither the patched maps nor a usable unpatched planet map, the
engine should not show a silently-wrong Earth clone. Parked work item: build a custom
placeholder sector whose tiles spell a readable sign — that the planet maps are missing and
how to remediate (apply the Galaxy patch / run `RENGAL.BAT`, or supply `updated/MAPGNN`) —
and use it as the last-resort fallback for a planet sector with no map at all. Not built;
the current fallback is the unpatched bytes.

## 6. Consequences for prior findings

Any earlier claim about Mercury/Venus/Mars/Jupiter sectors (10-45) that was read off the root
`mapx` files is about Earth-clones, not the real planets, and should be re-checked against
`updated/MAPG*`. The rocket-landing work in `docs/needs-gameplay.md` G59 used sector 40
(Earth) and sector 90 (X), both valid maps, so it is unaffected.
### 2a. Same-name files: what the patch changes about the Earth set

The table above compares `mapx` against `mapg`, which are different files doing
different jobs. The other axis is what `updated/` says about a filename the release
already has, and that is where the corruption fixes live.

Comparing the release against `updated/` by filename, case-insensitively: **59 shared
names, 11 differing.**

| file | difference |
| --- | --- |
| `mapx15` | 260 bytes, starting at byte 3837 of 4224 |
| `mapx10` | 1 byte, at 544 |
| `tlkx03` `tlkx11` `tlkx21` `tlkx22` `tlkx23` `tlkx31` `tlkx32` `tlkx33` `tlkx41` | same 384-byte size, content differs |

`mapx15` is the one to care about: 260 bytes at the end of the file is exactly the
defect `updated/GCHANGES.TXT` describes, where the tail of the sector-15 map holds the
city data belonging to `MAPX11` instead of the dungeon, and a Pangea entrance had been
removed as a consequence. The nine `tlkx` files are dialogue corrections.

Two cautions, both learned the hard way on 2026-11-05.

* **The casing differs between the layers and the comparison must know it.** The
  release spells the dialogue files `tlkx03`; `updated/` spells them `TLKX03`. A
  case-sensitive first pass reported "two files differ" and was wrong by nine, because
  the existence test silently failed and the file was skipped rather than counted. On a
  case-sensitive filesystem the two spellings are two files, so an installer that copies
  both leaves the corrupt one in play depending on which name the engine asks for.
* **The patch has two names and they are the same patch.** `GCHANGES.TXT` is headed
  *"Ultima II -- The Fixed Version, Patch by Mike Marcelais, The Moonstone Dragon,
  April 25, 1996"*, and the upgrade pack carries the same work as a delta:
  `references/up/Files/U2Upgrade/u2up.ini` has `u2-map.pat=Moonstone Dragon's Map Patch
  (1997)` and `base=u2-map.pat`, so the EGA upgrade patches are computed relative to it.
  "Galaxy patch" and "Moonstone Dragon map patch" are one thing seen from two packs.

Grade: **measured** — byte comparison of `references/ultima2/<name>` against
`references/ultima2/updated/<name>` for every shared filename, case-folded. The
installer's rule that follows from it is D8 in `docs/install-design.md` § 12: where a
patch layer and the release both carry a name, the patch copy wins and the release's
spelling of the filename is kept.
