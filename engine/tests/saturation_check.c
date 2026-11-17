/*
 * saturation_check.c — enhanced mode caps its counters; original mode still wraps.
 *
 * The original has no ceiling guard anywhere. asm/2045's `clc; adc $1,%al; daa` turns 99
 * into 00, asm/2DC1 drops the carry out of the gold add — the owner watched a +2 exp on a
 * 9999 character display 0001 — and asm/3C2E's Hotel clerk grind is four chained DAA adds
 * with nothing after them, which is the actual reason the period walkthrough says "stop at
 * 95". The owner ruled on 2026-11-11 that the enhanced game caps instead of rolling over,
 * at every counter: stats, gold, exp, food, and held counts.
 *
 * Fidelity mode keeps the wrap on purpose. It is byte-proven, and the original-fidelity run
 * is the differential; a fidelity build that quietly saturated would stop being one. So
 * every assertion here comes in a pair, and a change that only fixes one mode goes red.
 *
 * The gold and exp cases are end-to-end through u2_combat_attack rather than against the
 * helper, because the reward roll is random and the point is that whatever it rolls, a
 * 9999 purse cannot come back smaller. The counter-level cases are against the shared
 * helper, which is the single form every increment site now goes through.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/state.h"
#include "game/combat.h"

static int checks, fails;

/* No grid here: these tests assert the BCD saturation of gold and exp. A kill restores the
 * corpse's cell only when there is a world to restore it into (asm/2CB5); NULL says so, and
 * monsters_check.c carries the arm that has a real world. */
#define u2_combat_attack(c, slot, out) u2_combat_attack((c), NULL, (slot), (out))
#define CHECK(cond, ...) do {                                        \
        ++checks;                                                    \
        if (cond) { printf("  ok    "); }                            \
        else { ++fails; printf("  FAIL  "); }                        \
        printf(__VA_ARGS__); printf("\n");                           \
    } while (0)

static void arm_kill(u2_ctx *ctx, uint8_t gold_hi, uint8_t gold_lo,
                     uint8_t exp_hi, uint8_t exp_lo)
{
    memset(ctx, 0, sizeof *ctx);
    u2_state_init(&ctx->state);
    ctx->state.mon_hp[3] = 0x05;
    ctx->state.mon_type[3] = 0x68;
    ctx->state.mon_kind[3] = 0x70;
    ctx->state.stat_str = 0x40;
    ctx->state.stat_agi = 0x99;          /* so the swing cannot miss */
    ctx->state.readied_weapon = 1;
    ctx->state.gold_lo = gold_lo;        /* DS:58, the HIGH byte despite the name */
    ctx->state.gold_hi = gold_hi;        /* DS:59 */
    ctx->state.exp_lo = exp_lo;
    ctx->state.exp_hi = exp_hi;
}

int main(void)
{
    /* --- the shared counter helper ---------------------------------------- */
    CHECK(u2_bcd_inc1(0x99) == 0x00, "the raw increment wraps 99 -> 00 (asm/2045)");
    CHECK(u2_bcd_inc_sat(0x99, 0) == 0x00, "fidelity: 99 + 1 stays the wrap, got %02x",
          u2_bcd_inc_sat(0x99, 0));
    CHECK(u2_bcd_inc_sat(0x99, 1) == 0x99, "enhanced: 99 + 1 holds at 99, got %02x",
          u2_bcd_inc_sat(0x99, 1));
    CHECK(u2_bcd_inc_sat(0x50, 1) == 0x51, "the cap does not touch a value below it");
    CHECK(u2_bcd_inc_sat(0x09, 1) == 0x10, "and the nibble carry still works under the cap");
    CHECK(u2_bcd_add_sat(0x99, 0x15, 0) != 0x99, "fidelity: a two-digit add wraps past 99");
    CHECK(u2_bcd_add_sat(0x99, 0x15, 1) == 0x99, "enhanced: a two-digit add holds at 99");

    /* --- gold and exp, end to end through a kill ---------------------------- */
    u2_ctx c;
    u2_attack a;

    arm_kill(&c, 0x99, 0x99, 0x99, 0x99);
    c.fidelity = U2_FIDELITY_ORIGINAL;
    (void)u2_combat_attack(&c, 3, &a);
    CHECK(a.gold >= 1, "the reward rolled (%u gold, %u exp)", (unsigned)a.gold, (unsigned)a.exp);
    CHECK(u2_bcd16(c.state.gold_lo, c.state.gold_hi) < 9999,
          "fidelity: a kill on a 9999-gold character wraps the purse (now %02x%02x)",
          c.state.gold_lo, c.state.gold_hi);
    CHECK(u2_bcd16(c.state.exp_lo, c.state.exp_hi) < 9999,
          "fidelity: and the exp the owner saw go to 0001 still does");

    arm_kill(&c, 0x99, 0x99, 0x99, 0x99);
    c.fidelity = U2_FIDELITY_ENHANCED;
    (void)u2_combat_attack(&c, 3, &a);
    CHECK(c.state.gold_lo == 0x99 && c.state.gold_hi == 0x99,
          "enhanced: 9999 gold stays 9999 (got %02x%02x)",
          c.state.gold_lo, c.state.gold_hi);
    CHECK(c.state.exp_lo == 0x99 && c.state.exp_hi == 0x99,
          "enhanced: 9999 exp stays 9999 (got %02x%02x)",
          c.state.exp_lo, c.state.exp_hi);

    /* Below the ceiling the reward still has to land, or "cap" would be a freeze. The
     * field names are inverted against significance (state.h: gold_lo is DS:58, the HIGH
     * byte), so the expected number is read through u2_bcd16 rather than spelled out. */
    arm_kill(&c, 0x12, 0x34, 0x00, 0x10);
    c.fidelity = U2_FIDELITY_ENHANCED;
    const int purse_was = u2_bcd16(c.state.gold_lo, c.state.gold_hi);
    (void)u2_combat_attack(&c, 3, &a);
    CHECK(u2_bcd16(c.state.gold_lo, c.state.gold_hi) == purse_was + (int)a.gold,
          "enhanced: a purse below the cap is still paid in full (%d + %u -> %d)",
          purse_was, (unsigned)a.gold, u2_bcd16(c.state.gold_lo, c.state.gold_hi));

    printf("saturation_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
