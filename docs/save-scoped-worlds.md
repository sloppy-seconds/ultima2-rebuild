# Save-scoped worlds: moving sector write-back out of the content directory

Status: **built, 2026-11-05.** Every decision below (D1-D6) was ruled by the owner and
none of them was reopened. S1 (read layering), S2+S3 (write target and record path, in
one commit — see the ordering note below), S4 (the cache) and S5 (installer and
migration) are all shipped,
`make -C engine test` is green, and the migration was run against a real install built
from `references/ultima2` before it was committed.

One ordering correction, recorded where the plan will be read: **S2 and S3 cannot be
separate steps.** D1 makes the slot directory and the flat record the same path,
`saves/PLAYER`, so while the old record is a file at that name the slot directory
cannot exist and every sector write returns NOT_READY. The record has to move in the
same change as the write target. This was found by the CP2 playlog gate going red, not
by reading the plan.

Owner request, 2026-11-05: "we need to make sure any changes to maps go into the save
game path ... in fact maybe we do `./saves/PLAYER/PLAYER` ... that then also sets us up
for multiple savegames in future, and gives us a good dir to put any changed maps into."

## 1. The problem with today's layout

*This section describes the tree as it was before the change, and § 2 onward is what it
is now.*

Sector write-back currently lands in the **content** directory. `u2_world_store()`
writes into the layer the sector was read from (`world.c:415`), which is
`data/original/` or `data/updated/`. Three consequences, all observed:

1. **A player's release maps are permanently edited.** `mapx` is a 4096-byte grid and
   the engine writes the *stamped* grid back, not the pristine one — faithful to
   `asm/2305`, which writes from `word[DS:472]` and every live capture shows that word
   holding `0x1800`, the stamped grid. So creature type bytes land in the map file.
   Observed on the owner's install: `mapx20` differed from the release copy at
   `(35,23) 0x08 -> 0x30` and `(23,25) 0x0C -> 0xFC`.
2. **The corruption is sticky.** `map_base`, the pristine copy the creature restamp
   rebuilds terrain from, is loaded *from that same file*. So a baked creature byte
   becomes terrain, and the next restamp treats it as ground. Long play accumulates.
3. **Two files differing only by case.** The release spells the save `player`, the
   engine's `U2_SAVE_FILE_NAME` is `PLAYER`. `resolve_save_name()` (added 2026-11-05)
   makes the engine prefer the exact `PLAYER` and adopt a case variant only when it is
   absent, so a played install ends up holding both: the blank release template and the
   real game. That is what `saves/` looked like on jarvis on 2026-11-05.

There is also no room for a second save game. One `PLAYER` file, one world.

## 2. The layout

One directory per save game, holding the record and every sector that save has changed:

```
game/
  data/
    original/            # release content, treated as READ-ONLY
    updated/             # Galaxy patch layer, READ-ONLY
  themes/                # unchanged
  cache/
    tiles.u2tl           # D4: derived from the executable, same for every slot
  saves/
    PLAYER/              # the default save slot
      PLAYER             # the 384-byte record, spelled as the engine spells it
      mapx20             # only sectors this save has changed
      monx20
    <slot>/              # a future second save, same shape
  config.ini
  _INSTALL.tsv
```

`--install` stops writing into `data/` after first creation and never edits a content
file again. Content becomes an immutable base; all mutation goes under `saves/`.

## 3. Read and write rules

**Read order for a sector** (first hit wins):

1. `saves/<slot>/<prefix><NN>` — this save's overlay
2. `data/updated/` then `data/original/` — the content roots, as today

**Write target:** always `saves/<slot>/`. Never the content directory. This is the whole
point: a sector write can no longer damage the release files, and `--install --check`
stops reporting content drift as "played-in" because content never drifts.

**The blank template** the installer seeds (`saves/player` today) goes away. A new slot
starts with no overlay at all and reads pure content, which is what a new game should
do anyway. That also deletes the case-collision problem in § 1.3 by construction.

`resolve_save_name()` stays, and its job shrank to finding a record inside a slot
directory rather than guessing between two spellings in one directory. It was not
deleted: a release-spelled `player` dropped into a slot must still be found and still
be the file written back to, and `save_check` now asserts exactly that.

## 4. Migration

An existing install has `saves/PLAYER` (a file) and possibly `saves/player`. Migration
must be **move, never copy, and never overwrite**:

1. If `saves/PLAYER` is a file and `saves/PLAYER/` does not exist: create the directory,
   move the file to `saves/PLAYER/PLAYER`.
2. Any `mapx*`/`monx*` currently dirty in `data/original/` (detected by comparing against
   `_INSTALL.tsv`'s recorded sha) move to `saves/PLAYER/` and the content copy is
   restored from its recorded source. This is the repair for § 1.1 and § 1.2.
3. A lowercase `saves/player` that is byte-identical to the release blank is deleted.
   If it is NOT identical, it is a real save and gets promoted to its own slot rather
   than discarded — losing a player's game to a cleanup is the failure this whole
   document exists to prevent.
4. Migration runs once, is idempotent, and prints what it moved. A refusal must leave
   the old layout working.

## 4b. Amendment, same day: the engine adopts a legacy record, and a refused save says so

Two defects found by testing both layouts after the change, both fixed with owner
approval, and both of them corrections to this document rather than additions to it.

**The upgrade path was a dead end without an installer.** A tree an older engine left
behind holds the record as a FILE at `saves/PLAYER`, which is exactly where the slot
directory has to go, so nothing could be saved at all — and in the drop-in layout
(`--data-dir` at a raw release directory, which is what `--install`'s own refusal message
tells people to do) there is no installer to run. So `u2_slot_adopt_legacy()` does the
move: rename only, never an overwrite, never touching content, and it creates nothing
when there is nothing to adopt, so D6's "an empty slot is a lie" rule still holds. It
runs before the first save load, and that ordering is the whole hazard: adopted after
the load, the game holds a blank state and the first save overwrites the record it just
moved. `tests/save_gate.py` case 6 asserts that end to end and was demonstrated red with
the startup adoption removed — the character came back as six zero bytes with a
green-looking save on top of it.

What stays in the installer is the *content* repair, because telling a release map from
a stamped one needs `_INSTALL.tsv` and the engine has no such record.

**A refused save promised success.** `save_body` echoed ONE MOMENT PLEASE! before
writing and discarded the result of the sector stores, so a Q whose write failed left
the player told the game saved when nothing reached the disk — the silent mutation
`AGENTS.md` refuses in both directions. A failed sector store now counts as a failed
save, and the refusal prints the shipped `(INSERT PLAYER DISK)`, which is what the
original's own DOS wrapper loops on and so is not invented text. Note the asymmetry the
gate had to learn: a read-only slot directory stops the temp+rename a sector write uses
but NOT the record, which is rewritten in place and needs write permission on the file
rather than the directory. Both are asserted separately.

## 5. Decisions, all ruled

| # | question | recommendation |
| --- | --- | --- |
| **D1** | Default slot name. `saves/PLAYER/PLAYER` is faithful but reads oddly, a directory and a file with the same name. | Keep it. It matches the engine's `U2_SAVE_FILE_NAME`, and `--save NAME` selects other slots without renaming anything. |
| **D2** | Does the original game ever share this directory? It reads `mapx` from the game root, so maps we move under `saves/` become invisible to it. | Accept it. The engine's directory is the engine's. Worth stating in the release notes so nobody is surprised running both on one tree. |
| **D3** | Which files overlay? `mapx`/`monx` are written by the game. `tlkx` is never written by anything (docs/save-format.md), so overlaying it buys nothing. | Overlay `mapx`/`monx` only. Keep `tlkx`, `pic*` and the exe as pure content. |
| **D4** | Where does the tile cache `tiles.u2tl` live? | **Out of `saves/` entirely.** It was never save data: `tiles.c:243` says it plainly — "The cache directory is ours, and a fresh install has no 'our own spot' yet", so it used the save dir for convenience. It is derived from the player's `ultimaii.exe`, self-verifying against that exe, and identical for every slot. With this layout there is a proper place: `cache/tiles.u2tl` at the game root. Per-slot would re-extract once per save for no benefit. |
| **D5** | Multi-save UI now or later? The layout enables it; a `--save NAME` flag and a slot picker are separate work. | Layout now, flag later. Building the layout without the flag costs nothing extra and the flag is then small. |
| **D6** | Who creates the default slot directory — the installer, or the engine on first save? | The engine, lazily, the same way `ensure_cache_dir()` already does. A slot that exists but holds nothing is a lie about having a game. The installer only migrates existing ones. **Amended same day, owner approved:** the engine also *adopts* a record the pre-slot layout left behind — see § 4b. It still creates nothing on its own, and it still never touches content. |

### 5b. Amendment, 2026-11-17, owner ruled: a new character rotates the slot

D5 deferred `--save NAME`, and that deferral had a consequence nobody had priced: there is
**one** slot, and a slot holds the sector overlay as well as the record. So a brand new
character loaded the previous character's world — including, as it happened, nine cells a
creature had walked off and left stamped, which the next character met as scenery. The
owner's report was "there's still ghosts in the game, and it's from a brand new character";
the bytes were in `saves/PLAYER/mapx20`, and the new character had nothing to do with them.

The ruling is the owner's: **on character creation, archive the old slot and start a new
one.** Not delete — archive, so the evidence and the old game both survive.

`u2_slot_rotate()` (core/slot.c) renames `saves/PLAYER` to `saves/PLAYER.<YYYYMMDDTHHMMSS>`,
with a `.NN` counter if two creations land in one second, and it is called from
`u2_creation_apply()` after every validation and immediately before the first save of the new
character — so a refused form never costs anyone their save, and a rotation always has a
character to replace what it moved. Three rules come with it:

* **Only a game rotates.** A slot with no record, or a zero-length record, rotates to nothing.
  That is D6 again: an empty slot is not a game, and rotating one would leave a timestamped
  directory for every launch that never reached a save. Gated, not assumed.
* **A real record always rotates**, overlay or not. Creating a second character archives the
  first, which is the point — a character is worth keeping.
* **An archive is never a slot.** Nothing enumerates slots today, so the suffix is inert; when
  D5 lands and something does scan the save directory, it must skip the suffix or a rotated
  world becomes a selectable save that is really a corpse. The rule is written at
  `core/slot.h` where D5 will have to read it.

This is containment, not a cure. A fresh character starts on the release world and will
still accumulate its own footprints; why the engine leaves nine where the original leaves
none is `docs/needs-gameplay.md` G91, and it needs a controlled A/B before anyone patches
the producer. What the rotation removes is the *inheritance*, which is the half the owner
could see.

Gated in `engine/tests/run_api.sh` as AP2c: the overlay moves, the new slot holds a record
and no overlay, a real record is worth an archive, and an empty slot is not. Control:
stubbing out the rotate call goes red on the first three.

## 4a. What `--install` has to do

This is not a small part of the change, and it is where a careless version loses someone's
saved game.

1. **Stop seeding `saves/player`.** `install.c:570` copies the release blank into the save
   directory when nothing is there. Under the new layout a new game starts with no overlay
   and no record until the player actually creates a character, so the seed is removed. That
   is also what deletes the case-collision of § 1.3 by construction rather than by the
   `resolve_save_name()` workaround.
2. **Migrate existing installs** (§ 4), idempotently, printing every move, and refusing
   rather than guessing when it cannot tell what a file is.
3. **`--install --check` learns the new paths** and its vocabulary changes: `played-in`
   should no longer appear for content at all, because content is never written. If it
   does appear, that is now a defect to report, not a state to tolerate. That flips the
   meaning of an existing verdict, so it needs its own controls.
4. **Slot overlays are not install artefacts.** `mapx20` under `saves/PLAYER/` must not go
   into `_INSTALL.tsv`, which records what the installer wrote from what source. A slot is
   player data; the manifest is provenance.
5. **`--dry-run` must show the migration** before it happens. The pre-flight is the only
   chance to catch a migration that would move something it should not.

## 6. What has to change, and what guards it

- `engine/src/core/roots.c` — a save-slot layer joins the read order ahead of content.
- `engine/src/core/world.c` — `u2_world_store()` targets the slot; the "write back into
  the layer it was read from" rule at `world.c:409` is replaced by "always the slot".
- `engine/src/core/save.c` — the record path becomes `saves/<slot>/PLAYER`.
- `engine/src/core/install.c` — stop seeding `saves/player`; add the migration of § 4;
  make `--dry-run` show the migration; keep slot overlays out of `_INSTALL.tsv`.
- `engine/src/core/tiles.c` — the cache path moves from `<save_dir>/tiles.u2tl` to
  `cache/tiles.u2tl` (D4). `ensure_cache_dir()` already creates its parent, so the lazy
  creation behaviour carries over unchanged.
- `tools/save_cross_load.py` — this is the gate that loads a record between the engine
  and the original game. It reads and writes paths directly and **must be updated in the
  same change**, or it will test the old location and pass vacuously. Per AGENTS.md § 3,
  save handling changes require running it by hand, not in CI.
- `engine/tests/install_check.c` and `install_controls.py` — new properties: content is
  never written after install; a dirty sector lands in the slot; migration is idempotent;
  migration never deletes a non-blank lowercase save.

The property worth gating hardest is the one that matters most: **after install, no file
under `data/` is ever modified by running the game.** That is checkable by hashing the
content tree before and after a scripted play session, and it is the exact thing that
went wrong this time.

## 7. What this does not fix

The stamped-grid write-back stays faithful. Creatures still get baked into the overlay
copy of a sector, because the original does that. What changes is *where* it happens:
into a per-save overlay that can be deleted, rather than into the player's only copy of
the release data. If the accumulation turns out to be wrong, the fix is then local to the
overlay and reversible, which it is not today.
