/*
 * tests/passwall_check.c — asm/363B cmd_C slot 4: PASSWALL clears the cell you face, and
 * the cell it cleared is WALKABLE.
 *
 * Why this exists when cmd_C is "implemented and filmed": the CP7 headless cases and
 * captures/spice86/magic-casts-2026-11-15 prove the arm runs and the cell byte changes.
 * Neither proves the thing a player cares about, which is that you can then walk through
 * the hole. That is a different assertion, and it can break without any of those failing:
 * the walk gate is `(c & 0x8f) != 0` — bit 7 is rock, the LOW NIBBLE is a stamp in the way
 * — so a PASSWALL write that left any low bit set would clear the wall on screen and still
 * print -BLOCKED- when you tried to use it. The bytes write an immediate 0 for exactly
 * that reason, and `mov $0x0,%al` is not the same choice as `and $0x7f`.
 *
 * The second half is the reason this file is not just one CHECK. The original's grid is
 * STAMPED: a creature or object in a cell puts its index in the low nibble, and the stamp
 * pass runs over the grid before it draws. docs/outstanding.md § 2 carries that as an open
 * divergence — the engine draws creatures from the arrays and never stamps, so a cleared
 * cell stays cleanly 0 here, and in the original a stamp landing back in the cleared cell
 * would make it BLOCKED, because the low nibble is a movement test and not decoration.
 * Case 4 pins the half we DO model: a non-zero low nibble blocks, whatever the high bits
 * say. If that ever starts passing, the mask has changed and the PASSWALL row needs
 * re-reading, not just this test.
 *
 * Build and run by tests/run_cp7.sh.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/state.h"
#include "core/world.h"
#include "game/commands.h"
#include "game/dungeon.h"
#include "game/overworld.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* A Fighter standing at (5,5) facing east in a dungeon, with PASSWALL readied and one
 * charge, and the cell ahead set to whatever the case needs. Everything else is rock,
 * because a dungeon that is all rock is the cheapest fixture in which "can I walk east"
 * has exactly one answer. */
static void arm(u2_ctx *ctx, u2_overworld *ow, uint8_t ahead)
{
    memset(ctx, 0, sizeof *ctx);
    memset(ow, 0, sizeof *ow);
    u2_vram_init(&ctx->vram);
    u2_state_init(&ctx->state);

    ctx->state.sector_second = 5;                 /* a dungeon; cmd_C's gate is >= 4 */
    ctx->state.site_floor = 0;
    ctx->state.items_held[U2_ITEM_WAND] = 1;
    ctx->state.spell_pick = 4;                    /* PASSWALL */
    ctx->state.spell_counts[4] = 1;
    ctx->state.face_dx = 1;
    ctx->state.face_dy = 0;
    ctx->state.sector_x = 5;
    ctx->state.sector_y = 5;

    u2_world *w = &ow->world;
    for (int i = 0; i < U2_SECTOR_TILES; i++) w->map[i] = 0x80;
    u2_world_dungeon_put(w, 0, 5, 5, 0x00);       /* where we stand */
    u2_world_dungeon_put(w, 0, 6, 5, ahead);      /* the cell ahead */
    u2_dungeon_probe(ctx, ow);
}

int main(void)
{
    u2_ctx ctx;
    u2_overworld ow;

    /* ---- 1. rock ahead: it clears, and you walk through it ---------------------- */
    arm(&ctx, &ow, 0x80);
    CHECK(u2_world_dungeon_at(&ow.world, 0, 6, 5) == 0x80,
          "fixture: the cell ahead starts as solid rock");
    CHECK(u2_cmd_dispatch(&ctx, &ow, 'C') == U2_CMD_DONE, "PASSWALL runs");
    CHECK(u2_world_dungeon_at(&ow.world, 0, 6, 5) == 0x00,
          "the cleared cell is an immediate 0, not rock-with-the-bit-cleared (got 0x%02x)",
          u2_world_dungeon_at(&ow.world, 0, 6, 5));
    CHECK(ctx.state.spell_counts[4] == 0, "and the charge was spent");

    const int x_before = ctx.state.sector_x;
    (void)u2_dungeon_advance(&ctx, &ow);
    CHECK(ctx.state.sector_x == x_before + 1,
          "and you can WALK through it: x went %d -> %d, expected %d",
          x_before, ctx.state.sector_x, x_before + 1);

    /* ---- 2. control: open floor refuses, and still costs the spell -------------- */
    arm(&ctx, &ow, 0x00);
    (void)u2_cmd_dispatch(&ctx, &ow, 'C');
    CHECK(ctx.state.spell_counts[4] == 0,
          "control: casting at open floor still spends, because the spend precedes the gate");
    CHECK(u2_world_dungeon_at(&ow.world, 0, 6, 5) == 0x00,
          "control: and it cleared nothing, because there was nothing to clear");

    /* ---- 3. a door clears too (0xC0 passes the same bit-7 test) ----------------- */
    arm(&ctx, &ow, 0xC0);
    (void)u2_cmd_dispatch(&ctx, &ow, 'C');
    CHECK(u2_world_dungeon_at(&ow.world, 0, 6, 5) == 0x00,
          "a doorway (0xC0) clears the same way — the test is bit 7, not == 0x80");

    /* ---- 4. the other half of the mask: a stamp blocks, whatever the rock says --- */
    arm(&ctx, &ow, 0x01);
    const int x_stamped = ctx.state.sector_x;
    (void)u2_dungeon_advance(&ctx, &ow);
    CHECK(ctx.state.sector_x == x_stamped,
          "a low-nibble stamp alone blocks the step (x stayed %d, went to %d) — this is "
          "why a stamp landing back in a PASSWALL-cleared cell would matter",
          x_stamped, ctx.state.sector_x);

    arm(&ctx, &ow, 0x81);
    const int x_both = ctx.state.sector_x;
    (void)u2_dungeon_advance(&ctx, &ow);
    CHECK(ctx.state.sector_x == x_both, "and rock-with-a-stamp blocks the same way");

    printf("passwall_check: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
