/*
 * tiles.c — extract the 64 tile bitmaps from the player's executable, and keep
 * them in a self-verifying cache.
 *
 * Zero game content is committed anywhere in this repository, so the art has to
 * arrive at run time from a file the player owns. That makes the extractor a
 * correctness-critical component rather than a convenience: if it silently
 * mis-locates the table, the engine renders nonsense and the bug looks like a
 * renderer defect. Every invariant proven in the asm log is re-asserted here and
 * a mismatch is a hard failure.
 *
 * Cache layout (little-endian, our own format — not the game's):
 *
 *   0   "U2TL"        magic
 *   4   u8  version   1
 *   5   u8  count     64
 *   6   u8  width     16
 *   7   u8  height    16
 *   8   u32 tile_bytes 64
 *   12  u32 reserved  0
 *   16  u8[32]        SHA-256 of the source executable
 *   48  u8[64][2]     the per-tile headers, verbatim
 *   176 u8[64][64]    pixel data, 2bpp, leftmost pixel in the top two bits
 *   total 4272 bytes
 *
 * The shape follows the hint the fan upgrade's CGATILES gave (64-byte header
 * table + fixed-stride pixel data), which separates the header from the pattern.
 * It is our format, chosen for verification and mmap-ability, not a claim about
 * what the upgrade's file actually contains — that file is untrusted by policy.
 */

#define _POSIX_C_SOURCE 200809L   /* access(), rename() under -std=c11 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/tiles.h"
#include "core/compat.h"
#include "core/roots.h"
#include "core/sha256.h"

#define CACHE_MAGIC   "U2TL"
#define CACHE_VERSION 1
#define CACHE_HDR_LEN 176
#define CACHE_TOTAL   (CACHE_HDR_LEN + U2_TILE_COUNT * U2_TILE_BYTES)

/* The pack ships lowercase; the 1989 trilogy and later releases differ in case
 * only, so try the spellings rather than assume one. */
static const char *const k_exe_names[] = {
    "ultimaii.exe", "ULTIMAII.EXE", "Ultimaii.exe"
};

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static uint8_t *read_whole(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    *len_out = (size_t)sz;
    return buf;
}

/*
 * The pure core: no I/O, so a test can feed it a crafted buffer and prove the
 * assertions fire. Returns non-zero and leaves *t untouched on any mismatch.
 */
int u2_tiles_extract_from_memory(const uint8_t *mem, size_t len, u2_tiles *t)
{
    if (!mem || !t) return -1;

    /* The table and all 64 records have to be inside the file. */
    size_t need = U2_TILE_FIRST_PIX_OFF +
                  (size_t)(U2_TILE_COUNT - 1) * U2_TILE_FILE_STRIDE + U2_TILE_BYTES;
    if (len < need) return -1;

    /* 1. the index table: 64 words, uniform stride of exactly 66. Checked in the same pass that
     *    fills the array, because reading idx[i-1] in a second loop is what gcc's -Wanalyzer
     *    reported as "use of uninitialized value idx[i]" — a false positive, since the first loop
     *    writes every element, but a single pass is both true and simpler, and a suppression
     *    comment would outlive the reason for it. */
    uint16_t idx[U2_TILE_COUNT];
    for (int i = 0; i < U2_TILE_COUNT; i++) {
        const uint8_t *p = mem + U2_TILE_INDEX_FILE_OFF + (size_t)i * 2;
        idx[i] = (uint16_t)(p[0] | (p[1] << 8));
        if (i > 0 && (int)idx[i] - (int)idx[i - 1] != U2_TILE_FILE_STRIDE) return -1;
    }

    /* 2. the index values are data-image offsets; recover the base and require
     *    that the first record's pixels land exactly where the asm says. */
    long base = (long)U2_TILE_FIRST_PIX_OFF - 2 - (long)idx[0];
    if (base < 0) return -1;

    /* 3. every tile carries the 2-byte header asm/4A74 steps over. */
    uint8_t hdr[U2_TILE_COUNT][2];
    uint8_t px[U2_TILE_COUNT][U2_TILE_BYTES];
    for (int i = 0; i < U2_TILE_COUNT; i++) {
        size_t off = (size_t)base + idx[i];
        if (off + 2 + U2_TILE_BYTES > len) return -1;
        if (mem[off] != U2_TILE_MAGIC_BYTE || mem[off + 1] != U2_TILE_MAGIC_BYTE2)
            return -1;
        hdr[i][0] = mem[off];
        hdr[i][1] = mem[off + 1];
        memcpy(px[i], mem + off + 2, U2_TILE_BYTES);
    }

    memcpy(t->hdr, hdr, sizeof hdr);
    memcpy(t->px, px, sizeof px);
    return 0;
}

u2_tiles_result u2_tiles_extract(const char *exe_path, u2_tiles *t)
{
    if (!exe_path || !t) return U2_TILES_ERROR;

    size_t len = 0;
    uint8_t *mem = read_whole(exe_path, &len);
    if (!mem) return U2_TILES_NOT_READY;

    /* The digest is of the whole file: swapping the executable is exactly the
     * event we want the cache to notice. */
    sha256_ctx sc;
    sha256_init(&sc);
    sha256_update(&sc, mem, len);

    u2_tiles scratch;
    if (u2_tiles_extract_from_memory(mem, len, &scratch) != 0) {
        free(mem);
        return U2_TILES_BAD_EXE;
    }
    sha256_final(&sc, scratch.digest);
    free(mem);

    scratch.from_cache = 0;
    scratch.unverified = 0;
    snprintf(scratch.source, sizeof scratch.source, "%s", exe_path);
    *t = scratch;
    return U2_TILES_OK;
}

u2_tiles_result u2_tiles_cache_load(const char *path, u2_tiles *t)
{
    if (!path || !t) return U2_TILES_ERROR;

    size_t len = 0;
    uint8_t *mem = read_whole(path, &len);
    if (!mem) return U2_TILES_NOT_READY;
    if (len != CACHE_TOTAL || memcmp(mem, CACHE_MAGIC, 4) != 0 ||
        mem[4] != CACHE_VERSION ||
        mem[5] != U2_TILE_COUNT || mem[6] != U2_TILE_W || mem[7] != U2_TILE_H ||
        le32(mem + 8) != U2_TILE_BYTES) {
        free(mem);
        return U2_TILES_BAD_CACHE;
    }

    u2_tiles scratch;
    memcpy(scratch.digest, mem + 16, 32);
    memcpy(scratch.hdr, mem + 48, sizeof scratch.hdr);
    memcpy(scratch.px, mem + CACHE_HDR_LEN, sizeof scratch.px);
    free(mem);

    scratch.from_cache = 1;
    scratch.unverified = 0;
    scratch.source[0] = '\0';
    *t = scratch;
    return U2_TILES_OK;
}

u2_tiles_result u2_tiles_cache_store(const char *path, const u2_tiles *t)
{
    if (!path || !t) return U2_TILES_ERROR;

    uint8_t buf[CACHE_TOTAL];
    memcpy(buf, CACHE_MAGIC, 4);
    buf[4] = CACHE_VERSION;
    buf[5] = U2_TILE_COUNT;
    buf[6] = U2_TILE_W;
    buf[7] = U2_TILE_H;
    put_le32(buf + 8, U2_TILE_BYTES);
    put_le32(buf + 12, 0);
    memcpy(buf + 16, t->digest, 32);
    memcpy(buf + 48, t->hdr, sizeof t->hdr);
    memcpy(buf + CACHE_HDR_LEN, t->px, sizeof t->px);

    /* Atomic, same directory, temp + rename — the same reasoning as the sector
     * write-back: a half-written tile cache is worse than none, because it
     * passes the digest check only if the source happens to match. */
    char tmp[600];
    if ((size_t)snprintf(tmp, sizeof tmp, "%s.tmp.%ld", path, (long)getpid())
            >= sizeof tmp)
        return U2_TILES_ERROR;

    FILE *f = fopen(tmp, "wb");
    if (!f) return U2_TILES_ERROR;
    if (fwrite(buf, 1, sizeof buf, f) != sizeof buf ||
        fflush(f) != 0 || fsync(fileno(f)) != 0) {
        fclose(f);
        unlink(tmp);
        return U2_TILES_ERROR;
    }
    if (fclose(f) != 0) { unlink(tmp); return U2_TILES_ERROR; }
    if (rename(tmp, path) != 0) { unlink(tmp); return U2_TILES_ERROR; }
    return U2_TILES_OK;
}

u2_tiles_result u2_tiles_find_exe(char *buf, size_t bufsz, const u2_ctx *ctx)
{
    /* Searched across the content roots, not just the game directory: under the
     * explicit-layer layout the executable sits in the release layer, and a
     * loader that only looks beside the binary reports "no executable" for a
     * tree that has one. Release layers only -- the exe has no Galaxy spelling.
     * core/roots.h says why this is one list rather than a second copy. */
    if (u2_root_find_any(ctx, k_exe_names,
                         sizeof k_exe_names / sizeof k_exe_names[0],
                         1, buf, bufsz))
        return U2_TILES_OK;
    buf[0] = '\0';
    return U2_TILES_NOT_READY;
}

/* Create the cache's parent directory if absent.
 *
 * This is deliberately the opposite of the content directory rule. --data-dir is
 * the player's and is never created, because inventing an empty world directory
 * would hide a wrong path behind a silent empty map. The cache directory is ours,
 * and a fresh install has no "our own spot" yet -- without this the cache silently
 * never gets written and every start re-extracts, which is the exact behaviour the
 * owner asked for and would otherwise never happen. It now creates <content
 * root>/cache, one level down, so a plain mkdir is the whole job.
 * Failure here is not fatal: extraction still works in memory.
 */
static void ensure_cache_dir(const char *cache_path)
{
    char dir[600];
    const char *slash = strrchr(cache_path, '/');
    if (!slash || slash == cache_path) return;      /* relative to cwd, or root */
    if ((size_t)(slash - cache_path) >= sizeof dir) return;
    memcpy(dir, cache_path, (size_t)(slash - cache_path));
    dir[slash - cache_path] = '\0';
    if (access(dir, W_OK) == 0) return;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return;
}

u2_tiles_result u2_tiles_open(u2_ctx *ctx, u2_tiles *t,
                              char *exe_out, size_t exe_sz,
                              char *cache_out, size_t cache_sz)
{
    char exe_buf[512], cache_buf[512];
    const char *exe = NULL, *cache = NULL;

    if (exe_out && *exe_out) exe = exe_out;
    else if (u2_tiles_find_exe(exe_buf, sizeof exe_buf, ctx) == U2_TILES_OK)
        exe = exe_buf;

    /* The cache belongs to the content, not to a save (D4). It is derived from the
     * player's executable, it verifies itself against that executable's digest on
     * every load, and it is identical for every slot — so it lives beside the
     * content it came from, at <content root>/cache/tiles.u2tl. It used to sit in
     * the save directory for want of anywhere else, which is exactly what
     * ensure_cache_dir()'s comment admitted; now that the save directory means
     * "one player's game", leaving it there would re-extract the whole atlas once
     * per save for no benefit and file derived art under a player's saves.
     *
     * An explicit cache path from the caller still wins, unchanged. */
    if (cache_out && *cache_out) cache = cache_out;
    else if (snprintf(cache_buf, sizeof cache_buf, "%.400s/cache/tiles.u2tl",
                      u2_content_root(ctx)) >= (int)sizeof cache_buf)
        return U2_TILES_ERROR;
    else cache = cache_buf;

    if (exe_out && exe) snprintf(exe_out, exe_sz, "%s", exe);
    if (cache_out && cache) snprintf(cache_out, cache_sz, "%s", cache);

    if (exe) {
        /* Read once, hash, and only consult the cache if the digest matches.
         * A changed executable invalidates the cache by construction. */
        size_t len = 0;
        uint8_t *mem = read_whole(exe, &len);
        if (!mem) return U2_TILES_NOT_READY;

        sha256_ctx sc;
        sha256_init(&sc);
        sha256_update(&sc, mem, len);
        uint8_t digest[32];
        sha256_final(&sc, digest);

        u2_tiles cached;
        if (u2_tiles_cache_load(cache, &cached) == U2_TILES_OK &&
            memcmp(cached.digest, digest, 32) == 0) {
            free(mem);
            snprintf(cached.source, sizeof cached.source, "%s", cache);
            *t = cached;
            return U2_TILES_OK;
        }

        u2_tiles fresh;
        if (u2_tiles_extract_from_memory(mem, len, &fresh) != 0) {
            free(mem);
            return U2_TILES_BAD_EXE;
        }
        free(mem);
        memcpy(fresh.digest, digest, 32);
        fresh.from_cache = 0;
        fresh.unverified = 0;
        snprintf(fresh.source, sizeof fresh.source, "%s", exe);
        *t = fresh;

        /* Failing to cache is not fatal: the tiles are already in memory. The
         * next start-up pays extraction again, which is the pre-cache behaviour. */
        ensure_cache_dir(cache);
        (void) u2_tiles_cache_store(cache, t);
        return U2_TILES_OK;
    }

    /* No executable: a cache we cannot verify is still better than no art, but
     * the caller must be able to tell the difference. */
    u2_tiles cached;
    if (u2_tiles_cache_load(cache, &cached) == U2_TILES_OK) {
        cached.unverified = 1;
        snprintf(cached.source, sizeof cached.source, "%s", cache);
        *t = cached;
        return U2_TILES_OK;
    }
    return U2_TILES_NOT_READY;
}

const char *u2_tiles_result_text(u2_tiles_result r)
{
    switch (r) {
    case U2_TILES_OK:         return "ok";
    case U2_TILES_NOT_READY:  return "no executable and no cached tiles: point --data-dir at your Ultima II directory";
    case U2_TILES_BAD_EXE:    return "executable does not match the expected tile layout: not a supported Ultima II binary";
    case U2_TILES_BAD_CACHE:  return "tile cache is corrupt or from another executable";
    case U2_TILES_ERROR:      return "tile I/O error";
    }
    return "unknown";
}
