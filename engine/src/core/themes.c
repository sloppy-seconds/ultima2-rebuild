/*
 * themes.c — see themes.h. The rules are in the header; this file is their
 * implementation, and every refusal below exists because the Python builder it
 * replaces had already been bitten by the corresponding mistake.
 */

#define _POSIX_C_SOURCE 200809L   /* dirent, stat under -std=c11 */

#include "core/themes.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/copy.h"
#include "core/sha256.h"

#define MAX_ITEMS    512
#define ATLAS_BYTES  8320L        /* 65 x 128, no header (core/tiles_ega.h) */
#define ATLAS_MIN    8192L        /* 64 x 128, what the engine actually reads */
#define COLOR_BYTES    32L        /* ega.drv loads exactly 0x20 bytes */
#define PIC_BYTES   64000L        /* 320 x 200, one byte per pixel */
#define MAX_DEPTH      3
#define ATLAS_NAME   "EGATILES"
#define PREFIX       "EGATHEME."
#define DEFAULT_NAME "default"

typedef enum { ROLE_ATLAS, ROLE_COLOR, ROLE_PICTURE, ROLE_IGNORE, ROLE_BAD } role_t;

typedef struct {
    char dst[512];
    char src[1024];
    char kind[24];
    char theme[64];
    char key[96];                 /* dedups a root-hoisted picture against its twin */
    long size;
    char sha[U2_HASH_LEN + 1];
    int generated;
} titem;

typedef struct {
    titem *items;
    int n;
    int problems;
    int shadowed;
    int skipped;
    FILE *report;
} tplan;

/* The manifest for a reshaped default theme. It describes the atlas it sits
 * beside rather than inventing anything: 16x16 at 4bpp, identity mapping (no
 * MAP.TSV), no palette of its own, and every scope claimed — which is what the v1
 * loader assumed for a bare EGATILES too, so reshaping the default cannot quietly
 * narrow what the engine will draw with it. */
static const char *default_theme_ini(void)
{
    return "[theme]\n"
           "name = default\n"
           "tile_w = 16\n"
           "tile_h = 16\n"
           "bpp = 4\n"
           "pixel = nibble-high-left\n"
           "identity = yes\n"
           "scope = overworld,towns,dungeon,tower\n"
           "source = the fan pack's top-level EGATILES, reshaped by u2 --install\n";
}

/* ---------------------------------------------------------------- classify */

static int ends_upper(const char *up, const char *suffix)
{
    const size_t a = strlen(up), b = strlen(suffix);
    return a >= b && !strcmp(up + a - b, suffix);
}

/* PIC<3 or 4 letters><-|.><EGA|E>, the seven shipped bases being three letters
 * (DNG DRA SPA MIN OUT TWN CAS). The engine tries both separators and both cases,
 * so any spelling is loadable and the builder keeps the source's. */
static int pic_base(const char *up, char *base, size_t n)
{
    if (strncmp(up, "PIC", 3))
        return 0;
    const char *p = up + 3;
    size_t letters = 0;
    while (isalpha((unsigned char)*p) && letters < 4) { p++; letters++; }
    if (letters < 3 || letters > 4)
        return 0;
    if (*p != '-' && *p != '.')
        return 0;
    const char *tail = p + 1;
    if (strcmp(tail, "EGA") && strcmp(tail, "E"))
        return 0;
    if (base && n)
        snprintf(base, n, "%.*s", (int)letters, up + 3);
    return 1;
}

/* ^PIC[A-Z]{3,4}$ with no separator: a CGA picture, whose EGA sibling is the one
 * we want. */
static int pic_cga(const char *up)
{
    if (strncmp(up, "PIC", 3))
        return 0;
    const char *p = up + 3;
    size_t letters = 0;
    while (isalpha((unsigned char)*p)) { p++; letters++; }
    return *p == '\0' && letters >= 3 && letters <= 4;
}

static role_t classify(const char *name, long size, const char **detail)
{
    char up[128];
    snprintf(up, sizeof up, "%.127s", name);
    for (char *s = up; *s; s++)
        *s = (char)toupper((unsigned char)*s);

    if (!strcmp(up, ATLAS_NAME)) {
        if (size < ATLAS_MIN) {
            *detail = "below the size the engine reads";
            return ROLE_BAD;
        }
        *detail = "4bpp 16x16 x65";
        return ROLE_ATLAS;
    }
    if (!strcmp(up, "EGACOLOR")) {
        if (size != COLOR_BYTES) {
            *detail = "not the 32 bytes the driver loads";
            return ROLE_BAD;
        }
        *detail = "colour-index remap (ega.drv loads exactly 0x20)";
        return ROLE_COLOR;
    }
    if (pic_base(up, NULL, 0)) {
        if (size < PIC_BYTES) {
            *detail = "shorter than a 320x200 paletted picture";
            return ROLE_BAD;
        }
        *detail = "full-screen EGA picture";
        return ROLE_PICTURE;
    }

    /* Everything below is deliberately IGNORED rather than guessed at: the fan pack
     * ships patchers, .pat deltas, .IDX indexes and CGA art in the same directories
     * as the EGA themes, and a builder that invents roles for those puts game data
     * in a themes tree. */
    if (!strcmp(up, "CGATILES")) {
        *detail = "CGA atlas; the engine's CGA tiles come from the exe";
        return ROLE_IGNORE;
    }
    if (ends_upper(up, ".IDX")) {
        *detail = "fan-pack index for compressed CGA pictures";
        return ROLE_IGNORE;
    }
    static const char *const junk[] = { ".PAT", ".EXE", ".COM", ".DRV", ".PAL",
                                        ".CFG", ".INI", ".TXT", ".BAT" };
    for (size_t i = 0; i < sizeof junk / sizeof junk[0]; i++)
        if (ends_upper(up, junk[i])) {
            *detail = "patcher, driver or config, not art";
            return ROLE_IGNORE;
        }
    if (pic_cga(up)) {
        *detail = "CGA picture; the EGA spelling is the sibling we want";
        return ROLE_IGNORE;
    }
    if (!strncmp(up, "MAP", 3) || !strncmp(up, "MON", 3) || !strncmp(up, "TLK", 3)) {
        *detail = "game world data, belongs with --data-dir";
        return ROLE_IGNORE;
    }
    *detail = "unrecognised; not copied";
    return ROLE_IGNORE;
}

const char *u2_themes_classify(const char *name, long size)
{
    const char *detail = "";
    switch (classify(name, size, &detail)) {
    case ROLE_ATLAS:    return "atlas";
    case ROLE_COLOR:    return "color";
    case ROLE_PICTURE:  return "picture";
    case ROLE_BAD:      return "bad";
    default:            return "ignore";
    }
}

/* ------------------------------------------------------------------- names */

static void theme_name_for(const char *dir_name, int is_root, char *out, size_t n)
{
    char up[128];
    snprintf(up, sizeof up, "%.127s", dir_name);
    for (char *s = up; *s; s++)
        *s = (char)toupper((unsigned char)*s);

    if (is_root && strncmp(up, PREFIX, strlen(PREFIX))) {
        snprintf(out, n, "%s", DEFAULT_NAME);
        return;
    }
    const char *rest = strncmp(up, PREFIX, strlen(PREFIX)) ? up : up + strlen(PREFIX);
    if (!*rest)
        rest = up;
    char low[64];
    size_t i = 0;
    for (; rest[i] && i + 1 < sizeof low; i++)
        low[i] = (char)tolower((unsigned char)rest[i]);
    low[i] = '\0';
    snprintf(out, n, "%s", low);
}

/* -------------------------------------------------------------------- plan */

static int plan_has_dst(const tplan *p, const char *dst)
{
    for (int i = 0; i < p->n; i++)
        if (!strcasecmp(p->items[i].dst, dst))
            return 1;
    return 0;
}

static int plan_has_key(const tplan *p, const char *key)
{
    for (int i = 0; i < p->n; i++)
        if (*p->items[i].key && !strcmp(p->items[i].key, key))
            return 1;
    return 0;
}

static void add_copy(tplan *p, const char *src, const char *dst, const char *theme,
                     const char *kind, const char *key)
{
    if (p->n >= MAX_ITEMS)
        return;
    if (plan_has_dst(p, dst) || (key && *key && plan_has_key(p, key))) {
        p->shadowed++;
        return;
    }
    titem *it = &p->items[p->n++];
    memset(it, 0, sizeof *it);
    snprintf(it->dst, sizeof it->dst, "%.511s", dst);
    snprintf(it->src, sizeof it->src, "%.1023s", src);
    snprintf(it->kind, sizeof it->kind, "%.23s", kind);
    snprintf(it->theme, sizeof it->theme, "%.63s", theme ? theme : "");
    snprintf(it->key, sizeof it->key, "%.95s", key ? key : "");
    it->size = u2_file_size(src);
    if (u2_hash_file(src, it->sha, NULL) != 0)
        it->sha[0] = '\0';
}

static void add_generated(tplan *p, const char *dst, const char *theme, const char *body)
{
    if (p->n >= MAX_ITEMS)
        return;
    if (plan_has_dst(p, dst)) {
        p->shadowed++;
        return;
    }
    titem *it = &p->items[p->n++];
    memset(it, 0, sizeof *it);
    snprintf(it->dst, sizeof it->dst, "%.511s", dst);
    snprintf(it->kind, sizeof it->kind, "generated");
    snprintf(it->theme, sizeof it->theme, "%.63s", theme);
    snprintf(it->key, sizeof it->key, "GEN:%.80s", dst);
    it->size = (long)strlen(body);
    char hex[65];
    sha256_hex(body, (size_t)it->size, hex);
    memcpy(it->sha, hex, U2_HASH_LEN);
    it->sha[U2_HASH_LEN] = '\0';
    it->generated = 1;
    /* the body travels by name, not by pointer into a literal: keep the pointer in
     * src, which is unused for generated rows and is the widest field we have */
    snprintf(it->src, sizeof it->src, "%.1023s", body);
}

static int list_files(const char *dir, char names[][128], int max)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) && n < max) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char full[1200];
        snprintf(full, sizeof full, "%.1070s/%.127s", dir, e->d_name);
        if (u2_is_file(full))
            snprintf(names[n++], 128, "%.127s", e->d_name);
    }
    closedir(d);
    /* sorted by uppercase name, so the plan is reproducible and a collision is
     * decided by the filename rather than by readdir order */
    for (int i = 1; i < n; i++) {
        char tmp[128];
        memcpy(tmp, names[i], 128);
        int j = i - 1;
        while (j >= 0 && strcasecmp(names[j], tmp) > 0) {
            memcpy(names[j + 1], names[j], 128);
            j--;
        }
        memcpy(names[j + 1], tmp, 128);
    }
    return n;
}

static void walk_source(tplan *p, const char *root, const char *dir, int depth)
{
    char names[256][128];
    const int n = list_files(dir, names, 256);

    int is_theme = 0;
    for (int i = 0; i < n; i++)
        if (!strcasecmp(names[i], ATLAS_NAME))
            is_theme = 1;

    const int is_root = !strcmp(dir, root);
    char theme[64] = "";
    char outdir[128] = "";
    if (is_theme) {
        const char *base = strrchr(dir, '/');
        base = base ? base + 1 : dir;
        theme_name_for(base, is_root, theme, sizeof theme);
        if (!strcmp(theme, DEFAULT_NAME))
            snprintf(outdir, sizeof outdir, "%s", DEFAULT_NAME);
        else {
            char up[64];
            size_t i = 0;
            for (; theme[i] && i + 1 < sizeof up; i++)
                up[i] = (char)toupper((unsigned char)theme[i]);
            up[i] = '\0';
            snprintf(outdir, sizeof outdir, "%s%s", PREFIX, up);
        }
    }

    for (int i = 0; i < n; i++) {
        const char *detail = "";
        char full[1200];
        snprintf(full, sizeof full, "%.1070s/%.127s", dir, names[i]);
        const role_t role = classify(names[i], u2_file_size(full), &detail);

        if (role == ROLE_BAD) {
            p->problems++;
            fprintf(p->report, "  problem %s/%s: %s\n", dir, names[i], detail);
            continue;
        }
        if (role == ROLE_IGNORE) {
            p->skipped++;
            continue;
        }

        if (!is_theme) {
            /* Not a theme directory, so the only thing wanted from it is a
             * full-screen picture: the Galaxy pack ships the whole PIC*-E set and
             * no atlas at all, and that is a complete answer, not a defect.
             *
             * The dedup key is the picture BASE, not the filename: PICDNG.EGA and
             * PICDNG-E are the same picture in two spellings, pic_ega.c tries both,
             * and the themes root is where it looks — so one per base, first source
             * wins, or the root fills with duplicates that differ only in a dash. */
            if (role == ROLE_PICTURE) {
                char base[8], key[96];
                char up[128];
                size_t k = 0;
                for (; names[i][k] && k + 1 < sizeof up; k++)
                    up[k] = (char)toupper((unsigned char)names[i][k]);
                up[k] = '\0';
                if (pic_base(up, base, sizeof base))
                    snprintf(key, sizeof key, "PIC:%.6s", base);
                else
                    snprintf(key, sizeof key, "PIC:%.20s", up);
                add_copy(p, full, names[i], NULL, "picture", key);
            }
            continue;
        }

        char dst[512];
        if (role == ROLE_PICTURE) {
            snprintf(dst, sizeof dst, "%.100s/%.127s", outdir, names[i]);
            add_copy(p, full, dst, theme, "theme-file", NULL);
            if (!strcmp(theme, DEFAULT_NAME)) {
                /* Also hoisted to the root, because pic_ega.c resolves pictures at
                 * the top level of the themes dir and nowhere else, so the default
                 * theme's art is the set the engine can actually draw today. */
                char base[8], key[96];
                char up[128];
                size_t k = 0;
                for (; names[i][k] && k + 1 < sizeof up; k++)
                    up[k] = (char)toupper((unsigned char)names[i][k]);
                up[k] = '\0';
                if (pic_base(up, base, sizeof base))
                    snprintf(key, sizeof key, "PIC:%.6s", base);
                else
                    snprintf(key, sizeof key, "PIC:%.20s", up);
                add_copy(p, full, names[i], NULL, "picture", key);
            }
            continue;
        }

        if (!strcmp(theme, DEFAULT_NAME)) {
            /* The default theme is reshaped into a v2 directory like every other
             * theme; a bare EGATILES at the root is the vendor's layout, and the
             * engine reads default/THEME.INI through the same call. */
            snprintf(dst, sizeof dst, "%.7s/%.127s", DEFAULT_NAME,
                     role == ROLE_ATLAS ? "TILES" : names[i]);
        } else {
            snprintf(dst, sizeof dst, "%.100s/%.127s", outdir, names[i]);
        }
        add_copy(p, full, dst, theme, "theme-file", NULL);
    }

    if (is_theme && !strcmp(theme, DEFAULT_NAME)) {
        char dst[128];
        snprintf(dst, sizeof dst, "%s/THEME.INI", DEFAULT_NAME);
        add_generated(p, dst, DEFAULT_NAME, default_theme_ini());
    }

    if (depth >= MAX_DEPTH)
        return;

    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || e->d_name[0] == '.')
            continue;
        char child[1200];
        snprintf(child, sizeof child, "%.1070s/%.127s", dir, e->d_name);
        if (u2_is_dir(child))
            walk_source(p, root, child, depth + 1);
    }
    closedir(d);
}

/* ------------------------------------------------------------------- build */

static int cmp_items(const void *a, const void *b)
{
    return strcasecmp(((const titem *)a)->dst, ((const titem *)b)->dst);
}

static int write_themes_manifest(const tplan *p, const u2_themes_request *rq)
{
    char path[1200], tmp[1240];
    snprintf(path, sizeof path, "%.1180s/%s", rq->out, U2_THEMES_MANIFEST);
    snprintf(tmp, sizeof tmp, "%.1180s.u2tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    fprintf(f, "# built by u2 --install — do not hand-edit, regenerate\n");
    for (int i = 0; i < rq->n_sources; i++)
        fprintf(f, "# source %s\n", rq->sources[i]);
    fprintf(f, "# dst\tkind\tsource\tsize\tsha256:16\n");
    for (int i = 0; i < p->n; i++) {
        const titem *it = &p->items[i];
        if (it->generated)
            fprintf(f, "%s\tgenerated\tgenerated:%s\t%ld\t%s\n",
                    it->dst, it->theme, it->size, it->sha);
        else
            fprintf(f, "%s\t%s\t%s\t%ld\t%s\n",
                    it->dst, it->kind, it->src, it->size, it->sha);
    }
    if (fclose(f) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

int u2_themes_build(const u2_themes_request *rq, FILE *report, char *err, size_t errsz)
{
    if (!rq || !rq->out || !*rq->out || rq->n_sources <= 0) {
        snprintf(err, errsz, "themes need an output directory and at least one source");
        return 1;
    }
    titem *items = calloc(MAX_ITEMS, sizeof *items);
    if (!items) {
        snprintf(err, errsz, "out of memory");
        return 1;
    }
    tplan p = { items, 0, 0, 0, 0, report };

    for (int i = 0; i < rq->n_sources; i++) {
        if (!u2_is_dir(rq->sources[i])) {
            snprintf(err, errsz, "themes source %s is not a directory", rq->sources[i]);
            free(items);
            return 1;
        }
        fprintf(report, "-- themes source %s\n", rq->sources[i]);
        walk_source(&p, rq->sources[i], rq->sources[i], 0);
    }

    int themes = 0;
    for (int i = 0; i < p.n; i++)
        if (!strcmp(p.items[i].kind, "theme-file") &&
            (!strcasecmp(strrchr(p.items[i].dst, '/') ? strrchr(p.items[i].dst, '/') + 1
                                                     : p.items[i].dst, ATLAS_NAME) ||
             !strcasecmp(strrchr(p.items[i].dst, '/') ? strrchr(p.items[i].dst, '/') + 1
                                                      : p.items[i].dst, "TILES")))
            themes++;
    if (themes == 0) {
        snprintf(err, errsz,
                 "no directory containing an " ATLAS_NAME " was found in the themes "
                 "sources; a themes tree with no themes is worse than none at all");
        free(items);
        return 1;
    }
    if (p.problems) {
        snprintf(err, errsz, "%d file(s) in the themes sources are the wrong size",
                 p.problems);
        free(items);
        return 1;
    }

    if (u2_mkdir_p(rq->out) != 0) {
        snprintf(err, errsz, "cannot create %s", rq->out);
        free(items);
        return 1;
    }

    qsort(items, (size_t)p.n, sizeof *items, cmp_items);

    int written = 0, failed = 0;
    for (int i = 0; i < p.n; i++) {
        char full[1300];
        snprintf(full, sizeof full, "%.1190s/%.100s", rq->out, p.items[i].dst);
        char parent[1300];
        const char *slash = strrchr(full, '/');
        snprintf(parent, sizeof parent, "%.*s", slash ? (int)(slash - full) : 0, full);
        if (u2_mkdir_p(parent) != 0) { failed++; continue; }

        if (p.items[i].generated) {
            char sha[U2_HASH_LEN + 1];
            if (u2_write_verified(full, p.items[i].src, (size_t)p.items[i].size,
                                  p.items[i].sha) != 0) {
                (void)sha; failed++; continue;
            }
        } else if (u2_copy_verified(p.items[i].src, full, p.items[i].size,
                                    p.items[i].sha) != 0) {
            failed++;
            continue;
        }
        written++;
    }
    if (failed) {
        snprintf(err, errsz, "%d theme file(s) did not copy byte-identically", failed);
        free(items);
        return 1;
    }

    if (write_themes_manifest(&p, rq) != 0) {
        snprintf(err, errsz, "cannot write %s", U2_THEMES_MANIFEST);
        free(items);
        return 1;
    }

    fprintf(report, "themes: %d file(s), %d theme(s), %d shadowed, %d skipped, "
                    "manifest %s\n", written, themes, p.shadowed, p.skipped,
            U2_THEMES_MANIFEST);
    free(items);
    return 0;
}

/* ----------------------------------------------------------------- manifest */

typedef struct {
    char dst[512];
    char kind[24];
    char src[1024];
    long size;
    char sha[U2_HASH_LEN + 1];
} mrow;

/* Read the manifest, keeping the leading comment lines so a rewrite does not
 * silently drop the provenance of the sources that produced the tree. */
static int manifest_load(const char *themes_dir, char comments[16][200], int *nc,
                         mrow *rows, int max)
{
    char path[1200];
    snprintf(path, sizeof path, "%.1180s/%s", themes_dir, U2_THEMES_MANIFEST);
    FILE *f = fopen(path, "rb");
    *nc = 0;
    if (!f)
        return 0;                       /* absent is fine; we are creating it */
    char line[1500];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            if (*nc < 16) {
                line[strcspn(line, "\r\n")] = '\0';
                snprintf(comments[(*nc)++], 200, "%.199s", line);
            }
            continue;
        }
        if (line[0] == '\n' || n >= max)
            continue;
        char *dst = strtok(line, "\t");
        char *kind = strtok(NULL, "\t");
        char *src = strtok(NULL, "\t");
        char *size = strtok(NULL, "\t");
        char *sha = strtok(NULL, "\t");
        if (!dst || !kind || !src || !size || !sha)
            continue;
        sha[strcspn(sha, "\r\n")] = '\0';
        snprintf(rows[n].dst, sizeof rows[n].dst, "%.511s", dst);
        snprintf(rows[n].kind, sizeof rows[n].kind, "%.23s", kind);
        snprintf(rows[n].src, sizeof rows[n].src, "%.1023s", src);
        rows[n].size = strtol(size, NULL, 10);
        snprintf(rows[n].sha, sizeof rows[n].sha, "%.16s", sha);
        n++;
    }
    fclose(f);
    return n;
}

static int mrow_cmp(const void *a, const void *b)
{
    return strcasecmp(((const mrow *)a)->dst, ((const mrow *)b)->dst);
}

/* `comments` is a flat run of 200-byte rows, not a 2-D array parameter: a
 * `const char comments[16][200]` parameter is a pointer to an array of const
 * chars, and a caller's plain `char comments[16][200]` does not convert to that
 * conformingly before C23. Same reason as the palette parameters in core/frame.h.
 */
static int manifest_save(const char *themes_dir, const char *comments, int nc,
                         mrow *rows, int n)
{
    char path[1200], tmp[1240];
    snprintf(path, sizeof path, "%.1180s/%s", themes_dir, U2_THEMES_MANIFEST);
    snprintf(tmp, sizeof tmp, "%.1180s.u2tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    if (nc > 0) {
        for (int i = 0; i < nc; i++)
            fprintf(f, "%s\n", comments + 200 * i);
    } else {
        fprintf(f, "# built by u2 --install — do not hand-edit, regenerate\n");
    }
    fprintf(f, "# dst\tkind\tsource\tsize\tsha256:16\n");
    qsort(rows, (size_t)n, sizeof *rows, mrow_cmp);
    for (int i = 0; i < n; i++)
        fprintf(f, "%s\t%s\t%s\t%ld\t%s\n", rows[i].dst, rows[i].kind,
                rows[i].src, rows[i].size, rows[i].sha);
    if (fclose(f) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

int u2_themes_record(const char *themes_dir, const char *subdir,
                     const char *origin, char *err, size_t errsz)
{
    static mrow rows[512];
    char comments[16][200];
    int nc = 0;
    int n = manifest_load(themes_dir, comments, &nc, rows, 512);

    char dir[1200];
    snprintf(dir, sizeof dir, "%.1080s/%.100s", themes_dir, subdir);
    DIR *d = opendir(dir);
    if (!d) {
        snprintf(err, errsz, "cannot read %s to record it in the themes manifest", dir);
        return 1;
    }
    struct dirent *e;
    int added = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, U2_THEMES_MANIFEST))
            continue;
        char full[1300];
        snprintf(full, sizeof full, "%.1100s/%.127s", dir, e->d_name);
        if (!u2_is_file(full))
            continue;
        char dst[512];
        snprintf(dst, sizeof dst, "%.99s/%.127s", subdir, e->d_name);

        int seen = 0;
        for (int i = 0; i < n; i++)
            if (!strcasecmp(rows[i].dst, dst)) { seen = 1; break; }
        if (seen)
            continue;                       /* idempotent: never duplicate a row */
        if (n >= 512) {
            snprintf(err, errsz, "the themes manifest is full");
            closedir(d);
            return 1;
        }
        char sha[U2_HASH_LEN + 1];
        long sz = 0;
        if (u2_hash_file(full, sha, &sz) != 0) {
            snprintf(err, errsz, "cannot hash %s", full);
            closedir(d);
            return 1;
        }
        snprintf(rows[n].dst, sizeof rows[n].dst, "%s", dst);
        snprintf(rows[n].kind, sizeof rows[n].kind, "derived");
        snprintf(rows[n].src, sizeof rows[n].src, "%.1023s", origin ? origin : "generated");
        rows[n].size = sz;
        snprintf(rows[n].sha, sizeof rows[n].sha, "%s", sha);
        n++;
        added++;
    }
    closedir(d);

    if (manifest_save(themes_dir, &comments[0][0], nc, rows, n) != 0) {
        snprintf(err, errsz, "cannot rewrite %s", U2_THEMES_MANIFEST);
        return 1;
    }
    (void)added;
    return 0;
}

/* ------------------------------------------------------------------- check */

int u2_themes_check(const char *out, FILE *report, char *err, size_t errsz)
{
    char path[1200];
    snprintf(path, sizeof path, "%.1180s/%s", out, U2_THEMES_MANIFEST);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errsz, "%s has no %s", out, U2_THEMES_MANIFEST);
        return 1;
    }

    char line[1500];
    int rows = 0, missing = 0, wrong = 0;
    /* the atlas each theme directory is expected to hold, remembered as we go */
    char themes[64][64];
    int nthemes = 0;

    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char *dst = strtok(line, "\t");
        char *kind = strtok(NULL, "\t");
        char *src = strtok(NULL, "\t");
        char *size = strtok(NULL, "\t");
        char *sha = strtok(NULL, "\t");
        if (!dst || !kind || !src || !size || !sha)
            continue;
        rows++;
        sha[strcspn(sha, "\r\n")] = '\0';

        char full[1300];
        snprintf(full, sizeof full, "%.1190s/%.100s", out, dst);
        char got[U2_HASH_LEN + 1];
        long sz = 0;
        if (u2_hash_file(full, got, &sz) != 0) {
            fprintf(report, "  missing %s\n", dst);
            missing++;
            continue;
        }
        if (sz != strtol(size, NULL, 10) || strcmp(got, sha) != 0) {
            fprintf(report, "  wrong   %s differs from %s\n", dst, U2_THEMES_MANIFEST);
            wrong++;
            continue;
        }

        const char *slash = strchr(dst, '/');
        if (slash && nthemes < 64) {
            snprintf(themes[nthemes], sizeof themes[0], "%.*s",
                     (int)(slash - dst), dst);
            nthemes++;
        }
    }
    fclose(f);

    if (rows == 0) {
        snprintf(err, errsz, "%s lists no files", U2_THEMES_MANIFEST);
        return 1;
    }

    /* Every theme directory must actually hold an atlas. This is the
     * half-copied-theme case: a theme that lost its EGATILES or its EGACOLOR still
     * loads, draws something plausible, and is wrong. */
    int empty_themes = 0;
    for (int t = 0; t < nthemes; t++) {
        char dir[1300];
        snprintf(dir, sizeof dir, "%.1190s/%.63s", out, themes[t]);
        DIR *d = opendir(dir);
        if (!d)
            continue;
        int has_atlas = 0;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcasecmp(e->d_name, ATLAS_NAME) || !strcasecmp(e->d_name, "TILES"))
                has_atlas = 1;
        }
        closedir(d);
        if (!has_atlas) {
            fprintf(report, "  wrong   theme %s has no atlas\n", themes[t]);
            empty_themes++;
        }
    }

    const int bad = missing + wrong + empty_themes;
    if (bad) {
        snprintf(err, errsz, "%d problem(s) in the themes tree", bad);
        return 1;
    }
    fprintf(report, "themes ok: %d file(s)\n", rows);
    return 0;
}
