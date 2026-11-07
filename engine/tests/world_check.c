/*
 * tests/world_check.c — the guarantees of core/world.c.
 *
 * Every byte written here is engine-authored test filler, not game content: the
 * point of CP4 is that the map is runtime data the player supplies, so the test
 * proves the loader's structure (sizes, indexing, naming, write-back) without
 * committing a single tile byte.
 *
 * Build and run by tests/run_cp4.sh.
 */

#define _POSIX_C_SOURCE 200809L   /* mkdtemp, chmod under -std=c11 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/ctx.h"
#include "core/slot.h"
#include "core/world.h"

static int fails;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) printf("  ok    %s\n", msg);                        \
        else      { printf("  FAIL  %s\n", msg); fails++; }           \
    } while (0)

static long filesize(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

/* A deterministic, obviously-synthetic sector: tile = (x*3 + y) & 0xFF. */
static void fill_sector(uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(((i % U2_SECTOR_EDGE) * 3 + (i / U2_SECTOR_EDGE)) & 0xFF);
}

static void path_for(char *out, size_t n, const char *dir, const char *name)
{
    snprintf(out, n, "%s/%s", dir, name);
}

static int write_file(const char *p, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    if (fwrite(buf, 1, n, f) != n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : ".";
    char dir[512], missing[512], p[512], saves[512];
    uint8_t buf[U2_SECTOR_TILES + 256];
    uint8_t readback[U2_SECTOR_TILES + 256];
    u2_ctx ctx;
    u2_world w;
    u2_sector_id id;
    FILE *f;

    snprintf(dir, sizeof dir, "%s/content", base);
    snprintf(missing, sizeof missing, "%s/nope", base);
    /* The save slot lives here, and it is named rather than left to the "saves"
     * default on purpose: the default is relative to the cwd, and a test that
     * writes into whatever directory it happens to be run from is a test that
     * litters the engine tree. */
    snprintf(saves, sizeof saves, "%s/saves_check", base);
    mkdir(dir, 0755);

    memset(&ctx, 0, sizeof ctx);
    ctx.data_dir = dir;
    ctx.save_dir = saves;
    memset(&w, 0, sizeof w);
    id.hi = 3; id.lo = 0;          /* the sector the live capture was standing in */

    /* --- the content directory is never invented ---------------------- */
    ctx.data_dir = missing;
    CHECK(u2_world_open(&ctx) == U2_WORLD_NOT_READY,
          "a missing --data-dir is reported, not created");
    CHECK(filesize(missing) == -1,
          "opening a missing content dir leaves no directory behind");
    ctx.data_dir = dir;
    CHECK(u2_world_open(&ctx) == U2_WORLD_OK, "a real content dir opens");

    /* --- naming: MAPX + two digits, the self-patched FCB name --------- */
    u2_world_path(p, sizeof p, &ctx, "mapx", id);
    CHECK(strstr(p, "/mapx30") != NULL, "sector name is <dir>/mapx<hi><lo>");

    /* --- the read is 4096 bytes, whatever the file size -------------- */
    fill_sector(buf, U2_SECTOR_TILES + 128);
    path_for(p, sizeof p, dir, "mapx30");
    CHECK(write_file(p, buf, U2_SECTOR_TILES + 128) == 0, "test sector written");
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK, "4224-byte sector loads");
    CHECK(w.bytes_read == U2_SECTOR_TILES, "exactly 4096 bytes are read (CX at asm/2290)");
    CHECK(filesize(p) == U2_SECTOR_TILES + 128, "loading does not touch the file");

    /* --- tile addressing: y*64 + x, one byte per tile (asm/50B3) ----- */
    CHECK(u2_world_tile_at(&w, 0, 0) == buf[0], "tile (0,0) is byte 0");
    CHECK(u2_world_tile_at(&w, 5, 0) == buf[5], "tile (5,0) is byte 5");
    CHECK(u2_world_tile_at(&w, 0, 1) == buf[U2_SECTOR_EDGE],
          "tile (0,1) is byte 64, so the row stride is 64");
    CHECK(u2_world_tile_at(&w, 63, 63) == buf[U2_SECTOR_TILES - 1],
          "tile (63,63) is the last byte of the sector");
    CHECK(u2_world_tile_index(64, 0) == 0 && u2_world_tile_index(-1, 0) == 63,
          "coordinates fold into the sector instead of running off the end");

    /* --- a sector with no 128-byte tail (the Galaxy content) --------- */
    path_for(p, sizeof p, dir, "mapx15");
    CHECK(write_file(p, buf, U2_SECTOR_TILES) == 0, "tail-less sector written");
    id.hi = 1; id.lo = 5;
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK,
          "a 4096-byte sector loads — size is not asserted as 4224");
    id.hi = 3; id.lo = 0;

    /* --- missing and short files are named, not guessed at ----------- */
    id.hi = 9; id.lo = 9;
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_MISSING,
          "an absent sector reports MISSING");
    id.hi = 4; id.lo = 4;
    path_for(p, sizeof p, dir, "mapx44");
    CHECK(write_file(p, buf, 100) == 0, "truncated sector written");
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_SHORT,
          "a sector shorter than the read reports SHORT, not a partial success");

    /* --- the Galaxy rename: mapg / MAPG spellings are found ---------- */
    id.hi = 5; id.lo = 0;
    path_for(p, sizeof p, dir, "mapg50");
    CHECK(write_file(p, buf, U2_SECTOR_TILES) == 0, "mapg-spelled sector written");
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK,
          "mapg spelling found (fan pack renames mapx -> mapg)");
    path_for(p, sizeof p, dir, "MAPG60");
    CHECK(write_file(p, buf, U2_SECTOR_TILES) == 0, "MAPG-spelled sector written");
    id.hi = 6; id.lo = 0;
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK,
          "uppercase MAPG spelling found too");
    id.hi = 3; id.lo = 0;
    path_for(p, sizeof p, dir, "mapx30");

    /* --- the Galaxy patch: off Earth, prefer the patched mapg map over the unpatched
     *     mapx clone -- in ./updated first, then the content root -- and fall back to the
     *     unpatched bytes when no patched map exists. Sector 40 is BOTH Earth-era-4 and
     *     Jupiter, so the world byte is what disambiguates. docs/planet-maps-galaxy-patch.md */
    {
        u2_sector_id jup = { .hi = 4, .lo = 0 };
        static uint8_t earth[U2_SECTOR_TILES], planet[U2_SECTOR_TILES];
        char q[544];
        for (size_t i = 0; i < U2_SECTOR_TILES; i++) { earth[i] = 0x11; planet[i] = 0x22; }
        char udir[544]; snprintf(udir, sizeof udir, "%.400s/updated", dir);
        mkdir(udir, 0755);
        path_for(q, sizeof q, dir, "mapx40");
        CHECK(write_file(q, earth, U2_SECTOR_TILES) == 0, "unpatched Earth-clone mapx40 written");
        snprintf(q, sizeof q, "%.400s/updated/MAPG40", dir);
        CHECK(write_file(q, planet, U2_SECTOR_TILES) == 0, "patched updated/MAPG40 written");

        ctx.state.world = 0;
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK && w.map[0] == 0x11,
              "on Earth, sector 40 is the era map (mapx), not the planet map");
        ctx.state.world = 4;
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK && w.map[0] == 0x22,
              "on Jupiter, sector 40 prefers the patched updated/MAPG40");
        snprintf(q, sizeof q, "%.400s/updated/MAPG40", dir);
        unlink(q);
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK && w.map[0] == 0x11,
              "with no patched map, a planet falls back to the unpatched mapx bytes");
        /* Monster layouts follow the same rule: monx/mong mirror mapx/mapg. */
        path_for(q, sizeof q, dir, "monx40");
        CHECK(write_file(q, earth, U2_MON_TILES) == 0, "unpatched monx40 written");
        snprintf(q, sizeof q, "%.400s/updated/MONG40", dir);
        CHECK(write_file(q, planet, U2_MON_TILES) == 0, "patched updated/MONG40 written");
        ctx.state.world = 4;
        CHECK(u2_world_load(&ctx, &w, 0, jup) == U2_WORLD_OK && w.mon[0] == 0x22,
              "on Jupiter, monsters prefer the patched updated/MONG40");
        /* Dialogue follows the same rule: tlkx/tlkg mirror mapx/mapg. */
        path_for(q, sizeof q, dir, "tlkx40");
        CHECK(write_file(q, earth, U2_TLKX_BYTES) == 0, "unpatched tlkx40 written");
        snprintf(q, sizeof q, "%.400s/updated/TLKG40", dir);
        CHECK(write_file(q, planet, U2_TLKX_BYTES) == 0, "patched updated/TLKG40 written");
        CHECK(u2_world_load_tlkx(&ctx, &w, jup) == U2_WORLD_OK && w.tlkx[0] == 0x22,
              "on Jupiter, dialogue prefers the patched updated/TLKG40");
        ctx.state.world = 0;
    }

    /* --- layout B: the same two roles spelled as data/original + data/updated.
     *     These must be served by the one root list rather than a second code path,
     *     so the assertions are about precedence and provenance, not about a
     *     separate mode: the release layer outranks the game root, the patch layer
     *     outranks it off Earth and is not consulted on Earth, and a sector read
     *     from a layer writes back into that layer. That last one is the shadowing
     *     defect the older layout already had -- a sector read from ./updated used
     *     to store at the root, where the copy it came from still outranked it, so
     *     the player's world change was invisible forever. docs/install-design.md
     *     § 3 (D6: write-back stays in the content dir, re-addressed with saves). */
    {
        u2_sector_id jup = { .hi = 5, .lo = 1 };
        static uint8_t layer[U2_SECTOR_TILES], patched[U2_SECTOR_TILES], rootward[U2_SECTOR_TILES];
        char q[600], d[600];
        for (size_t i = 0; i < U2_SECTOR_TILES; i++) {
            layer[i] = 0x33; patched[i] = 0x44; rootward[i] = 0x55;
        }
        snprintf(d, sizeof d, "%.400s/data", dir);           mkdir(d, 0755);
        snprintf(d, sizeof d, "%.400s/data/original", dir);  mkdir(d, 0755);
        snprintf(d, sizeof d, "%.400s/data/updated", dir);   mkdir(d, 0755);

        snprintf(q, sizeof q, "%.400s/mapx51", dir);
        CHECK(write_file(q, rootward, U2_SECTOR_TILES) == 0,
              "layout B: a stale mapx51 sits at the game root");
        snprintf(q, sizeof q, "%.400s/data/original/mapx51", dir);
        CHECK(write_file(q, layer, U2_SECTOR_TILES) == 0,
              "layout B: data/original/mapx51 written");

        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK && w.map[0] == 0x33,
              "layout B: the release layer outranks the game root on Earth");
        CHECK(w.map_root && strcmp(w.map_root, "data/original") == 0,
              "layout B: the layer the map came from is recorded");

        snprintf(q, sizeof q, "%.400s/data/updated/MAPG51", dir);
        CHECK(write_file(q, patched, U2_SECTOR_TILES) == 0,
              "layout B: data/updated/MAPG51 written");
        ctx.state.world = 4;
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK && w.map[0] == 0x44,
              "layout B: off Earth the patch layer wins");
        CHECK(w.map_root && strcmp(w.map_root, "data/updated") == 0,
              "layout B: a patched sector records the patch layer");
        memset(&w, 0, sizeof w);
        ctx.state.world = 0;
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK && w.map[0] == 0x33,
              "layout B: on Earth the patched spelling is not tried at all");

        ctx.state.world = 4;
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK,
              "layout B: the patched sector reloads for write-back");
        u2_world_tile_put(&w, 3, 4, 0x77);
        CHECK(u2_world_store(&ctx, &w, 1) == U2_WORLD_OK,
              "layout B: the patched sector stores");
        /* The old rule was "write back into the layer it was read from", and it
         * existed to stop a write being shadowed forever by the patch it came
         * from. The slot serves that purpose better — it outranks every layer —
         * and it also stops the write editing the player's release data, which is
         * the thing docs/save-scoped-worlds.md was written to stop. */
        snprintf(q, sizeof q, "%.400s/data/updated/MAPG51", dir);
        f = fopen(q, "rb");
        int layer_untouched = 0;
        if (f) {
            size_t n = fread(readback, 1, sizeof readback, f);
            fclose(f);
            layer_untouched = n >= U2_SECTOR_TILES && readback[0] == 0x44
                           && readback[4 * U2_SECTOR_EDGE + 3] != 0x77;
        }
        CHECK(layer_untouched,
              "S2: the patch layer kept its installed bytes; the write went to the slot");
        snprintf(q, sizeof q, "%.400s/mapx51", dir);
        f = fopen(q, "rb");
        int root_untouched = 0;
        if (f) {
            size_t n = fread(readback, 1, sizeof readback, f);
            fclose(f);
            root_untouched = n >= U2_SECTOR_TILES && readback[0] == 0x55;
        }
        CHECK(root_untouched,
              "layout B: storing a patched sector wrote nothing beside the root mapx");
        snprintf(q, sizeof q, "%.400s/PLAYER/MAPG51", saves);
        f = fopen(q, "rb");
        int landed_in_slot = 0;
        if (f) {
            size_t n = fread(readback, 1, sizeof readback, f);
            fclose(f);
            landed_in_slot = n >= U2_SECTOR_TILES && readback[4 * U2_SECTOR_EDGE + 3] == 0x77;
        }
        CHECK(landed_in_slot,
              "S2: the overlay took the spelling the sector was read under, so the read side finds it");
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK &&
              w.map[4 * U2_SECTOR_EDGE + 3] == 0x77,
              "layout B: the change survives reload instead of being shadowed by its own layer");

        /* Dialogue and the stroke table are content too, so they cross the same
         * layers. tlkx is read-only, so only the read side is asserted. */
        snprintf(q, sizeof q, "%.400s/data/original/tlkx51", dir);
        CHECK(write_file(q, layer, U2_TLKX_BYTES) == 0, "layout B: data/original/tlkx51 written");
        snprintf(q, sizeof q, "%.400s/data/updated/TLKG51", dir);
        CHECK(write_file(q, patched, U2_TLKX_BYTES) == 0, "layout B: data/updated/TLKG51 written");
        ctx.state.world = 4;
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load_tlkx(&ctx, &w, jup) == U2_WORLD_OK && w.tlkx[0] == 0x44,
              "layout B: dialogue prefers the patched layer off Earth");
        ctx.state.world = 0;

        snprintf(q, sizeof q, "%.400s/data/original/monsters", dir);
        CHECK(write_file(q, layer, U2_STROKE_BYTES) == 0, "layout B: data/original/monsters written");
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load_strokes(&ctx, &w) == U2_WORLD_OK,
              "layout B: the stroke table is found in the release layer, not only beside the binary");
    }

    /* --- the save slot is a layer, and it is the first one ----------------
     *
     * docs/save-scoped-worlds.md § 3. A sector this save has changed lives in
     * saves/<slot>/ and outranks every content copy, because it is that player's
     * world; a slot with nothing in it reads pure content, which is the property
     * that keeps every pre-slot fixture in this file behaving as it always did.
     *
     * The overlay is mapx/monx only (D3), so the tlkx assertion below is not
     * decoration: a slot that overlaid dialogue would give a stale line back and
     * nothing else in this file would notice. */
    {
        u2_sector_id ov = { .hi = 6, .lo = 0 };
        static uint8_t under[U2_SECTOR_TILES], over[U2_SECTOR_TILES];
        char slot[600], q[600], d[600];
        for (size_t i = 0; i < U2_SECTOR_TILES; i++) { under[i] = 0x66; over[i] = 0x77; }

        snprintf(slot, sizeof slot, "%.400s/PLAYER", saves);
        mkdir(saves, 0755);
        mkdir(slot, 0755);

        snprintf(d, sizeof d, "%.400s/data/original", dir);
        snprintf(q, sizeof q, "%.400s/mapx60", d);
        CHECK(write_file(q, under, U2_SECTOR_TILES) == 0,
              "slot layer: a content mapx60 written");
        snprintf(q, sizeof q, "%.400s/monx60", d);
        CHECK(write_file(q, under, U2_MON_TILES) == 0, "slot layer: a content monx60 written");
        snprintf(q, sizeof q, "%.400s/tlkx60", d);
        CHECK(write_file(q, under, U2_TLKX_BYTES) == 0, "slot layer: a content tlkx60 written");

        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, ov) == U2_WORLD_OK && w.map[0] == 0x66,
              "slot layer: an empty slot reads pure content");
        CHECK(w.map_root && strcmp(w.map_root, "data/original") == 0,
              "  and reports the content layer it came from");

        snprintf(q, sizeof q, "%.400s/mapx60", slot);
        CHECK(write_file(q, over, U2_SECTOR_TILES) == 0, "slot layer: an overlaid mapx60 written");
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, ov) == U2_WORLD_OK && w.map[0] == 0x77,
              "slot layer: the slot shadows the content copy");
        CHECK(w.map_root && strcmp(w.map_root, U2_WORLD_SLOT_LAYER) == 0,
              "  and the provenance says slot, not a content root");

        /* The creature table overlays too, at its own size, and a kind the slot
         * does not hold still comes from content. */
        snprintf(q, sizeof q, "%.400s/tlkx60", slot);
        CHECK(write_file(q, over, U2_TLKX_BYTES) == 0, "slot layer: a tlkx60 placed in the slot");
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load_tlkx(&ctx, &w, ov) == U2_WORLD_OK && w.tlkx[0] == 0x66,
              "slot layer: tlkx is content, so the slot copy is NOT read (D3)");
        unlink(q);

        snprintf(q, sizeof q, "%.400s/monx60", slot);
        CHECK(write_file(q, over, U2_MON_TILES) == 0, "slot layer: an overlaid monx60 written");
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 0, ov) == U2_WORLD_OK && w.mon[0] == 0x77,
              "slot layer: the creature table overlays at 256 bytes");
        CHECK(u2_world_load(&ctx, &w, 1, ov) == U2_WORLD_OK && w.map[0] == 0x77,
              "  and a both-kind load keeps the map overlay (per-kind, not last-wins)");
    }

    /* --- write-back: dirty tracking, tail preservation, atomicity ---- */
    memset(&w, 0, sizeof w);
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK, "reload mapx30 for write-back");
    CHECK(w.map_dirty == 0, "a freshly loaded sector is not dirty");
    u2_world_tile_put(&w, 7, 9, 0xA5);
    CHECK(w.map_dirty == 1, "writing a tile marks the sector dirty");
    CHECK(u2_world_tile_at(&w, 7, 9) == 0xA5, "the tile change is visible");
    CHECK(u2_world_store(&ctx, &w, 1) == U2_WORLD_OK, "dirty sector stores");
    CHECK(w.map_dirty == 0, "storing clears dirty");

    /* The property the whole slot change exists for, asserted at the unit level so
     * a regression names the file that moved rather than just a hash. */
    path_for(p, sizeof p, dir, "mapx30");
    f = fopen(p, "rb");
    int content_untouched = 0;
    if (f) {
        size_t n = fread(readback, 1, sizeof readback, f);
        fclose(f);
        content_untouched = n == U2_SECTOR_TILES + 128 && readback[0] == buf[0]
                         && readback[9 * U2_SECTOR_EDGE + 7] != 0xA5;
    }
    CHECK(content_untouched,
          "S2: the content mapx30 is byte-identical to what was written into it");

    snprintf(p, sizeof p, "%.400s/PLAYER/mapx30", saves);
    CHECK(filesize(p) == U2_SECTOR_TILES + 128,
          "S2: the overlay is 4096 + the 128-byte tail carried from the content file");
    snprintf(p, sizeof p, "%.400s/PLAYER/mapx30.u2tmp", saves);
    CHECK(filesize(p) == -1, "write-back leaves no temp file behind");

    snprintf(p, sizeof p, "%.400s/PLAYER/mapx30", saves);
    f = fopen(p, "rb");
    CHECK(f != NULL, "stored sector reopens");
    if (f) {
        size_t n = fread(readback, 1, sizeof readback, f);
        fclose(f);
        CHECK(n == U2_SECTOR_TILES + 128, "stored file is 4096 + the preserved tail");
        CHECK(readback[9 * U2_SECTOR_EDGE + 7] == 0xA5, "the changed tile is on disk");
        CHECK(readback[U2_SECTOR_TILES] == buf[U2_SECTOR_TILES],
              "the first tail byte survived untouched");
        CHECK(readback[U2_SECTOR_TILES + 127] == buf[U2_SECTOR_TILES + 127],
              "the last tail byte survived untouched");
    }
    memset(&w, 0, sizeof w);
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK && w.map[9 * U2_SECTOR_EDGE + 7] == 0xA5,
          "S2: the overlay is what the next load of this save reads");

    /* --- what has to be writable, and what no longer does ---------------
     *
     * Content is read-only by design now, so a mounted-down-the-road release must
     * not stop a game being saved; the slot is the thing that has to accept a
     * write, and refusing with the INSERT analogue is its honest failure. */
    CHECK(chmod(dir, 0555) == 0, "content dir made read-only");
    memset(&w, 0, sizeof w);
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK,
          "a read-only content dir still plays (load succeeds)");
    u2_world_tile_put(&w, 1, 1, 0x5A);
    CHECK(u2_world_store(&ctx, &w, 1) == U2_WORLD_OK,
          "S2: a read-only content dir does not stop a save, because content is not the target");
    CHECK(chmod(dir, 0755) == 0, "content dir restored");

    {
        char slotdir[600];
        snprintf(slotdir, sizeof slotdir, "%.400s/PLAYER", saves);
        CHECK(chmod(slotdir, 0555) == 0, "save slot made read-only");
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK, "a read-only slot still plays");
        u2_world_tile_put(&w, 1, 2, 0x5B);
        CHECK(u2_world_store(&ctx, &w, 1) == U2_WORLD_NOT_READY,
              "storing into a read-only slot reports NOT_READY, the INSERT analogue");
        CHECK(chmod(slotdir, 0755) == 0, "save slot restored");
    }

    /* --- the monster table is the same shape at 256 bytes (asm/22D5) -- */
    id.hi = 3; id.lo = 0;
    path_for(p, sizeof p, dir, "monx30");
    CHECK(write_file(p, buf, U2_MON_TILES + 128) == 0, "monx file written");
    memset(&w, 0, sizeof w);
    CHECK(u2_world_load(&ctx, &w, 0, id) == U2_WORLD_OK, "monx sector loads");
    CHECK(w.bytes_read == U2_MON_TILES, "monx read is 256 bytes (CX at asm/22D5)");

    /* --- the game's real load order: BOTH kinds into one world, then a map write-back ---
     *
     * Everything above loads one kind and stores that kind, which is why the write-back
     * tests passed while the engine was corrupting player data: the overworld always loads
     * the map (asm/2290) and then the monsters (asm/22D5) into the same u2_world, and a
     * store path that derives its filename from whichever load ran last writes the map
     * buffer to monxNN and returns U2_WORLD_OK. This case is the one that can see it.
     *
     * Added red-first at the commit that documented the defect (docs/phase4-state.md,
     * OPEN DEFECT): run against the unfixed store, the MONX size assertion fails and the
     * map change never lands in MAPX. */
    memset(&w, 0, sizeof w);
    id.hi = 3; id.lo = 0;
    CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK,
          "both-kind world: map loads first");
    CHECK(u2_world_load(&ctx, &w, 0, id) == U2_WORLD_OK,
          "both-kind world: monx loads second, as asm/22D5 does in play");

    path_for(p, sizeof p, dir, "monx30");
    const long mon_size_before = filesize(p);
    f = fopen(p, "rb");
    size_t mon_n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    CHECK(mon_n > 0, "monx30 snapshotted before the map write-back");

    u2_world_tile_put(&w, 2, 0, 0x44);      /* the byte a boarded ship tile becomes */
    CHECK(u2_world_store(&ctx, &w, 1) == U2_WORLD_OK,
          "both-kind world: storing the map reports OK");

    path_for(p, sizeof p, dir, "monx30");
    CHECK(filesize(p) == mon_size_before,
          "storing the MAP does not touch the MONX file's size");
    f = fopen(p, "rb");
    int mon_intact = (f != NULL);
    if (f) {
        uint8_t after[U2_SECTOR_TILES + 256];
        size_t n = fread(after, 1, sizeof after, f);
        fclose(f);
        mon_intact = (n == mon_n && memcmp(after, buf, mon_n) == 0);
    }
    CHECK(mon_intact, "storing the MAP leaves the MONX bytes byte-identical");

    path_for(p, sizeof p, dir, "mapx30");
    f = fopen(p, "rb");
    int tile_on_disk = 0;
    if (f) {
        size_t n = fread(readback, 1, sizeof readback, f);
        fclose(f);
        tile_on_disk = (n >= U2_SECTOR_TILES && readback[0 * U2_SECTOR_EDGE + 2] != 0x44);
    }
    CHECK(tile_on_disk, "storing the MAP left the content mapx30 alone");

    snprintf(p, sizeof p, "%.400s/PLAYER/mapx30", saves);
    f = fopen(p, "rb");
    int overlay_has_it = 0;
    if (f) {
        size_t n = fread(readback, 1, sizeof readback, f);
        fclose(f);
        overlay_has_it = (n >= U2_SECTOR_TILES && readback[0 * U2_SECTOR_EDGE + 2] == 0x44);
    }
    CHECK(overlay_has_it, "storing the MAP wrote the slot's MAPX file, where the change belongs");

    /* --- W17: each layer reads on its own, so a delta is measurable -----------
     *
     * docs/demo-record-playback.md § 13.11 needs "what this save changed" measured
     * against "what shipped". That is only computable if the content bytes can be read
     * while a slot overlay is live. If u2_world_read_content() ever fell through to the
     * slot, both reads would agree, every delta would come out empty, and the demo
     * subsystem would report success while carrying no information — the vacuous-pass
     * shape AGENTS.md § 3 exists to stop. So the control is byte [7]: the slot copy
     * carries a different value there, which makes agreement detectable rather than
     * assumed.
     */
    {
        static uint8_t content[U2_SECTOR_TILES], slotb[U2_SECTOR_TILES], got[U2_SECTOR_TILES];
        char nm[32], slotdir[600];

        fill_sector(content, sizeof content);
        memcpy(slotb, content, sizeof slotb);
        slotb[7] = 0x5A;                            /* the only difference */

        path_for(p, sizeof p, dir, "mapx30");
        CHECK(write_file(p, content, U2_SECTOR_TILES) == 0, "W17: content sector written");
        CHECK(u2_slot_ensure(&ctx) == 0, "W17: the save slot exists");
        u2_slot_dir(&ctx, slotdir, sizeof slotdir);
        path_for(p, sizeof p, slotdir, "mapx30");
        CHECK(write_file(p, slotb, sizeof slotb) == 0, "W17: slot overlay written");

        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, id) == U2_WORLD_OK, "W17: the sector loads");
        CHECK(w.map_root && strcmp(w.map_root, U2_WORLD_SLOT_LAYER) == 0,
              "W17 fixture: the live load came from the slot, not from content");
        CHECK(w.map[7] == 0x5A, "W17 fixture: the live bytes carry the slot's difference");

        CHECK(u2_world_read_content(&ctx, 1, id, got, sizeof got, nm, sizeof nm)
                  == U2_WORLD_OK,
              "W17: the content layer reads while a slot overlay is live");
        CHECK(got[7] == content[7] && got[7] != 0x5A,
              "W17: the content read is NOT the overlay, so a delta is measurable");
        CHECK(strcmp(nm, "mapx30") == 0,
              "W17: the resolved filename is reported, never guessed");

        CHECK(u2_world_read_slot(&ctx, 1, id, got, sizeof got, nm, sizeof nm)
                  == U2_WORLD_OK,
              "W17: the slot layer reads on its own");
        CHECK(got[7] == 0x5A, "W17: the slot read carries the overlay byte");

        /* And with no overlay the delta is empty by construction, which is the clean
         * recording case § 13.1 claims already works. */
        unlink(p);
        CHECK(u2_world_read_slot(&ctx, 1, id, got, sizeof got, nm, sizeof nm)
                  == U2_WORLD_MISSING,
              "W17: no overlay is reported MISSING, not as an empty read");
        CHECK(u2_world_read_content(&ctx, 1, id, got, sizeof got, nm, sizeof nm)
                  == U2_WORLD_OK && got[7] == content[7],
              "W17: with no overlay the content read still works, so the delta is empty");
    }

    printf(fails ? "world check: FAIL (%d)\n" : "world check: all checks passed\n",
           fails);
    return fails != 0;
}
