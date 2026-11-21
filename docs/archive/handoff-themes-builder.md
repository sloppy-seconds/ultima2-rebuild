# Handover — theme builder: slice A, slice B, and the held slice C

> **LOCKED OFF 2026-11-04. Do not pick this up from here.** The theme work is
> finished to the state described below, the format contract now lives in
> `docs/theme-format.md`, and everything about *assembling* a game tree — including
> the parts of this handover that were never built — has moved to
> **`docs/install-design.md`**, which is the entry point from now on. Read that
> first; read this file only for the design rationale behind slice B and the
> record of what was refuted along the way.
>
> **What shipped.** Slice A: `tools/build_themes.py` +
> `tools/build_themes_selftest.sh` (19 controls), both wired into
> `tools/check_all.sh`. Slice B: the v2 format is implemented in the engine
> (`THEME.INI` / `TILES` / `PALETTE.RGB` / `MAP.TSV`), `--fmtowns` builds the FM
> Towns theme from the disc, `--ega-theme fmtowns` renders the overworld in FM
> Towns art and palette, and `engine/tests/theme_v2_check.c` (39 checks, 6 of them
> refusals) is wired into CP4. User-facing page: `docs/ega-graphics.md` § 4.
>
> **Three corrections to this handover's own plan**, each written into the section
> that owns it: **§ 2.4 is closed** — `EGACOLOR` is a colour-index remap decoded
> from `ega.drv`, not a palette, so § 5 item 6's hope that it and FM Towns could
> share one `PALETTE.RGB` mechanism is dead and slice B gave v2 its own; **§ 4.1's
> `scope` refusal** turned out to mean "refuse to draw", not "refuse to enter",
> because the second makes the game unwinnable behind an art flag; and **§ 4.3
> item 1's warning** about not overloading the atlas with a size field went further
> than anticipated — the frame is sized by the **art mode**, not the theme, because
> sizing it per theme turned a Ctrl-T cycle across tile sizes into a renderer
> resize that wrote past its own staging buffer. The owner ruled instead that
> `--ega` composites at 640x400 for every theme and a 16x16 theme is block-upscaled
> into it, which is the identical doubling the SDL window was already doing.
>
> **What was left held** is listed in `docs/install-design.md` § 10, split into
> what the installer inherits and what stays engine work.
>
> HEAD before this paragraph was `c38eda3`. `make -C engine test` CP0–CP7 green.
> `tools/check_all.sh` has **two pre-existing red gates unrelated to this work** —
> `cmd_provenance_derived` and `xref_provenance` — which were already failing and are not
> yours to inherit.
>
> Prerequisite reading: `AGENTS.md`, then `docs/fmtowns-graphics.md` (the FM Towns
> format, the palette, and three refutations), then `docs/ega-graphics.md` and
> `docs/ega-graphics-plan.md` § 2 (the theme resolution order, which is `[LOCKED]`).

---

## 0. What is being asked for

One tool, `tools/build_themes.py`, that takes a list of source paths and emits a
single `themes/<name>/` tree in the fan pack's own layout, so the existing
`--ega-theme-dir` can point at it and Ctrl-T cycles across art that came from
three different places.

The three sources, and **what each actually contributes** — this is not obvious
and getting it wrong wastes a morning:

| source | contributes | does NOT contribute |
| --- | --- | --- |
| `references/u2upgrade/` | `EGATILES`, `EGATHEME.{10,ALT,C64}`, `CGATILES`, `PIC*.EGA`, `PIC*.IDX` | the Galaxy maps |
| `original/updated/` | the Galaxy maps `MAPG*`/`MONG*`/`TLKG*`, and the `PIC*-E` picture set in both CGA and EGA spellings, plus a patched `ultimaii.exe` | **`EGATILES` — there is no tile atlas in here at all** |
| `references/fmtowns/` | `GRAPH/*.TIF`, the FM Towns banks | anything the current theme format can hold (§ 4) |

`original/updated/` looks like an art directory and mostly is one, but the tile
atlas only exists in `references/u2upgrade/`. Confirmed by listing: `updated/`
holds `PICCAS`, `PICCAS-E`, `picdng`, `PICDNG-E`, … `u2ega.exe`, and no
`EGATILES`.

**`themes/` is gitignored** (owner ruling, 2026-11-04). It will hold extracted art
from the player's own media, same posture as `tmp/tiles` and `bestiary/cache/`.
Add `/themes/` to `.gitignore` as part of slice A, and say why in a comment —
this repo has a history of ignoring things for a reason and needing the reason
written down.

---

## 1. Why FM Towns cannot be "just another theme"

`EGATILES` is 8320 bytes = 65 × 128, a **headerless flat array** of 16x16 tiles
at 4bpp packed, high nibble = left pixel. There is no index table, no size field,
no palette field. It can express exactly one thing: "tile N lives at N × 128".

That works for the fan pack for one reason, recorded in `tiles_ega.h`: **EGA tile
N aligns 1:1 with CGA tile N**, proven by the A–Z letter tiles landing at indices
32..57 in both atlases. FM Towns has no such alignment — that is the entire reason
`disasm/fmtowns-crosswalk.tsv` exists. Four independent blockers:

1. **Alignment.** `art 0x0C` is `ENEMY2/16`, not tile 12. A flat array cannot
   carry an indirection.
2. **Set-valued mapping.** One DOS art index maps to a *run* of FM tiles (the
   animation pair). A flat array holds one tile per index. No frame list, no
   frame period.
3. **Tile size.** FM Towns is 32x32. `U2_TILE_EGA_STRIDE` is derived from
   `U2_TILE_W` and the SDL blit assumes 16x16.
4. **Palette.** FM Towns needs its own 8-entry table, and index 3 is black — not
   an EGA fact.

So FM Towns needs a **format v2**. Squeezing it into `EGATILES` would mean
resampling 32x32 down to 16x16 and throwing away the animation, i.e. paying the
cost of a port to get a worse version of art we already have.

---

## 2. Measured facts to work from

### 2.1 The existing theme contract

`engine/src/core/tiles_ega.c`:

- `theme_relpath()` — `"default"` → `"EGATILES"`; any other name →
  `"EGATHEME.<UPPERCASED>/EGATILES"`. Names are matched case-insensitively on
  purpose, because the pack ships `EGATHEME.10` and `EGATHEME.ALT` in different
  cases.
- `entry_to_theme()` — the inverse, so anything it produces is loadable.
- `u2_tiles_ega_next_theme()` — collects theme names, **sorts them alphabetically**
  so Ctrl-T order is directory order and not `readdir` order.
- `u2_tiles_ega_validate()` — the fail-fast `--ega` launch check. `--ega` is
  strict: a missing themes dir or a missing theme is a refusal, not a silent
  fallback.
- Resolution order is `[LOCKED]` in `docs/ega-graphics-plan.md` § 2: an explicit
  `--ega-theme-dir` is authoritative with **no fall-through**, so a wrong path is
  reported rather than papered over. Keep that property in anything you build.

### 2.2 What a theme directory actually contains

Measured by listing the shipped themes:

```
EGATHEME.10/    EGATILES 8320, EGACOLOR 32, PICDNG.EGA 64000, PICSPA.EGA 64000
EGATHEME.ALT/   EGATILES 8320
EGATHEME.C64/   EGATILES 8320, EGACOLOR 32, PICDNG.EGA 64000, PICSPA.EGA 64000
```

So a theme may carry **its own pictures** as well as its own tiles. A theme
builder that only copies `EGATILES` will silently produce a theme that looks
half-recoloured: tiles from the theme, pictures from the top level.

### 2.3 The FM Towns side, already committed

- Format and palette: `docs/fmtowns-graphics.md` § 2, § 3.
- Mapping: `disasm/fmtowns-crosswalk.tsv`, 64 rows, `art_index → BANK/tile[,…]`.
- Regenerate with `python3 tools/fmtowns_xwalk.py --export
  disasm/fmtowns-owner-mapping.json <out>`; validate with `--check <tsv>`.
- **The crosswalk is exactly the v2 `MAP.TSV`.** Do not invent a second mapping
  format. It already has the set-valued structure and a `source` column.
- Scope caveat that will bite: the crosswalk covers **overworld and towns only**.
  Dungeon, tower and the whole `PLAYER` bank are unmapped. A v2 theme built today
  is an overworld theme, and should say so in its manifest.

### 2.4 CLOSED — `EGACOLOR` is a colour-index remap, not a palette

`engine/src/core/tiles_ega.h` used to state:

> the pack ships four interchangeable themes (default, 10, alt, c64), all the same
> format and size, **differing only in colour. None carries its own palette**, so
> the standard EGA RGB table in `core/frame.c` is used for all.

**Two of the three alternate themes do carry a colour file.** `EGATHEME.10/EGACOLOR`
and `EGATHEME.C64/EGACOLOR` exist, are 32 bytes, and differ from each other.
`EGATHEME.ALT` has no `EGACOLOR`, so the claim is true for `alt` and false for
`10` and `c64`.

The RGB555 reading was the wrong hypothesis, and the fan pack's own driver says why.
`references/u2upgrade/ega.drv` loads the file whole (`mov cx,0x20; rep movsb`) over a
built-in default table in its data area, and reads it in five places as a **colour-index
remap applied at draw time**: entries 0..7 recolour the `MONSTERS` overlay blocks, 8..15 the
text attribute handed to `int 0x10/ah=0x09`, 16..19 and 20..31 feed box and pen primitives.
The tile-atlas blit never consults it. Full evidence, addresses and grades:
`docs/ega-graphics.md` § 3.1.

**What this does to slice B.** § 5 item 6 hoped `EGACOLOR` and FM Towns could share one
`PALETTE.RGB` mechanism. They cannot: `EGACOLOR` remaps logical colour indices for the fan
pack's own drawing primitives, and an FM Towns palette is an RGB table for a different atlas.
Keep them apart. v2 still needs its own `PALETTE.RGB`, and honouring the fan pack's remap is a
separate feature with a separate name, not a field in the same file.

Two gaps found on the way, both recorded in `docs/ega-graphics.md` § 3.1 and neither
belonging to slice A: the engine reads `EGACOLOR` nowhere, and `pic_ega.c` resolves pictures
at the **top level** of the themes dir only — so the per-theme `PICDNG.EGA`/`PICSPA.EGA` that
themes `10` and `C64` ship (and which differ from the top-level files) are copied faithfully
and never drawn. The fan pack's driver does look for them theme-relative with a fall-back to
the bare name, so closing that gap is a loader change, not an art problem.

---

## 3. Slice A — the build tool, no engine change

**Goal.** `tools/build_themes.py` scans source paths and emits a `themes/` tree
that `--ega-theme-dir` already understands. Nothing in `engine/` changes. The
acceptance test is the existing engine, unmodified.

**Do it in this order.**

1. **Inventory first, write second.** Emit a report of what each source path
   contains and which theme each file would land in, and **read it before you
   copy anything.** The failure mode for this tool is a plausible-looking tree
   with a missing `EGACOLOR` or a stale `PICDNG.EGA`, which renders as a
   half-recoloured game and looks like a theme bug rather than a build bug.
2. Copy **whole theme directories**, never individual files. A theme is
   `EGATILES` + optional `EGACOLOR` + optional `PIC*.EGA`, and the optionality is
   per-theme (§ 2.2). Selective copying is how you get the half-recoloured bug.
3. Emit `EGATILES` **byte-identical** to the source. Do not decode and re-encode
   the atlas in slice A. There is no reason to, and any difference becomes
   something you have to defend.
4. Also emit a `default` theme from `references/u2upgrade/EGATILES` so the
   output directory is self-contained and `--ega-theme-dir themes/` alone works.
5. Add `/themes/` to `.gitignore` with the reason.

**CLI shape.**

```bash
python3 tools/build_themes.py \
    --source references/u2upgrade \
    --source original/updated \
    --out themes/ \
    [--dry-run] [--report]
```

`--dry-run` prints the plan and writes nothing. `--report` prints the inventory
from step 1. Default should be dry-run if you want a guard rail; if you make it
write by default, refuse to write into `original/` or `references/` exactly the
way `tools/u2_mkprobe.py` does.

**Acceptance — and write the failing control first, per `AGENTS.md` § 3.**

- `./u2-sdl --ega --data-dir <dir> --ega-theme-dir themes/ --ega-theme c64`
  renders, and the built `themes/EGATHEME.C64/EGATILES` is
  `cmp`-identical to `references/u2upgrade/EGATHEME.C64/EGATILES`.
- Ctrl-T cycles the same set of theme names it cycles from the source directory.
  If the name list differs, `entry_to_theme()` and your naming have diverged.
- **Control that must go red:** build a theme with `EGACOLOR` deliberately omitted
  and confirm your own gate notices. If nothing notices, your tool has no gate —
  say so in the commit rather than claiming coverage.
- `python3 tools/check_all.sh --fast` green, plus `make -C engine test` (slice A
  touches tooling, so the suite applies).

**Slice A explicitly does not** touch the engine, does not decode `EGACOLOR`, and
does not involve FM Towns. If you find yourself editing `engine/`, stop.

> *(the one deviation, and it was asked for: § 2.4 told the first slice to fix the
> `tiles_ega.h` comment with whatever it found, and the only way to fix that comment
> honestly was to read `ega.drv`. The decode is a separate commit from the tool, and
> `engine/` gained a comment and no code.)*

### 3.1 Slice A — done 2026-11-04, and what the build actually decided

`tools/build_themes.py` (with `tools/build_themes_selftest.sh`, 12 controls, both wired
into `tools/check_all.sh`). Built tree: 17 files, four themes (`10`, `alt`, `c64`,
`default`), every one `cmp`-identical to its source.

Three decisions the plan did not settle, resolved by reading the inventory first, as step 1
said to:

1. **Pictures dedupe by BASE, not by filename.** `PICDNG.EGA` and `PICDNG-E` are the same
   slot to `pic_ega.c`, which tries `-E` before `.EGA` *inside* a directory — so shipping
   both lets the suffix order decide which art draws, silently and opposite to the source
   order. One file per base, first `--source` wins, and the shadow is reported with a byte
   comparison. Measured: `PICDNG`/`PICDRA` agree between the two sources, `PICSPA` differs
   in 48 bytes, and that is the one place the choice actually matters.
2. **The tree carries `_BUILD.tsv`**, one row per file naming its source and size. It is
   what makes "which `EGATILES` is this?" answerable after the fact, and the verifier diffs
   it against the re-derived plan, so a hand-edited tree goes red as a stale manifest and as
   a byte mismatch at the same time.
3. **A `EGATHEME.*` with no `EGATILES` is a problem, not a theme.** The engine would list the
   name from the directory entry and then fail to load it, which is the worst of both.

The named control is control 2 in the selftest and it goes red twice over: `missing:
.../EGACOLOR` from the plan, and `theme 'foo' is missing EGACOLOR that its source ships
(half-copied theme)`. Controls 3-6 cover a truncated atlas, an edited atlas of the same
size, a stray file, and a stale manifest row; 7-10 cover the refusals.

Verified against the engine, not against my own re-reading of it: `--ega-theme-dir themes/
--ega-theme <t>` passes the strict launch check for all four themes and refuses a fifth
(`theme 'nosuchtheme' not found (looked in: ../themes)`), and a throwaway probe calling
`u2_tiles_ega_next_theme()` on both directories printed `10 alt c64 default` for each.

---

## 4. Slice B — FM Towns as a static v2 theme

**Goal.** FM Towns art in the engine, one tile per art index, **no animation**.
This proves the v2 format and the indirection end to end at the lowest cost, and
it is the step that makes the crosswalk pay for itself.

### 4.1 The v2 format

```
themes/<name>/
  THEME.INI      manifest: tile size, bpp, palette source, mapping file, scope
  TILES          flat array, tile_w * tile_h * bpp / 8 bytes per tile
  PALETTE.RGB    optional, 16 x 3 bytes; absent means the built-in EGA table
  MAP.TSV        optional, art_index -> tile[,tile...]; absent means identity
```

`THEME.INI` at minimum:

```ini
[theme]
name = fmtowns
tile_w = 32
tile_h = 32
bpp = 4
pixel = nibble-high-left
palette = PALETTE.RGB
map = MAP.TSV
identity = no
scope = overworld,towns
source = FM Towns (1990) GRAPH banks
```

Two design points worth holding firm on:

- **`MAP.TSV` absent means identity.** That is what keeps every existing
  `EGATILES` theme valid with no changes — the fan pack themes are identity
  mappings by definition.
- **`scope` is not decoration.** The crosswalk covers overworld and towns only.
  A theme that silently substitutes something for unmapped dungeon art is worse
  than one that refuses. Load it, and refuse the mode.

### 4.2 What the builder does in slice B

Reads `disasm/fmtowns-crosswalk.tsv` plus the FM Towns banks, and emits:

- `TILES` — the 64 FM tiles, **resampled to the size the engine can blit**.
  Decide deliberately: 32x32 native (needs § 4.3 item 2) or downsample to 16x16
  (no renderer change, and you lose the detail that was the point). I would take
  32 native and accept the renderer work; downsampling is how this ends up worse
  than EGA.
- `MAP.TSV` — the crosswalk, first tile of each set only. **Record the dropped
  frames in the manifest**, not silently.
- `PALETTE.RGB` — the FM Towns in-game table from `docs/fmtowns-graphics.md` § 3.
  Index 3 is black. That is not a bug to fix.

### 4.3 What the engine needs

In rough dependency order:

1. **A size-parameterised atlas.** `u2_tiles_ega` is fixed at
   `U2_TILE_COUNT × U2_TILE_EGA_BYTES`. Either generalise it or add a third atlas
   type; do not overload the EGA struct with a size field and call it done,
   because `u2_tile_ega_pixel()` hardcodes the stride and the compiler will not
   stop you being wrong.
2. **The renderer blit.** `renderer_sdl.c` picks the palette from
   `ctx->art_mode` (`U2_ART_EGA` → `u2_frame_palette_ega`) and assumes 16x16.
   The viewport geometry is `U2_VIEW_COLS/ROWS` of `U2_TILE_W/H`, so a 32x32
   atlas changes the window's pixel size. Check what `--scale` and the frame
   buffer do before assuming the map still fits.
3. **The lookup.** `u2_view_art()` returns an art index and the renderer indexes
   the atlas with it. With `MAP.TSV` present, that becomes a table lookup. Keep
   `view.h` pure — it is the tile-ID seam and it deliberately knows nothing about
   pixels. The indirection belongs in the atlas layer.
4. **`--fmtowns`, or better: generalise `--ega-theme-dir`.** The cleanest end
   state is one theme loader that reads `THEME.INI` when present and falls back
   to the legacy `EGATILES` assumption when it is not. That avoids a third art
   mode enum value and keeps Ctrl-T working across mixed formats. If you do add
   `U2_ART_FMTOWNS`, remember `ctx.h` documents `U2_ART_EGA` as falling back to
   CGA when art is absent — preserve that property.

### 4.4 Acceptance

- `--ega-theme-dir themes/ --ega-theme fmtowns` renders the overworld with FM
  Towns art. Screenshot it and **look at it** — `AGENTS.md` § 3 requires a frame
  for any claim about visible behaviour, and this whole thread started because a
  wrong palette made the art look like noise while every byte-level check passed.
- The letters still work. `art 0x20..0x39` → `MAP2_1/38..63` is the strongest
  known-correct part of the crosswalk; if site-name banners come out as garbage,
  the mapping or the row order is wrong, not the art.
- `--ega-theme c64` from the same directory still works. Mixed v1 and v2 themes
  in one directory must not break Ctrl-T.
- `make -C engine test` green. Add a CP-level test that loads a v2 theme built by
  the tool and asserts at least the letter rows decode to the right tiles — that
  is a real gate with a known-true answer, which is the kind this project trusts.
- **Controls first:** a `MAP.TSV` with a row pointing outside the atlas, a
  `THEME.INI` with `bpp = 3`, and a theme with `scope = overworld` used in a
  dungeon. All three must fail loudly.

---

### 4.5 Slice B — done 2026-11-04, and where it departed from the plan

Built: `tools/build_themes.py --fmtowns` derives `themes/fmtowns/` (80-tile pool,
64 art rows, 18 of them multi-frame, 48-byte palette) and `engine/src/core/
tiles_ega.c` loads it. `--ega-theme fmtowns` renders the overworld at 640x400; the
frame was checked against the `c64` render of the same sector and the geography, the
tree clusters and the avatar cell line up, so the indirection selects the right art
rather than merely drawing something.

Four decisions the plan left open, and why:

1. **The frame is sized by the art mode, not by the theme.** § 4.3 item 1 asked for
   a size-parameterised atlas; the atlas alone was not enough, because a 32x32 tile
   composited into a 320x200 frame has to be decimated to fit, which is the
   downsampling § 4.2 rejects. The first implementation sized the frame per theme,
   which moved the problem into the renderer: a Ctrl-T cycle across tile sizes then
   resizes a window, texture and staging buffer that `sdl_init` sized once, and
   `sdl_upload` iterating the frame's current `w`/`h` writes 1,024,000 bytes into a
   256,000-byte buffer. The owner ruled instead that **`--ega` always composites at
   640x400** and a 16x16 theme is block-upscaled into it, which is the identical
   doubling the SDL window was already doing (`SDL_SCALE 2`, nearest-neighbour), so
   nothing on screen changes and the resize path does not exist. `u2_frame` is
   heap-backed with `w`, `h` and `scale`; two writers keep the distinction honest —
   `u2_frame_put` (physical, what a tile blit uses, at `frame_scale / native_scale`)
   and `u2_frame_put_logical` (a 1x-layout pixel as a scale x scale block: text, the
   flash ring, the pictures). The accepted cost is a doubled v1 dump, and
   `theme_v2_check` pins the expansion exactly rather than approximately.
2. **The pool holds every frame, so nothing is dropped.** § 4.2 said "first tile of
   each set only, record the dropped frames in the manifest". Better was to put all
   of them in `TILES` and keep the whole list in `MAP.TSV`, with `frames = first` in
   the manifest as an engine policy rather than a data loss. Slice C gets animation
   without rebuilding the mapping, which was § 5's stated worry.
3. **`scope` refuses to draw, not to enter.** A theme whose scope omits dungeons
   prints `THEME ART DOES NOT COVER DUNGEONS` and draws the dungeon in CGA. Refusing
   the mode outright makes the game unwinnable behind an art flag; the owner's rule
   is that the player is told, and they are.
   > **Corrected 2026-11-06: the in-game notice was removed.** The dungeon is a CGA
   > wireframe under every theme and `dungeon.c` consults no theme atlas, so the
   > notice said the art was wrong where the theme makes no difference — and it fired
   > on every entry once fmtowns became the automatic theme. The fallback is logged to
   > `stderr` only. "Refuse to enter, not to draw" still stands. Current record:
   > `docs/theme-format.md` § 6.
4. **No new art-mode enum.** `--ega-theme-dir` reads `THEME.INI` when a theme
   directory has one and falls back to the legacy `EGATILES` assumption when it does
   not, so `U2_ART_EGA` still means "the 16-colour axis" and Ctrl-T cycles five
   themes out of a directory holding four v1 and one v2.

Two bugs the inventory-first habit caught, both recorded because they are the shape
of bug this project keeps meeting: the `THEME.INI` parser compared the key including
its trailing space, so `tile_w = 32` never matched and every v2 theme failed to load
with "tile_w/tile_h missing"; and the `MAP.TSV` parser walked past the field boundary
into the provenance column, so `MAP2_1/1` parsed as a third frame at tile 0 — a
plausible wrong answer that only the `map_n[6] == 2` assertion in the new test saw.

**And one that only a screenshot could catch, exactly as `AGENTS.md` § 3 warns.** The
first FM Towns frame rendered clean at 640x400 with correct art and no status text at
all: the whole text layer is written with CGA white, which is index 3, and the measured
FM Towns palette has index 3 as a second black. Every byte-level check in the suite
passed, because index 3 is a perfectly non-zero index — the blackness lives in the
palette, not the pixel. So a theme that ships `PALETTE.RGB` now also determines the
ink the text is drawn with (brightest entry; white for FM Towns, which is also what
its own letter tiles use against black), and `theme_v2_check` asserts the glyph is
drawn with *that index* rather than "with something", because an ink check that counts
non-zero pixels passes on the bug it exists to catch. The control was run: forcing the
loader to leave the CGA pair makes both assertions go red.

### 4.6 What slice C inherits, concretely

* `MAP.TSV` rows are already lists; the engine reads `map[art][0]` and
  `map_n[art]`. A frame clock needs a tick source the renderer reads, and the
  handoff's argument for game-tick over wall-clock still stands.
* `u2_tiles_ega.map` is `uint16_t [64][8]`. A second key for the `PLAYER` bank
  (race and class) does not fit that shape, so it grows the format rather than the
  data — § 5 item 2's "design it before you touch it" is still true.
* `THEME.INI` refuses an unknown `scope` word, so adding `dungeon` or `tower` to a
  theme is a manifest edit and a `parse_scope` line, not a loader change. The dungeon
  banks decode already.
* The frame scale is `U2_EGA_FRAME_SCALE` (2) for the whole `--ega` axis and 1 for
  CGA; it is not a theme field. A theme whose tiles are natively larger than that is
  refused at load rather than decimated, so a 64x64 set needs a deliberate change to
  `U2_FRAME_MAX_SCALE` and `U2_EGA_FRAME_SCALE` together, not a manifest edit.

## 5. Slice C — LOCKED OFF, moved to `docs/install-design.md` § 10

**Do not start this from here.** The list below is kept as the original
rationale — it records *why* each item was parked, which the summary in the design
doc does not repeat. What each item now blocks on, and which of them the installer
inherits rather than the engine, is in `docs/install-design.md` § 10.

C is everything the current crosswalk deliberately parked:

1. **Animation.** Frame lists and a frame clock. The stride is **1, adjacent** —
   `docs/fmtowns-graphics.md` § 7 records how the stride-2 claim got made and
   what refuted it. Needs: a per-art frame list in `MAP.TSV` (already
   set-valued), a tick source the renderer reads, and a decision about whether
   animation is wall-clock or game-tick. Game-tick is probably right, because the
   original is paused when the game is paused and a wall-clock animation would
   keep cycling in a scripted test run.
2. **The `PLAYER` bank.** Entirely unmapped. FM Towns varies the avatar by race
   and class, which is an axis the single DOS atlas does not have. This is not
   more rows in `MAP.TSV`; it needs a second key, so the mapping format grows a
   selector. Design it before you touch it.
3. **Dungeon and tower art.** `CHEST`, `DUNG1`, `DUNG2` decode and are available.
   Note `docs/art-index.md` § 5 and `docs/monster-spawn.md` § 9: dungeons do not
   stamp the grid the same way, so the dungeon appearance may not come from the
   map byte at all. Do not assume the overworld lookup applies.
4. **Horse and boat directional variants.** The owner reports facing variants;
   the crosswalk currently carries the animation pair only.
5. **`MAP2_2` planet-2 terrain.** 15 tiles differ per planet, and only `17` has
   been touched. Also unresolved: whether the per-planet band changes *meaning*
   more than once — tile 16 does (moongate vs shield) and tile 35 may.
6. **`EGACOLOR`.** If it decodes (§ 2.4), fold it into the same `PALETTE.RGB`
   mechanism rather than leaving two palette paths.
7. **Per-screen palettes.** Unproven whether title, town, dungeon or combat
   install a different FM Towns palette. The clean measurement is breaking on
   `EGB_palette` at `0x103c84` in `ENCHANT.EXP` and dumping the 512 bytes behind
   `[edi+8]`.

**The one thing to get right in slice B so C stays cheap:** keep `MAP.TSV`
set-valued end to end. If slice B collapses each set to its first tile on the way
into the engine and loses the rest, slice C has to rebuild the mapping layer.
Parse the full list, use `list[0]`, and keep the list.

---

## 6. Hazards

- **Do not hand-maintain a second copy of the crosswalk.** It is generated; the
  owner mapping JSON is the input. `--check` exists and goes red.
- **`EGACOLOR` is undecoded.** Do not ship a palette guess. A wrong palette is
  invisible to every byte-level test and obvious to a human, which is the worst
  possible combination — this exact failure already cost a session on the FM
  Towns side.
- **Half-copied themes.** § 2.2. Copy directories, not files.
- **No fall-through on an explicit theme dir.** `[LOCKED]` behaviour. A tool that
  silently searches elsewhere turns a typo into a mystery.
- **`themes/` must stay gitignored.** It is player-supplied game art. Same rule as
  `original/`, `references/`, `tmp/tiles`, `bestiary/cache/`.
- **The cleanroom wall.** `docs/tile-art-spec.md` is the only document someone
  drawing substitute art may read. A theme builder is not drawing art, but if it
  ever starts synthesising tiles rather than copying them, you are on the other
  side of that wall and must read `docs/cleanroom-procedure.md` first.
- **Two pre-existing red gates** in `check_all.sh` (§ status). Don't let them make
  you think your slice broke, and don't fix them as a drive-by.

---

## 7. What to update as you go

- ~~Slice A done~~ **done 2026-11-04** — marked in § 3.1, and the tool is in the
  `tools/` row of `AGENTS.md` § 4.
- ~~`EGACOLOR` decoded~~ **done 2026-11-04** — `engine/src/core/tiles_ega.h` fixed
  in the same commit as `docs/ega-graphics.md` § 3.1, and the method lesson went to
  `docs/harness-portability.md` § 6.
- ~~Slice B done~~ **done 2026-11-04** — `docs/ega-graphics.md` § 4 covers the user
  story, and the format spec now lives in its own document, `docs/theme-format.md`,
  rather than in this handover. Handovers are entry points, not contracts; read that
  file for fields and refusals and treat § 4.1 here as the design rationale it was
  written as.
- Anything that changes how a *generic* DOS decompilation harness should be built
  → `docs/harness-portability.md` § 6, same commit. Method transfers; conclusions
  do not.
- If slice C starts, it should become its own plan doc, not an append to this one.
