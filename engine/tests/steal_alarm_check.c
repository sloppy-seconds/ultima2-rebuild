/*
 * tests/steal_alarm_check.c — a failed steal alerts the town one time in eight.
 *
 * asm/3F5D's single refusal exit (L_00417e) prints NO LUCK!, rolls a third time, and when
 * the low three bits of that roll are zero calls asm/113A (u2_ai_alarm), which sets every
 * guard slot's role to 1 for the whole sector. Every failed steal reaches that exit — the
 * near-cell miss (asm/3F59), the merchant-marker miss, both luck rolls, the already-alerted
 * test and every quadrant miss all jump there — so the alarm can fire on an aim miss too.
 *
 * The engine used to print NO LUCK! and return, with the consequence left undecoded. This
 * gate is the red control for wiring it: without the third roll + u2_ai_alarm call in
 * steal_no_luck(), the "alarm fires" case below fails.
 *
 * Build and run from engine/, linking against every source under src/core, src/game and
 * src/debug — the same link line tests/run_cp6.sh uses.
 */

#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/rng.h"
#include "core/state.h"
#include "core/vram.h"
#include "game/commerce.h"
#include "game/overworld.h"
#include "platform/platform.h"
#include "gen_content.h"

static int checks, fails;

static uint64_t null_tick(u2_ctx *ctx) { (void)ctx; return 0; }
static void null_sleep(u2_ctx *ctx, uint32_t ms) { (void)ctx; (void)ms; }
static const u2_clock null_clock = {
    .name = "steal-alarm-null", .tick = null_tick, .sleep_ms = null_sleep,
};

/* The first rand_byte a freshly-seeded ctx produces. On a near-cell miss the steal
 * reaches steal_no_luck() before any luck roll, so this is exactly the third roll the
 * alarm test consumes. */
static uint8_t first_byte(uint8_t seed)
{
    u2_ctx c;
    memset(&c, 0, sizeof c);
    u2_state_init(&c.state);
    u2_rng_seed(&c, seed);
    return u2_rand_byte(&c);
}

/* A town with an empty world: the ray's near cell is 0, which is neither the shop counter
 * nor a sign tile, so u2_commerce_steal falls straight to steal_no_luck(). */
static void arm_steal(u2_ctx *ctx, u2_overworld *ow, uint8_t seed, int in_town)
{
    memset(ctx, 0, sizeof *ctx);
    memset(ow, 0, sizeof *ow);
    u2_vram_init(&ctx->vram);
    u2_state_init(&ctx->state);
    ctx->clock = &null_clock;
    ctx->state.sector_first = 2;
    ctx->state.sector_second = in_town ? 2 : 0;   /* 0 = world map: nobody to alert */
    ctx->state.sector_x = 10;
    ctx->state.sector_y = 10;
    ow->world.map_loaded = 1;
    u2_rng_seed(ctx, seed);
}

static int roles_all_one(u2_ctx *ctx)
{
    for (int s = 0; s <= 7; s++)
        if (ctx->state.mon_role[s] != 1) return 0;
    return 1;
}
static int roles_all_zero(u2_ctx *ctx)
{
    for (int s = 0; s <= 7; s++)
        if (ctx->state.mon_role[s] != 0) return 0;
    return 1;
}

#define CHECK(cond, ...) do {                                  \
        ++checks;                                              \
        if (cond) { printf("  ok    "); }                      \
        else { ++fails; printf("  FAIL  "); }                  \
        printf(__VA_ARGS__);                                   \
        printf("\n");                                          \
    } while (0)

int main(void)
{
    /* Find one seed whose first roll has low three bits zero (alarm) and one that does not
     * (no alarm), so the assertion is driven by the roll, not by a hardcoded seed. */
    int seed_alarm = -1, seed_noalarm = -1;
    for (int s = 0; s < 256; s++) {
        if (seed_alarm < 0 && (first_byte((uint8_t)s) & 0x7u) == 0u) seed_alarm = s;
        if (seed_noalarm < 0 && (first_byte((uint8_t)s) & 0x7u) != 0u) seed_noalarm = s;
        if (seed_alarm >= 0 && seed_noalarm >= 0) break;
    }
    CHECK(seed_alarm >= 0 && seed_noalarm >= 0,
          "found a seed for each arm (alarm seed %d, safe seed %d)", seed_alarm, seed_noalarm);

    /* --- alarm arm: failed steal, third roll low3 == 0 -> every guard slot goes to 1 --- */
    {
        u2_ctx ctx; u2_overworld ow;
        arm_steal(&ctx, &ow, (uint8_t)seed_alarm, 1);
        u2_commerce_steal(&ctx, &ow, 1, 0);
        CHECK(roles_all_one(&ctx),
              "failed steal with (roll & 7)==0 alerts the town (mon_role[0..7] all 1)");
    }

    /* --- safe arm: failed steal, third roll low3 != 0 -> no alarm --- */
    {
        u2_ctx ctx; u2_overworld ow;
        arm_steal(&ctx, &ow, (uint8_t)seed_noalarm, 1);
        u2_commerce_steal(&ctx, &ow, 1, 0);
        CHECK(roles_all_zero(&ctx),
              "failed steal with (roll & 7)!=0 leaves the town unalerted");
    }

    /* --- control: on the world map (sector_second==0) even a low3==0 roll alerts nobody --- */
    {
        u2_ctx ctx; u2_overworld ow;
        arm_steal(&ctx, &ow, (uint8_t)seed_alarm, 0);
        u2_commerce_steal(&ctx, &ow, 1, 0);
        CHECK(roles_all_zero(&ctx),
              "world-map guard: asm/113A alerts nobody when sector_second==0");
    }

    printf("steal_alarm_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
