# Handoff: build save-scoped worlds

## 0. Status

**Executed 2026-11-05, the day it was written.** S1 through S5 are shipped and
`make -C engine test` is green. What the handoff got right, what it got wrong, and what
is now someone else's problem:

* **S2 and S3 are one commit, not two.** D1 makes the slot directory and the flat
  record the same path, `saves/PLAYER`, so the record has to move in the same change
  as the write target or every store returns NOT_READY. The build order in § 3 could
  not be honoured as written; § 3 is left as it was so the reasoning stays visible.
* **The § 5 gate exists**: `engine/tests/content_immutable.py`, wired into `run_cp4.sh`,
  with its red control running every time. It was additionally demonstrated red against
  a store that wrote back into the layer it read from.
* **An un-migrated install used to be a dead end, and it is fixed.** A tree an older
  engine left with the record as a flat `saves/PLAYER` file could not save at all, and in
  the drop-in layout there is no installer to run. `u2_slot_adopt_legacy()` now moves it,
  before the first load, rename only. Spec § 4b records it as an amendment to D6 with the
  owner's approval, and records the second fix: a refused save no longer leaves ONE MOMENT
  PLEASE! standing, it prints the shipped `(INSERT PLAYER DISK)`.
* **`--install --check`'s `played-in` verdict is gone**, as § 7 said it must be, and the
  four new controls in `install_controls.py` (content drift, delete-a-real-save,
  overwrite-the-slot, dry-run-writes) each go red when their property breaks.
* **Consequence nobody wrote down in advance** (since corrected, see the bullet above):
  an install that had not been migrated used to report the INSERT-disk analogue until
  `--install` ran its migration. That is what prompted the D6 amendment.
* **`tools/save_cross_load.py` was run by hand, both directions**, after the record path
  moved: direction B booted the original and had it load the record the engine wrote at
  the new path. It passes. Do the same on any future save-handling change.
* Still open, and not this document's problem: `--save NAME` (D5), and the two
  pre-existing red gates named below.

Historical content follows, unchanged.

- **Repo:** `/var/home/paul/Projects/decompile-u2-v2`, branch `dungeon-render-diff`,
  HEAD `694cb19` at the time of writing.
- **Design:** `docs/save-scoped-worlds.md`. Read it first; it is the spec. Every decision
  in it is **ruled** (D1-D6, owner, 2026-11-05) so nobody re-argues them.
- **Nothing of this is built.** No code changed. The two commits that exist are the
  proposal and its amendment.
- **Trigger, measured not assumed:** the owner's `mapx20` on jarvis differed from the
  release copy at exactly two bytes, both creature stamps. Restored from the release on
  2026-11-05 via `--install`, so the immediate damage is undone and there is no fire.
- **Pre-existing red gates, not yours:** `cmd_provenance_derived` and `xref_provenance`
  in `tools/check_all.sh`. Everything else must be green.

## 1. Why this exists

Sector write-back currently lands in the **content** directory. Three consequences, all
observed rather than theorised:

1. A player's release maps are permanently edited. `u2_world_store()` writes the
   *stamped* grid, which is faithful — `asm/2305` writes from `word[DS:472]` and every
   live capture shows that word holding `0x1800`, the stamped grid. So creature type
   bytes land in `mapx`. Observed: `(35,23) 0x08 -> 0x30` and `(23,25) 0x0C -> 0xFC`.
2. The damage is sticky. `map_base`, the pristine copy the creature restamp rebuilds
   terrain from, is loaded **from that same file** (`world.c:281`). A baked stamp
   becomes terrain, and the next restamp treats it as ground.
3. `saves/` ends up holding two files differing only by case — the release blank
   `player` and the real `PLAYER` — because the installer seeds the blank
   (`install.c:562-575`) and the engine writes uppercase.

Plus: there is no room for a second save game.

**The fix in one sentence:** content becomes an immutable base, and every mutation goes
into a per-save overlay under `saves/<slot>/`.

## 2. The layout, as ruled

```
data/original/          # read-only after install
data/updated/           # read-only after install
cache/tiles.u2tl        # D4: moved OUT of saves/
saves/PLAYER/           # default slot
  PLAYER                # the 384-byte record
  mapx20, monx20        # only sectors this save changed
config.ini
_INSTALL.tsv
```

Read order for a sector: **slot overlay → `data/updated` → `data/original`**.
Write target: **always the slot, never content.**

Decisions, all settled — do not reopen without the owner:

| # | ruling |
| --- | --- |
| D1 | `saves/PLAYER/PLAYER`, odd-looking but faithful to `U2_SAVE_FILE_NAME` |
| D2 | the original game will not see slot maps; accepted, note it in release notes |
| D3 | only `mapx`/`monx` overlay; `tlkx`, `pic*` and the exe stay pure content |
| D4 | tile cache moves to `cache/` — it was convenience, not save data |
| D5 | layout now, `--save NAME` flag later |
| D6 | the engine creates a slot lazily on first save; the installer only migrates |

## 3. Build order

Deliberately ordered so the **irreversible part comes last**. Do not reorder this without
saying why.

**S1 — read layering.** A slot layer joins the search ahead of content for `mapx`/`monx`
only. Acceptance: a sector placed in `saves/PLAYER/` shadows the content copy; a slot
with nothing in it reads pure content and behaves exactly as today.

**S2 — write target.** `u2_world_store()` always writes the slot. Acceptance: after a
scripted play session, the content tree hashes **identical** to before. This is the
property the whole change exists for, so gate it, do not eyeball it.

**S3 — record path.** `u2_save_path()` becomes `saves/<slot>/PLAYER`. Acceptance:
`save_check`, `save_q_check` and `tools/save_cross_load.py` all green, and
`save_cross_load.py` is **run by hand**, not in CI — it needs `original/` and
`references/`, and per AGENTS.md § 3 it is a local gate.

**S4 — cache move.** `tiles.u2tl` to `cache/`. Acceptance: second run still serves from
the cache (there is already a CP4 check for exactly that, `run_cp4.sh:339`).

**S5 — installer and migration.** Last, because it moves the owner's files. Acceptance:
`--dry-run` shows the plan; running it twice is a no-op; a non-blank lowercase `player`
becomes its own slot and is **never** deleted.

## 4. Exact change sites

Verified against HEAD `694cb19`. Line numbers drift, so treat them as pointers and
re-read the function.

| file | what |
| --- | --- |
| `engine/src/core/roots.h:28,37,43` | `u2_roots[]`, `u2_root_path()`, `u2_root_find()` — where the slot layer joins the order |
| `engine/src/core/roots.c` | the roots table itself |
| `engine/src/core/world.c:409-416` | `u2_world_store()`'s "write back into the layer it was read from" rule, ending in the `root_path()` call — **replace** with "always the slot" |
| `engine/src/core/world.c:281` | `memcpy(w->map_base, w->map, ...)` — the pristine copy, and why § 1.2 is sticky |
| `engine/src/core/save.c:33` | `U2_SAVE_FILE_NAME` |
| `engine/src/core/save.c:41` | `save_dir()` |
| `engine/src/core/save.c:61` | `resolve_save_name()` — role shrinks to inside-a-slot, do **not** delete |
| `engine/src/core/save.c:87` | `u2_save_path()` |
| `engine/src/core/tiles.c:276-281` | cache path built from `save_dir` — this is the D4 convenience |
| `engine/src/core/tiles.c:243-252` | `ensure_cache_dir()`, whose comment is the evidence for D4; lazy creation carries over |
| `engine/src/core/install.c:414` | writes `saves = saves` into `config.ini` |
| `engine/src/core/install.c:562-575` | the `saves/player` seeding block — **remove** |
| `engine/src/core/install.h:50` | `U2_INSTALL_SAVES` |
| `engine/src/core/config.c:23,168,197,222` | the `paths/saves` key; note `saves` is deliberately exempt from the existence check because it is output |
| `tools/save_cross_load.py:140,145,193` | copies `PLAYER` and `mapx`/`monx` into a temp save dir — **must change in the same commit as S3** |

## 5. Gates that must move with the code

- `engine/tests/run_cp3.sh:26` → `save_check`
- `engine/tests/run_cp4.sh:21` → `world_check` (links `world.c` + `roots.c`, so it is the
  natural home for the S1/S2 assertions)
- `engine/tests/run_cp4.sh:240` → `save_q_check` (the P-restore path, links `panel.c`)
- `engine/tests/install_check.c` and `engine/tests/install_controls.py` — new properties:
  content never written after install; a dirty sector lands in the slot; migration is
  idempotent; a non-blank lowercase save is never deleted.
- `tools/save_cross_load.py` — local, on-demand, run by hand whenever save handling
  changes (AGENTS.md § 3).

**The gate that matters most**, and it does not exist yet: hash every file under `data/`,
run a scripted play session that walks and fights, hash again, require zero differences.
Write the control first — make it go red by writing one byte into `data/` — and run that
control from the same directory and ref position CI will use. A gate that cannot fail
where it enforces is the failure mode this project has been bitten by repeatedly.

## 6. The migration — the only irreversible part

From `docs/save-scoped-worlds.md` § 4. Restated because it is where a careless
implementation loses someone's saved game:

1. `saves/PLAYER` is a file and `saves/PLAYER/` does not exist → create the directory,
   **move** the file to `saves/PLAYER/PLAYER`. Never copy.
2. Any `mapx*`/`monx*` dirty in `data/original/`, detected by comparing against the sha
   recorded in `_INSTALL.tsv` → move to `saves/PLAYER/`, restore the content copy from
   its recorded source.
3. A lowercase `saves/player` byte-identical to the release blank → delete. **If it is
   not identical it is a real save and gets promoted to its own slot.** Losing a game to
   a cleanup is the failure this document exists to prevent.
4. Idempotent, prints every move, and a refusal must leave the old layout working.

## 7. Hazards

- **`--install --check`'s `played-in` verdict flips meaning.** Today it tolerates content
  drift as expected. After S2 it must never appear, and appearing is a defect. That is a
  semantic change to an existing verdict — it needs its own controls, and the controls
  need to be able to go red.
- **`resolve_save_name()` must survive.** Its job shrinks to finding a record inside a
  slot, but deleting it reintroduces the case bug it was written for. A release-spelled
  `player` dropped into a slot must still be found.
- **Case-insensitive comparison is a trap in both directions.** A case-sensitive shell
  loop once silently skipped 9 of 11 files and reported "2 differ". Use `strcasecmp`
  deliberately, and verify a count by listing the names, not by trusting `grep -c`.
- **`_INSTALL.tsv` must not record slot overlays.** A slot is player data; the manifest is
  provenance about what the installer wrote from where. Mixing them makes a player's save
  look like installer output and invites a "cleanup" that deletes it.
- **`save_cross_load.py` will pass vacuously if you forget it.** It builds its own temp
  save dir and copies files in. If it points at the old layout it tests nothing and says
  PASS.
- **`make -C engine fresh` complains after a container build.** Not a regression; run
  `make -C engine clean && make -C engine` before `fresh`.
- **Never run the game in place.** `tools/spice86_start.sh` copies pristine
  `references/ultima2/` per launch. The engine's own `--install` into a scratch dir is the
  safe way to test the migration; do not point it at the repo's `original/`.
- **Scratch goes to `/var/tmp`, never `/tmp`** (tmpfs here — it takes RAM from the model).

## 8. Recipes

```bash
# terminal build + full suite
make -C engine && make -C engine test

# SDL builds (no host SDL3; container only)
podman run --rm -v "$PWD":/src:Z localhost/u2-linuxbuild \
  make -C /src/engine sdl sdl-static

# every gate in the project, one verdict per line
bash tools/check_all.sh --fast

# the save-compat gate: LOCAL, on demand, needs original/ and references/
python3 tools/save_cross_load.py

# build a scratch install to test migration against, never the repo's own media
./engine/u2 --install --to /var/tmp/mig-test \
  --original-dir references/ultima2 \
  --fmtowns-media-dir references/fmtowns --dry-run
```

Owner's machine: `jarvis` (ssh alias). Live installs at `~/Games/u2` and
`~/Downloads/u2/ultima2`; both currently carry `u2-sdl-static` md5 `07734756` (matches
the local build at HEAD `694cb19`).
`~/Games/u2` holds a real save named `AAAAA` — **copy it before testing migration
against it**, and note it is `saves/PLAYER` (uppercase) alongside a blank `saves/player`.

## 9. Not in scope

- `--save NAME` and any slot picker (D5: layout now, flag later).
- Changing the stamped-grid write-back itself. It is faithful to `asm/2305`. What changes
  is *where* it lands, so it becomes a deletable overlay instead of permanent damage to
  the player's only copy.
- Proving whether the original's stamp accumulation is itself a bug. That needs a capture:
  play the real game in a copy until monsters stamp tiles, quit, diff its `mapx20`. Worth
  doing, separate ticket, and it is agent-drivable rather than an owner job.
