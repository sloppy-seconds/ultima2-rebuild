# Theme format — what `--ega-theme-dir` reads

Status: **specification, 2026-11-04.** This is the contract; the handover that
produced it is `docs/archive/handoff-themes-builder.md` and the user-facing page is
`docs/ega-graphics.md`. Where this file and the code disagree, the code is what
ships and this file is the bug.

Scope: the two theme formats one themes directory may hold, how a theme is
resolved and discovered, what each field means, and what is refused. Dungeon and
tower art is out of scope for both formats today (`docs/fmtowns-graphics.md` § 4).

Grades per `AGENTS.md` § 5. Everything here is measured from the shipped files or
implemented in `engine/src/core/tiles_ega.c`; nothing is recalled.

---

## 1. Why there are two formats

`EGATILES` is 8320 bytes = 65 x 128: a **headerless flat array** of 16x16 tiles at
4bpp packed, high nibble = left pixel. No index table, no size field, no palette
field. It can express exactly one statement — "tile N lives at N x 128" — and that
is enough for the fan pack because EGA tile N aligns 1:1 with CGA tile N (proven by
the A-Z letter tiles landing at 32..57 in both atlases, `docs/ega-graphics.md`).

An art set that does not align has nowhere to say so. The FM Towns banks are that
case: `art 0x0C` is `ENEMY2/16`, not tile 12; one DOS index maps to a *run* of FM
tiles; the tiles are 32x32; and the palette is not the EGA one. A flat array cannot
carry any of that, so v2 adds a manifest.

## 2. Directory layout and discovery

One directory holds both formats, and Ctrl-T cycles across them together:

```
<themes dir>/
  EGATILES                  v1: the "default" theme
  EGATHEME.<NAME>/EGATILES  v1: theme "<name>" (lower-cased by the engine)
  <name>/THEME.INI          v2: theme "<name>"
```

* **Resolution order for a named theme:** `<name>/THEME.INI` first, then
  `EGATHEME.<UPPER>/EGATILES`. A pack that ships both spellings means the author
  meant the manifest, and the manifest is the one that states geometry.
* **Discovery** (`u2_tiles_ega_next_theme`) recognises the file `EGATILES` as
  `default`, any directory `EGATHEME.*` as a v1 theme, and any other directory
  containing a `THEME.INI` as a v2 one. Names are sorted alphabetically so the
  cycle order is directory order and not `readdir` order.
* **Names are matched case-insensitively on purpose.** The pack ships
  `EGATHEME.10` and `EGATHEME.ALT` in different cases, and a copy that came off a
  FAT volume arrives uppercased. Refusing to look is indistinguishable from the
  theme not existing.
* **An explicit `--ega-theme-dir` is authoritative with no fall-through**
  (`docs/ega-graphics-plan.md` § 2, `[LOCKED]`). A wrong path is reported, never
  papered over by finding the theme somewhere else.

## 3. v1: `EGATILES`

No header. The file must be at least 64 x 128 bytes; the engine reads the first 64
tiles and ignores the 65th. Geometry is fixed: 16x16, 4bpp, stride 8, identity
mapping, built-in EGA palette, and it claims every scope. A file that exists and is
shorter is `U2_TILES_EGA_BAD` with a size in the message, which is distinct from
absent and stops the search rather than falling through.

A v1 theme may also carry `EGACOLOR` and `PIC*.EGA` beside its `EGATILES`. Neither
is read by this engine: `EGACOLOR` is a colour-index remap for the fan pack's own
driver (`docs/ega-graphics.md` § 3.1) and per-theme pictures are not resolved
because `pic_ega.c` looks at the top level of the themes directory only. They are
copied by the builder so nothing is lost when that changes.

## 4. v2: `THEME.INI` and its files

`THEME.INI` is `[key] = [value]` lines; `#` and `;` are comments, `[section]`
headers are skipped, keys are case-insensitive, and surrounding whitespace is
stripped from both sides. Unknown keys are ignored, so a manifest can grow without
breaking an older engine.

| key | required | meaning |
| --- | --- | --- |
| `name` | no | cosmetic; the directory name is the theme name |
| `tile_w`, `tile_h` | yes | tile size in pixels. `tile_w` must be a whole multiple of 16 and at most 32 |
| `bpp` | yes | bits per pixel. **Only 4 is implemented**, packed, high nibble = left pixel |
| `pixel` | no | only `nibble-high-left`; any other value is refused rather than guessed at |
| `palette` | no | filename of a `PALETTE.RGB` beside the manifest |
| `dng_wall` | no | filename of a `DNGWALL.TILE` beside the manifest — one 32x32 dungeon wall texture (see below) |
| `map` | no | filename of a `MAP.TSV` beside the manifest. **Absent means identity**, which is what keeps every v1 theme valid |
| `identity` | no | documentation only; the presence of `map` is what the loader reads || `scope` | no | comma list from `overworld`, `towns`, `dungeon`, `tower`. An unrecognised word is a refusal |
| `source`, `tiles_pool`, `art_count`, `art_with_multiple_frames`, `crosswalk`, `frames` | no | provenance and policy notes; the loader reads `frames` as nothing and the builder writes it as a record |

**`TILES`** is the pool: `tile_w * tile_h * bpp / 8` bytes per tile, and the file
size must be an exact multiple of that. At most 128 tiles.

**`PALETTE.RGB`** is exactly 16 x 3 bytes of 8-bit RGB. Absent means the built-in
EGA table in `core/frame.c`. When present the renderer uses it instead, and
re-reads it when a Ctrl-T cycle changes the theme, so a cycle from a themed palette
back to `c64` restores the EGA table rather than keeping the wrong colours.

**`DNGWALL.TILE`** (named by `dng_wall`) is exactly 1024 bytes — one 32x32 tile of
palette indices, one byte each, in the theme's own palette space (for the FM Towns
theme, `k_pal`). The enhanced-mode 3D dungeon samples it across the wall quads with
perspective foreshortening. It is **not** 4bpp-packed like `TILES`; it is raw indices,
because the corridor reads it as an index lookup, not a blit. The indices need no remap:
under a v2 theme the uploader already maps frame indices through the theme palette, so a
wall index lands on the same colour the theme's terrain tiles use. Absent means the
corridor draws its flat placeholder fill. The FM Towns derivation bakes it from `DUNG1`
tile 1 (a flat brick); which tile, and whether towers use a different bank, is open
(`docs/enhanced-dungeon-3d.md` § 5).

**`MAP.TSV`** is tab-separated, `#` comments, one row per art index:

```
0C	17,16	ENEMY2/17,ENEMY2/16
```

Column 1 is the DOS art index in hex, column 2 is a comma list of pool indices,
column 3 is free-text provenance and is **not parsed**. The tile list ends at the
first whitespace; parsing past that boundary turns `ENEMY2/16` into a third frame
at tile 0, which no other check notices. Art indices outside `00..3F`, pool indices
outside the atlas, a row with no tiles, more than 8 frames in a row, and a file with
no rows are each refused with the filename in the message.

The engine draws `list[0]` and keeps the rest of the list in
`u2_tiles_ega.map[art][]` with `map_n[art]`, so animation is a renderer change and
not a format change. An art index the map does not cover returns -1 from
`u2_tiles_ega_tile()` and the cell is left alone — drawing a guess is the failure
this exists to prevent.

## 5. What the theme decides about the display

### 5a. The map covers every art selection, including the player

`MAP.TSV` is not a lookup for terrain only. Every art index the renderer wants —
including the avatar the game composites on top of the view — is an **art** index,
and it must pass through the theme's map before it can address the atlas. The
atlas is indexed by *pool entry*, and the two are different number spaces the
moment a theme carries a real table.

This is worth writing down because getting it wrong was invisible. Passing the art
index straight at the atlas is correct for every v1 theme, because their map is
identity by definition, so the defect waited for the first v2 theme with a real
table. There, art 60 is the avatar and pool index 60 is a capital **O**: the
player became a letter, and no byte-level check noticed, because every mapping
file was exactly right. The crosswalk said `3C → ENEMY2/8`, the map said `3C → 72`,
and the atlas had the Fighter at 72. Only the renderer's last step was wrong.

The rule, then: a theme that does not cover an art index draws nothing there
rather than guessing, and that applies to the player marker exactly as it applies
to a tile. `engine/tests/overlay_map_control.py` keeps the claim honest by putting
the raw blit back and requiring the check to go red; it had passed for an entire
format revision before anyone looked.

**The frame size is a property of the art mode, not of the theme.** CGA composites
at 320x200. `--ega` composites at 640x400 whatever the active theme's tiles are
(`U2_EGA_FRAME_SCALE`), and a theme whose tiles are smaller than that is
block-upscaled into it. The atlas still reports its own `scale = tile_w / 16` —
its *native* scale — and the blit factor is `frame_scale / native`, so a 32x32 FM
Towns theme is blitted 1:1 and a 16x16 fan-pack theme becomes 2x2 blocks.

Two things follow, and both are deliberate.

* **A Ctrl-T cycle cannot change the window.** Sizing the frame per theme makes a
  cycle across tile sizes a renderer resize, and a renderer that sized its window,
  texture and staging buffer once at startup while the frame grew underneath it
  writes past its own buffer. With the size fixed by the art mode, cycling
  `c64 -> fmtowns -> c64` changes the pixels drawn and nothing else.
* **A v1 EGA dump is a doubled image, not a 1:1 record of the fan pack's pixels.**
  Accepted by the owner (2026-11-04). Nothing on screen changed, because the SDL
  window was already 2x (`renderer_sdl.c` `SDL_SCALE`) with nearest-neighbour
  filtering doing the identical doubling one step later; what changed is that the
  doubling now happens in the frame, so `--dump-frame` for a v1 theme writes
  640x400 of 2x2 blocks. `theme_v2_check` pins the relationship exactly: the x2
  frame must be the x1 frame with every pixel replaced by a 2x2 block, and the ink
  count must be exactly 4x, so a decimation or a mis-sampled block fails rather
  than rendering plausibly.

A theme whose native scale is *larger* than the frame's is refused at load with a
named reason, because the alternative is decimating the art — the one thing the
format exists to avoid. `U2_FRAME_MAX_SCALE` is 2; a bigger ask is refused by name.

Two writers, and the split is the whole design:

* `u2_frame_put` — a **physical** pixel. Tile blits use this, at the blit factor
  above, so a 32x32 tile contributes its own 32x32 pixels and nothing is decimated.
* `u2_frame_put_logical` — one pixel of the **1x layout** as a `scale x scale`
  block. Text, the cursor, the death-ring mask and the full-screen pictures use
  this, so the status band stays the same proportion of the screen instead of
  shrinking into the corner.

**Text ink follows the palette, not the CGA constants.** The whole text layer is
written with CGA white (index 3) on black (index 0), and the FM Towns palette has
index 3 as a second black — so a themed palette drawn with that pair produces status
text that is not there, which every byte-level check passes and a player sees at once.
When a theme supplies `PALETTE.RGB` the loader picks the brightest entry of it as the
frame's ink (`ctx->text_fg`, applied by `u2_frame_set_text_fg`), and the frame's
`text_fg`/`text_bg` are what `u2_font_draw_vram_rows` uses. For the FM Towns table
that is index 7, white, which is also what the FM Towns letter tiles use against
black. This is a choice, not a measurement: the port's own text colours have never
been captured. Themes without a palette keep the CGA pair and are unchanged.

**The cursor is the third consumer of that pair, and it was missed for a week.**
The cursor is not a character: the renderer repaints the glyph of the cell under it
inverse video. `sdl_flush` did that with `U2_CGA_WHITE` / `U2_CGA_BLACK` instead of
the frame's pair, which is the same colour on the built-in tables and not on a
themed one — so under FM Towns the block was black on black *and* the character
underneath it was repainted in black, leaving an empty cell. Reported from play on
2026-11-06 as "in the creation screen with the fmtowns theme the cursor is missing,
so it is hard to see what line you are editing". It is now painted with
`text_fg`/`text_bg` like every other text pixel.

The reason it survived is worth keeping: the grid is byte-for-byte correct when
this is wrong, so no text assertion can see it. `engine/tests/cursor_visible.py`
asserts on the **frame** instead — the cell's modal colour must differ from the
frame's background and be brighter, which a cursor drawn in the background colour
cannot satisfy — and `run_cp4.sh` drives it under both `fmtowns` and `default`,
because a fix that repaired the themed path by changing the ordinary one would be a
different defect. On the skeleton screen, whose cursor seam exists for this: the
CP0 selftest now places a cursor over a glyph, since `--dump-screen` saves the
first presented frame and that is the only screen a test can reach on a first
present.

## 5c. Which theme a run gets when it names none

Added 2026-11-06. Until then the art axis was opt-in only: an `--install` tree with
`themes/` in it still started in CGA unless the player typed `--ega`, so the better
art sat on disk unused, and where both the derived FM Towns theme and the fan pack's
`default` were installed, `default` won.

The rule now, in one sentence: **an explicit choice always wins, and when nothing is
chosen the engine takes EGA from an installed themes tree, preferring `10`, then
`default`, then `fmtowns`.** (Owner ruling 2026-11-13 reversed the original
fmtowns-first order; the fan pack's `10` is the preferred default.)

What counts as explicit, and therefore suppresses the inference:

| named | suppresses |
| --- | --- |
| `--ega`, `--classic` (alias `--cga`), `--cleanroom` | the art axis |
| `--ega-theme NAME`, `[themes] theme` | the theme preference **and** the axis — naming a theme is asking for themed art |
| `--enhanced` | the axis only (it implies EGA); the theme preference still applies |
| `[themes] dir` alone | nothing; it says where to look, not what to look for |

Three things make this safe rather than surprising.

* **It cannot change what a developer or a test run draws.** The candidate list is
  the installed layout — the configured dir, else `themes` beside the launch
  directory — and never `--data-dir` or the `references/u2upgrade` dev fallbacks.
  The CP2 byte-comparison baseline is measured in original CGA mode, and the test
  suite runs from `engine/`, which has neither a `themes/` nor a `config.ini`. That
  is a property of the layout, not a hope: CP2 is unchanged.
* **An explicit request must never be worse than silence.** Two bugs of that shape
  were caught while building this and both are now asserted: auto-detection set the
  mode but not the directory, so `--ega` alone refused to start over a theme it had
  just loaded while saying nothing succeeded; and `--ega-theme default` named a theme
  without selecting the axis, so naming a theme downgraded a run that would otherwise
  have had EGA. `--ega-theme` now implies the axis.
* **It is announced.** `u2: art mode: ega (theme fmtowns, auto-selected)` is printed
  at startup, on its own line, because `deploy/novnc/up.sh` greps the line above it
  for `cleanroom` / `tiles extracted` to verify the artifact and a check that
  silently stopped matching is the failure mode that script exists to prevent.

`--classic` is the opt-out (alias `--cga`): it forces the original CGA art even when
a themes tree is present. Without it, making EGA automatic would have removed a
capability that previously existed by default, which is the same defect the
strict-`--ega` ruling was written to stop, pointing the other way.

The gate is `engine/tests/artauto_check.c` (15 checks, v1 `EGATILES` fixtures so it
needs no art and no media), driving both lookup paths and both directions of the
rule, including `10` beating `fmtowns`. Its red control is the preference reversed to
fmtowns-first, which fails the preference checks and nothing else. The `--classic`
override is gated separately in `run_cp4.sh`: a themes tree with no flag must be EGA,
and the same tree with `--classic` (or the `--cga` alias) must be CGA.

## 6. Scope

`scope` is not decoration. The FM Towns crosswalk covers the overworld and towns
only, so its manifest says `scope = overworld,towns` and `scope_dungeon` is false.

**The dungeon is theme-neutral, so a missing dungeon scope is silent.** Until
2026-11-06 entering a dungeon with a scope-less theme active printed
`THEME ART DOES NOT COVER DUNGEONS` in the message band. That was removed on the
owner's ruling that the dungeon should simply play the same under every theme, and
the bytes agree: the dungeon is a first-person wireframe drawn by `dg_line` in CGA
colour 3 (asm/4B76), and `dungeon.c` consults no theme atlas at all. The notice was
saying the art was wrong in the one screen where the theme makes no difference — and
it fired on every dungeon entry once fmtowns became the automatic theme, while
`default` stayed silent only because its manifest *claims* `scope = dungeon`.

What remains: a `stderr` line naming the theme and the fallback, which is what a
theme author needs and costs the player nothing. `engine/tests/dungeon_art_neutral.py`
guards the structural claim (with its own selftest, wired into CP7), so if themed
dungeon art ever lands the scope question becomes real again and the gate says so
rather than letting the silence be inherited.

Refusing to *enter* was the literal reading of the handover's "refuse the mode" and
remains rejected: it makes the game unwinnable behind an art flag. The owner's rule
that a state change never happens silently is not engaged, because entering a dungeon
still prints its own entry text — that is the message — and nothing about the dungeon
changed between themes.

Towers and dungeons are the only scopes the format carries today, and only `dungeon`
has a reader.

## 7. Building themes, and the gates

`u2 --install --themes-dir DIR` writes both formats — it is the theme builder, and
the only one. The Python tool that used to do it (`tools/build_themes.py`) was
deleted rather than kept beside the C one, because two builders of one format is
two definitions of it, and this file is the definition they would have drifted
from. v1 themes are copied whole-directory, byte-identical, from the upgrade pack
and the Galaxy `updated/` directory.

The FM Towns v2 theme is derived by `u2 --install --fmtowns-media-dir DIR` from the
player's own disc image (`core/fmtowns.c`): an ISO-9660 reader over raw 2352-byte
sectors, the GRAPH bank nibble-unpack, and the DOS art-index mapping compiled into
the binary from `disasm/fmtowns-crosswalk.tsv` as `engine/content/fmtowns_map.h`.
The mapping has to be compiled in because a player has the disc and not the
repository. `tools/fmtowns_theme.py` remains as the developer's version of the same
derivation — it is what you use to look at more FM Towns media and extend the theme
— and the two are checked against each other byte for byte rather than allowed to
agree by assertion.

| gate | what it proves |
| --- | --- |
| `--install --check` | the themes tree matches its own `_BUILD.tsv`: every byte, no strays, no stale manifest row, and every theme directory still holding an atlas (the half-copied-theme case) |
| `engine/tests/install_check.c` | 70 checks over the whole install, themes included: the default reshape, a named theme copied whole, the root picture hoist, dedup by picture base, and refusals for a source with no atlas and for a short atlas |
| `engine/tests/install_controls.py` | 13 controls, including the named one: a theme built with `EGACOLOR` omitted must be caught |
| `engine/tests/theme_v2_check.c` | 41 checks: geometry, identity, the map indirection, the palette, the frame scale, the player marker going through the map, mixed v1+v2 directories in Ctrl-T, and six refusals that must all go red |
| `engine/tests/overlay_map_control.py` | control: restores the raw overlay blit and requires the map assertions to go red |
| `engine/tests/fmtowns_check.c` | the C derivation: a synthetic ISO exercises the sector reader, the pool order, the palette and the refusals with no media at all, and when the disc is present the four output files must be byte-identical to `tools/fmtowns_theme.py`'s |
| `python3 tools/fmtowns_xwalk.py --check` | the crosswalk the v2 theme is derived from is complete and its letter rows follow the structural rule |
| `python3 engine/tools/fmtowns_map2h.py --verify` | the mapping compiled into the binary still matches `disasm/fmtowns-crosswalk.tsv` exactly |

The first five run in CP4, which `make test` runs and `tools/check_all.sh` runs
below; the other two are wired into `check_all.sh`.
`themes/` is gitignored: it holds art extracted from the player's own media, the
same posture as `original/`, `tmp/tiles` and `bestiary/cache/`.
