/*
 * tests/trap_check.c — the dungeon trap's two arms, decided by the TOOLS counter.
 *
 * asm/277B runtime 0x28B1. On a successful ADVANCE the game rolls one rand against
 * the FLOOR byte [DS:25]; on `jb` a trap fires. The trap then:
 *   1. zeroes [DS:26] (the torch goes out FIRST, before anything else),
 *   2. prints 'ARGH! A TRAP!',
 *   3. reads [DS:66] (TOOLS):
 *        == 0  ->  jmp L_000e62, the DEATH handler (asm/0C62): HP/food/exp/gold
 *                  zeroed and ' IS DEAD!'. There is NO damage amount — a trap with
 *                  no tools is instant death. (Corrected 2026-11-13; the header
 *                  earlier called this "the damage tail, amount undecoded", which
 *                  was a misread of the jump target.)
 *        != 0  ->  prints 'ESCAPED! BY USE OF TOOLS!' and spends one packed-decimal
 *                  tool. No damage.
 *
 * This is a unit gate rather than a CP7 headless case because the trap roll is the
 * first rand_byte in u2_dungeon_advance (probe and dg_step read cells, not the RNG),
 * so seeding rng_state and setting the floor makes the trap fire deterministically —
 * something the headless run cannot promise, because contact rolls during a real walk
 * shift the stream under the roll.
 *
 * Build and run by tests/run_cp7.sh.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/state.h"
#include "core/rng.h"
#include "game/dungeon.h"
#include "game/overworld.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* Build a minimal dungeon context: the player at (5,5) facing east on a floor whose
 * cells are all open (a memset world reads 0x00 = floor), so dg_step always succeeds
 * and the trap roll is reached. rng_state all-zero makes the first rand_byte return 1,
 * and site_floor=2 puts that roll under the trap threshold. */
static void setup(u2_ctx *ctx, u2_overworld *ow, uint8_t tools)
{
    memset(ctx, 0, sizeof *ctx);
    memset(ow, 0, sizeof *ow);
    ow->x = 5; ow->y = 5;
    ctx->state.face_dx = 1;          /* facing east */
    ctx->state.face_dy = 0;
    ctx->state.site_floor = 2;       /* first rand (1) < 2 -> trap fires */
    ctx->state.tools = tools;
    ctx->state.hp = 0x50;            /* nonzero, so death is observable as a change */
    ctx->state.unk_52 = 0x50;
    for (int i = 0; i < 6; i++) ctx->state.rng_state[i] = 0;
    ctx->rng_rolls = 0;
}

int main(void)
{
    /* --- arm 1: no tools -> death ------------------------------------------ */
    {
        u2_ctx ctx; u2_overworld ow;
        setup(&ctx, &ow, 0);
        u2_dungeon_advance(&ctx, &ow);
        CHECK(ctx.state.hp == 0,
              "no-tools trap zeroes HP (death), got 0x%02x", ctx.state.hp);
        CHECK(ctx.death_pending == 1,
              "no-tools trap sets death_pending (reaches the death handler)");
        CHECK(ctx.state.tools == 0,
              "no-tools trap spends nothing (no tool to spend), got 0x%02x",
              ctx.state.tools);
    }

    /* --- arm 2: has tools -> escape, one tool spent, no death -------------- */
    {
        u2_ctx ctx; u2_overworld ow;
        setup(&ctx, &ow, 0x03);
        u2_dungeon_advance(&ctx, &ow);
        CHECK(ctx.state.hp == 0x50,
              "with-tools trap does NOT damage HP, got 0x%02x", ctx.state.hp);
        CHECK(ctx.death_pending == 0,
              "with-tools trap does not kill");
        CHECK(ctx.state.tools == 0x02,
              "with-tools trap spends exactly one packed-decimal tool (0x03->0x02), got 0x%02x",
              ctx.state.tools);
    }

    /* --- control: floor 0 has no traps (roll never fires) ------------------ */
    {
        u2_ctx ctx; u2_overworld ow;
        setup(&ctx, &ow, 0);
        ctx.state.site_floor = 0;    /* rand(1) >= 0 -> no trap */
        u2_dungeon_advance(&ctx, &ow);
        CHECK(ctx.state.hp == 0x50,
              "control: floor 0 has no traps, HP untouched, got 0x%02x", ctx.state.hp);
        CHECK(ctx.death_pending == 0,
              "control: floor 0 does not kill");
    }

    printf("trap_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
