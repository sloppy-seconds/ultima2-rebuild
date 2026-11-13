#ifndef U2_TILES_EGA_H
#define U2_TILES_EGA_H

/*
 * EGA tile atlas — the fan pack's 16-colour redraw of the 64 overworld tiles.
 *
 * This is the art axis of `--ega` (docs/ega-graphics-plan.md). It is a SEPARATE
 * representation from the CGA `u2_tiles` in core/tiles.h, not a mode flag on it:
 * a CGA tile is 16x16 at 2bpp (64 bytes, leftmost pixel in the top two bits),
 * while an EGA tile is 16x16 at 4bpp packed (128 bytes, high nibble = left
 * pixel). Keeping the two structs apart is what lets the CGA blit stay
 * byte-for-byte untouched while EGA is added alongside it.
 *
 * What the bytes are, measured (docs/ega-graphics-plan.md § 3, spike S1b):
 *   - the file is named EGATILES and is 8320 bytes = 65 x 128, a flat array of
 *     16x16 tiles at 4bpp packed. There is NO header and NO index table — the
 *     plan's "~128-byte header" assumption was refuted by the bytes.
 *   - EGA tile N aligns 1:1 with CGA tile N for N=0..63 (proven by the A-Z letter
 *     tiles landing at indices 32..57 in both atlases). The 65th tile is an extra
 *     the CGA set lacks; the engine uses indices 0..63 and ignores it.
 *   - the pack ships four interchangeable themes (default, 10, alt, c64), all the
 *     same format and size. Two of the three alternates (`10` and `C64`) also ship
 *     a 32-byte `EGACOLOR`; `EGATHEME.ALT` ships `EGATILES` alone, so "every theme
 *     carries one" is as false as "none does".
 *   - `EGACOLOR` is NOT an RGB palette and this engine does not read it. The fan
 *     pack's `ega.drv` loads it whole (`mov cx,0x20; rep movsb`) over a built-in
 *     default table and uses it as a per-theme COLOUR-INDEX REMAP applied at draw
 *     time: entries 0..7 recolour the overlay stream, 8..15 the text attribute
 *     handed to `int 0x10/ah=0x09`, 16..19 and 20..31 feed box and pen primitives.
 *     The tile-atlas blit never consults it, so tile colour comes from the
 *     `EGATILES` bytes themselves and the standard EGA RGB table in `core/frame.c`
 *     is still what the engine draws with. Addresses and grades:
 *     `docs/ega-graphics.md` § 3.1.
 *
 * Copyright posture, identical to the CGA atlas and the pic loader: this art is
 * the player's own, read at run time from the resolved themes directory and never
 * committed. With no EGA files present the engine falls back to the CGA path (or
 * the cleanroom substitutes) and nothing about a launch changes.
 */

#include <stddef.h>
#include <stdint.h>
#include "core/ctx.h"
#include "core/tiles.h"          /* U2_TILE_COUNT, U2_TILE_W, U2_TILE_H */

#define U2_TILE_EGA_BPP    4
#define U2_TILE_EGA_STRIDE (U2_TILE_W / 2)                    /* 8 bytes/row */
#define U2_TILE_EGA_BYTES  (U2_TILE_H * U2_TILE_EGA_STRIDE)   /* 128 */
#define U2_TILE_EGA_FILE   8320                               /* 65 * 128 */

/* Format v2 bounds (docs/handoff-themes-builder.md § 4.1). A THEME.INI names a
 * tile size and a pool of tiles, and the atlas below is sized for the largest
 * one this project has evidence for; a theme that exceeds a bound is refused
 * with a reason rather than truncated. */
#define U2_TILE_EGA_MAX_SIDE   32
#define U2_TILE_EGA_MAX_TILE   (U2_TILE_EGA_MAX_SIDE * U2_TILE_EGA_MAX_SIDE / 2) /* 512 */
#define U2_TILE_EGA_MAX_TILES  128
#define U2_TILE_EGA_MAX_FRAMES 8

/* The frame scale the EGA art axis composites at, whatever theme is active.
 * This is a property of the ART MODE and not of the theme, and that is a ruling
 * rather than an inference: `--ega` always draws 640x400, a 16x16 v1 theme is
 * block-upscaled into it, and a 32x32 v2 theme fills it 1:1. The alternative —
 * sizing the frame per theme — makes a Ctrl-T cycle across tile sizes a renderer
 * resize, and a renderer that sized its window once at startup while a frame grew
 * underneath it writes past its own staging buffer.
 *
 * The owner accepted the consequence (2026-11-04): a v1 EGA dump is a doubled
 * image, not a 1:1 record of the fan pack's pixels. Nothing on screen changes,
 * because the SDL window was already 2x (renderer_sdl.c SDL_SCALE) and SDL
 * nearest-neighbour was doing the same doubling one step later. */
#define U2_EGA_FRAME_SCALE 2

typedef struct u2_tiles_ega {
    /* The pool. For a v1 theme this is the EGATILES array and `art index ==
     * pool index`; for a v2 theme MAP.TSV supplies the indirection, so the pool
     * holds every frame the mapping names and an art index may point at several
     * of them. */
    uint8_t  px[U2_TILE_EGA_MAX_TILES][U2_TILE_EGA_MAX_TILE];
    int      count;                 /* tiles present in px */
    int      tile_w, tile_h;        /* 0 means the v1 default, 16x16 */
    int      stride;                /* bytes per row; 0 means tile_w/2 */
    int      bpp;                   /* 0 means 4 */
    /* The atlas's NATIVE scale: how many physical pixels one of its tile pixels
     * is worth at 1:1 (16x16 art -> 1, 32x32 art -> 2). The frame may be larger
     * than this, in which case the blit blocks the tile up to fill it; see
     * U2_EGA_FRAME_SCALE and frame.h. */
    int      scale;
    int      has_palette;
    uint8_t  palette[16][3];        /* only meaningful when has_palette */
    /* An optional dungeon wall texture the theme bakes (THEME.INI `dng_wall`).
     * One 32x32 tile of the theme's own palette indices, sampled across the 3D
     * corridor's wall quads. Absent means the corridor draws its flat fill. */
    int      has_dng_wall;
    uint8_t  dng_wall[U2_DNG_WALL_PX];
    int      has_map;
    uint16_t map[U2_TILE_COUNT][U2_TILE_EGA_MAX_FRAMES];
    uint8_t  map_n[U2_TILE_COUNT];
    int      scope_dungeon;         /* the theme claims dungeon art is mapped */
    int      scope_tower;
    /* A palette index this theme's art uses to mean "no pixel here", or -1 when the
     * art has no such convention. It is a property of the ART, not of the mode, and
     * the evidence differs per source: in the FM Towns banks 14 of 14 creature
     * tiles use index 0 (43% of their pixels) while only 2 of 43 terrain tiles use
     * it at all and index 3 is where their real black lives -- so 0 is a transparent
     * pen there. In the DOS EGA art index 0 is 72% of the TERRAIN tiles, so keying
     * it globally would punch holes in the map. A theme that does not declare this
     * draws every pixel, which is the safe default. */
    int      transparent_index;
    int      loaded;
    char     source[760];           /* the file we read, for diagnostics; sized so
                                     * a snprintf from a path buffer cannot truncate */
    char     dir[512];              /* the themes dir it was found in, so a mid-run
                                     * theme cycle scans the same directory */
    char     name[32];              /* the theme name it was loaded as */
} u2_tiles_ega;

/* Geometry accessors with the v1 defaults behind them. A zeroed struct means
 * "the legacy 16x16 4bpp atlas", which is what every existing caller and test
 * builds by hand, and it is why these exist: the stride is not a constant any
 * more, and reading `t->stride` directly on a hand-built atlas would give 0. */
static inline int u2_tiles_ega_tile_w(const u2_tiles_ega *t)
{ return t && t->tile_w > 0 ? t->tile_w : U2_TILE_W; }
static inline int u2_tiles_ega_tile_h(const u2_tiles_ega *t)
{ return t && t->tile_h > 0 ? t->tile_h : U2_TILE_H; }
static inline int u2_tiles_ega_stride(const u2_tiles_ega *t)
{ return t && t->stride > 0 ? t->stride : u2_tiles_ega_tile_w(t) / 2; }
static inline int u2_tiles_ega_count(const u2_tiles_ega *t)
{ return t && t->count > 0 ? t->count : U2_TILE_COUNT; }
static inline int u2_tiles_ega_scale(const u2_tiles_ega *t)
{ return t && t->scale > 0 ? t->scale : 1; }

/* How many physical pixels one pixel of this atlas is worth in a frame composited
 * at `frame_scale`. 1 means the tile is blitted 1:1; 2 means each tile pixel
 * becomes a 2x2 block. Never less than 1: a frame smaller than an atlas's native
 * scale is refused by the loader rather than decimating the art. */
static inline int u2_tiles_ega_present(const u2_tiles_ega *t, int frame_scale)
{
    const int native = u2_tiles_ega_scale(t);
    if (frame_scale < 1 || native < 1 || frame_scale < native) return 1;
    return frame_scale / native;
}

/* Which pool entry draws art index `art`. Identity when the theme carries no
 * map, which is every v1 theme by definition. Returns -1 for an art index the
 * map does not cover, and the caller must not draw rather than guess. */
static inline int u2_tiles_ega_tile(const u2_tiles_ega *t, int art)
{
    if (art < 0 || art >= U2_TILE_COUNT) return -1;
    if (!t || !t->has_map) return art;
    if (t->map_n[art] == 0) return -1;
    return t->map[art][0];
}

/* Colour index 0..15 of one EGA pixel. High nibble is the left pixel of each
 * byte, the opposite of the CGA atlas's top-two-bits convention. The stride is
 * the theme's, not a constant: a 32-wide 4bpp row is 16 bytes, not 8. */
static inline int u2_tile_ega_pixel(const u2_tiles_ega *t, int tile, int x, int y)
{
    const uint8_t b = t->px[tile][y * u2_tiles_ega_stride(t) + (x >> 1)];
    return (x & 1) ? (b & 0xF) : (b >> 4);
}

/* The four shipped theme names. "default" is the top-level EGATILES the game
 * itself uses; the others live in EGATHEME.<NAME>/ subdirectories. A format-v2
 * theme is a directory named <name>/ holding a THEME.INI, and it is discovered
 * by the same calls. */
#define U2_EGA_THEME_DEFAULT "default"

/* The theme the engine prefers when nothing is named: the fan pack's "10", which
 * the owner picked as the best-looking of the four (ruling 2026-11-13). The
 * autodetect tries this, then "default", then the derived FM Towns theme, then
 * falls back to CGA. */
#define U2_EGA_THEME_PREFERRED "10"

/* The directory an installed tree keeps its themes in, when nothing named one.
 * Same literal as U2_INSTALL_THEMES in core/install.h; install.c static-asserts
 * the two so a rename cannot silently split the layout from the lookup. */
#define U2_EGA_THEMES_DIR_NAME "themes"

typedef enum u2_tiles_ega_result {
    U2_TILES_EGA_OK = 0,
    U2_TILES_EGA_NOT_READY,   /* no themes dir / no theme found anywhere */
    U2_TILES_EGA_BAD,         /* file present but wrong size or shape */
    U2_TILES_EGA_ERROR        /* I/O failure */
} u2_tiles_ega_result;

/*
 * Resolve the themes directory and load the chosen theme's EGATILES.
 *
 * Directory resolution order (docs/ega-graphics-plan.md § 2, [LOCKED]):
 *   ctx->ega_theme_dir, if set; else ctx->data_dir; else the built-in default
 *   candidates (references/u2upgrade where the files sit on a dev checkout). The
 *   first candidate that actually contains the theme file wins, so a data-dir
 *   without EGA art falls through to the default rather than failing.
 *
 * `theme` is one of the four names above (NULL means "default"). Returns
 * U2_TILES_EGA_OK on success; any other value means the caller should keep the
 * CGA path (and, for the top-level notice, say so once).
 */
u2_tiles_ega_result u2_tiles_ega_open(u2_ctx *ctx, u2_tiles_ega *t,
                                      const char *theme);

/* Load one theme from an EXACT directory (no resolution search), recording the
 * dir in t->dir. This is what a mid-run cycle uses: it already knows the dir the
 * current atlas came from and only needs to swap the theme within it. */
u2_tiles_ega_result u2_tiles_ega_open_in(u2_ctx *ctx, u2_tiles_ega *t,
                                         const char *dir, const char *theme);

/*
 * The next theme name after `cur` when the themes in `dir` are walked in
 * alphabetical order of their directory entries, wrapping to the first at the
 * end. The top-level EGATILES is the theme "default"; each EGATHEME.<NAME>
 * subdirectory is the theme "<NAME>" lower-cased — the same mapping
 * u2_tiles_ega_open_in understands, so the returned name can be loaded directly.
 *
 * Fills `next` and returns the number of themes found (>= 1 when the directory
 * is readable), or 0 if the directory cannot be opened. If `cur` is not among
 * the themes found (e.g. it was loaded from a different directory) the first
 * theme in the order is returned, so a cycle always makes progress.
 */
int u2_tiles_ega_next_theme(const char *dir, const char *cur,
                            char *next, size_t n);

/*
 * Fail-fast launch check for --ega (strict mode). Returns U2_TILES_EGA_OK when
 * the chosen theme's EGATILES is present and loadable in the resolved themes
 * directory; otherwise a non-OK result and, if `err` is supplied, a one-line
 * actionable reason. main() calls this and refuses to start on any non-OK, so
 * --ega never silently draws CGA because the art is missing.
 */
u2_tiles_ega_result u2_tiles_ega_validate(u2_ctx *ctx, char *err, size_t n);

/*
 * Pick the art axis from what is installed when the player named nothing.
 *
 * Sets ctx->art_mode to U2_ART_EGA and, if no theme was named, ctx->ega_theme,
 * preferring the fan pack's `10` theme, then its `default`, then the derived FM
 * Towns theme. Returns 1
 * on a hit. Returns 0 and leaves the context as it found it when there is nothing
 * to find, which means the run stays on CGA — that is a default being unfilled, not
 * the refusal --ega enforces, because nothing was asked for.
 *
 * Looks only in the explicit themes dir (flag or config.ini) or `themes` beside
 * the launch directory. It deliberately does NOT consult --data-dir or the
 * dev-checkout fallbacks, so it cannot change what a developer's run or the test
 * suite draws. main() calls it before the strict --ega check.
 *
 * `may_change_mode` is 1 when nothing forced the art axis, so a hit may set it;
 * pass 0 when the mode is already decided and only the theme name is still open.
 * With 0 and the mode on CGA the probe is skipped, because there is no theme to
 * pick for CGA art.
 *
 * `chosen` receives the theme name for the startup announcement.
 */
int u2_tiles_ega_autodetect(u2_ctx *ctx, char *chosen, size_t n, int may_change_mode);

const char *u2_tiles_ega_result_text(u2_tiles_ega_result r);

#endif /* U2_TILES_EGA_H */
