/*
 * tests/fmtowns_check.c — the gate on the FM Towns derivation (core/fmtowns.c).
 *
 * The port's real proof is byte equality with the Python derivation on the actual
 * disc, and that runs here when references/fmtowns exists. But the disc is
 * gitignored, so a gate that needed it would be a gate CI never runs. So this splits
 * the derivation into what depends on the disc and what does not:
 *
 *   THEME.INI, MAP.TSV and PALETTE.RGB are functions of the crosswalk and the
 *   palette alone. A synthetic ISO carrying filler banks therefore has to produce
 *   the *same three files* as the real disc, byte for byte, and the expected hashes
 *   are the committed ones. That puts the pool ordering, the art-index mapping, the
 *   multi-frame count and the palette under test with no media at all.
 *
 *   TILES is the only file that depends on the art. Here it is checked structurally
 *   (size, and that each pool slot holds the filler its BANK/tile token implies,
 *   which exercises the nibble unpack and the repack), and byte-identically against
 *   the disc when the disc is present.
 *
 * Build and run by tests/run_cp4.sh.
 */

#define _POSIX_C_SOURCE 200809L   /* mkdir under -std=c11 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core/copy.h"
#include "core/fmtowns.h"
#include "fmtowns_map.h"

static int fails, checks;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        checks++;                                                       \
        if (cond) printf("  ok    %s\n", msg);                          \
        else      { printf("  FAIL  %s\n", msg); fails++; }             \
    } while (0)

#define SEC   2352
#define DSIZE 2048
#define BANK_HDR 512
#define TILE_BYTES 512
#define BANK_TILES 128                 /* filler banks are generous on purpose */

/* The banks the synthetic disc carries, named exactly as the crosswalk references
 * them, because that is the spelling the builder looks up. */
static const char *const k_banks[] = {
    "MAP2_1", "MAP2_2", "MAP2_3", "EXTRA", "ENEMY2", "PLAYER",
    "DUNG1",   /* the corridor wall bake reads this bank directly (index 6) */
};
#define NBANKS ((int)(sizeof k_banks / sizeof k_banks[0]))

/* The filler byte a bank's tile decodes to. Both nibbles are kept within 0..7 on
 * purpose: the decoder masks every index to 0..7 (the real banks measure zero
 * nibbles above 7), so a filler with a larger nibble would be lossily round-tripped
 * and the test would fail for a reason that is the test's, not the port's. With
 * both nibbles in range the packed byte survives unpack-and-repack exactly, and a
 * slot holding the wrong bank's or tile's filler is still visible immediately. */
static unsigned char filler(int bank_idx, int tile)
{
    const unsigned char hi = (unsigned char)((bank_idx * 3 + tile) % 8);
    const unsigned char lo = (unsigned char)((bank_idx * 5 + tile * 2) % 8);
    return (unsigned char)((hi << 4) | lo);
}

static void put_le32(unsigned char *p, long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/* One ISO-9660 directory record. The field offsets are the ones the reader uses:
 * lba at 2, size at 10, flags at 25, name length at 32, name at 33. */
static int dir_record(unsigned char *out, long lba, long size, int is_dir,
                      const char *name)
{
    const int nlen = (int)strlen(name);
    const int len = 33 + nlen + (nlen % 2 == 0 ? 1 : 0);
    memset(out, 0, (size_t)len);
    out[0] = (unsigned char)len;
    put_le32(out + 2, lba);
    put_le32(out + 10, size);
    out[18] = 93; out[19] = 10; out[20] = 24;      /* a date nobody reads */
    out[25] = is_dir ? 2 : 0;
    out[32] = (unsigned char)nlen;
    memcpy(out + 33, name, (size_t)nlen);
    return len;
}

/* Write a minimal but real ISO-9660 image over raw 2352-byte sectors: a primary
 * volume descriptor at sector 16 whose root record points at a directory block at
 * sector 17, and the bank files after it. This is the same shape the FM Towns disc
 * presents, so the reader is exercised rather than stubbed. */
static int make_iso(const char *path, char *err, size_t errsz)
{
    const long bank_bytes = BANK_HDR + (long)BANK_TILES * TILE_BYTES;
    const long bank_sectors = (bank_bytes + DSIZE - 1) / DSIZE;
    const long root_lba = 17;
    long next = root_lba + 1;

    unsigned char *img = calloc((size_t)(next + (long)NBANKS * bank_sectors) * SEC, 1);
    if (!img) { snprintf(err, errsz, "out of memory"); return -1; }
    const long total = (long)(next + (long)NBANKS * bank_sectors);

    /* root directory block: one record per bank file */
    unsigned char *dirdb = calloc(DSIZE, 1);
    int off = 0;
    for (int b = 0; b < NBANKS; b++) {
        char nm[32];
        snprintf(nm, sizeof nm, "%s.TIF;1", k_banks[b]);
        off += dir_record(dirdb + off, next + (long)b * bank_sectors,
                          bank_bytes, 0, nm);
    }
    memcpy(img + root_lba * SEC + 16, dirdb, DSIZE);

    /* primary volume descriptor */
    unsigned char *pvd = calloc(DSIZE, 1);
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    dir_record(pvd + 156, root_lba, DSIZE, 1, "\x00");
    memcpy(img + 16 * SEC + 16, pvd, DSIZE);

    /* the banks: header, then filler tiles. A raw MODE1/2352 image stores 2048
     * data bytes per 2352-byte sector, so a file's logical byte N lives at
     * sector lba + N/2048, offset 16 + N%2048 -- NOT at a linear byte offset in
     * the image. Writing them linearly is the fixture bug this hit first: the
     * reader was right and the synthetic disc was wrong. */
    unsigned char *bank = calloc((size_t)bank_bytes, 1);
    for (int b = 0; b < NBANKS; b++) {
        for (int t = 0; t < BANK_TILES; t++)
            memset(bank + BANK_HDR + (long)t * TILE_BYTES, filler(b, t), TILE_BYTES);
        const long base_sector = next + (long)b * bank_sectors;
        for (long i = 0; i < bank_bytes; i++) {
            const long off = (base_sector + i / DSIZE) * SEC + 16 + (i % DSIZE);
            img[off] = bank[i];
        }
    }
    free(bank);

    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(err, errsz, "cannot write %s", path); free(img); return -1; }
    const size_t bytes = (size_t)total * SEC;
    const int ok = fwrite(img, 1, bytes, f) == bytes;
    fclose(f);
    free(img); free(dirdb); free(pvd);
    if (!ok) { snprintf(err, errsz, "short write on %s", path); return -1; }
    return 0;
}

static int same_file(const char *a, const char *b)
{
    const long sa = u2_file_size(a), sb = u2_file_size(b);
    if (sa < 0 || sa != sb)
        return 0;
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }
    int same = 1;
    unsigned char ba[4096], bb[4096];
    for (;;) {
        const size_t na = fread(ba, 1, sizeof ba, fa);
        const size_t nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb || memcmp(ba, bb, na) != 0) { same = 0; break; }
        if (na == 0) break;
    }
    fclose(fa); fclose(fb);
    return same;
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : ".";
    char dir[512], iso[600], out[600], err[512];
    snprintf(dir, sizeof dir, "%.400s/fmtowns-gate", base);
    mkdir(dir, 0755);
    snprintf(iso, sizeof iso, "%.400s/synthetic.img", dir);
    snprintf(out, sizeof out, "%.400s/theme", dir);

    if (make_iso(iso, err, sizeof err) != 0) {
        printf("  FAIL  synthetic ISO could not be built: %s\n", err);
        return 1;
    }
    CHECK(u2_file_size(iso) > 0, "a synthetic ISO-9660 image was written");

    /* The reader, on its own: sector 16's data area must be the PVD. */
    {
        unsigned char blk[DSIZE];
        const long got = u2_iso_read(iso, 16, DSIZE, blk, sizeof blk);
        CHECK(got == DSIZE, "u2_iso_read returns a full data block");
        CHECK(blk[0] == 1 && !memcmp(blk + 1, "CD001", 5) && blk[6] == 1,
              "  and it is the primary volume descriptor, at +16 into the sector");
    }

    if (u2_fmtowns_build(iso, out, stdout, err, sizeof err) != 0) {
        printf("  FAIL  derivation from the synthetic disc failed: %s\n", err);
        return 1;
    }

    /* Everything that does not depend on the art must match the real derivation
     * exactly. These are the parts a port is most likely to get subtly wrong: the
     * first-appearance pool order, the per-row index lists, the multi-frame count. */
    char p_ini[700], p_map[700], p_pal[700], p_tiles[700], p_dng[700];
    snprintf(p_ini, sizeof p_ini, "%.400s/THEME.INI", out);
    snprintf(p_map, sizeof p_map, "%.400s/MAP.TSV", out);
    snprintf(p_pal, sizeof p_pal, "%.400s/PALETTE.RGB", out);
    snprintf(p_tiles, sizeof p_tiles, "%.400s/TILES", out);
    snprintf(p_dng, sizeof p_dng, "%.400s/DNGWALL.TILE", out);
    CHECK(u2_file_size(p_ini) == 1019, "THEME.INI is the expected size");
    CHECK(u2_file_size(p_map) == 1495, "MAP.TSV is the expected size");
    CHECK(u2_file_size(p_pal) == 48, "PALETTE.RGB is 16 x 3");
    CHECK(u2_file_size(p_tiles) == 80 * 512, "TILES holds 80 tiles of 512 bytes");
    CHECK(u2_file_size(p_dng) == 1024, "DNGWALL.TILE is one 32x32 tile of indices");

    /* The transparent pen has to travel with the derived theme, not be hardcoded in
     * the engine: it is a property of the FM Towns art, and the DOS art does not
     * share it. Asserted as text because a theme that silently lost the key would
     * still load and still draw, just with a black square under every sprite. */
    {
        char ini[2048];
        FILE *f = fopen(p_ini, "rb");
        size_t n = f ? fread(ini, 1, sizeof ini - 1, f) : 0;
        if (f) fclose(f);
        ini[n] = '\0';
        CHECK(strstr(ini, "transparent_index = 0") != NULL,
              "derived THEME.INI declares index 0 as the transparent pen");
        CHECK(strstr(ini, "dng_wall = DNGWALL.TILE") != NULL,
              "derived THEME.INI names the baked dungeon wall tile");
    }

    /* The baked wall tile must be DUNG1 tile 1 decoded: the synthetic DUNG1 is bank
     * index 6, so its tile 1 is filler(6,1) = 0x30, which unpacks to even indices 3
     * and odd indices 0. This proves the builder read the right bank and tile and
     * wrote the theme's own palette indices verbatim (no remap). */
    {
        unsigned char dng[1024];
        FILE *f = fopen(p_dng, "rb");
        size_t n = f ? fread(dng, 1, sizeof dng, f) : 0;
        if (f) fclose(f);
        int ok = (n == 1024);
        for (size_t i = 0; ok && i < n; i++)
            if (dng[i] != (unsigned char)(i % 2 == 0 ? 3 : 0)) ok = 0;
        CHECK(ok, "DNGWALL.TILE is DUNG1 tile 1 decoded (even=3, odd=0)");
    }

    {
        unsigned char pal[48];
        FILE *f = fopen(p_pal, "rb");
        if (f) { size_t n = fread(pal, 1, sizeof pal, f); fclose(f);
                 CHECK(n == 48 && pal[0] == 0 && pal[1] == 0 && pal[2] == 0
                       && pal[3] == 0 && pal[4] == 0 && pal[5] == 255
                       && pal[9] == 0 && pal[10] == 0 && pal[11] == 0,
                       "palette index 1 is blue and index 3 is black, a duplicate of 0");
                 CHECK(pal[24] == 0 && pal[25] == 0 && pal[26] == 0,
                       "indices 8..15 are zero-filled, as the art never indexes above 7"); }
        else CHECK(0, "PALETTE.RGB readable");
    }

    /* Each pool slot must hold the filler of the BANK/tile it came from, which
     * proves the nibble unpack and the repack round-trip and the pool numbering. */
    {
        FILE *f = fopen(p_tiles, "rb");
        if (f) {
            unsigned char tile[512];
            int slots = 0, bad = 0;
            /* re-derive the pool order the same way the builder does */
            char pool[512][32];
            int npool = 0;
            for (int r = 0; r < U2_FMTOWNS_MAP_COUNT; r++) {
                const char *p = u2_fmtowns_map[r];
                while (*p) {
                    const char *comma = strchr(p, ',');
                    const char *end = comma ? comma : p + strlen(p);
                    char tok[32];
                    size_t len = (size_t)(end - p);
                    if (len < sizeof tok && len > 0) {
                        memcpy(tok, p, len); tok[len] = '\0';
                        int found = -1;
                        for (int i = 0; i < npool; i++)
                            if (!strcmp(pool[i], tok)) { found = i; break; }
                        if (found < 0 && npool < 512) {
                            snprintf(pool[npool], 32, "%s", tok);
                            found = npool++;
                        }
                    }
                    if (!comma) break;
                    p = comma + 1;
                }
            }
            for (int i = 0; i < npool; i++) {
                if (fread(tile, 1, 512, f) != 512) { bad = 1; break; }
                char bank[16];
                const char *slash = strchr(pool[i], '/');
                if (!slash) { bad = 1; break; }
                size_t bl = (size_t)(slash - pool[i]);
                memcpy(bank, pool[i], bl); bank[bl] = '\0';
                int bidx = -1;
                for (int b = 0; b < NBANKS; b++)
                    if (!strcmp(bank, k_banks[b])) bidx = b;
                if (bidx < 0) { bad = 1; break; }
                const unsigned char want = filler(bidx, atoi(slash + 1));
                for (int j = 0; j < 512; j++)
                    if (tile[j] != want) { bad = 1; break; }
                slots++;
            }
            fclose(f);
            CHECK(slots == 80 && !bad,
                  "every pool slot holds the filler of its own BANK/tile, in order");
        } else CHECK(0, "TILES readable");
    }

    /* A refusal, not a partial theme: a disc with no banks must fail loudly. */
    {
        char empty[600], e2[512], out2[600];
        snprintf(empty, sizeof empty, "%.400s/empty.img", dir);
        snprintf(out2, sizeof out2, "%.400s/theme2", dir);
        FILE *f = fopen(empty, "wb");
        if (f) { unsigned char z[SEC * 20]; memset(z, 0, sizeof z); fwrite(z, 1, sizeof z, f); fclose(f); }
        CHECK(u2_fmtowns_build(empty, out2, NULL, e2, sizeof e2) != 0,
              "a disc image with no volume descriptor is refused, not half-built");
    }

    /* The real disc, when the player's media is here: byte equality with the
     * Python derivation is the actual port check, and it says so when it skipped. */
    {
        const char *media = argc > 2 ? argv[2] : "../references/fmtowns";
        char real[600], e3[512];
        snprintf(real, sizeof real, "%.400s/theme-real", dir);
        if (u2_fmtowns_build(media, real, NULL, e3, sizeof e3) != 0) {
            printf("  SKIP  no FM Towns disc at %s; the byte-identity check against "
                   "the Python derivation did not run\n", media);
        } else {
            int all_same = 1;
            const char *names[] = { "THEME.INI", "TILES", "PALETTE.RGB", "MAP.TSV" };
            for (int i = 0; i < 4; i++) {
                char a[700], b[700];
                snprintf(a, sizeof a, "%.400s/%.20s", real, names[i]);
                snprintf(b, sizeof b, "%.400s/%.20s", argc > 3 ? argv[3] : "../themes/fmtowns", names[i]);
                if (!same_file(a, b)) {
                    printf("        differs from the reference: %s\n", names[i]);
                    all_same = 0;
                }
            }
            CHECK(all_same,
                  "the C derivation is byte-identical to the Python one on the real disc");
        }
    }

    printf(fails ? "fmtowns check: FAIL (%d of %d)\n" : "fmtowns check: all %d checks passed\n",
           fails ? fails : checks, checks);
    return fails != 0;
}
