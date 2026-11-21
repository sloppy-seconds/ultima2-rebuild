/*
 * tiles_ega.c — load an EGA or v2 tile theme into the engine.
 *
 * Two formats, one loader, because Ctrl-T has to cycle across both:
 *
 *   v1  the fan pack's `EGATILES` — a headerless flat array of 65 16x16 tiles at
 *       4bpp packed, high nibble = left pixel. Art index == tile index. See the
 *       header for the evidence and docs/ega-graphics.md for the user story.
 *
 *   v2  a directory with a THEME.INI naming a TILES pool, an optional
 *       PALETTE.RGB and an optional MAP.TSV (docs/archive/handoff-themes-builder.md
 *       § 4.1). This is what an art set that does NOT align 1:1 with the DOS art
 *       indices needs — the FM Towns banks are the reason it exists.
 *
 * `MAP.TSV` absent means identity, and that is what keeps every v1 theme valid
 * with no changes. An explicit --ega-theme-dir stays authoritative with no
 * fall-through (docs/ega-graphics-plan.md § 2, [LOCKED]): a wrong path is
 * reported, never papered over.
 *
 * Zero game content is committed, so — exactly like the CGA extractor — the art
 * has to arrive at run time from a file the player owns. A missing file is a
 * clean NOT_READY; a file that is present and wrong is U2_TILES_EGA_BAD with a
 * reason, and `--ega` refuses to start rather than drawing a plausible half-theme.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define _POSIX_C_SOURCE 200809L   /* dirent, stat, strcasecmp under -std=c11 (mingw provides all) */
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include "core/tiles_ega.h"
#include "core/fmtowns.h"      /* U2_FMTOWNS_THEME: one literal, not a second copy */
#include "core/artifact.h"    /* a release artifact does not know about references/ */
#include "core/ini.h"            /* the shared `key = value` reader (ini.h says why) */
#include "core/frame.h"           /* U2_FRAME_MAX_SCALE: a theme may not ask for
                                   * a frame bigger than the build can composite */

#define V2_INI     "THEME.INI"
#define V2_TILES   "TILES"

/* ---------------------------------------------------------------- helpers */

static int path_is_dir(const char *p)
{
    struct stat st;
    return p && *p && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Size of a regular file, or -1 if it is not one. */
static long path_size(const char *p)
{
    struct stat st;
    if (!p || stat(p, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (long)st.st_size;
}

/* Read a whole file into a malloc'd buffer. Returns 0 on success. */
static int slurp(const char *path, unsigned char **out, long *out_len)
{
    const long sz = path_size(path);
    if (sz < 0) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    const size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return -1; }
    buf[sz] = '\0';
    *out = buf;
    if (out_len) *out_len = sz;
    return 0;
}

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
        *--e = '\0';
    while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
}

/* THEME.INI is read by the engine's one ini parser, unscoped: the file has no
 * sections, and the refusal semantics are the ones theme_v2_check.c pins. Keeping
 * a private copy here would be a second grammar to drift from config.ini, which is
 * the exact thing docs/install-design.md § 4 rules out. */
static int ini_get(const char *ini, const char *key, char *out, size_t n)
{
    return u2_ini_get(ini, NULL, key, out, n);
}

/* ------------------------------------------------------------------- v1 */

/* Try to read a whole legacy EGATILES from `path`. On success fills t->px with
 * the first 64 tiles and returns U2_TILES_EGA_OK. A file that exists but is too
 * short is U2_TILES_EGA_BAD (present but wrong), distinct from absent. */
static u2_tiles_ega_result try_load_v1(const char *path, const char *dir,
                                       const char *theme, u2_tiles_ega *t)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return U2_TILES_EGA_NOT_READY;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return U2_TILES_EGA_ERROR; }
    const long sz = ftell(f);
    rewind(f);
    if (sz < (long)(U2_TILE_COUNT * U2_TILE_EGA_BYTES)) {
        fclose(f);
        return U2_TILES_EGA_BAD;
    }
    /* The rows of the pool are wider than a v1 tile, so read tile by tile rather
     * than in one block: a single fread into px would stride at 512 and interleave
     * garbage between the tiles. */
    const int ntiles = (int)(sz / U2_TILE_EGA_BYTES);
    int stored = 0;
    uint8_t row[U2_TILE_EGA_BYTES];
    while (stored < ntiles && stored < U2_TILE_EGA_MAX_TILES) {
        if (fread(row, 1, sizeof row, f) != sizeof row) break;
        memcpy(t->px[stored], row, sizeof row);
        stored++;
    }
    fclose(f);
    if (stored < U2_TILE_COUNT)
        return U2_TILES_EGA_BAD;

    t->count = stored;
    t->tile_w = U2_TILE_W;
    t->tile_h = U2_TILE_H;
    t->stride = U2_TILE_EGA_STRIDE;
    t->bpp = U2_TILE_EGA_BPP;
    t->scale = 1;
    t->has_map = 0;
    t->has_palette = 0;
    memset(t->map_n, 0, sizeof t->map_n);
    t->scope_dungeon = 1;      /* the v1 atlas is the one the game itself uses */
    t->scope_tower = 1;
    /* The DOS art has no transparent pen: index 0 is ordinary background there, so
     * a v1 theme draws every pixel. */
    t->transparent_index = -1;
    snprintf(t->source, sizeof t->source, "%s", path);
    snprintf(t->dir, sizeof t->dir, "%s", dir ? dir : "");
    snprintf(t->name, sizeof t->name, "%s", theme ? theme : U2_EGA_THEME_DEFAULT);
    t->loaded = 1;
    return U2_TILES_EGA_OK;
}

/* ------------------------------------------------------------------- v2 */

static void bad_reason(char *err, size_t n, const char *fmt, ...);

/* Parse the scope list. An unrecognised word is a refusal, not a shrug: scope is
 * the field that decides whether an unmapped mode is refused or silently drawn
 * with the wrong art, so a typo in it must not read as "everything". */
static int parse_scope(const char *v, int *dungeon, int *tower,
                       char *err, size_t n)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%s", v);
    *dungeon = *tower = 0;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        trim(tok);
        if (!*tok) continue;
        if (!strcasecmp(tok, "dungeon")) *dungeon = 1;
        else if (!strcasecmp(tok, "tower")) *tower = 1;
        else if (strcasecmp(tok, "overworld") && strcasecmp(tok, "towns")) {
            bad_reason(err, n, "THEME.INI scope has an unknown word '%s'", tok);
            return -1;
        }
    }
    return 0;
}

static void bad_reason(char *err, size_t n, const char *fmt, ...)
{
    if (!err || n == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

/* Load a v2 theme from a directory that is known to hold a THEME.INI. Every
 * refusal names the file and the reason, because the operator is reading this at
 * a terminal after their theme failed to appear, not reading this source. */
static u2_tiles_ega_result try_load_v2(const char *dirpath, const char *theme,
                                       const char *dir, u2_tiles_ega *t,
                                       char *err, size_t errn,
                                       int ctx_frame_scale)
{
    u2_tiles_ega_result rc = U2_TILES_EGA_BAD;
    unsigned char *ini_buf = NULL, *tiles_buf = NULL, *map_buf = NULL;
    long ini_len = 0, map_len = 0;
    char path[700], val[64];
    int tw = 0, th = 0, bpp = 0, scale = 0, dungeon = 0, tower = 0;
    int transparent = -1;

    if (err && errn) err[0] = '\0';
    memset(t->map_n, 0, sizeof t->map_n);
    t->has_map = 0;
    t->has_palette = 0;
    t->has_dng_wall = 0;

    snprintf(path, sizeof path, "%s/%s", dirpath, V2_INI);
    if (slurp(path, &ini_buf, &ini_len) != 0) {
        bad_reason(err, errn, "cannot read %s", path);
        return U2_TILES_EGA_ERROR;
    }
    const char *ini = (const char *)ini_buf;

    if (ini_get(ini, "tile_w", val, sizeof val) == 1) tw = atoi(val);
    if (ini_get(ini, "tile_h", val, sizeof val) == 1) th = atoi(val);
    if (ini_get(ini, "bpp", val, sizeof val) == 1) bpp = atoi(val);
    if (tw <= 0 || th <= 0) {
        bad_reason(err, errn, "%s: tile_w/tile_h missing or not positive", path);
        goto done;
    }
    if (tw > U2_TILE_EGA_MAX_SIDE || th > U2_TILE_EGA_MAX_SIDE) {
        bad_reason(err, errn, "%s: %dx%d exceeds the %d px this engine blits",
                   path, tw, th, U2_TILE_EGA_MAX_SIDE);
        goto done;
    }
    /* bpp 3 is the control case: a theme that asks for a packing this loader
     * does not implement must not be drawn with a 4bpp guess. */
    if (bpp != 4) {
        bad_reason(err, errn, "%s: bpp = %d, and only 4bpp packed tiles are "
                   "implemented (high nibble = left pixel)", path, bpp);
        goto done;
    }
    if (ini_get(ini, "pixel", val, sizeof val) == 1 &&
        strcasecmp(val, "nibble-high-left") != 0) {
        bad_reason(err, errn, "%s: pixel = '%s' is not implemented", path, val);
        goto done;
    }
    scale = tw / U2_TILE_W;
    if (scale * U2_TILE_W != tw || scale < 1) {
        bad_reason(err, errn, "%s: tile_w = %d is not a whole multiple of the "
                   "%d px layout this engine composites", path, tw, U2_TILE_W);
        goto done;
    }
    if (scale > U2_FRAME_MAX_SCALE) {
        bad_reason(err, errn, "%s: a %d px tile needs a x%d frame and this build "
                   "supports up to x%d", path, tw, scale, U2_FRAME_MAX_SCALE);
        goto done;
    }
    /* A theme whose tiles are NATIVELY larger than the frame this art mode
     * composites at would have to be decimated to fit, which is the one thing the
     * format exists to avoid, so it is refused rather than shrunk. */
    if (ctx_frame_scale > 0 && scale > ctx_frame_scale) {
        bad_reason(err, errn, "%s: %d px tiles need a x%d frame and this run "
                   "composites at x%d; the frame scale is set by the art mode, "
                   "not by the theme", path, tw, scale, ctx_frame_scale);
        goto done;
    }
    if (ini_get(ini, "scope", val, sizeof val) == 1) {
        if (parse_scope(val, &dungeon, &tower, err, errn) != 0) goto done;
    } else {
        dungeon = tower = 1;     /* unscoped means it claims nothing specific */
    }
    /* Optional. Absent means the art has no transparent pen and every pixel draws.
     * Refused rather than ignored when out of range: a typo here would silently
     * punch holes in the wrong colour, which is the kind of wrong that looks fine. */
    if (ini_get(ini, "transparent_index", val, sizeof val) == 1) {
        transparent = atoi(val);
        if (transparent < 0 || transparent > 15) {
            bad_reason(err, errn, "%s: transparent_index = %s is not a palette "
                       "index (0..15)", path, val);
            goto done;
        }
    }

    /* --- the pool --- */
    snprintf(path, sizeof path, "%s/%s", dirpath, V2_TILES);
    long tsz = 0;
    if ((tsz = path_size(path)) < 0) {
        bad_reason(err, errn, "no %s next to %s", V2_TILES, V2_INI);
        goto done;
    }
    const long tbytes = (long)(tw * th * bpp / 8);
    if (tbytes <= 0 || tsz % tbytes != 0) {
        bad_reason(err, errn, "%s is %ld B, not a whole number of %ld B tiles",
                   path, tsz, tbytes);
        goto done;
    }
    const int pool = (int)(tsz / tbytes);
    if (pool > U2_TILE_EGA_MAX_TILES) {
        bad_reason(err, errn, "%s holds %d tiles and this engine caps an atlas "
                   "at %d", path, pool, U2_TILE_EGA_MAX_TILES);
        goto done;
    }
    if (slurp(path, &tiles_buf, NULL) != 0) {
        bad_reason(err, errn, "cannot read %s", path);
        rc = U2_TILES_EGA_ERROR;
        goto done;
    }
    for (int i = 0; i < pool; i++)
        memcpy(t->px[i], tiles_buf + (long)i * tbytes, (size_t)tbytes);

    /* --- palette --- */
    if (ini_get(ini, "palette", val, sizeof val) == 1 && *val) {
        char pp[700];
        snprintf(pp, sizeof pp, "%s/%s", dirpath, val);
        unsigned char *pb = NULL;
        if (slurp(pp, &pb, NULL) != 0) {
            bad_reason(err, errn, "cannot read the palette %s named by %s", val, path);
            goto done;
        }
        if (path_size(pp) < (long)sizeof t->palette) {
            free(pb);
            bad_reason(err, errn, "%s is smaller than 16 RGB triples", pp);
            goto done;
        }
        memcpy(t->palette, pb, sizeof t->palette);
        free(pb);
        t->has_palette = 1;
    }

    /* --- optional dungeon wall texture --- */
    if (ini_get(ini, "dng_wall", val, sizeof val) == 1 && *val) {
        char wp[700];
        snprintf(wp, sizeof wp, "%s/%s", dirpath, val);
        unsigned char *wb = NULL;
        if (slurp(wp, &wb, NULL) != 0) {
            bad_reason(err, errn, "cannot read the dungeon wall tile %s named by %s", val, path);
            goto done;
        }
        if (path_size(wp) < (long)sizeof t->dng_wall) {
            free(wb);
            bad_reason(err, errn, "%s is smaller than one 32x32 tile (%d indices)",
                       wp, U2_DNG_WALL_PX);
            goto done;
        }
        memcpy(t->dng_wall, wb, sizeof t->dng_wall);
        free(wb);
        t->has_dng_wall = 1;
    }

    /* --- the map --- */
    if (ini_get(ini, "map", val, sizeof val) == 1 && *val) {
        char mp[700];
        snprintf(mp, sizeof mp, "%s/%s", dirpath, val);
        if (slurp(mp, &map_buf, &map_len) != 0) {
            bad_reason(err, errn, "cannot read the map %s named by %s", val, path);
            goto done;
        }
        int rows = 0;
        char *ml = (char *)map_buf;
        for (char *line = ml, *nx; line; line = nx) {
            nx = strchr(line, '\n');
            if (nx) *nx++ = '\0';
            trim(line);
            if (!*line || *line == '#') continue;
            char *art_s = line;
            char *rest = strpbrk(line, " \t");
            if (!rest) { bad_reason(err, errn, "%s: row '%s' has no tile list", mp, line); goto done; }
            *rest++ = '\0';
            while (*rest == ' ' || *rest == '\t') rest++;
            int art = (int)strtol(art_s, NULL, 16);
            if (art < 0 || art >= U2_TILE_COUNT) {
                bad_reason(err, errn, "%s: art index %s is outside the 00..%02X set",
                           mp, art_s, U2_TILE_COUNT - 1);
                goto done;
            }
            /* The tile list is ONE field: it ends at the first whitespace, and
             * anything after that is the provenance column. Parsing past the
             * boundary turns `MAP2_1/1` into a third frame at tile 0, which is
             * invisible to every other check in this file. */
            char *fs = strpbrk(rest, " \t");
            if (fs) *fs = '\0';
            int n = 0;
            for (char *tok = rest, *c; tok && *tok; tok = c) {
                c = tok;
                while (*c && *c != ',') c++;
                if (*c) *c++ = '\0';
                if (!*tok) continue;
                const int idx = (int)strtol(tok, NULL, 10);
                if (idx < 0 || idx >= pool) {
                    bad_reason(err, errn, "%s: art %02X points at tile %d, and the "
                               "atlas holds %d", mp, art, idx, pool);
                    goto done;
                }
                if (n >= U2_TILE_EGA_MAX_FRAMES) {
                    bad_reason(err, errn, "%s: art %02X lists more than %d frames",
                               mp, art, U2_TILE_EGA_MAX_FRAMES);
                    goto done;
                }
                t->map[art][n++] = (uint16_t)idx;
            }
            if (n == 0) {
                bad_reason(err, errn, "%s: art %02X has no tile", mp, art);
                goto done;
            }
            t->map_n[art] = (uint8_t)n;
            rows++;
        }
        if (rows == 0) {
            bad_reason(err, errn, "%s is named by %s but has no rows", mp, path);
            goto done;
        }
        t->has_map = 1;
    }

    t->count = pool;
    t->tile_w = tw;
    t->tile_h = th;
    t->stride = tw * bpp / 8;
    t->bpp = bpp;
    t->scale = scale;
    t->scope_dungeon = dungeon;
    t->scope_tower = tower;
    t->transparent_index = transparent;
    snprintf(t->source, sizeof t->source, "%s", path);
    snprintf(t->dir, sizeof t->dir, "%s", dir ? dir : "");
    snprintf(t->name, sizeof t->name, "%s", theme ? theme : "");
    t->loaded = 1;
    rc = U2_TILES_EGA_OK;

done:
    free(ini_buf);
    free(tiles_buf);
    free(map_buf);
    return rc;
}

/* ------------------------------------------------------- theme resolution */

/* Does this directory hold a v2 theme? */
static int is_v2_dir(const char *p)
{
    char q[700];
    if (snprintf(q, sizeof q, "%s/%s", p, V2_INI) >= (int)sizeof q) return 0;
    return path_size(q) >= 0;
}

/* The theme name maps to a path under the themes dir. v2 first: a directory
 * named <name>/ with a THEME.INI is explicit about its geometry, and a pack that
 * ships both spellings means the author meant the v2 one. Then the v1
 * EGATHEME.<UPPERCASED>/EGATILES. Names are matched case-insensitively on
 * purpose, because the pack ships EGATHEME.10 and EGATHEME.ALT in different
 * cases and a copy that came off a FAT volume can arrive uppercased. */
static int theme_relpath(const char *theme, char *out, size_t n)
{
    if (!theme || !*theme || strcmp(theme, "default") == 0)
        return snprintf(out, n, "EGATILES") < (int)n ? 0 : -1;

    char up[16];
    size_t i = 0;
    for (; theme[i] && i + 1 < sizeof up; i++) {
        const char c = theme[i];
        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    up[i] = '\0';
    return snprintf(out, n, "EGATHEME.%s/EGATILES", up) < (int)n ? 0 : -1;
}

/* The v2 directory name for a theme: the name as given, lower-cased, because
 * u2_tiles_ega_next_theme reports v2 themes lower-cased the same way it reports
 * EGATHEME.* ones. */
static int theme_v2_relpath(const char *theme, char *out, size_t n)
{
    if (!theme || !*theme) return -1;
    char low[32];
    size_t i = 0;
    for (; theme[i] && i + 1 < sizeof low; i++) {
        const char c = theme[i];
        low[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    low[i] = '\0';
    return snprintf(out, n, "%s", low) < (int)n ? 0 : -1;
}

/* Candidate themes directories, in resolution order (docs/ega-graphics-plan.md
 * § 2). An explicit --ega-theme-dir is authoritative: the search is confined to
 * it, so naming a directory and not finding the theme there is a refusal rather
 * than a silent hit in a fallback. With no explicit dir, fall through --data-dir
 * and then the two bare-relative defaults where the Exodus upgrade package sits
 * on a dev checkout (the repo's references/ dir, one level up from engine/).
 * Returns the count. */
static int ega_candidates(u2_ctx *ctx, const char **cands)
{
    if (ctx && ctx->ega_theme_dir && *ctx->ega_theme_dir) {
        cands[0] = ctx->ega_theme_dir;
        return 1;
    }
    int nc = 0;
    if (ctx && ctx->data_dir && *ctx->data_dir)
        cands[nc++] = ctx->data_dir;
    if (!u2_is_release_artifact()) {
        cands[nc++] = "../references/u2upgrade";
        cands[nc++] = "references/u2upgrade";
    }
    /* The installed layout's own themes directory, last. Without it an explicit
     * --ega in a tree that has themes/ beside the binary refuses to start while
     * saying nothing at all succeeds, because auto-detection knows about themes/
     * and this list did not — an explicit request must never be worse than an
     * implicit one. Dev fallbacks are ordered ahead of it on purpose, so nothing
     * a developer or a test run resolved before resolves differently now. */
    cands[nc++] = U2_EGA_THEMES_DIR_NAME;
    return nc;
}

/* What the frame scale is for this run. It is a property of the art mode, not of
 * the theme (U2_EGA_FRAME_SCALE), so every caller derives it the same way and a
 * theme can never shrink or grow the window under the renderer. */
static int ega_frame_scale(const u2_ctx *ctx)
{
    return (ctx && ctx->art_mode == U2_ART_EGA) ? U2_EGA_FRAME_SCALE : 1;
}

/* Load one theme out of one candidate directory, v2 before v1. */
static u2_tiles_ega_result open_in_dir(const u2_ctx *ctx, const char *cand,
                                       const char *theme,
                                       u2_tiles_ega *t, char *err, size_t errn)
{
    char v2[40], v1[64], path[700];

    if (theme_v2_relpath(theme, v2, sizeof v2) == 0 &&
        snprintf(path, sizeof path, "%s/%s", cand, v2) < (int)sizeof path &&
        path_is_dir(path) && is_v2_dir(path))
        return try_load_v2(path, theme, cand, t, err, errn, ega_frame_scale(ctx));

    if (theme_relpath(theme, v1, sizeof v1) != 0) return U2_TILES_EGA_ERROR;
    if (snprintf(path, sizeof path, "%s/%s", cand, v1) >= (int)sizeof path)
        return U2_TILES_EGA_ERROR;
    return try_load_v1(path, cand, theme, t);
}

/* Record what the theme implies for the display: the frame scale its tile size
 * needs, and its RGB table if it ships one. A v1 theme CLEARS the palette flag,
 * so cycling from a v2 theme back to `c64` restores the built-in EGA table
 * instead of keeping the FM Towns colours over EGA art. */
static void apply_to_ctx(u2_ctx *ctx, const u2_tiles_ega *t)
{
    if (!ctx || !t) return;
    /* The frame scale follows the ART MODE, not the theme: `--ega` composites at
     * U2_EGA_FRAME_SCALE whatever the theme's own tiles are, and a theme with
     * smaller tiles is block-upscaled to fill it (frame.h, and
     * docs/theme-format.md § 5). Sizing the frame per theme is what turned a
     * Ctrl-T cycle across tile sizes into a renderer resize, and a renderer that
     * sized once at startup while the frame grew underneath it wrote past its own
     * staging buffer. */
    ctx->frame_scale = ega_frame_scale(ctx);
    if (t->has_palette) {
        memcpy(ctx->theme_palette, t->palette, sizeof ctx->theme_palette);
        ctx->theme_palette_set = 1;
        /* Brightest entry, by summed channels. The FM Towns table is
         * {black, blue, red, BLACK, green, cyan, yellow, white}, so index 3 — the
         * CGA white the whole text layer is written with — is a second black, and
         * the status band would be invisible. Picking the brightest is a choice,
         * not a measurement of what the FM Towns port did with its text; it is
         * recorded as one in docs/theme-format.md § 6. */
        int best = 0, bestv = -1;
        for (int i = 0; i < 16; i++) {
            const int v = t->palette[i][0] + t->palette[i][1] + t->palette[i][2];
            if (v > bestv) { bestv = v; best = i; }
        }
        ctx->text_fg = (uint8_t)best;
    } else {
        ctx->theme_palette_set = 0;
        ctx->text_fg = 0;      /* unset: the frame keeps the CGA pair */
    }
    /* The dungeon wall texture travels with the theme, and a v1 theme (or a v2 one
     * that ships no dng_wall) clears it, so cycling back to a plain theme restores
     * the flat corridor fill rather than keeping the last theme's brick. */
    if (t->has_dng_wall) {
        memcpy(ctx->dng_wall, t->dng_wall, sizeof ctx->dng_wall);
        ctx->dng_wall_set = 1;
    } else {
        ctx->dng_wall_set = 0;
    }
}

/*
 * Choose the art axis from what is actually installed, when the player asked for
 * neither. Opened 2026-11-06: a tree built by --install with themes in it still
 * started in CGA unless the player typed --ega, so the better art sat on disk
 * unused, and the fan pack's `default` theme was preferred over the derived FM
 * Towns one even when it was the only reason the install had a themes tree.
 *
 * Two rules keep this a default rather than a surprise:
 *
 *   An explicit choice always wins. --ega, --cga, --enhanced, --ega-theme,
 *   --ega-theme-dir and config.ini's [themes] section all mean "I said what I
 *   want", and auto-detection only fills what nothing set. That is why the
 *   installer now records the theme it actually built: if it wrote
 *   `theme = default` over a fmtowns derivation, the config would be an explicit
 *   choice and this function could never prefer fmtowns on exactly the trees it
 *   exists for.
 *
 *   The candidate list is the INSTALLED layout, never the dev fallbacks.
 *   ega_candidates() adds `references/u2upgrade` for a dev build and falls
 *   through to --data-dir; consulting those here would flip every developer and
 *   every test run to EGA, and the CP2 byte-comparison baseline is measured in
 *   original CGA mode. So: the explicit dir if one is set, else `themes` beside
 *   the launch directory. Nothing else.
 *
 * Returns 1 and sets ctx->art_mode (and ctx->ega_theme, when nothing named one)
 * if a usable theme was found; returns 0 and leaves the context untouched
 * otherwise, which is CGA, which is not a refusal — the strict rule of 2026-11-04
 * is about an explicit --ega silently downgrading, and nobody asked here.
 *
 * `chosen` receives the theme name for the startup announcement.
 */
int u2_tiles_ega_autodetect(u2_ctx *ctx, char *chosen, size_t n, int may_change_mode)
{
    if (!ctx) return 0;

    /* CGA with the mode already decided has no theme to pick: the player asked for
     * the 4-colour art (or for substitute art), and naming a theme for art that is
     * not being drawn would be a fact about nothing. Probing anyway would also
     * probe at frame scale 1, where a 32x32 theme is refused for a reason that is
     * an artefact of the probe rather than of the theme. */
    if (!may_change_mode && ctx->art_mode != U2_ART_EGA)
        return 0;

    const char *dirs[1];
    dirs[0] = (ctx->ega_theme_dir && *ctx->ega_theme_dir)
            ? ctx->ega_theme_dir : U2_EGA_THEMES_DIR_NAME;

    /* Owner ruling 2026-11-13: the fan pack's `10` is the preferred default (the
     * best-looking of the four), then the pack's `default`, then the derived FM
     * Towns theme, then CGA. This reverses the old fmtowns-first order: on a full
     * install that has both the fan themes and a derived FM Towns theme the default
     * is now `10`, and fmtowns is reached by naming it (--ega-theme fmtowns) or by
     * not installing the fan pack. */
    const char *names[3];
    int nn = 0;
    if (ctx->ega_theme && *ctx->ega_theme) {
        names[nn++] = ctx->ega_theme;
    } else {
        names[nn++] = U2_EGA_THEME_PREFERRED;
        names[nn++] = U2_EGA_THEME_DEFAULT;
        names[nn++] = U2_FMTOWNS_THEME;
    }

    /* The frame scale is derived from the art mode (ega_frame_scale), and a v2
     * theme whose tiles are larger than the frame is REFUSED rather than
     * decimated. So the mode has to be EGA before the probe or the 32x32 FM Towns
     * theme is rejected for a reason that is an artefact of the probe. Roll back
     * on failure so a tree with no themes is left exactly as it was found. */
    const u2_art_mode was = ctx->art_mode;
    const char *was_theme = ctx->ega_theme;
    const char *was_dir = ctx->ega_theme_dir;
    if (may_change_mode)
        ctx->art_mode = U2_ART_EGA;

    for (int d = 0; d < 1; d++) {
        for (int i = 0; i < nn; i++) {
            static u2_tiles_ega probe;
            memset(&probe, 0, sizeof probe);
            if (u2_tiles_ega_open_in(ctx, &probe, dirs[d], names[i])
                    != U2_TILES_EGA_OK)
                continue;
            if (!(ctx->ega_theme && *ctx->ega_theme))
                ctx->ega_theme = names[i];
            /* Pin the directory that worked. The strict launch check resolves
             * through ega_candidates(), which without an explicit dir looks at
             * --data-dir and the dev fallbacks and NOT at `themes` — so leaving
             * this unset made the check refuse to start over a theme this function
             * had just loaded, with "no EGA themes directory found" naming
             * directories the player has never heard of. Recording it also means
             * the overworld's own later open() resolves the same way, so the theme
             * announced at startup is the theme that draws. */
            if (!(ctx->ega_theme_dir && *ctx->ega_theme_dir))
                ctx->ega_theme_dir = dirs[d];
            if (chosen && n)
                snprintf(chosen, n, "%s", names[i]);
            return 1;
        }
    }

    ctx->art_mode = was;
    ctx->ega_theme = was_theme;
    ctx->ega_theme_dir = was_dir;
    if (may_change_mode) {
        ctx->theme_palette_set = 0;
        ctx->text_fg = 0;
        ctx->frame_scale = 1;
    }
    return 0;
}

u2_tiles_ega_result u2_tiles_ega_open_in(u2_ctx *ctx, u2_tiles_ega *t,
                                         const char *dir, const char *theme)
{
    if (!t || !dir) return U2_TILES_EGA_ERROR;
    t->loaded = 0;
    t->source[0] = '\0';
    t->dir[0] = '\0';
    const u2_tiles_ega_result r =
        open_in_dir(ctx, dir, theme ? theme : U2_EGA_THEME_DEFAULT, t, NULL, 0);
    if (r == U2_TILES_EGA_OK) apply_to_ctx(ctx, t);
    return r;
}

u2_tiles_ega_result u2_tiles_ega_open(u2_ctx *ctx, u2_tiles_ega *t,
                                      const char *theme)
{
    if (!t) return U2_TILES_EGA_ERROR;
    t->loaded = 0;
    t->source[0] = '\0';

    const char *name = (theme && *theme) ? theme : U2_EGA_THEME_DEFAULT;
    const char *cands[4];
    const int nc = ega_candidates(ctx, cands);

    u2_tiles_ega_result last = U2_TILES_EGA_NOT_READY;
    for (int i = 0; i < nc; i++) {
        const u2_tiles_ega_result r = open_in_dir(ctx, cands[i], name, t, NULL, 0);
        if (r == U2_TILES_EGA_OK) {
            apply_to_ctx(ctx, t);
            return r;
        }
        if (r == U2_TILES_EGA_BAD)      /* present but wrong: stop, do not fall through */
            return r;
        last = r;
    }
    return last;
}

/* Map a directory entry name to a theme name, or 0 if it is not a theme.
 * "EGATILES" (case-insensitive) is "default"; "EGATHEME.<X>" is "<x>" lower-
 * cased; any other directory holding a THEME.INI is that directory's own name,
 * lower-cased. This is the inverse of the two relpath helpers, so a name produced
 * here loads back through u2_tiles_ega_open_in unchanged. */
static int entry_to_theme(const char *d_name, const char *dir, char *out, size_t n)
{
    if (strcasecmp(d_name, "EGATILES") == 0)
        return snprintf(out, n, "default") < (int)n ? 1 : 0;
    if (strncasecmp(d_name, "EGATHEME.", 9) == 0) {
        char low[24];
        size_t i = 0;
        const char *s = d_name + 9;
        for (; s[i] && i + 1 < sizeof low; i++) {
            const char c = s[i];
            low[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
        low[i] = '\0';
        return snprintf(out, n, "%s", low) < (int)n ? 1 : 0;
    }
    if (dir) {
        char full[700];
        if (snprintf(full, sizeof full, "%s/%s", dir, d_name) < (int)sizeof full &&
            path_is_dir(full) && is_v2_dir(full))
            return snprintf(out, n, "%s", d_name) < (int)n ? 1 : 0;
    }
    return 0;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int u2_tiles_ega_next_theme(const char *dir, const char *cur,
                            char *next, size_t n)
{
    if (!dir || !*dir || !next || n == 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;

    /* Collect the theme names, then sort them so the cycle order is the
     * alphabetical order of the directory's themes rather than readdir's
     * arbitrary order. A fixed cap is fine: no pack ships more than a handful. */
    #define U2_EGA_MAX_THEMES 32
    static char names[U2_EGA_MAX_THEMES][32];
    static const char *ptrs[U2_EGA_MAX_THEMES];
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < U2_EGA_MAX_THEMES) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char theme[32];
        if (!entry_to_theme(e->d_name, dir, theme, sizeof theme))
            continue;
        /* EGATILES must be a file and EGATHEME.* a directory; a stat keeps a
         * stray file named EGATHEME.FOO from being read as a theme dir. A v2
         * theme is a directory too, and it is the only way a bare name like
         * `fmtowns` counts at all — the first version of this test assumed any
         * non-EGATHEME entry was a file, which silently left every v2 theme out
         * of Ctrl-T while --ega-theme still loaded it. */
        char full[700];
        if (snprintf(full, sizeof full, "%s/%s", dir, e->d_name) < (int)sizeof full) {
            struct stat st;
            if (stat(full, &st) == 0) {
                const int want_dir = (strncasecmp(e->d_name, "EGATHEME.", 9) == 0)
                                  || is_v2_dir(full);
                if (S_ISDIR(st.st_mode) != want_dir)
                    continue;
            }
        }
        snprintf(names[count], 32, "%s", theme);
        ptrs[count] = names[count];
        count++;
    }
    closedir(d);
    if (count == 0) return 0;

    qsort(ptrs, (size_t)count, sizeof ptrs[0], cmp_str);

    /* Find cur in the sorted list and take the next, wrapping. Absent cur (or a
     * NULL cur) yields the first theme, so the very first Ctrl-T from an unknown
     * state still lands on a real theme. */
    int idx = -1;
    for (int i = 0; i < count; i++)
        if (cur && strcmp(ptrs[i], cur) == 0) { idx = i; break; }
    const int nxt = (idx < 0) ? 0 : (idx + 1) % count;
    snprintf(next, n, "%s", ptrs[nxt]);
    return count;
    #undef U2_EGA_MAX_THEMES
}

/*
 * Fail-fast launch check for --ega. Confirms the chosen theme is present and
 * loadable WITHOUT filling a tile struct, so main() can refuse to start rather
 * than fall back to CGA after the title screen.
 *
 * The owner ruled (2026-11-04) that --ega is strict: it refuses to start when the
 * themes directory does not exist or the chosen theme is not in it. This
 * supersedes the plan's earlier "never fail a launch, fall back to CGA" default
 * (docs/ega-graphics-plan.md § 2/§ 7). On any non-OK result `err` holds a
 * one-line, actionable reason naming the directory searched.
 *
 * A v2 theme is checked by loading it for real, because its failure modes are
 * semantic (a map row pointing outside the atlas, a bpp nobody implements) and
 * no size check can see them. The cost is one file read at startup.
 */
u2_tiles_ega_result u2_tiles_ega_validate(u2_ctx *ctx, char *err, size_t n)
{
    const char *theme = (ctx && ctx->ega_theme && *ctx->ega_theme)
                      ? ctx->ega_theme : U2_EGA_THEME_DEFAULT;

    /* An explicitly named themes dir that does not exist is the clearest error,
     * and it is checked before the fall-through search so a typo in
     * --ega-theme-dir is reported as such rather than as "theme not found". */
    if (ctx && ctx->ega_theme_dir && *ctx->ega_theme_dir &&
        !path_is_dir(ctx->ega_theme_dir)) {
        if (err) snprintf(err, n, "themes dir does not exist: %s", ctx->ega_theme_dir);
        return U2_TILES_EGA_NOT_READY;
    }

    const char *cands[4];
    const int nc = ega_candidates(ctx, cands);
    int any_dir = 0;
    char searched[256];
    searched[0] = '\0';
    size_t used = 0;

    for (int i = 0; i < nc; i++) {
        const int exists = path_is_dir(cands[i]);
        if (exists) any_dir = 1;
        if (err && used + 1 < sizeof searched) {
            const int w = snprintf(searched + used, sizeof searched - used,
                                   "%s%s", used ? ", " : "", cands[i]);
            if (w > 0) used += (size_t)w;
        }
        if (!exists) continue;

        /* v2: load it properly, and remember the scale the frame must have. */
        char v2[40], path[700];
        if (theme_v2_relpath(theme, v2, sizeof v2) == 0 &&
            snprintf(path, sizeof path, "%s/%s", cands[i], v2) < (int)sizeof path &&
            path_is_dir(path) && is_v2_dir(path)) {
            static u2_tiles_ega probe;
            memset(&probe, 0, sizeof probe);
            char why[256] = "";
            const u2_tiles_ega_result r = try_load_v2(path, theme, cands[i],
                                                      &probe, why, sizeof why,
                                                      ega_frame_scale(ctx));
            if (r == U2_TILES_EGA_OK) apply_to_ctx(ctx, &probe);
            if (r != U2_TILES_EGA_OK && err && *why)
                snprintf(err, n, "%s", why);
            return r;
        }

        char rel[64];
        if (theme_relpath(theme, rel, sizeof rel) != 0) {
            if (err) snprintf(err, n, "invalid theme name: %s", theme);
            return U2_TILES_EGA_ERROR;
        }
        if (snprintf(path, sizeof path, "%s/%s", cands[i], rel) >= (int)sizeof path)
            continue;
        const long sz = path_size(path);
        if (sz >= (long)(U2_TILE_COUNT * U2_TILE_EGA_BYTES)) {
            if (ctx) {
                ctx->frame_scale = ega_frame_scale(ctx);
                ctx->theme_palette_set = 0;
            }
            return U2_TILES_EGA_OK;
        }
        if (sz >= 0) {   /* present but too short: a real file, wrong shape */
            if (err) snprintf(err, n, "EGATILES present but wrong size (%ld B): %s",
                              sz, path);
            return U2_TILES_EGA_BAD;
        }
    }

    if (err) {
        if (!any_dir)
            snprintf(err, n, "no EGA themes directory found (looked in: %s)", searched);
        else
            snprintf(err, n, "theme '%s' not found (looked in: %s)", theme, searched);
    }
    return U2_TILES_EGA_NOT_READY;
}

const char *u2_tiles_ega_result_text(u2_tiles_ega_result r)
{
    switch (r) {
    case U2_TILES_EGA_OK:        return "ok";
    case U2_TILES_EGA_NOT_READY: return "no theme found in any themes dir";
    case U2_TILES_EGA_BAD:       return "theme present but wrong shape";
    case U2_TILES_EGA_ERROR:     return "I/O error";
    }
    return "unknown";
}
