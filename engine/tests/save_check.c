/*
 * tests/save_check.c — the byte-compatibility guarantees of core/save.c.
 *
 * The owner's constraint: engine saves must be interchangeable with the
 * original's PLAYER file, in both directions. That is a strong claim, so every
 * assertion here is about exact bytes on disk, not about behaviour.
 *
 * Build and run by tests/run_cp3.sh.
 */

#define _POSIX_C_SOURCE 200809L   /* mkdtemp under -std=c11 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/ctx.h"
#include "core/save.h"

static int fails;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) { printf("  ok    %s\n", msg); }                           \
        else      { printf("  FAIL  %s\n", msg); fails++; }                  \
    } while (0)

static long filesize(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static long slurp(const char *p, uint8_t *buf, size_t cap)
{
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return (long)n;
}

int main(int argc, char **argv)
{
    char base[256], dir[1024], slot[1088], path[1120];
    uint8_t buf[U2_PLAYER_FILE_SIZE + 8];

    /* Scratch lives where the caller says (tests pass a /var/tmp dir); the
     * engine tree itself must never accumulate save artefacts. */
    snprintf(base, sizeof base, "%s", argc > 1 ? argv[1] : ".");
    snprintf(dir, sizeof dir, "%s/saves_check.XXXXXX", base);
    if (!mkdtemp(dir)) { printf("  FAIL  mkdtemp in %s\n", base); return 1; }
    /* saves/<slot>/PLAYER, the shape docs/save-scoped-worlds.md rules. The slot
     * and the record share a name on purpose (D1), so the two paths below differ
     * by exactly that repetition, and a test that spelled only one of them would
     * be asserting the old layout. */
    snprintf(slot, sizeof slot, "%s/PLAYER", dir);
    snprintf(path, sizeof path, "%s/PLAYER", slot);

    /* --- the blank template is the shipped disk image -------------------- */
    u2_save_blank_template(buf);
    int shape_ok = (buf[U2_PLAYER_BLANK_MARK_OFF] == U2_PLAYER_BLANK_MARK_BYTE);
    for (int i = 0; i < U2_PLAYER_FILE_SIZE && shape_ok; i++) {
        if (i != U2_PLAYER_BLANK_MARK_OFF) shape_ok = (buf[i] == 0);
    }
    CHECK(shape_ok, "blank template: 384 bytes, one non-zero byte (0x1a at 0x100)");
    CHECK(U2_REC(&((u2_state){0}))[0] == 0, "a fresh state reads as a blank record");

    /* --- ensure() creates the slot and seeds the record ------------------ */
    u2_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.save_dir = dir;
    rmdir(dir);                       /* ensure() must be able to recreate it */
    CHECK(u2_save_ensure(&ctx) == U2_SAVE_BLANK, "ensure() on a missing dir seeds a blank");
    CHECK(ctx.save_seeded == 1, "ensure() reports that it seeded the file");
    {
        struct stat st;
        CHECK(stat(slot, &st) == 0 && S_ISDIR(st.st_mode),
              "ensure() created the slot directory, not just the save directory (D6)");
    }
    CHECK(filesize(path) == U2_PLAYER_FILE_SIZE, "seeded PLAYER file is 384 bytes");
    CHECK(slurp(path, buf, sizeof buf) == U2_PLAYER_FILE_SIZE &&
          buf[0] == 0 && buf[U2_PLAYER_BLANK_MARK_OFF] == U2_PLAYER_BLANK_MARK_BYTE,
          "seeded file matches the pristine blank byte for byte");
    CHECK(u2_save_ensure(&ctx) == U2_SAVE_OK, "ensure() is idempotent on the second call");

    /* --- load() on the blank is the original's "generate a character" ---- */
    CHECK(u2_save_load(&ctx) == U2_SAVE_BLANK, "load() of a blank record reports BLANK");
    CHECK(u2_save_record_present(&ctx) == 0, "blank record is not a character");

    /* --- round trip: store then load reproduces the record exactly ------- */
    u2_state *st = &ctx.state;
    memcpy(st->name, "BRITISH KNIGHT", 14);
    st->sex = 'M';
    st->pclass = 2;                    /* 0..3, already de-ASCII'd */
    st->race = '3';                    /* stored as typed, per asm/5400 */
    st->stat_str = 0x18; st->stat_agi = 0x12; st->stat_sta = 0x16;
    st->stat_cha = 0x11; st->stat_wis = 0x14; st->stat_int = 0x13;
    st->hp = 4; st->food = 4;
    st->gold_lo = 0x01; st->gold_hi = 0x25;

    CHECK(u2_save_store(&ctx) == U2_SAVE_OK, "store() writes the record");
    CHECK(filesize(path) == U2_PLAYER_FILE_SIZE, "store() keeps the file at 384 bytes");

    u2_ctx back;
    memset(&back, 0, sizeof back);
    back.save_dir = dir;
    CHECK(u2_save_load(&back) == U2_SAVE_OK, "load() finds a character after store()");
    CHECK(memcmp(back.state.name, "BRITISH KNIGHT", 14) == 0, "name survives the round trip");
    CHECK(back.state.sex == 'M' && back.state.pclass == 2 && back.state.race == '3',
          "sex / class / race survive");
    CHECK(back.state.stat_str == 0x18 && back.state.stat_int == 0x13, "stats survive");
    CHECK(back.state.hp == 4 && back.state.food == 4 && back.state.gold_lo == 0x01,
          "hp / food / gold survive");

    /* The whole 256-byte record must be identical, not just the fields above. */
    CHECK(memcmp(U2_REC(&back.state), U2_REC(&ctx.state), U2_PLAYER_RECORD_SIZE) == 0,
          "all 256 record bytes round-trip");

    /* --- the tail is preserved, as it is on a real disk ------------------ */
    CHECK(slurp(path, buf, sizeof buf) == U2_PLAYER_FILE_SIZE &&
          buf[U2_PLAYER_BLANK_MARK_OFF] == U2_PLAYER_BLANK_MARK_BYTE,
          "the 128-byte tail survives a save (only the record is written)");
    int tail_zero = 1;
    for (int i = U2_PLAYER_RECORD_SIZE; i < U2_PLAYER_FILE_SIZE; i++) {
        if (i != U2_PLAYER_BLANK_MARK_OFF && buf[i] != 0) tail_zero = 0;
    }
    CHECK(tail_zero, "nothing else in the tail was invented by the engine");

    /* --- unusable paths report NOT_READY instead of inventing a place ---- */
    u2_ctx bad;
    memset(&bad, 0, sizeof bad);
    bad.save_dir = path;               /* a file, not a directory */
    CHECK(u2_save_ensure(&bad) == U2_SAVE_NOT_READY, "save-dir pointing at a file is NOT_READY");

    char shortdir[1200], shortslot[1260], shortfile[1300];
    snprintf(shortdir, sizeof shortdir, "%s/short", dir);
    mkdir(shortdir, 0755);
    snprintf(shortslot, sizeof shortslot, "%s/PLAYER", shortdir);
    mkdir(shortslot, 0755);
    snprintf(shortfile, sizeof shortfile, "%s/PLAYER", shortslot);
    FILE *f = fopen(shortfile, "wb");
    if (f) { fwrite("junk", 1, 4, f); fclose(f); }
    memset(&bad, 0, sizeof bad);
    bad.save_dir = shortdir;
    CHECK(u2_save_ensure(&bad) == U2_SAVE_NOT_READY, "a short PLAYER file is NOT_READY");
    CHECK(slurp(shortfile, buf, sizeof buf) == 4, "a short PLAYER file is not clobbered");

    /* --- a too-large file is refused, not truncated ---------------------- */
    char bigdir[1300], bigslot[1360], bigfile[1400];
    snprintf(bigdir, sizeof bigdir, "%s/big", dir);
    mkdir(bigdir, 0755);
    snprintf(bigslot, sizeof bigslot, "%s/PLAYER", bigdir);
    mkdir(bigslot, 0755);
    snprintf(bigfile, sizeof bigfile, "%s/PLAYER", bigslot);
    f = fopen(bigfile, "wb");
    /* sizeof buf, not a literal: the fixture only needs to be longer than the 384-byte
     * record, and a hand-picked 500 read past the end of the buffer to get there.
     * AddressSanitizer found this the first time the suite ran under it. */
    if (f) { memset(buf, 0xEE, sizeof buf); fwrite(buf, 1, sizeof buf, f); fclose(f); }
    memset(&bad, 0, sizeof bad);
    bad.save_dir = bigdir;
    CHECK(u2_save_load(&bad) == U2_SAVE_NOT_READY, "an oversized PLAYER file is refused");

    /* --- the release spelling inside a slot is still found ---------------
     *
     * resolve_save_name()'s job shrank with the layout but did not go away: DOS
     * names are case-insensitive and this pack spells the record `player`, so a
     * hand-copied release save dropped into a slot has to be read AND written
     * under the name it actually has. Deleting that function would reintroduce a
     * silent fresh start, which is the bug it was written for. */
    {
        char reldir[1200], relslot[1260], relfile[1300], other[1320];
        snprintf(reldir, sizeof reldir, "%s/release", dir);
        mkdir(reldir, 0755);
        snprintf(relslot, sizeof relslot, "%s/PLAYER", reldir);
        mkdir(relslot, 0755);
        snprintf(relfile, sizeof relfile, "%s/player", relslot);
        snprintf(other, sizeof other, "%s/PLAYER", relslot);
        u2_save_blank_template(buf);
        f = fopen(relfile, "wb");
        if (f) { fwrite(buf, 1, U2_PLAYER_FILE_SIZE, f); fclose(f); }

        u2_ctx rel;
        memset(&rel, 0, sizeof rel);
        rel.save_dir = reldir;
        CHECK(u2_save_load(&rel) == U2_SAVE_BLANK,
              "a release-spelled `player` in a slot is found, not skipped");
        memcpy(rel.state.name, "SLOTTEST", 8);
        CHECK(u2_save_store(&rel) == U2_SAVE_OK, "  and storing to it succeeds");
        CHECK(slurp(relfile, buf, sizeof buf) == U2_PLAYER_FILE_SIZE &&
              memcmp(buf, "SLOTTEST", 8) == 0,
              "  the write went to the file that was there, under its own spelling");
        CHECK(filesize(other) == -1,
              "  and no second, differently spelled record was created beside it");
        unlink(relfile); rmdir(relslot); rmdir(reldir);
    }

    /* --- an upgrade from the pre-slot layout ------------------------------
     *
     * An older engine kept the record as a FILE at `<save_dir>/PLAYER`, which is
     * where the slot directory has to go, so until it moves nothing can be saved.
     * The engine adopts it: rename only, never overwrite, never touching content.
     * The played-in CONTENT repair stays in the installer because that one needs
     * the manifest; this one needs nothing but the name collision. */
    {
        char lg[1088], lgflat[1152], lgslot[1152], lgrec[1216];
        snprintf(lg,     sizeof lg,     "%s/legacy", dir);
        snprintf(lgflat, sizeof lgflat, "%s/PLAYER", lg);
        snprintf(lgslot, sizeof lgslot, "%s/PLAYER", lg);
        snprintf(lgrec,  sizeof lgrec,  "%s/PLAYER", lgslot);
        mkdir(lg, 0755);
        u2_save_blank_template(buf);
        memcpy(buf, "LEGACY", 6);
        f = fopen(lgflat, "wb");
        if (f) { fwrite(buf, 1, U2_PLAYER_FILE_SIZE, f); fclose(f); }

        u2_ctx lgctx;
        memset(&lgctx, 0, sizeof lgctx);
        lgctx.save_dir = lg;
        CHECK(u2_save_ensure(&lgctx) == U2_SAVE_OK,
              "a pre-slot flat record is adopted, not refused");
        CHECK(filesize(lgrec) == U2_PLAYER_FILE_SIZE &&
              slurp(lgrec, buf, sizeof buf) == U2_PLAYER_FILE_SIZE &&
              memcmp(buf, "LEGACY", 6) == 0,
              "  the record moved into the slot with its bytes intact");
        {
            struct stat lst;
            /* The path is not absent, it is the slot directory now — which is the
             * whole point, and why "the flat file is gone" has to be asserted as a
             * type change rather than a missing path. */
            CHECK(stat(lgflat, &lst) == 0 && S_ISDIR(lst.st_mode),
                  "  and the flat file is gone: the name is the slot directory now");
        }
        memset(&lgctx.state, 0, sizeof lgctx.state);
        CHECK(u2_save_load(&lgctx) == U2_SAVE_OK &&
              memcmp(lgctx.state.name, "LEGACY", 6) == 0,
              "  and the game loads the save it found");
        CHECK(u2_save_ensure(&lgctx) == U2_SAVE_OK, "  and ensure() is idempotent after it");

        /* A half-done move is a state this code cannot reason about, so it refuses
         * rather than guessing — and refuses without touching either file. */
        char hd[1088], hdflat[1152], hdtmp[1152];
        snprintf(hd,     sizeof hd,     "%s/halfdone", dir);
        snprintf(hdflat, sizeof hdflat, "%s/PLAYER", hd);
        snprintf(hdtmp,  sizeof hdtmp,  "%s/.PLAYER.migrating", hd);
        mkdir(hd, 0755);
        f = fopen(hdflat, "wb");
        if (f) { memset(buf, 'x', sizeof buf); fwrite(buf, 1, U2_PLAYER_FILE_SIZE, f); fclose(f); }
        f = fopen(hdtmp, "wb");
        if (f) { memset(buf, 'y', sizeof buf); fwrite(buf, 1, U2_PLAYER_FILE_SIZE, f); fclose(f); }
        u2_ctx hdctx;
        memset(&hdctx, 0, sizeof hdctx);
        hdctx.save_dir = hd;
        CHECK(u2_save_ensure(&hdctx) == U2_SAVE_NOT_READY,
              "an interrupted move is refused, not guessed at");
        CHECK(filesize(hdflat) == U2_PLAYER_FILE_SIZE && filesize(hdtmp) == U2_PLAYER_FILE_SIZE,
              "  and refusal leaves both files exactly as they were");
        unlink(hdflat); unlink(hdtmp); rmdir(hd);
        unlink(lgrec); rmdir(lgslot); rmdir(lg);
    }

    /* --- cleanup ---------------------------------------------------------- */
    unlink(path); unlink(shortfile); unlink(bigfile);
    rmdir(shortslot); rmdir(shortdir); rmdir(bigslot); rmdir(bigdir);
    rmdir(slot); rmdir(dir);

    printf(fails ? "SAVE CHECK: FAIL (%d)\n" : "SAVE CHECK: PASS\n", fails);
    return fails ? 1 : 0;
}
