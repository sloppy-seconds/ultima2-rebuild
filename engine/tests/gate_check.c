/*
 * tests/gate_check.c — the time-door spec, as far as it is proven.
 *
 * Every expectation here is a live observation from 2026-10-29 (docs/moongates.md,
 * captures/spice86/moongate-cycle). The point of this file is not only to pin what is
 * known and to REJECT what is not: the pairings the rule derives must match the two that were
 * watched live, and the grid properties the rule implies are asserted rather than tabulated.
 * implementation that quietly invents a destination era for slot 0 or 3 goes red here
 * rather than shipping a door that opens onto a guess.
 *
 * Build and run from engine/:
 *   cc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/gate_check \
 *      tests/gate_check.c src/game/gates.c src/core/state.c && /tmp/gate_check
 */

#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/state.h"
#include "game/gates.h"
#include "game/monsters.h"
#include "game/overworld.h"

static int checks, fails;

/* Link stubs. u2_gate_transit now syncs the creature table and autosaves the outgoing
 * sector before the era digit moves (the G64 save-on-transit fix). This harness links
 * gates.c + world.c + roots.c + slot.c + state.c only -- not monsters.c or overworld.c --
 * and every transit leg here returns before that autosave (the refusals at the two
 * early returns), so these two symbols are never called. They exist to satisfy the
 * linker, and the sector-write behaviour they stand in for is gated end-to-end by
 * tests/gate_transit_save.py, not here. */
void u2_monsters_store(u2_ctx *ctx, u2_world *w) { (void)ctx; (void)w; }
int u2_overworld_autosave(u2_ctx *ctx, u2_overworld *ow) { (void)ctx; (void)ow; return 0; }

#define CHECK(cond, ...) do {                                  \
        ++checks;                                              \
        if (cond) { printf("  ok    "); }                      \
        else { ++fails; printf("  FAIL  "); }                  \
        printf(__VA_ARGS__);                                   \
        printf("\n");                                          \
    } while (0)

/* The 20 table cells, read off original/ultimaii.exe at file 0x2998 and confirmed
 * against 12 live gate cells across eras 1, 2 and 3. */
static const struct { int era, slot, x, y; } k_table[] = {
    { 0, 0, 29, 56 }, { 0, 1, 31, 56 }, { 0, 2, 33, 56 }, { 0, 3, 35, 56 },
    { 1, 0, 34,  8 }, { 1, 1, 47, 28 }, { 1, 2, 36, 56 }, { 1, 3, 20, 37 },
    { 2, 0, 16, 16 }, { 2, 1, 48, 24 }, { 2, 2, 42, 24 }, { 2, 3, 19, 52 },
    { 3, 0, 56, 52 }, { 3, 1, 28, 12 }, { 3, 2, 33, 23 }, { 3, 3, 20, 52 },
    { 4, 0, 18, 23 }, { 4, 1, 50, 34 }, { 4, 2, 52, 24 }, { 4, 3,  8, 11 },
};

int main(int argc, char **argv)
{
    /* --- the table ------------------------------------------------------- */
    for (size_t i = 0; i < sizeof k_table / sizeof k_table[0]; ++i) {
        int x = -1, y = -1;
        const int got = u2_gate_cell(k_table[i].era, k_table[i].slot, &x, &y);
        CHECK(got == 1 && x == k_table[i].x && y == k_table[i].y,
              "gate cell era%d slot%d = (%d,%d), got (%d,%d)",
              k_table[i].era, k_table[i].slot, k_table[i].x, k_table[i].y, x, y);
    }
    CHECK(u2_gate_cell(5, 0, NULL, NULL) == 0, "era 5 is out of range");
    CHECK(u2_gate_cell(2, 4, NULL, NULL) == 0, "slot 4 is out of range");

    /* --- the observed pairings ------------------------------------------- */
    CHECK(u2_gate_partner_era(1, 1) == 2, "era1 slot1 -> era2 (observed both ways)");
    CHECK(u2_gate_partner_era(2, 1) == 1, "era2 slot1 -> era1 (observed both ways)");
    CHECK(u2_gate_partner_era(2, 2) == 3, "era2 slot2 -> era3 (observed both ways)");
    CHECK(u2_gate_partner_era(3, 2) == 2, "era3 slot2 -> era2 (observed both ways)");

    /* The control that makes this spec able to fail in the place that matters. Slots 0
     * and 3 were never transited; a partner here is an invention, not a finding. */
    /* The two slots that had never been watched, now derived from asm/26B7+26C3+26C6 rather
     * than observed. The rule is `target = k if k < era else k + 1`, so gate k is the two-way
     * link between era k and era k+1. Asserted as a property over the whole grid, not as five
     * more rows of expected values, because the property is what the Codex independently agrees
     * with and a wrong rule shows up somewhere in it even if a table is copied correctly. */
    CHECK(u2_gate_partner_era(2, 0) == 0, "era2 slot0 -> era0 (derived, never watched)");
    CHECK(u2_gate_partner_era(2, 3) == 4, "era2 slot3 -> era4 (derived, never watched)");
    for (int era = 0; era < 5; ++era) {
        int seen[5] = { 0, 0, 0, 0, 0 };
        for (int slot = 0; slot < U2_GATE_SLOTS; ++slot)
            seen[u2_gate_partner_era(era, slot)]++;
        int ok = (seen[era] == 0);
        for (int e = 0; e < 5; ++e)
            if (e != era) ok = ok && seen[e] == 1;
        CHECK(ok, "era%d's four doors reach the other four eras exactly once", era);
    }
    for (int k = 0; k < U2_GATE_SLOTS; ++k)
        CHECK(u2_gate_partner_era(k, k) == k + 1 && u2_gate_partner_era(k + 1, k) == k,
              "slot%d is the two-way link between era%d and era%d", k, k, k + 1);
    /* And nothing else round-trips. Four adjacent-era pairs, and each pair makes the round trip
     * from both ends, so the leg count is eight and not four -- the first draft of this check
     * said four and was wrong about its own arithmetic, not about the rule. */
    int round = 0;
    for (int era = 0; era < 5; ++era)
        for (int slot = 0; slot < U2_GATE_SLOTS; ++slot) {
            int p = u2_gate_partner_era(era, slot);
            if (u2_gate_partner_era(p, slot) == era) round++;
        }
    CHECK(round == 8, "only the four adjacent-era pairs round-trip, from both ends = 8 legs, "
                      "got %d", round);

    /* --- index to slot ---------------------------------------------------- */
    CHECK(u2_gate_slot(0x00) == 0, "index 00 -> slot 0");
    CHECK(u2_gate_slot(0x02) == 1, "index 02 -> slot 1");
    CHECK(u2_gate_slot(0x04) == 2, "index 04 -> slot 2");
    CHECK(u2_gate_slot(0x06) == 3, "index 06 -> slot 3");
    CHECK(u2_gate_slot(0x08) == 0, "index 08 masks to 0, i.e. `and $0x7`");

    /* --- the rotation ------------------------------------------------------ */
    u2_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.state.gate_index = 0;
    ctx.state.gate_countdown = U2_GATE_COUNTDOWN_RELOAD;
    for (int i = 0; i < U2_GATE_COUNTDOWN_RELOAD - 1; ++i)
        u2_gate_turn(&ctx, NULL);
    CHECK(ctx.state.gate_index == 0, "index has not moved while the countdown runs");
    u2_gate_turn(&ctx, NULL);                        /* the expiry */
    CHECK(ctx.state.gate_countdown == U2_GATE_COUNTDOWN_RELOAD,
          "countdown reloads to 8 on expiry, got %u", ctx.state.gate_countdown);
    CHECK(ctx.state.gate_index == 2, "index advances by 2 on expiry, got %u",
          ctx.state.gate_index);
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (int i = 0; i < U2_GATE_COUNTDOWN_RELOAD; ++i)
            u2_gate_turn(&ctx, NULL);
    }
    CHECK(ctx.state.gate_index == 0, "index wraps 06 -> 00 after four rotations, got %u",
          ctx.state.gate_index);

    /* --- a cold load does NOT get a door for ~256 turns --------------------
     * asm/2701 is `decb 0x6f ; je`, so a byte that starts at zero walks ff, fe, fd ... and
     * only rotates when it comes back round to zero. Observed directly:
     * captures/spice86/g73-6f-unit found DS:6F = 0x00 at load and 0xfe moments later, with
     * DS:6E fixed at 00 for the whole session. The previous `countdown <= 1` form rotated on
     * turn 1, which is a door the original has not made yet -- the kind of difference a bot
     * built against the engine would never see and would fail on against the game. */
    memset(&ctx, 0, sizeof ctx);
    ctx.state.sector_first = 2;
    ctx.state.gate_countdown = U2_GATE_COUNTDOWN_AT_LOAD;
    ctx.state.gate_index = 0;
    static u2_overworld ow_cold;          /* static: it carries tiles and frame buffers */
    memset(&ow_cold, 0, sizeof ow_cold);
    u2_gate_turn(&ctx, &ow_cold);
    CHECK(ctx.state.gate_countdown == 0xff,
          "the first tick from a cold load wraps to 0xff, got %u", ctx.state.gate_countdown);
    CHECK(ctx.state.gate_index == 0 && !ow_cold.gate_stamped,
          "and nothing rotates and no door appears on that tick");
    int cx = 0, cy = 0;
    CHECK(!u2_gate_current_cell(&ctx, &ow_cold, &cx, &cy),
          "a cold load has no door to step on");
    for (int i = 0; i < 255; ++i)          /* 1 already spent above, 256 total */
        u2_gate_turn(&ctx, &ow_cold);
    CHECK(ctx.state.gate_index == 2 && ctx.state.gate_countdown == U2_GATE_COUNTDOWN_RELOAD,
          "the first rotation lands on turn 256, got index %u cd %u",
          ctx.state.gate_index, ctx.state.gate_countdown);
    CHECK(ow_cold.gate_stamped, "and that rotation is what stamps the door");
    /* Control on the count itself: one turn short must not have rotated, one turn more must
     * be back to a plain 8-turn cycle. Without this the 256 could be an off-by-one that
     * still looks like a long wait. */
    memset(&ctx, 0, sizeof ctx);
    ctx.state.sector_first = 2;
    ctx.state.gate_countdown = U2_GATE_COUNTDOWN_AT_LOAD;
    for (int i = 0; i < 255; ++i)
        u2_gate_turn(&ctx, NULL);
    CHECK(ctx.state.gate_index == 0, "255 turns from cold is not yet a rotation, got %u",
          ctx.state.gate_index);
    u2_gate_turn(&ctx, NULL);
    CHECK(ctx.state.gate_index == 2, "256 is, got %u", ctx.state.gate_index);
    for (int i = 0; i < 8; ++i)
        u2_gate_turn(&ctx, NULL);
    CHECK(ctx.state.gate_index == 4,
          "and from there it is the ordinary 8-turn cycle, got %u", ctx.state.gate_index);


    /* --- a settlement has no doors, and no clock ---------------------------
     * Measured 2026-11-02 inside Port Boniface (sector 31, ones digit 1) with the rotation
     * armed: the town grid held no 0xC0 at all, and DS:6F sat unchanged across six moves
     * where the same poke in sector 30 rotated the slot within four. Two mechanisms, and
     * each is asserted on its own so neither can be carried by the other.
     * captures/spice86/s31-south-edge/. */
    static u2_overworld ow_site;          /* static: it carries tiles and frame buffers */
    memset(&ow_site, 0, sizeof ow_site);
    ow_site.gate_stamped = 1;             /* armed: on the world map this WOULD stamp */
    memset(&ctx, 0, sizeof ctx);
    ctx.state.sector_first = 3;
    ctx.state.sector_second = 1;          /* a village sector */
    ctx.state.gate_index = 4;             /* slot 2, the live slot in the capture */
    ctx.state.gate_countdown = 1;
    int sgx = -1, sgy = -1;
    CHECK(!u2_gate_current_cell(&ctx, &ow_site, &sgx, &sgy),
          "inside a settlement the armed door answers no cell");
    /* The control that makes the refusal above mean something: identical state on the
     * world map does stamp, so what refused is the site test and not the stamp flag,
     * the table, or the slot arithmetic. */
    ctx.state.sector_second = 0;
    sgx = sgy = -1;
    CHECK(u2_gate_current_cell(&ctx, &ow_site, &sgx, &sgy) && sgx == 33 && sgy == 23,
          "the same state on the world map stamps (33,23), got (%d,%d)", sgx, sgy);
    /* And the clock, with the same shape of control. */
    ctx.state.sector_second = 1;
    ctx.state.gate_countdown = 1;
    for (int i = 0; i < 6; ++i)
        u2_gate_turn(&ctx, &ow_site);
    CHECK(ctx.state.gate_index == 4 && ctx.state.gate_countdown == 1,
          "the countdown does not advance inside a settlement (index %u, cd %u)",
          ctx.state.gate_index, ctx.state.gate_countdown);
    ctx.state.sector_second = 0;
    u2_gate_turn(&ctx, &ow_site);         /* one tick on the world map, cd was 1 */
    CHECK(ctx.state.gate_index == 6,
          "the same tick on the world map does rotate, got index %u", ctx.state.gate_index);

    /* --- a planet has no doors and no clock --------------------------------
     * `asm/26F9`, the only caller of the countdown decrement, opens `mov 0x6d,%al ;
     * cmp $0x0,%al ; je`, so the clock is stopped outright off-Earth. The reason this is
     * not merely a missing animation is that the era row is indexed with the sector's tens
     * digit, and off-Earth that digit is the WORLD index: Mercury arriving in sector 10
     * would otherwise be read as era 1 and get Pangea's doors. Every other leg in this file
     * runs on Earth, so none of them could see it. */
    static u2_overworld ow_planet;        /* static: it carries tiles and frame buffers */
    memset(&ow_planet, 0, sizeof ow_planet);
    ow_planet.gate_stamped = 1;           /* armed: on Earth this WOULD stamp */
    memset(&ctx, 0, sizeof ctx);
    ctx.state.world = 1;                  /* Mercury */
    ctx.state.sector_first = 1;           /* sector 10, which is world 1, not era 1 */
    ctx.state.sector_second = 0;          /* outdoors, so the site test cannot be what refuses */
    ctx.state.gate_index = 4;             /* slot 2 */
    ctx.state.gate_countdown = 1;
    int pgx = -1, pgy = -1;
    CHECK(!u2_gate_current_cell(&ctx, &ow_planet, &pgx, &pgy),
          "off-Earth the armed door answers no cell");
    /* Control, same shape as the settlement one: identical state with the world byte at 0
     * does stamp, so what refused is the world test and not the stamp flag, the slot
     * arithmetic or the table. Era row 1 slot 2 is (36,56). */
    ctx.state.world = 0;
    pgx = pgy = -1;
    CHECK(u2_gate_current_cell(&ctx, &ow_planet, &pgx, &pgy) && pgx == 36 && pgy == 56,
          "the same state on Earth stamps (36,56), got (%d,%d)", pgx, pgy);
    /* And the clock, with its own control. */
    ctx.state.world = 1;
    ctx.state.gate_countdown = 1;
    for (int i = 0; i < 6; ++i)
        u2_gate_turn(&ctx, &ow_planet);
    CHECK(ctx.state.gate_index == 4 && ctx.state.gate_countdown == 1,
          "the countdown does not advance off-Earth (index %u, cd %u)",
          ctx.state.gate_index, ctx.state.gate_countdown);
    ctx.state.world = 0;
    u2_gate_turn(&ctx, &ow_planet);       /* one tick on Earth, cd was 1 */
    CHECK(ctx.state.gate_index == 6,
          "the same tick on Earth does rotate, got index %u", ctx.state.gate_index);

    /* --- the two transit refusals that need no sector load -----------------
     * Both return before u2_world_load, so they run without a content directory.
     * The positive legs (era2 slot1 <-> era1 slot1, era2 slot2 <-> era3 slot2) are the
     * live observations in docs/moongates.md § 3; reproducing them here would need a
     * sector store, and the table checks above already pin the coordinates they use. */
    static u2_overworld ow;               /* static: it carries tiles and frame buffers */
    memset(&ow, 0, sizeof ow);
    ctx.state.sector_first = 2;
    ctx.state.gate_index = 2;             /* slot 1 */
    ow.x = 48; ow.y = 24;                 /* exactly era2 slot1, the live transit cell */
    CHECK(u2_gate_transit(&ctx, &ow) == 0,
          "no door before the first rotation stamps one (era 0 at load behaves so)");
    ow.gate_stamped = 1;
    ctx.state.gate_index = 0;             /* slot 0: stamped, but never observed a partner */
    ow.x = 16; ow.y = 16;
    /* The two slots the rule now fills. Asserted as a destination rather than as a driven
     * transit: u2_gate_transit ends in u2_world_load, which needs a sector store this harness
     * has none of, and a leg that half-applies an era change before failing to load is not
     * evidence of anything. The pairings themselves are pinned by the grid checks above. */
    ctx.state.gate_index = 0;             /* slot 0, era 2 -> era 0 */
    ow.x = 16; ow.y = 16;
    CHECK(u2_gate_partner_era(2, 0) == 0, "era2 slot0 has a destination now, not a refusal");
    int dx = 0, dy = 0;
    CHECK(u2_gate_cell(0, 0, &dx, &dy) && dx == 29 && dy == 56,
          "era2 slot0 arrives at era0 slot0 = (29,56), got (%d,%d)", dx, dy);
    ctx.state.gate_index = 6;             /* slot 3, era 2 -> era 4 */
    ow.x = 19; ow.y = 52;
    CHECK(u2_gate_partner_era(2, 3) == 4, "era2 slot3 has a destination now, not a refusal");
    CHECK(u2_gate_cell(4, 3, &dx, &dy) && dx == 8 && dy == 11,
          "era2 slot3 arrives at era4 slot3 = (8,11), got (%d,%d)", dx, dy);
    ctx.state.gate_index = 4;             /* slot 2, but the player is not standing on it */
    ow.x = 1; ow.y = 1;
    CHECK(u2_gate_transit(&ctx, &ow) == 0, "no transit unless the player is on the cell");

    /* --- the door's own cell, which is what the renderer stamps --------- */
    int gx = 0, gy = 0;
    ctx.state.sector_first = 2;
    ctx.state.gate_index = 2;                 /* slot 1 */
    ow.gate_stamped = 0;
    CHECK(u2_gate_current_cell(&ctx, &ow, &gx, &gy) == 0, "no cell while unstamped");
    ow.gate_stamped = 1;
    CHECK(u2_gate_current_cell(&ctx, &ow, &gx, &gy) == 1 && gx == 48 && gy == 24,
          "stamped door at era2 slot1 = (48,24), got (%d,%d)", gx, gy);
    ctx.state.gate_index = 4;                 /* slot 2 */
    CHECK(u2_gate_current_cell(&ctx, &ow, &gx, &gy) == 1 && gx == 42 && gy == 24,
          "the rotation moves the door: era2 slot2 = (42,24), got (%d,%d)", gx, gy);
    /* The stamp renders through the engine's own proven mapping: map byte 0xC0 -> staging
     * 0x60 -> art 0x30, which is the moongate record in docs/tile-art-spec.md. */
    CHECK(U2_GATE_STAGING == (uint8_t)((0xC0 >> 1) & 0xFE),
          "staging 0x%02x is what the fill loop makes of map byte 0xC0", U2_GATE_STAGING);
    CHECK(u2_view_art(U2_GATE_STAGING) == 0x30,
          "staging halves to art 0x30, the moongate tile, got 0x%02x",
          u2_view_art(U2_GATE_STAGING));

    /* --- the non-circular check: read the table back out of the executable ------ */
    /* Everything above is the engine agreeing with itself and with a transcription.
     * This half is the engine agreeing with the bytes, and it is the only assertion here
     * that could catch a mistyped coordinate. The executable is gitignored, so this SKIPs
     * in CI and bites on a machine that has the player's own copy — same arrangement as
     * the tile extractor's real-binary half.
     *
     * File offset 0x2998 is the runtime displacement %cs:0x2798 from asm/2708 plus the
     * 0x200 file/runtime offset. Row stride 8 bytes, 4 slots of 2 bytes, era = DS:49. */
    const char *exe_path = argc > 1 ? argv[1] : NULL;
    if (exe_path && exe_path[0]) {
        FILE *f = fopen(exe_path, "rb");
        if (!f) {
            printf("  FAIL  cannot read %s\n", exe_path);
            ++fails;
        } else {
            unsigned char tbl[5 * U2_GATE_ERA_STRIDE];
            if (fseek(f, 0x2998, SEEK_SET) != 0 ||
                fread(tbl, 1, sizeof tbl, f) != sizeof tbl) {
                printf("  FAIL  cannot read 40 bytes at file 0x2998 in %s\n", exe_path);
                ++fails;
            } else {
                fclose(f);
                int bad = 0;
                for (int era = 0; era < 5; ++era) {
                    for (int slot = 0; slot < U2_GATE_SLOTS; ++slot) {
                        int x = -1, y = -1;
                        (void)u2_gate_cell(era, slot, &x, &y);
                        const int bx = tbl[era * U2_GATE_ERA_STRIDE + slot * 2];
                        const int by = tbl[era * U2_GATE_ERA_STRIDE + slot * 2 + 1];
                        if (x != bx || y != by) {
                            printf("  FAIL  era%d slot%d: engine (%d,%d) vs binary (%d,%d)\n",
                                   era, slot, x, y, bx, by);
                            ++bad;
                        }
                    }
                }
                checks += 20;
                fails += bad;
                if (bad == 0)
                    printf("  ok    all 20 gate cells read back from the binary match the engine\n");
                else
                    printf("  FAIL  %d gate cells disagree with the binary\n", bad);
            }
        }
    } else {
        printf("  skip  binary cross-check (no ultimaii.exe passed)\n");
    }

    printf("gate_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
