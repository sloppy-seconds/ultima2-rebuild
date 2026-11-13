/*
 * fmtowns.c — see fmtowns.h. The port of the FM Towns derivation.
 *
 * Three things are deliberately NOT reimplemented more cleverly than the Python
 * they came from, because a port's only honest test is byte equality and cleverness
 * is how a port ends up quietly different:
 *
 *   * the ISO reader walks directory records the same way, including the
 *     sector-boundary resync when a record length byte is zero, and takes the file
 *     name from offset 33 of the record exactly as the Python does;
 *   * the bank "decoder" is not a decoder. A raw GRAPH bank (DUNG1/DUNG2/CHEST/MAP*)
 *     is a 512-byte header and then 32x32 tiles at 4bpp packed, high nibble left,
 *     values already masked to 0..7. The monster banks and EXTRA are instead TIFF-
 *     wrapped (magic 49 49 2a 00) with FillOrder=2, i.e. low nibble left; the strip
 *     still starts at byte 512 and EXTRA is still 32x32, so only the nibble order
 *     differs for the banks the theme references. There is no compression and no
 *     bitplane interleave, and pretending otherwise would be the kind of plausible
 *     wrong model this project keeps recording in its handoffs. See
 *     docs/fmtowns-dungeon-render-brief.md § 4;
 *   * the pool order is first-appearance across the crosswalk rows, in file order,
 *     so TILES is numbered identically on both sides.
 */

#define _POSIX_C_SOURCE 200809L   /* dirent, stat under -std=c11 */

#include "core/fmtowns.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "core/copy.h"
#include "core/sha256.h"
#include "fmtowns_map.h"          /* generated: engine/content/fmtowns_map.h */

#define SEC        2352           /* raw CD sector, MODE1/2352 */
#define DSIZE      2048           /* data bytes per sector */
#define BANK_HDR    512           /* GRAPH bank header, skipped */
#define TILE_BYTES  512           /* 32x32 at 4bpp */
#define TILE_PX    1024
#define MAX_POOL    512
/* Which DUNG1 tile is baked as the corridor wall texture. Tiles ~1-9 are the flat
 * blue brick (docs/enhanced-dungeon-3d.md § 0d); tile 1 is a clean brick face. This
 * is a design pick, not a value read from the FM Towns binary's tile-index table, and
 * it is a single constant to change once that table is decoded. */
#define DNG_WALL_BANK "DUNG1"
#define DNG_WALL_TILE 1
#define MAX_BANKS    16
#define MAX_FILES   512
#define MAX_TOKENS   16
#define MAX_BLOB  (8u * 1024u * 1024u)

/* Read off the running FM Towns machine, indices 0..7. Index 3 really is black —
 * a duplicate of 0, not a placeholder — and the letter tiles use exactly {3,7},
 * which is black on white, which is what the letters look like in a real
 * screenshot. docs/fmtowns-graphics.md § 3 has the whole story, including the
 * earlier misread that made every bank look like dithered noise. */
static const int k_pal[8][3] = {
    { 0, 0, 0 }, { 0, 0, 255 }, { 255, 0, 0 }, { 0, 0, 0 },
    { 0, 255, 0 }, { 0, 255, 255 }, { 255, 255, 0 }, { 255, 255, 255 },
};

typedef struct {
    char name[64];                /* uppercased, ";N" version suffix stripped */
    long lba;
    long size;
} dentry;

typedef struct {
    char bank[16];
    unsigned char *blob;
    long ntiles;
} bank_cache;

static long le32(const unsigned char *p)
{
    return (long)p[0] | ((long)p[1] << 8) | ((long)p[2] << 16) | ((long)p[3] << 24);
}

static long le16(const unsigned char *p)
{
    return (long)p[0] | ((long)p[1] << 8);
}

/* FillOrder (TIFF tag 266) of a GRAPH bank's first IFD: 1 = high nibble left,
 * 2 = low nibble left. Raw banks carry no TIFF magic and are fill 1. This mirrors
 * fmtowns_xwalk.py:bank_dims so the C and Python derivations agree byte for byte. */
static int bank_fill_order(const unsigned char *bank)
{
    if (!(bank[0] == 'I' && bank[1] == 'I' && bank[2] == 0x2a && bank[3] == 0))
        return 1;
    long ifd = le32(bank + 4);
    int n = (int)le16(bank + ifd);
    for (int k = 0; k < n; k++) {
        long e = ifd + 2 + k * 12;
        if (le16(bank + e) == 266)          /* FillOrder, SHORT */
            return (int)le16(bank + e + 8);
    }
    return 1;
}

/* ------------------------------------------------------------------- the ISO */

long u2_iso_read(const char *img_path, long lba, size_t length,
                 unsigned char *out, size_t outsz)
{
    if (!img_path || !out || length > outsz)
        return -1;
    FILE *f = fopen(img_path, "rb");
    if (!f)
        return -1;
    size_t done = 0;
    while (done < length) {
        size_t chunk = length - done;
        if (chunk > DSIZE)
            chunk = DSIZE;
        const long sector = lba + (long)(done / DSIZE);
        if (fseek(f, sector * SEC + 16, SEEK_SET) != 0) { fclose(f); return -1; }
        if (fread(out + done, 1, chunk, f) != chunk) { fclose(f); return -1; }
        done += chunk;
    }
    fclose(f);
    return (long)done;
}

static int iso_root(const char *img, long *lba, long *size)
{
    unsigned char blk[DSIZE];
    for (long s = 16; s < 32; s++) {
        if (u2_iso_read(img, s, DSIZE, blk, sizeof blk) != DSIZE)
            continue;
        if (blk[0] == 1 && !memcmp(blk + 1, "CD001", 5) && blk[6] == 1) {
            const unsigned char *r = blk + 156;      /* the root directory record */
            *lba = le32(r + 2);
            *size = le32(r + 10);
            return 0;
        }
    }
    return -1;
}

static void strip_version(char *name)
{
    char *semi = strrchr(name, ';');
    if (semi)
        *semi = '\0';
    for (char *s = name; *s; s++)
        *s = (char)toupper((unsigned char)*s);
}

static int walk_dir(const char *img, long lba, long size, dentry *files,
                    int *nf, int depth)
{
    if (size <= 0 || (unsigned long)size > MAX_BLOB || depth > 8)
        return -1;
    unsigned char *data = malloc((size_t)size);
    if (!data)
        return -1;
    if (u2_iso_read(img, lba, (size_t)size, data, (size_t)size) != size) {
        free(data);
        return -1;
    }

    long off = 0;
    while (off < size) {
        const int n = data[off];
        if (n == 0) {
            /* a zero length byte means the rest of this sector is padding; the
             * next record starts at the next sector boundary. Skipping it by
             * `off += n` instead would spin here forever. */
            off = (off / DSIZE + 1) * DSIZE;
            continue;
        }
        if (off + n > size)
            break;
        const int nlen = data[off + 32];
        const int flags = data[off + 25];
        if (nlen >= 1 && off + 33 + nlen <= size) {
            char nm[64];
            const int take = nlen < 63 ? nlen : 63;
            memcpy(nm, data + off + 33, (size_t)take);
            nm[take] = '\0';
            const int self = (nlen == 1 && (nm[0] == 0 || nm[0] == 1));
            if (!self) {
                if (flags & 2) {
                    if (walk_dir(img, le32(data + off + 2), le32(data + off + 10),
                                 files, nf, depth + 1) != 0) {
                        free(data);
                        return -1;
                    }
                } else if (*nf < MAX_FILES) {
                    strip_version(nm);
                    snprintf(files[*nf].name, sizeof files[*nf].name, "%.63s", nm);
                    files[*nf].lba = le32(data + off + 2);
                    files[*nf].size = le32(data + off + 10);
                    (*nf)++;
                }
            }
        }
        off += n;
    }
    free(data);
    return 0;
}

/* ------------------------------------------------------------------- media */

static int looks_like_img(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".img") || !strcasecmp(dot, ".iso"));
}

static int find_img(const char *media, char *out, size_t n, char *err, size_t errsz)
{
    if (u2_is_file(media)) {
        snprintf(out, n, "%.1023s", media);
        return 0;
    }
    if (!u2_is_dir(media)) {
        snprintf(err, errsz, "--fmtowns-media-dir %s is neither a disc image nor a "
                 "directory containing one", media);
        return -1;
    }
    DIR *d = opendir(media);
    if (!d) {
        snprintf(err, errsz, "cannot read %s", media);
        return -1;
    }
    char best[512];
    best[0] = '\0';
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !looks_like_img(e->d_name))
            continue;
        char full[1024];
        snprintf(full, sizeof full, "%.500s/%.255s", media, e->d_name);
        if (!u2_is_file(full))
            continue;
        if (!*best || strcasecmp(e->d_name, strrchr(best, '/') ? strrchr(best, '/') + 1
                                                                : best) < 0)
            snprintf(best, sizeof best, "%.511s", full);
    }
    closedir(d);
    if (!*best) {
        snprintf(err, errsz, "%s holds no .img or .iso disc image", media);
        return -1;
    }
    snprintf(out, n, "%.1023s", best);
    return 0;
}

static const dentry *find_file(const dentry *files, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(files[i].name, name))
            return &files[i];
    return NULL;
}

/* ------------------------------------------------------------------ decoding */

static unsigned char *read_blob(const char *img, const dentry *f)
{
    if (f->size <= 0 || (unsigned long)f->size > MAX_BLOB)
        return NULL;
    unsigned char *buf = malloc((size_t)f->size);
    if (!buf)
        return NULL;
    if (u2_iso_read(img, f->lba, (size_t)f->size, buf, (size_t)f->size) != f->size) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* One tile of a GRAPH bank as 1024 palette indices. The bank stores two pixels per
 * byte. Raw banks are high nibble left with values already within 0..7 (the mask is
 * what the Python did and is kept rather than "cleaned up"); TIFF banks with
 * FillOrder=2 (EXTRA) are low nibble left. See bank_fill_order above. */
static void tile_indices(const unsigned char *bank, long ntiles, long which,
                         unsigned char *px /* TILE_PX */)
{
    (void)ntiles;
    const int fill = bank_fill_order(bank);
    const long base = BANK_HDR + which * TILE_BYTES;
    for (int i = 0; i < TILE_PX; i++) {
        const unsigned char b = bank[base + (i >> 1)];
        if (fill == 2)
            px[i] = (unsigned char)(i % 2 == 0 ? (b & 0xF) : ((b >> 4) & 0xF));
        else
            px[i] = (unsigned char)((i % 2 == 0 ? (b >> 4) : b) & 7);
    }
}

static void pack_tile(const unsigned char *px, unsigned char *out /* TILE_BYTES */)
{
    for (int i = 0; i < TILE_PX; i += 2)
        out[i / 2] = (unsigned char)(((px[i] & 0xF) << 4) | (px[i + 1] & 0xF));
}

/* --------------------------------------------------------------------- build */

static int split_tokens(const char *row, char toks[MAX_TOKENS][32])
{
    int n = 0;
    const char *p = row;
    while (*p && n < MAX_TOKENS) {
        const char *comma = strchr(p, ',');
        const char *end = comma ? comma : p + strlen(p);
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        const char *stop = end;
        while (stop > p && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;
        const size_t len = (size_t)(stop - p);
        if (len > 0 && len < 32) {
            memcpy(toks[n], p, len);
            toks[n][len] = '\0';
            n++;
        }
        if (!comma) break;
        p = comma + 1;
    }
    return n;
}

int u2_fmtowns_build(const char *media, const char *out_dir, FILE *report,
                     char *err, size_t errsz)
{
    if (!media || !*media || !out_dir || !*out_dir) {
        snprintf(err, errsz, "--fmtowns-media-dir needs a disc image or a directory");
        return 1;
    }

    char img[1024];
    if (find_img(media, img, sizeof img, err, errsz) != 0)
        return 1;

    long root_lba = 0, root_size = 0;
    if (iso_root(img, &root_lba, &root_size) != 0) {
        snprintf(err, errsz, "no ISO-9660 primary volume descriptor in %s", img);
        return 1;
    }
    dentry *files = calloc(MAX_FILES, sizeof *files);
    int nf = 0;
    if (!files || walk_dir(img, root_lba, root_size, files, &nf, 0) != 0) {
        snprintf(err, errsz, "cannot read the directory tree of %s", img);
        free(files);
        return 1;
    }
    if (nf == 0) {
        snprintf(err, errsz, "%s has no files; is it the FM Towns disc?", img);
        free(files);
        return 1;
    }

    /* Pool: first-appearance order over the crosswalk rows, exactly as the Python
     * numbers it. Two arrays kept in step — the token, and the packed tile bytes. */
    static char pool[MAX_POOL][32];
    int pool_n = 0;
    int row_idx[U2_FMTOWNS_MAP_COUNT][MAX_TOKENS];
    int row_n[U2_FMTOWNS_MAP_COUNT];
    int multi = 0;

    for (int r = 0; r < U2_FMTOWNS_MAP_COUNT; r++) {
        char toks[MAX_TOKENS][32];
        const int n = split_tokens(u2_fmtowns_map[r], toks);
        row_n[r] = n;
        for (int i = 0; i < n; i++) {
            int found = -1;
            for (int p = 0; p < pool_n; p++)
                if (!strcmp(pool[p], toks[i])) { found = p; break; }
            if (found < 0) {
                if (pool_n >= MAX_POOL) {
                    snprintf(err, errsz, "tile pool overflows %d entries", MAX_POOL);
                    free(files);
                    return 1;
                }
                snprintf(pool[pool_n], sizeof pool[0], "%.31s", toks[i]);
                found = pool_n++;
            }
            row_idx[r][i] = found;
        }
        if (n > 1)
            multi++;
    }

    unsigned char *tiles = calloc((size_t)pool_n, TILE_BYTES);
    if (!tiles) {
        snprintf(err, errsz, "out of memory");
        free(files);
        return 1;
    }

    bank_cache banks[MAX_BANKS];
    int nbanks = 0;
    memset(banks, 0, sizeof banks);

    for (int p = 0; p < pool_n; p++) {
        char bank[16];
        const char *slash = strchr(pool[p], '/');
        if (!slash) {
            snprintf(err, errsz, "crosswalk token '%s' has no BANK/tile form", pool[p]);
            goto fail;
        }
        const size_t blen = (size_t)(slash - pool[p]);
        if (blen == 0 || blen >= sizeof bank) {
            snprintf(err, errsz, "crosswalk token '%s' has a bad bank name", pool[p]);
            goto fail;
        }
        memcpy(bank, pool[p], blen);
        bank[blen] = '\0';
        for (char *s = bank; *s; s++)
            *s = (char)toupper((unsigned char)*s);
        const long which = strtol(slash + 1, NULL, 10);

        bank_cache *bc = NULL;
        for (int b = 0; b < nbanks; b++)
            if (!strcmp(banks[b].bank, bank)) { bc = &banks[b]; break; }
        if (!bc) {
            char key[32];
            snprintf(key, sizeof key, "%.15s.TIF", bank);
            const dentry *f = find_file(files, nf, key);
            if (!f) {
                snprintf(err, errsz, "no GRAPH/%s on the FM Towns disc in %s", key, img);
                goto fail;
            }
            if (nbanks >= MAX_BANKS) {
                snprintf(err, errsz, "more than %d banks referenced", MAX_BANKS);
                goto fail;
            }
            bc = &banks[nbanks];
            bc->blob = read_blob(img, f);
            if (!bc->blob) {
                snprintf(err, errsz, "cannot read GRAPH/%s", key);
                goto fail;
            }
            snprintf(bc->bank, sizeof bc->bank, "%.15s", bank);
            bc->ntiles = (f->size - BANK_HDR) / TILE_BYTES;
            nbanks++;
        }
        if (which < 0 || which >= bc->ntiles) {
            snprintf(err, errsz, "%s: bank %s has %ld tiles", pool[p], bank, bc->ntiles);
            goto fail;
        }
        unsigned char px[TILE_PX];
        tile_indices(bc->blob, bc->ntiles, which, px);
        pack_tile(px, tiles + (size_t)p * TILE_BYTES);
    }

    /* --- the dungeon wall texture, baked from DUNG1 ---
     * Independent of the crosswalk (which is overworld/towns only): the corridor
     * needs one flat-brick tile, so read DUNG1 directly and decode tile
     * DNG_WALL_TILE into the theme's own palette indices. A missing bank is a
     * refusal, not a silent skip — a theme that quietly lacks the wall art shows
     * up as a corridor bug rather than a build bug. */
    unsigned char dngwall[TILE_PX];
    {
        char key[32];
        snprintf(key, sizeof key, "%.15s.TIF", DNG_WALL_BANK);
        const dentry *f = find_file(files, nf, key);
        if (!f) {
            snprintf(err, errsz, "no GRAPH/%s on the FM Towns disc in %s", key, img);
            goto fail;
        }
        unsigned char *blob = read_blob(img, f);
        if (!blob) {
            snprintf(err, errsz, "cannot read GRAPH/%s", key);
            goto fail;
        }
        const long ntiles = (f->size - BANK_HDR) / TILE_BYTES;
        if (DNG_WALL_TILE < 0 || DNG_WALL_TILE >= ntiles) {
            free(blob);
            snprintf(err, errsz, "%s has %ld tiles, no tile %d", key, ntiles, DNG_WALL_TILE);
            goto fail;
        }
        tile_indices(blob, ntiles, DNG_WALL_TILE, dngwall);
        free(blob);
    }

    /* PALETTE.RGB: 16 x 3. Only 0..7 are measured; 8..15 are zero-filled because
     * the art never indexes above 7, and that is stated rather than left to be
     * discovered later as a colour bug. */
    unsigned char pal[48];
    memset(pal, 0, sizeof pal);
    for (int i = 0; i < 8; i++) {
        pal[i * 3 + 0] = (unsigned char)k_pal[i][0];
        pal[i * 3 + 1] = (unsigned char)k_pal[i][1];
        pal[i * 3 + 2] = (unsigned char)k_pal[i][2];
    }

    char ini[1200];
    snprintf(ini, sizeof ini,
             "[theme]\n"
             "name = " U2_FMTOWNS_THEME "\n"
             "tile_w = 32\n"
             "tile_h = 32\n"
             "bpp = 4\n"
             "pixel = nibble-high-left\n"
             "transparent_index = 0\n"
             "palette = PALETTE.RGB\n"
             "dng_wall = DNGWALL.TILE\n"
             "map = MAP.TSV\n"
             "identity = no\n"
             "scope = " U2_FMTOWNS_SCOPE "\n"
             "source = FM Towns (1990) GRAPH banks\n"
             "frames = first\n"
             "tiles_pool = %d\n"
             "art_count = %d\n"
             "art_with_multiple_frames = %d\n"
             "crosswalk = " U2_FMTOWNS_XWALK "\n"
             "# `frames = first` is a slice-B policy, not a claim about the original:\n"
             "# MAP.TSV keeps every frame the crosswalk names, and the engine draws\n"
             "# list[0] until animation exists. The frames are in TILES, so nothing\n"
             "# was thrown away to get here.\n"
             "# `transparent_index = 0` is measured, not assumed: 14 of 14 ENEMY2 tiles\n"
             "# and 6 of 7 EXTRA tiles use index 0, covering 43 and 35 percent of their\n"
             "# pixels, while only 2 of 43 MAP2_1 terrain tiles use it at all and one of\n"
             "# those is a single pixel. Terrain keeps its real black at index 3, which\n"
             "# the palette also carries -- two blacks, one of them spare. That is what\n"
             "# makes 0 the transparent pen rather than a guess.\n",
             pool_n, U2_FMTOWNS_MAP_COUNT, multi);

    char mtsv[4096];
    size_t used = (size_t)snprintf(mtsv, sizeof mtsv,
             "# MAP.TSV -- DOS art index -> tile indices into TILES, generated from\n"
             "# " U2_FMTOWNS_XWALK ". Do not hand-edit.\n"
             "# Column 3 repeats the source BANK/tile tokens for provenance. A row is\n"
             "# set-valued: the engine uses the first index and keeps the rest.\n"
             "# art\ttiles\tsource\n");
    for (int r = 0; r < U2_FMTOWNS_MAP_COUNT; r++) {
        char idxs[256];
        idxs[0] = '\0';
        size_t off = 0;
        for (int i = 0; i < row_n[r]; i++) {
            const int w = snprintf(idxs + off, sizeof idxs - off, "%s%d",
                                   i ? "," : "", row_idx[r][i]);
            if (w < 0 || (size_t)w >= sizeof idxs - off)
                break;
            off += (size_t)w;
        }
        const int w = snprintf(mtsv + used, sizeof mtsv - used, "%02X\t%s\t%s\n",
                               r, idxs, u2_fmtowns_map[r]);
        if (w < 0 || (size_t)w >= sizeof mtsv - used) {
            snprintf(err, errsz, "MAP.TSV overflowed its buffer");
            goto fail;
        }
        used += (size_t)w;
    }

    if (u2_mkdir_p(out_dir) != 0) {
        snprintf(err, errsz, "cannot create %s", out_dir);
        goto fail;
    }

    struct { const char *name; const void *body; size_t len; } parts[] = {
        { "THEME.INI",   ini,  strlen(ini) },
        { "TILES",       tiles, (size_t)pool_n * TILE_BYTES },
        { "PALETTE.RGB", pal,  sizeof pal },
        { "DNGWALL.TILE", dngwall, sizeof dngwall },
        { "MAP.TSV",     mtsv, used },
    };
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
        char path[1200];
        snprintf(path, sizeof path, "%.1100s/%.20s", out_dir, parts[i].name);
        char hex[65];
        sha256_hex(parts[i].body, parts[i].len, hex);
        char sha[17];
        memcpy(sha, hex, 16);
        sha[16] = '\0';
        if (u2_write_verified(path, parts[i].body, parts[i].len, sha) != 0) {
            snprintf(err, errsz, "cannot write %s", path);
            goto fail;
        }
    }

    if (report)
        fprintf(report, "fmtowns: %d tiles in the pool from %d banks, %d art indices, "
                        "%d with multiple frames\n", pool_n, nbanks,
                U2_FMTOWNS_MAP_COUNT, multi);

    for (int b = 0; b < nbanks; b++)
        free(banks[b].blob);
    free(tiles);
    free(files);
    return 0;

fail:
    for (int b = 0; b < nbanks; b++)
        free(banks[b].blob);
    free(tiles);
    free(files);
    return 1;
}
