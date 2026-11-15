/*
 * save_q_check.c — CP4b: the Q save round trip.
 *
 * The owner's test, automated: create a character, move somewhere, save with
 * Q, quit, start again, press P — and land where you saved. The bytes say it
 * works because the save routine copies the live position into the entry
 * pair before writing (file 0x2520..0x2529: [0x5A]:=[0x00], [0x5B]:=[0x01]),
 * and the P path restores from that pair (file 0x0A26..0x0A2F). This test
 * proves the engine reproduces both halves, plus the location guard and the
 * CP11 extension point (the policy struct a "save anywhere" community patch
 * will flip).
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "core/ctx.h"
#include "core/save.h"
#include "core/seam.h"
#include "core/state.h"
#include "core/vram.h"
#include "core/world.h"
#include "game/overworld.h"
#include "game/monsters.h"
#include "gen_content.h"

static int fails;
static void ok(int cond, const char *what)
{
    printf("  %s  %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) fails++;
}

static int vram_contains(u2_ctx *ctx, const char *needle)
{
    char row[64];
    for (int r = 0; r < U2_ROWS; r++) {
        u2_vram_row_text(&ctx->vram, r, row, sizeof row);
        if (strstr(row, needle)) return 1;
    }
    return 0;
}

/* A synthetic sector file: 4096 tile bytes + the 128-byte tail. */
static void make_sector(const char *dir, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    unsigned char buf[64];
    memset(buf, 0x04, sizeof buf);
    for (int i = 0; i < 64; i++) fwrite(buf, 1, 64, f);   /* 4096 tiles */
    memset(buf, 0, sizeof buf);
    for (int i = 0; i < 2; i++) fwrite(buf, 1, 64, f);    /* 128 tail   */
    fclose(f);
}

/* The creature block is a separate file with a separate size: 256 bytes read at
 * asm/22D5 with CX=0x100, and the same block written back at asm/2305. */
static void make_mon_sector(const char *dir, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    unsigned char buf[256];
    memset(buf, 0, sizeof buf);
    fwrite(buf, 1, sizeof buf, f);
    fclose(f);
}

/* Open a synthetic overworld: sector 2/0, no tiles (the save path never
 * touches art, and the tile cache would need the player's executable). */
static void open_world(u2_ctx *ctx, u2_overworld *ow)
{
    memset(ow, 0, sizeof *ow);
    u2_world_open(ctx);
    u2_sector_id id = { 2, 0 };
    u2_world_load(ctx, &ow->world, 1, id);
    ow->x = 30; ow->y = 40;
    ow->sector_hi = 2; ow->sector_lo = 0;
    /* overworld_open() sets the on-foot stamp (DS:13 = 0x78) on the way into the world, and
     * this shortcut bypasses it. Without this line the fixture is a state the game cannot be
     * in — a character in the world with no sprite stamped — and the Q on-foot guard added
     * 2026-10-29 correctly refuses it with ONLY ON FOOT!. The guard is right and the fixture
     * was quietly relying on the guard being a stub. */
    ctx->state.player_stamp = 0x78;
}

int main(int argc, char **argv)
{
    const char *tmp = argc > 1 ? argv[1] : "/var/tmp/u2-saveq";
    char data[256], saves[256], saves_path[300];
    snprintf(data, sizeof data, "%s/data", tmp);
    snprintf(saves, sizeof saves, "%s/saves", tmp);
    /* The record is inside its slot, and the slot shares the record's name (D1).
     * engine/src/core/slot.h owns the shape; a gate that spelled the old flat path
     * would read a file the engine no longer opens and call it a save. */
    snprintf(saves_path, sizeof saves_path, "%s/PLAYER/PLAYER", saves);
    mkdir(tmp, 0755); mkdir(data, 0755); mkdir(saves, 0755);
    make_sector(data, "mapx20");
    make_mon_sector(data, "monx20");

    /* --- run one: a fresh character walks and saves ---------------------- */
    u2_ctx a;
    memset(&a, 0, sizeof a);
    a.data_dir = data;
    a.save_dir = saves;
    u2_vram_init(&a.vram);
    u2_state_init(&a.state);
    memcpy(a.state.name, "TESTER", 6);          /* a named record */
    u2_save_ensure(&a);

    u2_overworld ow;
    open_world(&a, &ow);
    seam_goto(&a, 0, U2_MSG_BOTTOM);
    seam_str_out(&a, U2_S_CMD_PROMPT);

    /* Guard: indoors refuses, and nothing was written. */
    u2_overworld inside = ow;
    inside.sector_lo = 2;
    ok(u2_overworld_save(&a, &inside, &U2_SAVE_POLICY_ORIGINAL) == 1 &&
       vram_contains(&a, U2_S_Q_OUTDOORS),
       "Q indoors refuses with ONLY OUTDOORS! (asm/3DE4)");
    {
        /* Read the record file directly: entry_x/entry_y are record offsets
         * 0x24/0x25 (DS:5A/5B minus the DS:36 record base). */
        unsigned char rec[256];
        FILE *rf = fopen(saves_path, "rb");
        int read_ok = rf && fread(rec, 1, 256, rf) == 256;
        if (rf) fclose(rf);
        ok(read_ok && rec[0x24] == 0 && rec[0x25] == 0,
           "the refusal left the record untouched");
    }

    /* --- a save that cannot be written has to say so ------------------------
     *
     * ONE MOMENT PLEASE! is printed before the write, so a failure after it left
     * the player told that the game saved when nothing reached the disk. The
     * original cannot fail here — its DOS wrapper loops on "(INSERT PLAYER DISK)"
     * until a disk arrives — so the refusal text is the shipped one.
     *
     * Placed here, before any refusal text has ever reached the screen, so the text
     * assertion cannot pass on a message left over from an earlier case. And it
     * covers both halves of the write separately, because they fail for different
     * reasons: a read-only slot directory stops the temp+rename a sector write uses
     * but NOT the record, which is rewritten in place and needs write permission on
     * the file rather than the directory. */
    {
        char slotdir[300], recfile[300];
        snprintf(slotdir, sizeof slotdir, "%.290s/PLAYER", saves);
        snprintf(recfile, sizeof recfile, "%.290s/PLAYER/PLAYER", saves);

        ok(chmod(recfile, 0444) == 0, "record file made unwritable");
        ok(u2_overworld_save(&a, &ow, &U2_SAVE_POLICY_ORIGINAL) == 1 &&
           vram_contains(&a, U2_S_DISK_PLAYER),
           "a Q whose record write fails says so, not a silent ONE MOMENT PLEASE!");
        ok(chmod(recfile, 0644) == 0, "record file writable again");

        ok(chmod(slotdir, 0555) == 0, "save slot made read-only");
        u2_world_tile_put(&ow.world, 5, 5, 0x08);   /* dirty the map: the sector write must be tried */
        ok(u2_overworld_save(&a, &ow, &U2_SAVE_POLICY_ORIGINAL) == 1,
           "a Q whose sector write fails is a failed save too, not a discarded one");
        ok(ow.world.map_dirty == 1,
           "  and the dirty flag survives, so the change is not silently dropped");
        ok(chmod(slotdir, 0755) == 0, "save slot writable again");
    }

    /* The faithful save at the walked-to position. */
    ok(u2_overworld_save(&a, &ow, &U2_SAVE_POLICY_ORIGINAL) == 0 &&
       vram_contains(&a, U2_S_Q_MOMENT),
       "Q outdoors saves, echoing ONE MOMENT PLEASE! (asm/3DFA)");
    ok(ow.world.map_dirty == 0,
       "clean sectors are not rewritten (dirty-only store contract)");

    /* --- the creature block is the other half of the save write -------------
     * asm/2305 names MONXFF (file 0x2548) as well as MAPXFF (0x2538), so the
     * original rewrites the creature block on the same path. Q used to write only
     * the map: a kill cleared the arrays, the file kept the monster alive, and it
     * came back on reload. */
    {
        char monpath[300];
        /* The creature block this save dirtied lands in the slot, not beside the
         * content copy it was read from — that is the property, and reading the
         * content file here would assert the layout this change replaced. */
        snprintf(monpath, sizeof monpath, "%s/PLAYER/monx20", saves);
        u2_sector_id id = { 2, 0 };
        if (u2_world_load(&a, &ow.world, 0, id) != U2_WORLD_OK) {
            ok(0, "the synthetic monx20 did not load");
        } else {
            a.state.mon_x[3] = 10; a.state.mon_y[3] = 10;
            a.state.mon_hp[3] = 40; a.state.mon_type[3] = 0x30;
            u2_monsters_store(&a, &ow.world);
            ok(ow.world.mon_dirty == 1,
               "copying the arrays out dirties the creature block");
            ok(u2_overworld_save(&a, &ow, &U2_SAVE_POLICY_ORIGINAL) == 0,
               "Q saves with a dirty creature block");
            unsigned char mb[256];
            FILE *mf = fopen(monpath, "rb");
            int got = mf ? (int)fread(mb, 1, 256, mf) : 0;
            if (mf) fclose(mf);
            ok(got == 256 && mb[96 + 3] == 0x30,
               "Q writes the creature block, not only the map (asm/2305 names MONXFF)");
            ok(ow.world.mon_dirty == 0,
               "and the dirty flag clears, so a clean save rewrites nothing");

            /* The kill: three fields cleared, as asm/2E3D leaves them. */
            a.state.mon_hp[3] = 0; a.state.mon_type[3] = 0; a.state.mon_kind[3] = 0;
            u2_monsters_store(&a, &ow.world);
            u2_overworld_save(&a, &ow, &U2_SAVE_POLICY_ORIGINAL);
            mf = fopen(monpath, "rb"); got = mf ? (int)fread(mb, 1, 256, mf) : 0;
            if (mf) fclose(mf);
            ok(got == 256 && mb[96 + 3] == 0,
               "a killed creature is gone from the file too, so it does not revive on reload");
        }
    }

    /* The CP11 extension point: same guard, policy flipped, saves anyway. */
    u2_save_policy mod = { 1, 1, 1 };
    ok(u2_overworld_save(&a, &inside, &mod) == 0,
       "policy allow_indoors saves anyway (community-patch hook, CP11)");
    /* restore the faithful save as the final state of run one */
    u2_overworld_save(&a, &ow, &U2_SAVE_POLICY_ORIGINAL);

    /* --- run two: quit, restart, P loads the record ---------------------- */
    u2_ctx b;
    memset(&b, 0, sizeof b);
    b.data_dir = data;
    b.save_dir = saves;
    u2_vram_init(&b.vram);
    u2_state_init(&b.state);
    ok(u2_save_load(&b) == U2_SAVE_OK, "P finds the saved record");
    ok(b.state.entry_x == 30 && b.state.entry_y == 40,
       "the record carries the saved position, not the sector spawn");
    ok(b.state.sector_first == 2 && b.state.sector_second == 0,
       "and the sector it was saved in");
    ok(b.state.name[0] == 'T' && b.state.name[5] == 'R',
       "name survived the round trip");
    /* u2_overworld_init_state consumes exactly these four fields as the
     * spawn; asserting them here is the P contract without needing the
     * tile art (which only a real executable can supply). */

    /* --- the silent auto-save -------------------------------------------
     *
     * Placed after the reload assertions on purpose: these writes clobber the
     * record, and the P contract above must be judged against the Q save, not this.
     *
     * Two triggers are proven in the original: entering a site (asm/341E, asm/343F,
     * asm/34B5/34CF, asm/34E4) and launching a rocket from an Earth wilderness sector
     * (asm/38B0). Both call the save body directly, neither prints anything, and the
     * site-entry path carries none of Q's three location guards. Measured 2026-11-03:
     * entering Port Boniface from sector 30 rewrote `player`, and the sector stored was
     * the one the character came FROM. */
    {
        unsigned char rec[256];
        FILE *rf;
        int got;

        /* The record right now holds the Q save: sector 20, entry (30,40). Every
         * assertion below uses a value that differs from that, or the test would pass
         * vacuously on a save_body that wrote nothing at all. */

        /* A state Q refuses on two of its three guards: indoors, and aboard a vehicle.
         * The auto-save is NOT gated on those two axes, so it must still write here.
         * It IS gated on Earth, because that gate lives in the store (asm/22FD) and not
         * in Q — that half is asserted below with world = 0x09. This half has to hold
         * world at 0, or the two properties cannot be told apart by the test. (An earlier
         * version of this block set world to 0x0A deep space and asserted the write still
         * happened, which is the claim captures/spice86/g82-planet-x-live refuted.) */
        u2_overworld bad = ow;
        bad.sector_lo = 2;
        bad.x = 34;
        bad.y = 20;
        a.state.world = 0;
        a.state.player_stamp = 0x28;          /* rocket */
        ok(u2_overworld_save(&a, &bad, &U2_SAVE_POLICY_ORIGINAL) == 1,
           "Q still refuses that state (its guards are unchanged)");

        u2_vram_init(&a.vram);
        ok(u2_overworld_autosave(&a, &bad) == 0,
           "the auto-save is not gated on location or vehicle the way Q is");
        ok(!vram_contains(&a, U2_S_Q_MOMENT) && !vram_contains(&a, U2_S_Q_PROMPT),
           "the auto-save prints nothing (asm/38B0 has no str_out before the call)");

        rf = fopen(saves_path, "rb");
        got = rf && fread(rec, 1, 256, rf) == 256;
        if (rf) fclose(rf);
        ok(got && rec[0x14] == 2,
           "the auto-save wrote the sector it was handed, which Q refused to save");
        ok(got && rec[0x24] == 34 && rec[0x25] == 20,
           "and the return cell, so the caller's state is what gets checkpointed");

        a.state.player_stamp = 0x78;

        /* Ordering, the load-bearing part. site_enter() calls this BEFORE swapping the
         * sector digit, so the record holds the outdoor sector. Get it the other way and
         * dying inside a town stops returning the player outside. The assertion above is
         * what pins the mechanism: whatever sector the caller is in at the call is what
         * lands on disk, so calling after the swap would store the site instead. */
        u2_overworld out = ow;
        out.sector_lo = 0;
        out.x = 12;
        out.y = 13;
        ok(u2_overworld_autosave(&a, &out) == 0, "auto-save on site entry writes");
        rf = fopen(saves_path, "rb");
        got = rf && fread(rec, 1, 256, rf) == 256;
        if (rf) fclose(rf);
        ok(got && rec[0x24] == 12 && rec[0x25] == 13,
           "and the caller's cell, so the outdoor state is what gets checkpointed");

        /* Off Earth the store is gated out by asm/22FD, so the identical call must write
         * nothing. The control that keeps this assertion able to fail is the one directly
         * above it: same call, same buffers, world byte 0x00, and the record DOES move.
         * Measured on a genuine Planet X in captures/spice86/g82-planet-x-live — ten
         * transitions, 91 files, one-second md5 resolution, zero writes. */
        unsigned char before[256];
        rf = fopen(saves_path, "rb");
        got = rf && fread(before, 1, 256, rf) == 256;
        if (rf) fclose(rf);
        ok(got, "the on-Earth auto-save left a record to compare against");

        a.state.world = 0x09;                 /* Planet X */
        out.x = 77;
        out.y = 78;
        ok(u2_overworld_autosave(&a, &out) == 0,
           "off Earth the auto-save reports no failure — refusing is not an error");
        rf = fopen(saves_path, "rb");
        got = rf && fread(rec, 1, 256, rf) == 256;
        if (rf) fclose(rf);
        ok(got && memcmp(rec, before, 256) == 0,
           "and off Earth it wrote nothing, so the cell stayed at the Earth value");
        a.state.world = 0;
        /* The sector mechanism is proven by the discriminating assertion above (a
         * handed-down 2 lands on disk where the previous record held 0). Asserting 0
         * here too would pass against a mutant that hardcodes 0, so it is left out on
         * purpose rather than padding the count with a check that cannot fail. */
    }

    /* --- the parked-rocket triple round-trips through the record ---------
     *
     * DS:69/6A/6B are the parked-rocket record and they live INSIDE the saved block,
     * which is the only reason a rocket left on a planet survives a restart. The
     * property that matters is not "the value comes back" — the struct is the record,
     * so that is nearly free. It is that the three bytes land at the disk offsets the
     * original uses, because that is what tools/save_cross_load.py depends on and
     * nothing else would notice a drift. */
    {
        unsigned char rec[256];
        FILE *rf;
        int got;

        a.state.rocket_parked   = 1;
        a.state.rocket_parked_x = 0x21;
        a.state.rocket_parked_y = 0x43;
        ok(u2_save_store(&a) == U2_SAVE_OK, "store with a rocket parked");
        rf = fopen(saves_path, "rb");
        got = rf && fread(rec, 1, 256, rf) == 256;
        if (rf) fclose(rf);
        /* Record offset = DS offset - 0x36, so DS:69/6A/6B are 0x33/0x34/0x35. */
        ok(got && rec[0x33] == 1 && rec[0x34] == 0x21 && rec[0x35] == 0x43,
           "the triple sits at record offsets 0x33/0x34/0x35 (DS:69/6A/6B)");

        memset(&a.state, 0, sizeof a.state);
        a.state.rocket_parked = 0;
        ok(u2_save_load(&a) == U2_SAVE_OK, "load it back");
        ok(a.state.rocket_parked == 1 && a.state.rocket_parked_x == 0x21 &&
           a.state.rocket_parked_y == 0x43,
           "and the parked rocket survives a restart, which is what it is for");

        /* The Earth asymmetry, asserted as data: the original CLEARS the flag on a
         * survived Earth dive because Earth's pads are authored into mapx41. A record
         * with the flag clear must load as clear, or the engine would stamp a second
         * rocket onto a map that already has three. */
        a.state.rocket_parked = 0;
        ok(u2_save_store(&a) == U2_SAVE_OK && u2_save_load(&a) == U2_SAVE_OK &&
           a.state.rocket_parked == 0,
           "a cleared flag stays cleared, so Earth does not get a phantom rocket");
    }

    printf("save_q_check: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
