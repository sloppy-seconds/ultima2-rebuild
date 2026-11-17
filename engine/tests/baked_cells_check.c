/* baked_cells_check.c — the map write-back must be able to say what it is baking.
 *
 * The symptom this exists for: a player starts a brand new character and walks past
 * monsters that are not there. They are real bytes. The engine writes its stamped grid
 * into the save slot's overlay — the original does too, asm/2305 writes the buffer at
 * [DS:472] — and the overlay is read back ahead of content, so a stamp left behind by a
 * creature that has moved on becomes authored ground on the next load and is permanent.
 *
 * Measured on the same sector, 2026-11-17:
 *   mapx20 written by the original   2 cells differ from the release map, 2 under a
 *                                    live creature, 0 bare
 *   mapx20 written by the engine    12 cells differ, 3 under a live creature, 9 bare
 *
 * So the property the original has and the engine does not is: at the moment of the save,
 * no cell differs from the ground with nothing standing on it. This gate does NOT enforce
 * that on the write — an earlier attempt substituted `map_base` for such cells and the CP4
 * ghost gate caught it, because the release maps ship with creature art baked into the
 * spawn cells, so `map_base` is not a clean ground and the substitution restored one
 * ghost with another. The producer is where that fix belongs. What this gate enforces is
 * the precondition for ever fixing it: that the save path can see the class at all.
 * `--dump-state`'s `ghosts` is measured on a live frame and reported 0 on the save above.
 */
#include <stdio.h>
#include <string.h>
#include "core/ctx.h"
#include "core/world.h"
#include "core/slot.h"
#include "game/monsters.h"
#include "game/ai.h"

#define EDGE U2_SECTOR_EDGE
#define GRASS 0x08
#define NSLOT 4

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static const struct { int x, y; uint8_t type; } CREW[NSLOT] = {
    { 10, 10, 0x30 }, { 12, 14, 0x34 }, { 20, 20, 0xFC }, { 30,  8, 0x2C },
};

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : ".";
    char p[512], savedir[400];
    static uint8_t ground[U2_SECTOR_TILES];
    static uint8_t mon[U2_MON_TILES];
    u2_ctx ctx;
    u2_world w;
    u2_sector_id id = { 3, 1 };

    memset(&ctx, 0, sizeof ctx);
    ctx.data_dir = base;
    snprintf(savedir, sizeof savedir, "%s/saves", base);
    ctx.save_dir = savedir;
    memset(&w, 0, sizeof w);

    /* An open grass sector, so any non-grass byte is provably a stamp. Fixture is
     * engine-authored filler, same rule as monsters_check.c and world_check.c. */
    for (int i = 0; i < U2_SECTOR_TILES; i++) ground[i] = GRASS;
    snprintf(p, sizeof p, "%s/mapx31", base);
    FILE *f = fopen(p, "wb");
    if (!f) { printf("  FAIL  cannot write %s\n", p); return 1; }
    fwrite(ground, 1, sizeof ground, f); fclose(f);

    memset(mon, 0, sizeof mon);
    for (int i = 0; i < NSLOT; i++) {
        mon[0 * U2_MON_SLOTS + i] = (uint8_t)CREW[i].x;
        mon[1 * U2_MON_SLOTS + i] = (uint8_t)CREW[i].y;
        mon[2 * U2_MON_SLOTS + i] = 20;
        mon[3 * U2_MON_SLOTS + i] = CREW[i].type;
        mon[4 * U2_MON_SLOTS + i] = GRASS;   /* the terrain saved under each spawn cell */
    }
    snprintf(p, sizeof p, "%s/monx31", base);
    f = fopen(p, "wb");
    if (!f) { printf("  FAIL  cannot write %s\n", p); return 1; }
    fwrite(mon, 1, sizeof mon, f); fclose(f);

    if (u2_world_open(&ctx) != U2_WORLD_OK) { printf("  FAIL  content dir did not open\n"); return 1; }
    if (u2_world_load(&ctx, &w, 0, id) != U2_WORLD_OK) { printf("  FAIL  mapx load\n"); return 1; }
    if (u2_world_load(&ctx, &w, 1, id) != U2_WORLD_OK) { printf("  FAIL  monx load\n"); return 1; }
    CHECK(u2_monsters_spawn(&ctx, &w) == NSLOT, "fixture: expected %d creatures", NSLOT);
    u2_monsters_clear_spawn_cells(&ctx, &w);
    u2_monsters_restamp(&ctx, &w);

    /* Walk them for a while, then save. A clean producer should report nothing, and this
     * is the half that says so: the move path restores each vacated cell from the slot's
     * saved terrain byte, and with that byte correct nothing is left behind. */
    for (int t = 0; t < 40; t++)
        u2_ai_creature_turn(&ctx, &w, 32, 32);
    ctx.diag_baked_unowned = -1;
    CHECK(u2_world_store(&ctx, &w, 1) == U2_WORLD_OK, "map store failed");
    CHECK(ctx.diag_baked_unowned == 0,
          "the save baked %d cell(s) no creature stands on — in the original's own file "
          "that number is 0", ctx.diag_baked_unowned);

    /* Now plant what a session leak leaves behind: a creature's type byte on a cell
     * nobody occupies, which is the shape of the nine cells found in a real overlay. The
     * store must report them. Control: with the counter removed this goes red, which is
     * the whole point — a gate that cannot see the class cannot ever be fixed. */
    w.map[17 * EDGE + 5] = 0x30;
    w.map[40 * EDGE + 61] = 0xFC;
    ctx.diag_baked_unowned = 0;
    CHECK(u2_world_store(&ctx, &w, 1) == U2_WORLD_OK, "second map store failed");
    CHECK(ctx.diag_baked_unowned >= 2,
          "the write-back did not report two planted footprints (said %d)",
          ctx.diag_baked_unowned);

    /* And a live creature's stamp must NOT be counted: refusing to bake footprints is
     * not the same as refusing to save the world. */
    CHECK(ctx.diag_baked_unowned < NSLOT + 2,
          "the counter is counting live creature stamps as footprints (%d)",
          ctx.diag_baked_unowned);

    printf("%s: %d checks, %d failures\n", fails ? "BAKED-CELLS FAIL" : "baked-cells ok",
           checks, fails);
    return fails ? 1 : 0;
}
