/*
 * tests/commerce_check.c — the TRANSACT selector and the price model, as far as they are
 * proven.
 *
 * This file exists to reject two specific things, both of which were believed here and
 * both of which were wrong:
 *
 *   1. that the price formula cannot produce the numbers the game prints. It can. The two
 *      observations that said otherwise — a grocery quote of "0200" and an axe at 25 gp —
 *      were both misread, and both were re-measured on 2026-10-29 by stopping the CPU at
 *      the printer (asm/17B7) and at the charge (asm/2052) during real purchases
 *      (captures/spice86/g23-food-live, captures/spice86/g23-weapons). The grocery quote is
 *      0020 and the axe is 0020, and asm/20AB yields both.
 *   2. that the shop is chosen by which NPC you talked to. It is not: asm/1590 switches on
 *      the PLAYER's quadrant within the sector and on the site kind. Eight cells, and a
 *      wrong one of these is a shop that opens in the wrong half of town.
 *
 * Build and run from engine/, linking this file against every source under src/core,
 * src/game and src/debug — the same link line tests/run_cp6.sh uses.
 */

#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/state.h"
#include "core/vram.h"
#include "game/commerce.h"
#include "game/monsters.h"
#include "game/overworld.h"
#include "platform/platform.h"
#include "gen_content.h"

static int checks, fails;

/* read_digit goes through seam_key_poll, so an offer test needs an input backend. One
 * digit of hundreds is the whole script: the amount is one key, and the second poll
 * reports the input finished so nothing downstream can block on a human. */
static int offer_digit_left = 1;

static int offer_poll(u2_ctx *ctx, u2_key *out)
{
    (void)ctx;
    if (offer_digit_left <= 0) return -1;
    offer_digit_left = 0;
    memset(out, 0, sizeof *out);
    out->ch = '5';
    return 1;
}

/* wait is not optional. seam_key_wait calls input->wait, and the read_digit path behind
 * an offer goes through it, so a backend that only fills .poll is a call through a NULL
 * pointer -- which is exactly how this test first failed, at address 0x0. The scripted
 * answer is the same either way: the key is already in the queue. */
static int offer_wait(u2_ctx *ctx, u2_key *out) { return offer_poll(ctx, out); }

static const u2_input offer_input = {
    .name = "commerce-check-script", .poll = offer_poll, .wait = offer_wait,
};

/* The amount prompt waits on the screen between polls, and the wait goes through the
 * clock seam. The test links the game core and not the platform layer, so it brings its
 * own clock that does nothing: the digit is already in the queue, so a real delay would
 * only slow the run down. */
static uint64_t offer_tick(u2_ctx *ctx) { (void)ctx; return 0; }
static void offer_sleep(u2_ctx *ctx, uint32_t ms) { (void)ctx; (void)ms; }
static const u2_clock offer_clock = {
    .name = "commerce-check-null", .tick = offer_tick, .sleep_ms = offer_sleep,
};

/* Does the needle appear anywhere on the grid? Row and column are not asserted because
 * the placement of a transact line is one of the things asm/4209 grades as low
 * confidence -- the bytes prove the printer runs, not where the text lands. */
static int grid_has(u2_ctx *ctx, const char *needle)
{
    char row[U2_COLS + 1];
    for (int r = 0; r < U2_ROWS; r++) {
        for (int c = 0; c < U2_COLS; c++) row[c] = (char)ctx->vram.cell[r][c].ch;
        row[U2_COLS] = '\0';
        if (strstr(row, needle)) return 1;
    }
    return 0;
}

/* A site with one creature standing one step east of the player, so both rays in
 * u2_commerce_transact and u2_commerce_offer land on it. */
static void arm_offer(u2_ctx *ctx, u2_overworld *ow, uint8_t role, uint8_t blessing,
                      int first, int second)
{
    memset(ctx, 0, sizeof *ctx);
    memset(ow, 0, sizeof *ow);
    u2_vram_init(&ctx->vram);
    u2_state_init(&ctx->state);
    ctx->input = &offer_input;
    ctx->clock = &offer_clock;
    offer_digit_left = 1;
    ctx->state.sector_first = (uint8_t)first;
    ctx->state.sector_second = (uint8_t)second;
    ctx->state.blessing = blessing;
    /* The offer is PAID before the recipient is identified: asm/3C2E stores the digit to
     * DS:27, zeroes DS:28 and calls asm/2052_gold_spend_price, which subtracts the pair from
     * gold and refuses with YOU DONT HAVE THAT MUCH! when the BCD borrow says you are short.
     * A zero-gold fixture therefore never reaches any arm below — it stops at the price check,
     * which is how this fixture silently lost eight checks on 2026-11-11 when the charge was
     * added. Funded at 9999 so the arms under test are reachable; the payment is asserted on
     * its own rather than assumed away here. */
    ctx->state.gold_lo = 0x99;      /* DS:58, the HIGH byte despite the name */
    ctx->state.gold_hi = 0x99;      /* DS:59 */
    ow->x = 10;
    ow->y = 10;
    ow->world.map_loaded = 1;
    ctx->state.mon_type[5] = 0x60;
    ctx->state.mon_x[5] = 11;
    ctx->state.mon_y[5] = 10;
    ctx->state.mon_role[5] = role;
}

#define CHECK(cond, ...) do {                                  \
        ++checks;                                              \
        if (cond) { printf("  ok    "); }                      \
        else { ++fails; printf("  FAIL  "); }                  \
        printf(__VA_ARGS__);                                   \
        printf("\n");                                          \
    } while (0)

/* The maxed-character stat pair every capture in this project has used: int = cha = 0x40,
 * so int + cha = 0x80, bit length 8, and the loop count is exactly the caller's index. */
#define MAX_INT 0x40u
#define MAX_CHA 0x40u

static int price_is(uint8_t index, uint8_t in, uint8_t cha, const char *want)
{
    uint8_t p[2];
    char got[5];
    u2_price_from_stats(index, in, cha, p);
    snprintf(got, sizeof got, "%c%c%c%c", '0' + (p[0] >> 4), '0' + (p[0] & 0x0F),
             '0' + (p[1] >> 4), '0' + (p[1] & 0x0F));
    return strcmp(got, want) == 0;
}

int main(void)
{
    /* --- the price walk ---------------------------------------------------- */

    /* asm/20AB's own reachable sequence, re-derived from the bytes and printed in its
     * header. Index == loop count at maxed stats. It starts at 1, not 0: a loop count of
     * zero does not skip the loop, it wraps (see below), so 0004 is a term of the sequence
     * and not a price the routine can leave behind. */
    static const char *const k_seq[] = {
        "0008", "0012", "0020", "0032", "0052",
        "0084", "0136", "0220", "0356", "0576", "0932"
    };
    for (size_t i = 0; i < sizeof k_seq / sizeof k_seq[0]; ++i)
        CHECK(price_is((uint8_t)(i + 1), MAX_INT, MAX_CHA, k_seq[i]),
              "asm/20AB index %zu on int=cha=0x40 -> %s", i + 1, k_seq[i]);

    /* asm/231E is `dec %di ; jne`, so a loop count of zero runs 65536 times instead of
     * zero. Unreachable from any shop — every caller passes a positive index and the bit
     * length of a byte is at most 8, so DI = index + 8 - bitlen is never zero — and mirrored
     * anyway, which is what this check pins: the wrap happens, and it is not the sequence
     * term. */
    CHECK(!price_is(0, MAX_INT, MAX_CHA, "0004"),
          "index 0 takes asm/231E's 0xFFFF wrap rather than leaving the seed 0004");

    /* The live anchors. Each is a number the game actually printed or actually charged. */
    CHECK(price_is(3, MAX_INT, MAX_CHA, "0020"),
          "grocery quote (asm/17B2 passes AL=3) -> 0020, as asm/17B7 was stopped reading");
    CHECK(price_is(3, MAX_INT, MAX_CHA, "0020"),
          "weapons AX (asm/1F00 passes the menu digit) -> 0020, as asm/20AB left it");
    CHECK(price_is(2, MAX_INT, MAX_CHA, "0012"),
          "armour CLOTH (asm/1DD8 doubles the digit: 1 -> 2) -> 0012, the observed quote");
    CHECK(price_is(4, MAX_INT, MAX_CHA, "0032"),
          "transport horse (asm/1FAD passes AL=4) -> 0032, the observed charge");
    CHECK(price_is(4, MAX_INT, 0x20, "0052"),
          "the same horse at charisma 0x20 -> 0052, which is what proved [DS:4E] is charisma");
    /* The wiki's grocery figure, at the grocery's own index. asm/20AB's header works this
     * example with AL=4 and lands on 0052 by an arithmetic slip — its own table puts 52 at
     * loop count 5, and AL=4 gives 6. The grocery passes the CONSTANT 3 (asm/17B2), which
     * at the wiki's minimum stats of 0x10/0x10 gives bit length 6 and DI = 3+8-6 = 5, and
     * 5 is 52. The third-party number agrees once the caller is the right one. */
    CHECK(price_is(3, 0x10, 0x10, "0052"),
          "grocery (AL=3) at minimum stats 0x10/0x10 -> 0052, the wiki's maximum");

    /* The second pair, DS:29/2A, is the other half of the Fibonacci step and the model
     * predicts it too: after three steps P1 = 20 and P2 = 12. asm/17B7 was stopped with
     * DS:27..2A = 00 20 00 12, so this is a second independent agreement, not a re-statement
     * of the first. commerce.c keeps only P1, which is all the original prints or charges,
     * so the check here is that the walk is right, expressed as: index n+1 is the sum of the
     * two before it in decimal. */
    for (int i = 3; i < 7; ++i) {
        uint8_t a[2], b[2], s[2];
        u2_price_from_stats((uint8_t)(i - 2), MAX_INT, MAX_CHA, a);
        u2_price_from_stats((uint8_t)(i - 1), MAX_INT, MAX_CHA, b);
        u2_price_from_stats((uint8_t)i, MAX_INT, MAX_CHA, s);
        const int av = ((a[0] >> 4) * 10 + (a[0] & 0xF)) * 100 +
                       ((a[1] >> 4) * 10 + (a[1] & 0xF));
        const int bv = ((b[0] >> 4) * 10 + (b[0] & 0xF)) * 100 +
                       ((b[1] >> 4) * 10 + (b[1] & 0xF));
        const int sv = ((s[0] >> 4) * 10 + (s[0] & 0xF)) * 100 +
                       ((s[1] >> 4) * 10 + (s[1] & 0xF));
        CHECK(sv == av + bv, "step %d: %d + %d == %d (the pairs really do walk a sum)",
              i, av, bv, sv);
    }

    /* A naive binary add — the mistake shape — must NOT reproduce the sequence. This is the
     * control that keeps the daa_add comment honest: 4+8 in packed decimal is 0x12, and in
     * binary it is 0x0C, which would print 0012 and 000C as different numbers. */
    CHECK(price_is(2, MAX_INT, MAX_CHA, "0012") && !price_is(2, MAX_INT, MAX_CHA, "000C"),
          "the third term is 12 and not 0x0C: the adjust is load-bearing");

    /* --- the selector ------------------------------------------------------ */

    struct { int x, y, kind; u2_shop want; const char *what; } k_sel[] = {
        { 38,  5, 2, U2_SHOP_ARMOUR,    "sector 32 (38,5): the armour purchase" },
        { 55, 21, 1, U2_SHOP_GROCERY,   "sector 31 (55,21): the grocery purchase" },
        {  9, 10, 2, U2_SHOP_WEAPONS,   "sector 22 (9,10): the weapons purchase" },
        { 48, 53, 2, U2_SHOP_TRANSPORT, "sector 22 (48,53): the transport purchase" },
        { 13, 17, 1, U2_SHOP_CLUE,      "sector 11 ORACLE sign: the clue seller" },
        { 45, 47, 1, U2_SHOP_CLERIC,    "sector 11 CLERGY sign: the cleric shrine" },
        { 13, 57, 2, U2_SHOP_PUB,       "sector 82 PUB sign: the tavern" },
        { 13, 47, 1, U2_SHOP_WIZARD,    "sector 21 WIZARDRY sign: the wizard shoppe" },
        /* The boundary is `cmp $0x20` with `jb`, so 0x20 itself is the east/south side. */
        { 32, 32, 2, U2_SHOP_TRANSPORT, "x=0x20,y=0x20 is the south-east cell, not the north" },
        { 31, 31, 2, U2_SHOP_WEAPONS,   "x=0x1F,y=0x1F is still north-west" },
    };
    for (size_t i = 0; i < sizeof k_sel / sizeof k_sel[0]; ++i) {
        const u2_shop got = u2_commerce_shop_for(k_sel[i].x, k_sel[i].y, k_sel[i].kind);
        CHECK(got == k_sel[i].want, "%s -> shop %d (expected %d)",
              k_sel[i].what, (int)got, (int)k_sel[i].want);
    }

    /* The quadrant is the PLAYER's, not the vendor's. A shop two cells north of a player in
     * the north-west still resolves north-west; that is what makes the sign band transparent
     * rather than accidental. */
    CHECK(u2_commerce_shop_for(38, 5, 2) == u2_commerce_shop_for(38, 3, 2),
          "the ray's target cell does not change the answer; the player's half does");

    /* --- the charge -------------------------------------------------------- */

    u2_ctx ctx;
    memset(&ctx, 0, sizeof ctx);

    /* 9999 - 0012 = 9987, the sector-32 armour purchase, byte for byte. */
    ctx.state.gold_lo = 0x99;   /* DS:58, the HIGH byte despite the name */
    ctx.state.gold_hi = 0x99;   /* DS:59 */
    const uint8_t p12[2] = { 0x00, 0x12 };
    CHECK(u2_spend_price(&ctx, p12) == 1 && ctx.state.gold_lo == 0x99 &&
          ctx.state.gold_hi == 0x87,
          "9999 - 0012 -> 9987, the observed armour charge");

    /* The borrow has to cross the byte boundary in decimal, not in hex: 1000 - 0001 = 0999.
     * A plain binary subtract with a hex borrow would write 0x0F 0x99 and print 09F9. */
    memset(&ctx, 0, sizeof ctx);
    ctx.state.gold_lo = 0x10;
    ctx.state.gold_hi = 0x00;
    const uint8_t p01[2] = { 0x00, 0x01 };
    CHECK(u2_spend_price(&ctx, p01) == 1 && ctx.state.gold_lo == 0x09 &&
          ctx.state.gold_hi == 0x99,
          "1000 - 0001 -> 0999: the borrow is decimal (got %02X%02X)",
          ctx.state.gold_lo, ctx.state.gold_hi);

    /* Cannot afford it: gold is restored and the caller is told, because asm/2052's refusal
     * arm abandons the shop rather than returning to it. */
    memset(&ctx, 0, sizeof ctx);
    ctx.state.gold_lo = 0x00;
    ctx.state.gold_hi = 0x20;      /* 0020 gold */
    const uint8_t p200[2] = { 0x02, 0x00 };
    CHECK(u2_spend_price(&ctx, p200) == 0 && ctx.state.gold_lo == 0x00 &&
          ctx.state.gold_hi == 0x20,
          "0020 - 0200 refuses and leaves gold alone (got %02X%02X)",
          ctx.state.gold_lo, ctx.state.gold_hi);

    /* 0004 - 0005: the underflow asm/2052's header records as briefly visible in simulation
     * and then undone. The engine must not ship that transient as a result. */
    memset(&ctx, 0, sizeof ctx);
    ctx.state.gold_lo = 0x00;
    ctx.state.gold_hi = 0x04;
    const uint8_t p05[2] = { 0x00, 0x05 };
    CHECK(u2_spend_price(&ctx, p05) == 0 && ctx.state.gold_lo == 0x00 &&
          ctx.state.gold_hi == 0x04,
          "0004 - 0005 refuses without leaving 9999 behind (got %02X%02X)",
          ctx.state.gold_lo, ctx.state.gold_hi);

    /* --- asm/3F5D: which category is stealable, and which is not ---------------------
     * The quadrant partition is NOT the shop selector's. The selector fills all eight cells;
     * STEAL fills three. A gate that only checked the three winners could not tell a real
     * table from a default, so the losers are asserted one by one. */
    CHECK(u2_steal_loot_for(0x20, 0x1F, 1) == U2_STEAL_FOOD,
          "village north-east -> food (expected %d)", U2_STEAL_FOOD);
    CHECK(u2_steal_loot_for(0x20, 0x1F, 2) == U2_STEAL_ARMOUR,
          "town north-east -> armour (expected %d)", U2_STEAL_ARMOUR);
    CHECK(u2_steal_loot_for(0x1F, 0x1F, 2) == U2_STEAL_WEAPONS,
          "town north-west -> weapons (expected %d)", U2_STEAL_WEAPONS);

    /* The boundary cells, because >= 0x20 and < 0x20 are the actual comparisons. */
    CHECK(u2_steal_loot_for(0x1F, 0x1F, 1) == U2_STEAL_NO_LUCK,
          "village north-west is the clue seller and has no steal arm");
    CHECK(u2_steal_loot_for(0x20, 0x20, 2) == U2_STEAL_NO_LUCK,
          "town south-east is the transport shoppe and has no steal arm");
    CHECK(u2_steal_loot_for(0x1F, 0x20, 2) == U2_STEAL_NO_LUCK,
          "town south-west is the pub and has no steal arm");
    CHECK(u2_steal_loot_for(0x20, 0x20, 1) == U2_STEAL_NO_LUCK,
          "village south-east is the cleric shrine and has no steal arm");
    CHECK(u2_steal_loot_for(0x1F, 0x20, 1) == U2_STEAL_NO_LUCK,
          "village south-west is the wizard shoppe and has no steal arm");
    CHECK(u2_steal_loot_for(0x20, 0x1F, 0) == U2_STEAL_NO_LUCK,
          "a site kind of 0 is neither town nor village and steals nothing");
    CHECK(u2_steal_loot_for(0x20, 0x1F, 3) == U2_STEAL_NO_LUCK,
          "a dungeon site kind (3) steals nothing either — and dungeons are parked anyway");

    /* The half-plane edges, in both axes, for the two kinds that do have arms. */
    CHECK(u2_steal_loot_for(0x00, 0x00, 2) == U2_STEAL_WEAPONS,
          "(0,0) is the far north-west corner of a town");
    CHECK(u2_steal_loot_for(0x3F, 0x00, 2) == U2_STEAL_ARMOUR,
          "(0x3F,0) is the far north-east corner of a town");
    CHECK(u2_steal_loot_for(0x3F, 0x3F, 2) == U2_STEAL_NO_LUCK,
          "(0x3F,0x3F) is the south-east corner of a town and steals nothing");
    CHECK(u2_steal_loot_for(0x3F, 0x1F, 1) == U2_STEAL_FOOD &&
          u2_steal_loot_for(0x3F, 0x20, 1) == U2_STEAL_NO_LUCK,
          "the village north/south edge falls between 0x1F and 0x20");

    /* The tribute ladder, enumerated over every possible input rather than sampled. The
     * rungs are packed-decimal compares on the HIGH byte of HP, so the boundaries are 0x50,
     * 0x75 and 0x99 — and 0x99 is not a round number in binary, which is exactly the kind of
     * edge a sample misses. All 256 values are cheap and there is no reason to guess. */
    for (int v = 0; v <= 0xFF; v++) {
        uint8_t want;
        if (v < 0x50)      want = 3;
        else if (v < 0x75) want = 2;
        else if (v < 0x99) want = 1;
        else               want = 0;
        CHECK(u2_tribute_raise_for((uint8_t)v) == want,
              "tribute rung for HP high byte 0x%02X", v);
    }

    /* The three boundaries named explicitly, because the loop above would pass even if the
     * expected values were derived from the same wrong reading of the bytes. These are the
     * numbers the live capture produced: 0001 took +300, 5000 took +200, 7501 took +100,
     * and 9901 took nothing (captures/spice86/lord-british-tribute). */
    CHECK(u2_tribute_raise_for(0x00) == 3 && u2_tribute_raise_for(0x4F) == 3,
          "0x00 and 0x4F are both on the +300 rung");
    CHECK(u2_tribute_raise_for(0x50) == 2,
          "0x50 is the first value off the +300 rung, so the compare is jb not jbe");
    CHECK(u2_tribute_raise_for(0x74) == 2 && u2_tribute_raise_for(0x75) == 1,
          "the +200 rung ends at 0x74 and +100 starts at 0x75");
    CHECK(u2_tribute_raise_for(0x98) == 1 && u2_tribute_raise_for(0x99) == 0,
          "0x99 is the overflow guard: above it a +100 would carry past a packed-decimal byte");

    /* --- cmd_O: the Hotel California raise, enumerated rather than sampled ----------
     * asm/3C2E L_003f7e is one draw that decides BOTH whether the raise fires and which
     * stat it lands on: `r = rng & 7`, fires when `r < 6`. So the split is 6 of 8 and the
     * third-party walkthrough's 75/25 is exactly right, and the claim that the chance
     * depends on turn count is exactly wrong -- there is no turn term anywhere. */
    {
        int fired = 0, idx_seen[8] = {0};
        for (int roll = 0; roll < 256; ++roll) {
            int idx = -1; uint8_t out = 0;
            if (u2_offer_raise_for((uint8_t)roll, 1, 0x00, &idx, &out)) {
                fired++;
                idx_seen[idx]++;
            }
        }
        CHECK(fired == 192, "6 of 8 rolls raise, so 192 of 256, got %d", fired);
        for (int i = 0; i < 6; ++i)
            CHECK(idx_seen[i] == 32, "stat %d is hit by an equal share of rolls, got %d", i, idx_seen[i]);
        CHECK(idx_seen[6] == 0 && idx_seen[7] == 0, "rolls 6 and 7 never raise");
    }
    /* The gain is four `adc 0x27,%al ; daa`, so +4 per 100 gold, in packed decimal. */
    {
        int idx = -1; uint8_t out = 0;
        CHECK(u2_offer_raise_for(0, 1, 0x00, &idx, &out) && out == 0x04,
              "100 gold buys +4, got %02x", out);
        /* Packed decimal, so the results are BCD and not hex: +20 is 0x20 and +36 is 0x36.
         * The first draft of these two lines said 0x14 and was wrong about its own base,
         * which is the mistake this arithmetic exists to make visible. */
        CHECK(u2_offer_raise_for(0, 5, 0x00, &idx, &out) && out == 0x20,
              "500 gold buys +20 (four adds of 5), got %02x", out);
        CHECK(u2_offer_raise_for(0, 9, 0x00, &idx, &out) && out == 0x36,
              "900 gold, the largest single digit, buys +36, got %02x", out);
    }
    /* No ceiling guard exists on the store, which is the whole reason the walkthrough's
     * "stop at 95" is arithmetic and not superstition: 95 + 4 = 99 is the last safe raise,
     * and 96 wraps to 00. */
    {
        int idx = -1; uint8_t out = 0;
        u2_offer_raise_for(0, 1, 0x95, &idx, &out);
        CHECK(out == 0x99, "95 + 4 is 99, the last safe stop, got %02x", out);
        u2_offer_raise_for(0, 1, 0x96, &idx, &out);
        CHECK(out == 0x00, "96 + 4 wraps to 00, because nothing guards the store, got %02x", out);
        /* Owner observation 2026-11-11, from play: every stat maxed at 99, an offer of 100
         * to the sector-32 clerk, and the stat came back as 4. Not 3 -- and that one is the
         * carry chain. The first add wraps 99 to 00 and leaves CF set, and `adc` feeds it
         * into the second add, so the four adds of 1 land on 04. A version that cleared the
         * carry between the adds, which is the obvious way to write four decimal adds, lands
         * on 03 and passes every other check in this block. */
        u2_offer_raise_for(0, 1, 0x99, &idx, &out);
        CHECK(out == 0x04,
              "99 + 4 is 04, not 03: the wrap's carry feeds the next add, got %02x", out);
        /* 95 +2 +2 +2 +2: 97, 99, then the third add crosses the byte and the carry falls
         * off the end (99 +2 -> DAA -> A1 -> +60 -> 101 -> 01), then +2 +carry -> 04. */
        u2_offer_raise_for(0, 2, 0x95, &idx, &out);
        CHECK(out == 0x04, "95 + 8 overshoots past 99 and wraps, got %02x", out);
    }
    /* The index the roll selects is the stat offset: DS:4B + r, and DS:4B..50 are
     * str, agi, sta, cha, wis, int in that order. */
    {
        int idx = -1; uint8_t out = 0;
        for (int r = 0; r < 6; ++r) {
            u2_offer_raise_for((uint8_t)r, 1, 0x00, &idx, &out);
            CHECK(idx == r, "roll %d selects stat index %d, got %d", r, r, idx);
        }
    }

    /* --- the ring: asm/4209's blessing writer and asm/3C2E's reader -----------
     * Owner report 2026-11-11: he spoke to Father Antos, was given the blessing, offered
     * 500 gold to Antos and got THANK YOU VERY MUCH!, and could not tell whether a ring
     * had come of it. All of that is correct behaviour -- Antos is not the ring's source,
     * and the engine had never written DS:6C at all, so the ring arm was unreachable and
     * nothing had tested it. These are the tests that were missing.
     *
     * The Antos line is record 1 of tlkx93, copied from tools/tlkx_decode.py's output for
     * that file (the embedded CRs are 0x0d in the original; a plain space keeps the
     * record one record, which is all this test reads from it). */
    {
        u2_ctx ctx; u2_overworld ow;

        /* The writer: Planet X (world 9), a castle sector (ones digit 3), role 0x81.
         * The buffer is laid out the way the real files are: tlkx93 begins with a NUL,
         * so record 1 -- the one a role byte of 0x81 selects -- starts at offset 1. A
         * fixture that put the text at offset 0 reads back empty, which is how this test
         * first failed. */
        arm_offer(&ctx, &ow, 0x81, 0, 9, 3);
        ctx.state.world = 9;
        ow.world.tlkx[0] = 0;
        memcpy(ow.world.tlkx + 1,
               "FATHER ANTOS CHANTS: YOU HAVE EARNED MY BLESSING. RETURN AND CLAIM THE RING!",
               sizeof "FATHER ANTOS CHANTS: YOU HAVE EARNED MY BLESSING. RETURN AND CLAIM THE RING!");
        ow.world.tlkx_loaded = 1;
        CHECK(u2_commerce_transact(&ctx, &ow, 1, 0) == U2_TRANSACT_TREATED,
              "a role-0x81 slot on Planet X takes the scripted talk arm");
        CHECK(grid_has(&ctx, "FATHER ANTOS CHANTS"),
              "Antos speaks his tlkx93 record before anything else happens");
        CHECK(ctx.state.blessing == 1,
              "asm/4209:422B -- the blessing byte DS:6C is the writer's store, not a guess");

        /* Controls: each of the three conditions alone is not enough. */
        arm_offer(&ctx, &ow, 0x81, 0, 9, 3);
        ctx.state.world = 0;                       /* same castle, wrong world */
        ow.world.tlkx[0] = 0;
        memcpy(ow.world.tlkx + 1, "FATHER ANTOS CHANTS: BLESSING",
               sizeof "FATHER ANTOS CHANTS: BLESSING");
        ow.world.tlkx_loaded = 1;
        (void)u2_commerce_transact(&ctx, &ow, 1, 0);
        CHECK(ctx.state.blessing == 0,
              "control: the same words from the same role on Earth do not bless you");

        arm_offer(&ctx, &ow, 0x81, 0, 9, 2);       /* Planet X, but a town not a castle */
        ctx.state.world = 9;
        ow.world.tlkx[0] = 0;
        memcpy(ow.world.tlkx + 1, "FATHER ANTOS CHANTS: BLESSING",
               sizeof "FATHER ANTOS CHANTS: BLESSING");
        ow.world.tlkx_loaded = 1;
        (void)u2_commerce_transact(&ctx, &ow, 1, 0);
        CHECK(ctx.state.blessing == 0,
              "control: role 0x81 in a Planet X town does not bless you either");

        arm_offer(&ctx, &ow, 0x82, 0, 9, 3);       /* the castle, but a different role */
        ctx.state.world = 9;
        ow.world.tlkx[0] = 0;
        memcpy(ow.world.tlkx + 1, "SING LEE THE COOK YELLS: KICHEN CLOST",
               sizeof "SING LEE THE COOK YELLS: KICHEN CLOST");
        ow.world.tlkx_loaded = 1;
        (void)u2_commerce_transact(&ctx, &ow, 1, 0);
        CHECK(ctx.state.blessing == 0,
              "control: the cook in the same castle does not bless you");

        /* The reader, unblessed: the line is EARN THE RING! and the slot stays empty. */
        arm_offer(&ctx, &ow, 0x82, 0, 3, 2);
        CHECK(u2_commerce_offer(&ctx, &ow, 1, 0) == U2_OFFER_EARN_RING,
              "500 gold to the sector-32 role-0x82 man without the blessing");
        CHECK(grid_has(&ctx, U2_S_OFFER_EARN_RING), "EARN THE RING! is what he says");
        CHECK(ctx.state.items_held[0] == 0, "and no ring changes hands");
        CHECK(ctx.state.gold_lo == 0x94 && ctx.state.gold_hi == 0x99,
              "and the 500 was taken even though he refused: 9999 -> 9499 (%02x%02x)",
              ctx.state.gold_lo, ctx.state.gold_hi);

        /* The reader, blessed: THE RING IS YOURS! and DS:D6 goes to 1. */
        arm_offer(&ctx, &ow, 0x82, 1, 3, 2);
        CHECK(u2_commerce_offer(&ctx, &ow, 1, 0) == U2_OFFER_RING,
              "the same offer after the blessing");
        CHECK(grid_has(&ctx, U2_S_OFFER_RING_IS_YOURS), "THE RING IS YOURS! is what he says");
        CHECK(ctx.state.items_held[0] == 1, "DS:D6, the RING slot, is set to 1");
        CHECK(ctx.state.gold_lo == 0x94 && ctx.state.gold_hi == 0x99,
              "the grant costs the same 500 (9999 -> 9499)");

        /* Broke: the exact state the owner was in on 2026-11-11, 317 gold against a 500 ask.
         * The original answers with asm/2070's line and never reaches the blessing check, and
         * the trial subtract is added back so a refusal costs nothing. The line WRAPS on a
         * real screen — `YOU DONT HAVE` on one row, `THAT MUCH!` on the next, status band
         * interleaved — so both halves are looked for rather than the whole sentence, which
         * grid_has() could never match across a row boundary. */
        arm_offer(&ctx, &ow, 0x82, 1, 3, 2);
        ctx.state.gold_lo = 0x03;               /* 317 gold */
        ctx.state.gold_hi = 0x17;
        CHECK(u2_commerce_offer(&ctx, &ow, 1, 0) == U2_OFFER_BROKE,
              "317 gold against a 500 ask is refused for money, not for the blessing");
        CHECK(grid_has(&ctx, "YOU DONT HAVE"),
              "YOU DONT HAVE THAT MUCH! is on the grid (first half: the line wraps, and the "
              "wrap point moves with the right margin)");
        CHECK(ctx.state.gold_lo == 0x03 && ctx.state.gold_hi == 0x17,
              "and the add-back left the purse untouched (%02x%02x)",
              ctx.state.gold_lo, ctx.state.gold_hi);
        CHECK(ctx.state.items_held[0] == 0, "and no ring, of course");

        /* The charge is not a quest-arm thing. asm/2052 runs before the role gate, so a plain
         * recipient is paid too — which is also what makes the Hotel clerk's stat raise a
         * service bought at 100 gold per 4 points rather than a free gift. */
        arm_offer(&ctx, &ow, 0x01, 0, 3, 2);    /* role bit 7 clear: THANK YOU */
        CHECK(u2_commerce_offer(&ctx, &ow, 1, 0) == U2_OFFER_THANK_YOU,
              "a plain recipient still says THANK YOU VERY MUCH!");
        CHECK(ctx.state.gold_lo == 0x94 && ctx.state.gold_hi == 0x99,
              "and still cost 500, because asm/2052 runs before the role gate (%02x%02x)",
              ctx.state.gold_lo, ctx.state.gold_hi);

        /* Controls on the reader: the sector gate and the amount gate. */
        arm_offer(&ctx, &ow, 0x82, 1, 3, 1);       /* sector 31: blessed, wrong town */
        CHECK(u2_commerce_offer(&ctx, &ow, 1, 0) == U2_OFFER_THANK_YOU,
              "control: the whole quest arm is gated on sector 32, not on the blessing");
        CHECK(ctx.state.items_held[0] == 0, "control: no ring outside sector 32");

        /* The chain as the owner walked it: bless on Planet X, go back to 1990, pay the
         * man. One run, both halves, and the ring only exists because of the first. */
        arm_offer(&ctx, &ow, 0x81, 0, 9, 3);
        ctx.state.world = 9;
        ow.world.tlkx[0] = 0;
        memcpy(ow.world.tlkx + 1, "FATHER ANTOS CHANTS: YOU HAVE EARNED MY BLESSING",
               sizeof "FATHER ANTOS CHANTS: YOU HAVE EARNED MY BLESSING");
        ow.world.tlkx_loaded = 1;
        (void)u2_commerce_transact(&ctx, &ow, 1, 0);
        CHECK(ctx.state.blessing == 1, "chain: the blessing is earned");
        ctx.state.sector_first = 3;                /* the trip back through time */
        ctx.state.sector_second = 2;
        ctx.state.world = 0;
        ctx.state.mon_role[5] = 0x82;              /* and the man in town */
        offer_digit_left = 1;
        CHECK(u2_commerce_offer(&ctx, &ow, 1, 0) == U2_OFFER_RING,
              "chain: Antos's blessing is what makes the ring obtainable");
        CHECK(ctx.state.items_held[0] == 1, "chain: the ring is in the inventory at the end");
    }

    printf("commerce_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}