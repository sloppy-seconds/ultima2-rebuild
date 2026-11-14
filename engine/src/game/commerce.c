/*
 * commerce.c — the TRANSACT ray, the vendor selector, and the shops it reaches.
 *
 * See commerce.h for the shape and docs/commerce.md for the evidence. Every claim about
 * order in this file is byte-cited to the asm/ tree; where a shop is deliberately NOT
 * implemented, the gap is named at the branch that would have done it, because a silent
 * fall-through is how an unmodelled subsystem starts looking like a working one.
 *
 * Content policy: as everywhere in the engine, no game text is hard-coded here. Every
 * literal comes from the extractor's table, which reads it out of the user's own copy at
 * build time.
 */
#include "game/commerce.h"

#include <stdio.h>
#include <string.h>

#include "core/rng.h"
#include "core/seam.h"
#include "core/state.h"
#include "core/world.h"
#include "game/ai.h"
#include "game/monsters.h"

#include "gen_content.h"

/* The merchant marker, and the one non-sign tile the ray also accepts in the near cell.
 * Both are stamped-grid values (DS:1800), not mapx file bytes: the grid is what
 * asm/50B3_map_tile_ptr reads, and it is the map plus the creature stamps on top of it. */
#define TILE_MERCHANT_MARK  0x68u
#define TILE_SHOP_COUNTER   0x7Cu

/* ---- asm/20AB: the price --------------------------------------------------- */

/*
 * The `adc` + `daa` pair from asm/20AB's loop body, with DAA's carry-out taken only from
 * the >0x99 adjustment. That restriction is not a shortcut: an earlier model let the
 * low-nibble fixup feed the following adc and produced 4, 8, 112, 220 — plausible-looking
 * and wrong from the third term onward, which is the shape of error asm/20AB's header
 * exists to record. The corrected walk reproduces the two anchors the file hand-computes
 * and both live prices measured on 2026-10-29.
 */
static uint8_t daa_add(uint8_t a, uint8_t b, int cin, int *cout)
{
    unsigned sum = (unsigned)a + (unsigned)b + (unsigned)cin;
    const int af = ((a & 0x0Fu) + (b & 0x0Fu) + cin) > 0x0F;
    int cf = sum > 0xFF;
    unsigned v = sum & 0xFFu;

    if ((v & 0x0Fu) > 9u || af)
        v += 6u;
    if (v > 0x99u || cf) {
        v += 0x60u;
        cf = 1;
    }
    *cout = cf;
    return (uint8_t)(v & 0xFFu);
}

/* asm/20CF: `inc %si ; clc ; rcr $1,%al ; or %al,%al ; jne` — shifts to reach zero, so a
 * zero input still costs one shift. That is a bit length, not a popcount; an earlier pass
 * called it popcount and asm/20AB records the correction. */
static int shifts_to_zero(uint8_t v)
{
    int n = 0;
    do {
        n++;
        v = (uint8_t)(v >> 1);
    } while (v != 0);
    return n;
}

void u2_price_from_stats(uint8_t index, uint8_t stat_int, uint8_t stat_cha, uint8_t price[2])
{
    /* asm/20AD: `clc ; adc $0x8,%al` — the caller's index, offset by eight, and the carry
     * it leaves is the same one the int+cha add below consumes. */
    unsigned seed = (unsigned)index + 8u;
    const int carry_from_seed = seed > 0xFF;

    /* asm/20B2: both pairs seeded to 0004. */
    unsigned p1_hi = 0x00u, p1_lo = 0x04u;   /* DS:27 / DS:28 — printed and charged */
    unsigned p2_hi = 0x00u, p2_lo = 0x04u;   /* DS:29 / DS:2A — the other half of the walk */

    /* asm/20C2: `mov [DS:50],%al ; adc [DS:4E],%al` — int + charisma, added as raw packed
     * decimal bytes with no daa, which is a quirk of the original and is kept. */
    const unsigned stat_sum = (unsigned)stat_int + (unsigned)stat_cha + (carry_from_seed ? 1u : 0u);
    int loops = (int)seed - shifts_to_zero((uint8_t)(stat_sum & 0xFFu));
    if (loops < 0)
        loops = 0;
    /* asm/231E: `dec %di ; jne`. A loop count of zero does not skip the loop, it wraps to
     * 0xFFFF and runs 65536 times. Reachable in principle, never observed; mirrored because
     * the bytes say so and guessing that the original guarded it would be the invention. */
    if (loops == 0)
        loops = 0x10000;

    for (int i = 0; i < loops; i++) {
        int cf = 0;
        const unsigned old_lo = p1_lo, old_hi = p1_hi;
        p1_lo = daa_add((uint8_t)p1_lo, (uint8_t)p2_lo, 0, &cf);
        p2_lo = old_lo;
        p1_hi = daa_add((uint8_t)p1_hi, (uint8_t)p2_hi, cf, &cf);
        p2_hi = old_hi;
    }

    price[0] = (uint8_t)p1_hi;
    price[1] = (uint8_t)p1_lo;
}

/* The four-digit print: asm/17B7 loads AL=[DS:27] and DI=[DS:28] and asm/51A5 emits AL's
 * two decimal digits then DI's, so DS:27 is the high byte and the pair reads in address
 * order. This is the same order asm/2052 charges in, and it is why state.h's gold_lo /
 * gold_hi names are about address, not significance. */
static void print_price(u2_ctx *ctx, const uint8_t price[2])
{
    char buf[5];
    buf[0] = (char)('0' + (price[0] >> 4));
    buf[1] = (char)('0' + (price[0] & 0x0Fu));
    buf[2] = (char)('0' + (price[1] >> 4));
    buf[3] = (char)('0' + (price[1] & 0x0Fu));
    buf[4] = '\0';
    seam_str_out(ctx, buf);
}

/* ---- asm/2052: the charge -------------------------------------------------- */

/* asm/2052's `cmc ; sbb ; das ; cmc` pair, byte by byte, with the borrow threaded from the
 * low byte to the high byte the way the two `cmc`s do it. Mirrors the same helper
 * game/overworld.c keeps private for HP and food: no int round-trip, because decoding a
 * packed-decimal pair and re-encoding it rewrites nibbles the original preserves. */
static int bcd_sbb(unsigned a, unsigned sub, int borrow_in, unsigned *out)
{
    const int af = ((a & 0x0Fu) < (sub & 0x0Fu) + (unsigned)borrow_in) ? 1 : 0;
    int d = (int)a - (int)sub - borrow_in;
    const int cf = (d < 0) ? 1 : 0;
    d &= 0xFF;
    if ((unsigned)(d & 0x0F) > 9u || af)
        d = (d - 6) & 0xFF;
    if (cf || (unsigned)d > 0x99u)
        d = (d - 0x60) & 0xFF;
    *out = (unsigned)d;
    return cf;
}

/* `clc ; adc $1,%al ; daa` — the one-step BCD increment asm/181A uses for food and asm/2045
 * uses for an owned-count slot. Both discard the carry out of the byte, so 99 wraps to 00
 * and nothing upstream guards it.
 *
 * No caller uses it directly any more: every increment site in this file went through
 * u2_bcd_inc_sat, which applies this same rule and then holds the value in enhanced mode
 * where the owner ruled that counters cap rather than roll over (core/ctx.h, U2_SATURATE).
 * It stays because u2_bcd_inc_sat is the shared form of exactly this instruction, and
 * inlining it there would hide which original opcode the behaviour comes from. */
[[maybe_unused]] static uint8_t bcd_inc1(uint8_t v)
{
    int cf = 0;
    return daa_add(v, 1u, 0, &cf);
}

int u2_spend_price(u2_ctx *ctx, const uint8_t price[2])
{
    u2_state *st = &ctx->state;

    /* asm/2052 subtracts [DS:27]:[DS:28] from [DS:58]:[DS:59], high byte first in address
     * order, and the second `das`'s borrow is the verdict: `jb` is taken when there was NO
     * borrow, because the trailing `cmc` inverts it. So a returned borrow of 1 means the
     * player cannot pay, and asm/2070 adds the pair back and prints the refusal — and then
     * does not return to the shop, which is why a caller must not print a success line
     * after a 0. */
    unsigned lo = 0, hi = 0;
    int borrow = bcd_sbb(st->gold_hi, price[1], 0, &lo);      /* DS:59 - DS:28 */
    borrow = bcd_sbb(st->gold_lo, price[0], borrow, &hi);     /* DS:58 - DS:27 */
    if (borrow) {
        seam_str_out(ctx, U2_S_DONT_HAVE_THAT_MUCH);
        return 0;
    }
    st->gold_hi = (uint8_t)lo;
    st->gold_lo = (uint8_t)hi;
    return 1;
}

/* ---- the input the shops need ---------------------------------------------- */

/* One line of engine-authored text, announced as such.
 *
 * The `[engine]` prefix is this project's convention for anything the original never printed
 * -- the same one --cleanroom uses for substitute art and title.c uses for stub screens. A
 * gap has to be visible, because an unimplemented branch that returns quietly is
 * indistinguishable from a hung game, and "it froze" is exactly what an owner reported for
 * one of these. It must never be mistaken for game text, because then a missing feature
 * reads as the reconstruction faithfully reproducing a refusal that never existed. */
static void engine_gap(u2_ctx *ctx, const char *what)
{
    char buf[64];
    snprintf(buf, sizeof buf, "[engine] %s", what);
    seam_str_out(ctx, buf);
}

/* Put what was just printed on the screen before blocking for the key that answers it.
 *
 * The overworld composes and presents once per loop iteration, at the top of the loop, and a
 * handler that blocks here never returns there. Owner report 2026-10-29: the TRANSPORT shoppe
 * question was invisible while the engine waited for Y or N, which read as the game freezing,
 * and answering late dropped the Y into the command loop as a YELL. The text was always being
 * written correctly; nothing was showing it.
 *
 * Deliberately called at the sites that print-then-block rather than inside seam_key_wait.
 * The universal version was tried and broke CP4: presenting from the shared wait path also
 * runs at the title screen, where --dump-screen sets its done-flag during the flush and the
 * title flow loops on a no-key result forever. The original needs no such call anywhere -- it
 * writes to 0xB8000 and the CRT is always displaying it -- so this is port plumbing at the
 * places that need it, not a property of every wait.
 *
 * It is now a thin alias for seam_key_wait_shown, which carries the contract, the composition
 * detail (a whole frame, not a bare text flush, or the map goes black behind the question)
 * and the same warning about folding it into the shared path. The shops keep the local verb
 * because half a dozen call sites read better with it. */
static int show_and_wait(u2_ctx *ctx, u2_key *k)
{
    return seam_key_wait_shown(ctx, k);
}

/* asm/2028_read_decimal_digit: block until one decimal digit arrives, echo it, return it.
 * Non-digits are rejected by reading again. Returns -1 only when the engine's input is
 * exhausted, which has no counterpart in the original (it blocks forever) and is the
 * caller's cue to do nothing rather than to guess. */
static int read_digit(u2_ctx *ctx)
{
    for (;;) {
        u2_key k;
        if (show_and_wait(ctx, &k) == 0)
            return -1;
        if (k.ch >= '0' && k.ch <= '9') {
            seam_char_print(ctx, k.ch);
            return k.ch - '0';
        }
    }
}

/* asm/1FEB / asm/2017: read ONE key of any kind, echo it, and treat only 'Y' as yes.
 *
 * Uppercase is the byte (`cmp $0x59,%al`), not a stylistic choice, and it is a real quirk:
 * a player who types lowercase y is refused. Captures that appear to show a lowercase 'y'
 * working were driven through tools/spice86_type.py, which reports `sent y as Y` — the
 * injector sends the shifted key. Do not "fix" this by accepting both.
 *
 * The original also emits 0x8D after the key. asm/2028's own DOUBT block calls that byte
 * unexplained, and cmd_M made the same call for the same reason: emitting a line break
 * nobody can account for is a worse guess than not emitting one. */
static int read_yes(u2_ctx *ctx)
{
    u2_key k;
    if (show_and_wait(ctx, &k) == 0)
        return 0;
    seam_char_print(ctx, k.ch);
    return k.ch == 'Y';
}

/* ---- the two catalogues that share one shape ------------------------------- */

static u2_transact_result sell_spell(u2_ctx *ctx, int wizard);

/*
 * asm/1D1D (armour) and asm/1E4F (weapons) are the same routine twice:
 *
 *   print the menu
 *   read a digit; 0, or the digit the shop's own bound names, is "THANKS FOR COMING BY..."
 *   [DS:1F] = digit
 *   "AH! YES! " / "AH! YES! A " then the item NAME from the indexed pool
 *   "\rFOR YOU ONLY " then the four price digits
 *   "\rHOW 'BOUT IT? " then one key
 *   not 'Y' -> "OH, WELL."
 *   'Y'     -> charge, "SOLD!", then ONE packed-decimal increment into the owned-count
 *              array at [DS:96 + digit] / [DS:76 + digit]
 *
 * The array index is the menu digit, not digit-minus-one, and slot 0 is never sold: buying
 * CLOTH (menu 1) moved DS:97 and buying AX (menu 3) moved DS:79, both measured live
 * (captures/spice86/armour-buy and captures/spice86/g23-weapons). `counts_base` carries
 * that difference instead of hiding it in a call site.
 */
static u2_transact_result sell_from_catalogue(u2_ctx *ctx,
                                              const char *menu,
                                              const char *ah_yes,
                                              int max_digit,
                                              int price_doubles,
                                              const char *const *names,
                                              int names_n,
                                              uint8_t *counts,
                                              int counts_n,
                                              int counts_base)
{
    seam_str_out(ctx, menu);

    const int digit = read_digit(ctx);
    if (digit < 0)
        return U2_TRANSACT_REFUSED;
    if (digit == 0 || digit > max_digit) {
        seam_str_out(ctx, U2_S_SHOP_THANKS);
        return U2_TRANSACT_REFUSED;
    }

    seam_str_out(ctx, ah_yes);
    /* asm/1FC1 / asm/20E9: `clc ; adc $0x1d,%al` (armour) and `adc $0x13,%al` (weapons)
     * before the pool printer at asm/508C. Those constants are the group bases the
     * extractor already folds into U2_T_ARMOUR and U2_T_WEAPONS, so the menu digit indexes
     * the pool directly — the same thing cmd_W does for the WEAR menu. */
    seam_str_out(ctx, (digit >= 0 && digit < names_n) ? names[digit] : "");

    uint8_t price[2];
    u2_price_from_stats((uint8_t)(price_doubles ? digit * 2 : digit),
                        ctx->state.stat_int, ctx->state.stat_cha, price);
    seam_str_out(ctx, U2_S_SHOP_FOR_YOU_ONLY);
    print_price(ctx, price);

    seam_str_out(ctx, U2_S_SHOP_HOW_BOUT);
    if (!read_yes(ctx)) {
        seam_str_out(ctx, U2_S_SHOP_OH_WELL);
        return U2_TRANSACT_REFUSED;
    }
    if (!u2_spend_price(ctx, price))
        return U2_TRANSACT_REFUSED;

    seam_str_out(ctx, U2_S_SHOP_SOLD);
    const int idx = digit + counts_base;
    if (idx >= 0 && idx < counts_n)
        counts[idx] = u2_bcd_inc_sat(counts[idx], U2_SATURATE(ctx));
    return U2_TRANSACT_OPEN;
}

/* asm/1D1D. Price index is the digit DOUBLED (`add %al,%al` at runtime 1DD8), which is why
 * CLOTH at 12 is fib-step 2 and not step 1. */
static u2_transact_result shop_armour(u2_ctx *ctx)
{
#ifdef U2_T_ARMOUR_N
    return sell_from_catalogue(ctx, U2_S_ARMOUR_MENU, U2_S_SHOP_AH_YES, 6, 1,
                               U2_T_ARMOUR, U2_T_ARMOUR_N,
                               ctx->state.armour_owned,
                               (int)sizeof ctx->state.armour_owned, -1);
#else
    (void)ctx;
    return U2_TRANSACT_NO_DEALER;
#endif
}

/* asm/1E4F. Price index is the digit itself, confirmed live: menu 3 (AX) entered
 * asm/20AB with AL=0x03 and left DS:27/28 = 00 20, and the charge took exactly 20
 * (captures/spice86/g23-weapons).
 *
 * The offer prefix is "AH! YES! ", the same literal the armour shoppe uses, and that call is
 * made by the frame rather than the listing. The binary holds a second offer literal
 * "AH! YES! A " at 0x1ED9, and a listing-only reading assigned it here — which rendered menu 3
 * as "AH! YES! A AXE". captures/spice86/g23-weapons/w5-axe.png shows "AH! YES! AXE", so the
 * shoppe does not use it. Who does is unresolved: it sits in the same string neighbourhood as
 * the magic shoppe's menu, and no capture has run the wizard shoppe, so it stays unassigned
 * rather than being moved somewhere plausible. */
static u2_transact_result shop_weapons(u2_ctx *ctx)
{
#ifdef U2_T_WEAPONS_N
    return sell_from_catalogue(ctx, U2_S_WEAPONS_MENU, U2_S_SHOP_AH_YES, 8, 0,
                               U2_T_WEAPONS, U2_T_WEAPONS_N,
                               ctx->state.weapons_owned,
                               (int)sizeof ctx->state.weapons_owned, 0);
#else
    (void)ctx;
    return U2_TRANSACT_NO_DEALER;
#endif
}

/* asm/1799 -> asm/17B2 -> asm/17B7 -> asm/17C5: the grocery quote. The price index is the
 * constant 3, and the pair it produces is both what is printed and what is charged —
 * measured 2026-10-29 with asm/17B7 and asm/2052 both stopped and both reading 00 20, and
 * gold falling 9999 -> 9979 (captures/spice86/g23-food-live). */
static u2_transact_result shop_grocery(u2_ctx *ctx)
{
    uint8_t price[2];
    u2_price_from_stats(3, ctx->state.stat_int, ctx->state.stat_cha, price);

    seam_str_out(ctx, U2_S_FOOD_COSTS);
    print_price(ctx, price);
    seam_str_out(ctx, U2_S_FOOD_PER100);

    if (!read_yes(ctx)) {
        seam_str_out(ctx, U2_S_FOOD_NO);
        return U2_TRANSACT_REFUSED;
    }
    seam_str_out(ctx, U2_S_FOOD_YES_A);
    seam_str_out(ctx, U2_S_FOOD_YES_B);
    if (!u2_spend_price(ctx, price))
        return U2_TRANSACT_REFUSED;

    /* asm/181A: `clc ; mov [DS:53],%al ; adc $0x1,%al ; daa ; mov %al,[DS:53]` — one food
     * unit, in packed decimal, on the low byte only. "PER 100" is the vendor's joke about
     * the price, not a quantity: the byte adds one. */
    ctx->state.food = u2_bcd_inc_sat(ctx->state.food, U2_SATURATE(ctx));
    seam_str_out(ctx, U2_S_THANK_YOU_AGAIN);
    return U2_TRANSACT_OPEN;
}

/* asm/1F75: the transport shoppe. The price index is the constant 4, which is what makes
 * a horse 32 gold for a maxed character and 52 for the character with charisma 0x20 —
 * the second of those is the measurement that proved [DS:4E] is charisma.
 *
 * A bought horse is not an inventory item. The transaction writes gold and DS:13 and the
 * mount happens as part of the purchase; no tile is stamped and no slot moves
 * (captures/spice86/shop-window-transport). */
static u2_transact_result shop_transport(u2_ctx *ctx)
{
    uint8_t price[2];
    u2_price_from_stats(4, ctx->state.stat_int, ctx->state.stat_cha, price);

    seam_str_out(ctx, U2_S_TRANSPORT_MENU);
    print_price(ctx, price);
    seam_str_out(ctx, U2_S_TRANSPORT_DEAL);

    if (!read_yes(ctx)) {
        seam_str_out(ctx, U2_S_TRANSPORT_BYE);
        return U2_TRANSACT_REFUSED;
    }
    if (!u2_spend_price(ctx, price))
        return U2_TRANSACT_REFUSED;

    seam_str_out(ctx, U2_S_TRANSPORT_RIDE);
    ctx->state.player_stamp = 0x22u;      /* HORSE, the constant at asm/201C */
    return U2_TRANSACT_OPEN;
}

/* ---- the selector ---------------------------------------------------------- */

u2_shop u2_commerce_shop_for(int x, int y, int site_kind)
{
    const int east = (x & 0xFF) >= 0x20;
    const int south = (y & 0xFF) >= 0x20;
    const int town = (site_kind == 2);

    if (east && !south) return town ? U2_SHOP_ARMOUR  : U2_SHOP_GROCERY;
    if (!east && !south) return town ? U2_SHOP_WEAPONS : U2_SHOP_CLUE;
    if (east && south)  return town ? U2_SHOP_TRANSPORT : U2_SHOP_CLERIC;
    return town ? U2_SHOP_PUB     : U2_SHOP_WIZARD;
}

/* ---- the tribute: TRANSACT aimed at a type-0x6C creature ------------------- */

/* See the header comment in commerce.h. The rungs are written as comparisons in the same
 * order the original evaluates them, because the order IS the behaviour: 0x50 before 0x75
 * before 0x99, and a rung that preloads its value before testing means a fall-through is
 * what selects the next one. */
uint8_t u2_tribute_raise_for(uint8_t hp_high)
{
    if (hp_high < 0x50u) return 3u;
    if (hp_high < 0x75u) return 2u;
    if (hp_high < 0x99u) return 1u;
    return 0u;
}

/* asm/4249..42E2, reached by asm/40CB matching creature type 0x6c and asm/40D4 jumping here.
 *
 * The whole span had never been seen executing until 2026-10-29; it now has 54 observations
 * and every rung fired, so the sequence below is not inferred. Three ordering facts in it
 * are easy to get wrong and each one is load-bearing:
 *
 *   1. The 50 GP is spent BEFORE the ladder is evaluated, so the tribute is charged even on
 *      the rung that raises nothing. Observed: gold 7349 -> 7299 at HP 9901.
 *   2. asm/2052 prints its own refusal and does not return to the caller's success path, so
 *      a failed spend must stop here rather than fall through to the raise.
 *   3. Only [DS:51], the high byte of HP, is read and written. The low two digits never move
 *      — the trailing 01 of the seeded HP survived the entire climb from 0001 to 9901.
 */
static void tribute(u2_ctx *ctx)
{
    u2_state *st = &ctx->state;
    const uint8_t price[2] = { 0x00u, 0x50u };   /* [DS:27] high, [DS:28] low = 50 GP */

    seam_str_out(ctx, U2_S_TRIBUTE_WELCOME);
    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_TRIBUTE_CHARGE);
    seam_char_print(ctx, '\r');

    if (!u2_spend_price(ctx, price))
        return;

    const uint8_t rung = u2_tribute_raise_for(st->hp);

    seam_str_out(ctx, U2_S_TRIBUTE_RAISE);
    /* asm/42D1 zeroes DI and calls asm/51A5, which renders AL as two packed-decimal digits
     * and then DI as two more. So the amount is always four digits with the value in the top
     * pair: rung 3 prints "0300", rung 0 prints "0000". A leading zero in this font is easy
     * to misread as '@' when a frame is transcribed by eye. Built a digit at a time rather than
     * with a format string, both because asm/55B5 is literally a nibble-to-ASCII pair and
     * because snprintf's truncation analysis cannot know the rung is under 10. */
    char num[5];
    num[0] = (char)('0' + (rung / 10u));
    num[1] = (char)('0' + (rung % 10u));
    num[2] = '0';
    num[3] = '0';
    num[4] = '\0';
    seam_str_out(ctx, num);

    /* asm/42E2: `clc ; mov 0x51,%al ; adc 0x1f,%al ; daa ; mov 0x51,%al`. */
    int cf = 0;
    const uint8_t hp_new = daa_add(st->hp, rung, 0, &cf);
    /* Enhanced mode does not let a heal roll the hundreds byte over; see U2_SATURATE. */
    st->hp = (U2_SATURATE(ctx) && hp_new < st->hp) ? st->hp : hp_new;
}

u2_transact_result u2_commerce_transact(u2_ctx *ctx, u2_overworld *ow, int dx, int dy)
{
    u2_state *st = &ctx->state;

    /* asm/406E: nothing deals in the open world. */
    if (st->sector_second == 0 || !ow->world.map_loaded) {
        seam_str_out(ctx, U2_S_NO_RESPONSE);
        return U2_TRANSACT_NO_DEALER;
    }

    /* asm/23E6_find_creature_at_cursor: cursor = player + delta, then a scan of the
     * creature slots for one standing on that cell. The scan is `DI = 31` down through
     * `dec %di ; jne`, so slot 0 is never examined — recorded, not fixed: whether that is
     * a bug or a deliberate reserve slot is not established, and monx31 slot 0 is a
     * populated Guard. */
    const int cx = (ow->x + dx) & (U2_SECTOR_EDGE - 1);
    const int cy = (ow->y + dy) & (U2_SECTOR_EDGE - 1);
    int slot = 0;
    for (int i = U2_MON_SLOTS - 1; i >= 1; i--) {
        if (st->mon_type[i] == 0)
            continue;                       /* asm/2406: an empty slot is type 0 */
        if (st->mon_x[i] == (uint8_t)cx && st->mon_y[i] == (uint8_t)cy) {
            slot = i;
            break;
        }
    }

    if (slot != 0) {
        /* asm/40CB: the typed arm of the split. A role byte below 0x80 means the slot has no
         * scripted tlkx line, and the creature TYPE then decides. Type 0x6c is the tribute and
         * it is fully implemented — it needed no tlkx loader, which is why it was reachable the
         * whole time while the talking arm below was not. */
        if (st->mon_role[slot] < 0x80u && st->mon_type[slot] == 0x6Cu) {
            tribute(ctx);
            return U2_TRANSACT_TREATED;
        }

        /* The talking branch: asm/40C0 splits scripted from typed on bit 7 of the slot's
         * role byte. Set means the slot has a scripted line.
         *
         * asm/40C0 splits on the slot's role byte: bit 7 set means "this NPC has a
         * scripted line" and asm/4209 prints record (role - 0x80) out of the sector's
         * tlkx buffer at [DS:476] via asm/154E; bit 7 clear walks the rest of the creature-type
         * chain (0x60 guard, 0x64 jester, 0x68 merchant, 0xF0 fighter, 0xF4 cleric, 0xF8 wizard,
         * 0xFC thief), each printing one fixed line. The selector was observed executing
         * on 2026-10-29 with AL=0x02 for a role-0x82 Jester (captures/spice86/g23-food-live),
         * so the mechanism is no longer the open question docs/handoff-commerce-transact.md
         * recorded. The engine half is now in place too: core/world.c loads tlkxNN and
         * u2_world_tlkx_record walks it exactly as asm/154E does.
         * the guess the handoff told this branch to avoid.
         *
         * Silence was not an acceptable placeholder, and this is the case AGENTS.md describes:
         * a turn consumed, nothing printed, no cited byte justifying the quiet. Owner report
         * 2026-10-29 -- two NPCs south of the TRANSPORT shoppe in Port Boniface, "neither of
         * them respond", and aiming at the vendor through one of them looked like the game
         * had frozen. The creature scan runs BEFORE the tile test (asm/23E6 precedes
         * asm/407C), so an NPC standing in a shop's sign cell swallows the ray and this
         * branch used to return nothing at all. Say what is missing rather than looking hung. */
        if (st->mon_role[slot] >= 0x80u) {
            /* asm/4209: `stc ; cmc ; sbb $0x80,%al ; cmc` is just AL = role - 0x80, then
             * `call asm/154E` prints that record out of the sector's dialogue buffer. The
             * role byte IS the record index -- the rule § 4.5 of the commerce handoff called
             * "completely unknown", and it was closed on 2026-10-29 by stopping on asm/154E
             * with AL=0x02 over a role-0x82 slot and reading the line that came out.
             *
             * No line is a real outcome, not an error: a role byte can point past the 256-byte
             * read, and sectors ship without a tlkx file at all. Saying so beats silence, and
             * silence is what this branch used to produce. */
            char line[160];
            const int n = u2_world_tlkx_record(&ow->world,
                                               (int)st->mon_role[slot] - 0x80,
                                               line, sizeof line);

            /* asm/4209:422B -- the blessing, and the writer that state.h spent months
             * calling untraced. Three conditions read straight off the bytes: the world
             * byte is 9 (Planet X), the sector's ones digit is 3 (a castle), and the
             * slot's role is 0x81, which is record 1 of tlkx93 -- FATHER ANTOS CHANTS:
             * YOU HAVE EARNED MY BLESSING. RETURN AND CLAIM THE RING!. The reader is
             * asm/3C2E cmd_O's ring arm, which lives in sector 32 -- Earth, 1990, town --
             * so the quest is two trips and not one conversation: earn the flag on Planet
             * X, then go back through time and pay the man in town. That half is the
             * owner's recollection, confirmed by the two gates; the store itself is bytes.
             *
             * Placed before the n > 0 test on purpose: in asm/4209 the store follows the
             * print with no branch on whether a record came out, so a sector with no tlkx
             * file still blesses you. The engine's "no dialogue for that role" line is an
             * engine affordance and is not in the original at all. */
            if (st->world == 9 && st->sector_second == 3 && st->mon_role[slot] == 0x81u)
                st->blessing = 1;
            if (n > 0) {
                seam_str_out(ctx, line);
                /* asm/4209 then holds the line on screen for a beat (di = 4 around a call to
                 * L_000911). The duration is NOT proven -- asm/4209's own header grades that
                 * call as low confidence -- so no number is invented here. The line stays in
                 * the CMD window until the next message displaces it, which is readable. */
                return U2_TRANSACT_TREATED;
            }
            engine_gap(ctx, "no dialogue for that role");
            return U2_TRANSACT_NO_DEALER;
        }

        /* role < 0x80: asm/40CB walks the creature-type chain. Type 0x6C is the tribute and
         * is handled above; the remaining arms (0x60 guard, 0x64 jester, 0x68 merchant,
         * 0xF0 fighter, 0xF4 cleric, 0xF8 wizard, 0xFC thief) are the street_* handlers and
         * are still open -- each prints one fixed line held in the executable, so they need
         * extractor entries rather than a loader. */
        /* asm/40CB..asm/41D9: the typed chain. Each arm is a compare on the creature TYPE at
         * [slot+0x197], and on a match a `call str_out` over one fixed literal followed by a
         * tail jump to cmd_done. The order below is the order the bytes test in, and the order
         * is the behaviour: 0x6C was already taken by the tribute above, so this chain starts
         * at the guard.
         *
         * None of these seven handlers has ever been seen executing in the 60-harvest corpus,
         * so the strings are the evidence and they are now extracted rather than typed by hand.
         * asm/4108's own `# name-evidence:` line claims 'A JESTER SAYS:' while the db line it
         * cites says "A JESTER SINGS:"; the bytes win and the header is the hypothesis. */
        switch (st->mon_type[slot]) {
        case 0x60u: seam_str_out(ctx, U2_S_STREET_GUARD);      return U2_TRANSACT_TREATED;
        case 0x64u: seam_str_out(ctx, U2_S_STREET_JESTER);     return U2_TRANSACT_TREATED;
        case 0x68u: seam_str_out(ctx, U2_S_STREET_MERCHANT);   return U2_TRANSACT_TREATED;
        case 0xF0u: seam_str_out(ctx, U2_S_STREET_FIGHTER);    return U2_TRANSACT_TREATED;
        case 0xF4u: seam_str_out(ctx, U2_S_STREET_CLERIC);     return U2_TRANSACT_TREATED;
        case 0xF8u: seam_str_out(ctx, U2_S_STREET_WIZARD);     return U2_TRANSACT_TREATED;
        case 0xFCu: seam_str_out(ctx, U2_S_STREET_THIEF);      return U2_TRANSACT_TREATED;
        default:
            /* asm/41D5's last compare falls through to asm/4206, whose forced-kept `jmp`
             * lands on asm/40A6 -- the same "FUNNY, NO RESPONSE!" the tile refusals print.
             * So an ordinary monster aimed at with TRANSACT does answer, in its way, and this
             * is a cited byte rather than an engine affordance. */
            seam_str_out(ctx, U2_S_NO_RESPONSE);
            return U2_TRANSACT_NO_DEALER;
        }
    }

    /* asm/407C: the near cell. asm/407F's `js` skips the counter test when the tile has
     * its high bit set, which is what lets a ray cross a shop's sign band — the sign
     * letters are high-bit tiles. That is the whole answer to the ray-extent question in
     * the handoff: the ray is exactly two cells, and the sign row is not an obstacle
     * because it is the transparent case. */
    const uint8_t near1 = u2_world_tile_at(&ow->world, cx, cy);
    if (!(near1 == TILE_SHOP_COUNTER || (near1 & 0x80u))) {
        seam_str_out(ctx, U2_S_NO_RESPONSE);
        return U2_TRANSACT_NO_DEALER;
    }

    /* asm/4085 steps the cursor by the same delta and asm/409F requires the merchant
     * marker there. On a match asm/40A3 tail-jumps to asm/1590. */
    const int fx = (cx + dx) & (U2_SECTOR_EDGE - 1);
    const int fy = (cy + dy) & (U2_SECTOR_EDGE - 1);
    if (u2_world_tile_at(&ow->world, fx, fy) != TILE_MERCHANT_MARK) {
        seam_str_out(ctx, U2_S_NO_RESPONSE);
        return U2_TRANSACT_NO_DEALER;
    }

    const u2_shop shop = u2_commerce_shop_for(ow->x, ow->y, st->sector_second);
    switch (shop) {
    case U2_SHOP_ARMOUR:    return shop_armour(ctx);
    case U2_SHOP_WEAPONS:   return shop_weapons(ctx);
    case U2_SHOP_GROCERY:   return shop_grocery(ctx);
    case U2_SHOP_TRANSPORT: return shop_transport(ctx);
    case U2_SHOP_CLERIC:
    case U2_SHOP_WIZARD:
        /* Both gated on class upstream (asm/184C tests [DS:47] == 1, asm/19BA tests
         * [DS:47] == 2) and both wired 2026-11-15. What was blocking this was the question the
         * previous draft of this branch refused to guess, and the answer is that the axes really
         * are different: asm/19D7's `cmp $0x4 ; jb ; adc $0x3` moves the wizard's menu digits
         * 4/5/6 onto pool slots 7/8/9, and the price is quoted on the SLOT. See sell_spell. */
        return sell_spell(ctx, shop == U2_SHOP_WIZARD);
    case U2_SHOP_CLUE:
        /* GAP — the clue seller takes digit x 100 by byte placement (the digit goes into
         * the hundreds byte and the low byte is zeroed; there is no multiply), but the clue
         * itself is a pool this project has not mapped to a selection rule. */
        engine_gap(ctx, "clue seller not implemented -> clue pool");
        return U2_TRANSACT_NO_DEALER;
    case U2_SHOP_PUB:
        /* GAP — the pub's 1-BUY is a constant 5 gold and its 2-TIP drives the barkeep
         * chatter table (asm/155D + asm/16BD), which is read but has never been observed
         * executing. Half a pub is worse than none. */
        return U2_TRANSACT_NO_DEALER;
    case U2_SHOP_NONE:
        break;
    }
    seam_str_out(ctx, U2_S_NO_RESPONSE);
    return U2_TRANSACT_NO_DEALER;
}

/* ---- the two shoppes that sell spells -------------------------------------- */

#ifdef U2_T_SPELLS_N
#  define SPELL_AT(i) (((i) >= 0 && (i) < U2_T_SPELLS_N) ? U2_T_SPELLS[(i)] : "")
#else
#  define SPELL_AT(i) ""
#endif

/* asm/1869 (the cleric shrine) and asm/19D7 (the magic shoppe) are one routine wearing two
 * vocabularies. Both:
 *
 *   greet, print a six-item menu, read a digit
 *   0 or >= 7 -> the shoppe's own farewell, and nothing else happens
 *   "FIVE " + the spell NAME from pool[0x24 + slot] + "S FOR " + the price, then ask
 *   anything other than 'Y' is a refusal
 *   'Y' -> the shoppe's yes line, charge, then ADD FIVE to DS:B6[slot] in packed decimal
 *
 * Four things the bytes insist on that a rewrite would lose.
 *
 * The price is asm/20AB called with the SLOT, not the menu digit, so both shoppes price the
 * same spell identically and the wizard's remap moves the price along with the spell.
 *
 * The quantity is five, not one, and it is `clc ; adc $0x5,%al ; daa` with no ceiling: 99 plus
 * five is 04. The cleric says the number out loud -- "I WILL TAKE 5!" -- which is the only
 * place in the game five is spoken, and it is about the QUANTITY, not the gold. A per-unit
 * price read out of that line is a misreading of it, and it is the shape of the disagreement
 * the price column in disasm/source-third-party-spells.tsv is still carrying.
 *
 * The wizard's menu digit is not the slot. asm/19D7's `cmp $0x4 ; jb ; adc $0x3` maps 4/5/6
 * onto 7/8/9, because the three spells only the wizard sells sit at pool slots 7/8/9. The
 * cleric needs no remap for the same reason in reverse: its three extras are PASSWALL,
 * SURFACE and PRAYER, which are slots 4/5/6. One +3, and its cause is the pool's layout
 * rather than anything about classes.
 *
 * Neither shoppe tests a class here. The gate is upstream -- asm/184C refuses a non-cleric at
 * the shrine and asm/19BA refuses a non-wizard at the shoppe -- and by the time this runs the
 * class question is settled. Adding a second test would be a new bug, exactly as it would in
 * cmd_C.
 */
static u2_transact_result sell_spell(u2_ctx *ctx, int wizard)
{
    u2_state *st = &ctx->state;

    if (wizard) {
        seam_str_out(ctx, U2_S_SHOP_WIZ_WELCOME);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_SHOP_MENU_LADDERS);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_SHOP_WIZ_LINE2);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_SHOP_WIZ_PROMPT);
    } else {
        seam_str_out(ctx, U2_S_SHOP_CLERIC_WELC);
        /* asm/1869: `mov 0x46,%al ; cmp $0x1,%al ; je SISTER`. [DS:46] is the sex byte, and
         * the byte the create flow leaves there is the typed character -- 'M' or 'F' -- which
         * is what the Z screen compares against and what the API reports. An immediate 1 is
         * therefore never equal, and read literally the shrine calls every visitor BROTHER.
         * INFERRED, not observed: no capture has stood a female cleric at a shrine. The
         * comparison is written against the value the bytes test rather than against 'F',
         * because making it say what the greeting is expected to mean would be the invention;
         * if a capture ever shows SISTER, the thing that was wrong is [DS:46]'s meaning and
         * this line changes with it. See docs/needs-gameplay.md. */
        seam_str_out(ctx, st->sex == 1 ? U2_S_SHOP_CLERIC_SIS : U2_S_SHOP_CLERIC_BRO);
        /* asm/1869 then calls L_00527a, and asm/507A is a one-instruction stub in this tree,
         * so what it emits is not reconstructed. The engine prints the character's name, which
         * is what "BROTHER " reads as and is the only thing in the record that fits. Graded
         * inferred and named here rather than left as an unremarked gap in the greeting. */
        seam_str_out(ctx, (const char *)st->name);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_SHOP_MENU_LADDERS);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_SHOP_CLERIC_LINE2);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_SHOP_CLERIC_PROMPT);
    }

    const int digit = read_digit(ctx);
    /* `call L_002228 ; je bye` then `cmp $0x7,%al ; jae bye`: the emitter's zero test and the
     * bound together mean the accepted set is 1..6 and nothing else. */
    if (digit <= 0 || digit > 6) {
        seam_str_out(ctx, wizard ? U2_S_SHOP_WIZ_BYE : U2_S_SHOP_CLERIC_BYE);
        return U2_TRANSACT_REFUSED;
    }

    int slot = digit;
    if (wizard)
        slot = u2_shop_wizard_slot(digit);

    seam_str_out(ctx, U2_S_SHOP_FIVE);
    seam_str_out(ctx, SPELL_AT(slot));
    seam_str_out(ctx, U2_S_SHOP_S_FOR);

    uint8_t price[2];
    u2_price_from_stats((uint8_t)slot, st->stat_int, st->stat_cha, price);
    print_price(ctx, price);

    seam_char_print(ctx, '\r');
    seam_str_out(ctx, wizard ? U2_S_SHOP_WIZ_YESQ : U2_S_SHOP_CLERIC_YESQ);
    if (!read_yes(ctx)) {
        if (wizard) {
            /* The wizard's refusal is two lines: "NO!" and then the same farewell the
             * out-of-range digit gets, because the not-Y arm jumps back to it. */
            seam_str_out(ctx, U2_S_SHOP_WIZ_NO);
            seam_str_out(ctx, U2_S_SHOP_WIZ_BYE);
        } else {
            seam_str_out(ctx, U2_S_SHOP_CLERIC_NO_A);
            seam_char_print(ctx, '\r');
            seam_str_out(ctx, U2_S_SHOP_CLERIC_NO_B);
        }
        return U2_TRANSACT_REFUSED;
    }

    if (wizard) {
        seam_str_out(ctx, U2_S_SHOP_WIZ_YES);
    } else {
        seam_str_out(ctx, U2_S_SHOP_CLERIC_YES_A);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_SHOP_CLERIC_YES_B);
    }

    /* asm/2252, the same charge asm/2052 uses everywhere else, and it runs BEFORE the count
     * moves. Whether the original underflows gold when there is none is not what this models;
     * the engine refuses, and the yes line has already printed, which is the original's order
     * and the original's cosmetic consequence. */
    if (!u2_spend_price(ctx, price))
        return U2_TRANSACT_REFUSED;

    if (slot >= 0 && slot < (int)(sizeof st->spell_counts / sizeof st->spell_counts[0]))
        st->spell_counts[slot] = u2_bcd_add_five(st->spell_counts[slot]);
    return U2_TRANSACT_OPEN;
}

/* asm/19D7's menu remap, split out so it can be gated: `cmp $0x4,%al ; jb keep ; clc ;
 * adc $0x3,%al`.
 *
 * The wizard's menu offers 1-LIGHT, 2-L.D., 3-L.U., 4-M.M., 5-BLINK, 6-KILL, and the spell
 * pool holds MAGIC MISSILE, BLINK and KILL at slots 7, 8 and 9. So the menu digit and the
 * ready number are different axes for 4/5/6 and the same axis for 1/2/3, and the +3 is a
 * fact about the pool's layout rather than about wizards. The cleric needs no remap because
 * its three extras -- PASSWALL, SURFACE, PRAYER -- are slots 4/5/6 and already line up.
 *
 * This is the rule a rewrite silently gets wrong in the interesting direction: buying 6-KILL
 * and storing it at slot 6 leaves the player holding a spell that `M 6` reads as PRAYER and
 * `M 9` reads as empty, which is not a crash and not even visible in the shop. */
int u2_shop_wizard_slot(int menu_digit)
{
    return menu_digit < 4 ? menu_digit : menu_digit + 3;
}

/* asm/1869 and asm/19D7 both finish a sale with `clc ; mov 0x1f,%bl ; mov %bx,%di ;
 * mov 0xb6(%di),%al ; adc $0x5,%al ; daa ; mov %al,0xb6(%di)`: five charges, packed decimal,
 * and no ceiling anywhere in the sequence. 99 plus five is 04.
 *
 * Deliberately NOT routed through u2_bcd_inc_sat. The saturation policy exists for the
 * counters the owner ruled cap; this one demonstrably does not, and a spell count that
 * saturates would be a quieter divergence than any of the ones this project has caught. */
uint8_t u2_bcd_add_five(uint8_t count)
{
    int cf = 0;
    return daa_add(count, 0x05, 0, &cf);
}

/* asm/3FAB..asm/3FDD's branch structure, with the rolls taken out. The quadrant tests in the
 * original read DS:00/DS:01 (the player) and DS:4A (the site kind) directly, and they are
 * written as a chain of jae/jb/jne exits to the same NO LUCK label rather than as a table, so
 * the table here is the reading, not a restatement of the shape. */
u2_steal_result u2_steal_loot_for(int x, int y, int site_kind)
{
    const int east  = x >= 0x20;
    const int north = y <  0x20;

    if (site_kind == 1 && north && east) return U2_STEAL_FOOD;
    if (site_kind == 2 && north)         return east ? U2_STEAL_ARMOUR : U2_STEAL_WEAPONS;
    return U2_STEAL_NO_LUCK;
}

/* asm/3F37 + asm/3F5D — cmd_S, STEAL. See commerce.h for the table and
 * asm/3F5D_steal_attempt.asm for the byte-level reading it comes from.
 *
 * The shape is worth stating plainly because it is the opposite of what the word suggests. A
 * player who types S is not pickpocketing anyone: there is no creature scan in the chain at
 * all. The ray lands on the merchant marker, two rolls decide luck, and then the quadrant the
 * PLAYER occupies picks which category of goods is added to the inventory — the same quadrant
 * table asm/1590 uses to decide which shop is standing there, which is the sense in which the
 * shop is being robbed. Nothing is taken from the vendor's stock, because there is no stock:
 * the owned-count arrays simply go up.
 *
 * And it is NOT asm/0FCD_combat_thief_steal, which is a THIEF monster emptying one of the
 * player's slots during a fight. commands.c had those two conflated for four days on the
 * strength of the shared English word, and the CP6 gate exists partly so that conflation
 * cannot come back quietly. */

/* asm/3F7E, the one refusal exit. Every failed steal reaches it — asm/3F59's near-cell miss
 * and asm/3F5D's merchant-marker miss, both luck rolls, the already-alerted test and every
 * quadrant miss all jump here — and it prints NO LUCK!, then rolls a THIRD time and, when the
 * low three bits come out zero (one time in eight), calls asm/113A, which is u2_ai_alarm: the
 * whole town's guards go to role 1. So a botched steal has a ~12.5% chance of alerting the
 * sector. Decoded 2026-11-13; the consequence was previously left undecoded and unmodelled. */
static u2_steal_result steal_no_luck(u2_ctx *ctx)
{
    seam_str_out(ctx, U2_S_STEAL_NO_LUCK);
    if ((u2_rand_byte(ctx) & 0x7u) == 0u)
        u2_ai_alarm(ctx);
    return U2_STEAL_NO_LUCK;
}

u2_steal_result u2_commerce_steal(u2_ctx *ctx, u2_overworld *ow, int dx, int dy)
{
    u2_state *st = &ctx->state;

    /* asm/3F37: one step, and asm/3F59's test on the cell there. Identical to TRANSACT's
     * near-cell rule — a sign tile (high bit set) or the counter tile 0x7C — because it is the
     * same ray, not a similar one. */
    const int cx = (ow->x + dx) & (U2_SECTOR_EDGE - 1);
    const int cy = (ow->y + dy) & (U2_SECTOR_EDGE - 1);
    const uint8_t near1 = u2_world_tile_at(&ow->world, cx, cy);
    if (!(near1 == TILE_SHOP_COUNTER || (near1 & 0x80u)))
        return steal_no_luck(ctx);

    /* asm/3F5D steps again and requires the merchant marker. */
    const int fx = (cx + dx) & (U2_SECTOR_EDGE - 1);
    const int fy = (cy + dy) & (U2_SECTOR_EDGE - 1);
    if (u2_world_tile_at(&ow->world, fx, fy) != TILE_MERCHANT_MARK)
        return steal_no_luck(ctx);

    /* asm/3FA0: a non-thief faces a roll that a thief (class 3) skips, and then everyone faces
     * a second one. The test is `js` after asm/5217, whose last flag-setting instruction is
     * `adc 0x244,%al`, so the sign is bit 7 of the byte it returns — half the time, and
     * measured rather than assumed: rand_byte's body is five bytes of stc/adc chain into AL.
     *
     * So the owner's recollection that stealing "probably needs a thief" is half right: a
     * thief is not required, it just fails half as often. */
    if (st->pclass != 3 && (u2_rand_byte(ctx) & 0x80u))
        return steal_no_luck(ctx);
    if (u2_rand_byte(ctx) & 0x80u)
        return steal_no_luck(ctx);

    /* asm/3FAB. Refuses on [DS:1D7] == 1, and DS:1D7 is mon_role[0] — the base of the role
     * array, read un-indexed, on a path with no creature scan to have indexed it. The engine
     * reproduces the byte rather than guessing which way the guess goes; see the caveat in
     * asm/3F5D_steal_attempt.asm. */
    if (st->mon_role[0] == 1)
        return steal_no_luck(ctx);

    /* The quadrant, on the player's own position and the site kind. It is ow->x/ow->y and NOT
     * st->player_x: that field is named as though it were the player, but state.h puts it at
     * DS:0x23, which is the staged candidate cell the step gate and the ray cursor use, and
     * dump_state.c carries a warning about exactly that confusion. asm/3F5D reads DS:00/DS:01,
     * which is the player, which is ow->x/ow->y — the same pair u2_commerce_shop_for is called
     * with. */
    switch (u2_steal_loot_for(ow->x, ow->y, st->sector_second)) {
    case U2_STEAL_FOOD: {
        /* asm/3FDD: the village grocery. One unit of food, packed decimal, on DS:53 — the
         * same byte asm/181A moves when you BUY it, so a stolen meal and a bought one are
         * indistinguishable in the save. */
        seam_str_out(ctx, U2_S_STEAL_FOOD);
        st->food = u2_bcd_inc_sat(st->food, U2_SATURATE(ctx));
        return U2_STEAL_FOOD;
    }
    case U2_STEAL_ARMOUR:
        /* asm/3FF9: `roll & 3`, +1, then `adc $1 / das` on [DS:96 + that]. armour_owned
         * starts at DS:97, so the struct index is the mask itself: CLOTH..PLATE, and
         * REFLECT and POWER are unreachable. */
        seam_str_out(ctx, U2_S_STEAL_ARMOUR);
        { const int i = u2_rand_byte(ctx) & 3u;
          st->armour_owned[i] = u2_bcd_inc_sat(st->armour_owned[i], U2_SATURATE(ctx)); }
        return U2_STEAL_ARMOUR;
    case U2_STEAL_WEAPONS:
        /* asm/4024: `roll & 7`, +1, on [DS:76 + that]. weapons_owned[0] is HANDS and the +1
         * is what keeps it unstolen; the same +1 puts the ceiling at index 8, so the QUICK
         * SWORD cannot be stolen either. */
        seam_str_out(ctx, U2_S_STEAL_WEAPONS);
        { const int i = (u2_rand_byte(ctx) & 7u) + 1;
          st->weapons_owned[i] = u2_bcd_inc_sat(st->weapons_owned[i], U2_SATURATE(ctx)); }
        return U2_STEAL_WEAPONS;
    case U2_STEAL_NO_LUCK:
        /* The south halves, and every site kind that is neither a town nor a village. The game
         * has no arm here for a transport shoppe, a shrine, a pub, a magic shoppe or a clue
         * seller, so "NO LUCK!" is the complete answer and not a stub. */
        return steal_no_luck(ctx);
    }
    return steal_no_luck(ctx);
}

/* ---- cmd_O: OFFER GOLD -------------------------------------------------- */

/* The pure half, so a gate can enumerate every roll and every amount instead of sampling a
 * seeded RNG. Mirrors asm/3C2E L_003f7e..L_003fa8: ONE draw decides both whether the raise
 * fires and which stat it lands on, and the increment is four `adc 0x27,%al ; daa` with the
 * carry chained across all four. Returns 1 when it fires (`r < 6`, so 6 of 8 = 75%). */
int u2_offer_raise_for(uint8_t roll, uint8_t hundreds, uint8_t cur_bcd,
                       int *stat_index, uint8_t *new_bcd)
{
    const int r = roll & 7u;
    if (r >= 6)
        return 0;
    int cf = 0;
    uint8_t v = cur_bcd;
    for (int i = 0; i < 4; ++i)
        v = daa_add(v, hundreds, cf, &cf);
    *stat_index = r;
    *new_bcd = v;
    return 1;
}

/* The six stat bytes are contiguous at DS:4B..DS:50 in this order, which is what lets the
 * roll index them directly the way `mov 0x4b(%di),%al` does. */
#define U2_OFFER_STAT_N 6

u2_offer_result u2_commerce_offer(u2_ctx *ctx, u2_overworld *ow, int dx, int dy)
{
    u2_state *st = &ctx->state;
    uint8_t *const stats[U2_OFFER_STAT_N] = {
        &st->stat_str, &st->stat_agi, &st->stat_sta,
        &st->stat_cha, &st->stat_wis, &st->stat_int
    };

    /* asm/25E6 leaves the aimed creature's slot in DI and cmd_O stores it to DS:1F, then
     * tests it against zero. The engine's equivalent is the one-cell scan TRANSACT uses
     * (slots 31 down to 1, slot 0 never examined), and the two agree on what zero means:
     * because slot 0 is not a possible result, "nothing found" and "DS:1F == 0" are the same
     * value. asm/25E6's own body has not been read, so the extent is inherited from the
     * decoded scan rather than claimed for cmd_O's bytes. */
    const int cx = (ow->x + dx) & (U2_SECTOR_EDGE - 1);
    const int cy = (ow->y + dy) & (U2_SECTOR_EDGE - 1);
    int slot = 0;
    for (int i = U2_MON_SLOTS - 1; i >= 1; i--) {
        if (st->mon_type[i] == 0)
            continue;
        if (st->mon_x[i] == (uint8_t)cx && st->mon_y[i] == (uint8_t)cy) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        seam_str_out(ctx, U2_S_OFFER_TO_WHOM);
        return U2_OFFER_NOBODY;
    }

    /* asm/3C2E asks for the amount only AFTER it knows there is a recipient, so a miss never
     * prompts for one. One digit of hundreds into DS:27, exactly as the shops read their
     * menu digits. */
    seam_str_out(ctx, U2_S_OFFER_HOW_MUCH);
    const int d = read_digit(ctx);
    if (d < 0)
        return U2_OFFER_NOBODY;                 /* caller gone; nothing printed, nothing moved */
    const uint8_t hundreds = (uint8_t)d;

    /* The offer is PAID, and it is paid before the game decides who is being paid.
     * asm/3C2E stores the digit to DS:27, zeroes DS:28, and calls L_002252, which is
     * asm/2052_gold_spend_price — a routine whose own header lists cmd_O among its five
     * inbound callers. DS:27 subtracts from the HIGH gold byte, so one unit is 100 gold and
     * 5 costs 500. If the BCD subtract borrows, asm/2070 adds the pair back, prints
     * YOU DONT HAVE THAT MUCH! and tail-jumps to the command loop instead of returning, so
     * this arm prints nothing further.
     *
     * This call was missing here entirely, and the owner reported the omission on 2026-11-11:
     * with 317 gold he offered 500 to the sector-32 ring man and got EARN THE RING!. The
     * original never reaches the blessing check on too little money; it says YOU DONT HAVE
     * THAT MUCH! and takes nothing. The amount is therefore a price and not a threshold, and
     * the `cmp $0x5` below is a minimum on top of it. Because the charge precedes the role
     * gate it applies to every recipient: a 1-hundred offer to a random townsman costs 100
     * gold and then says THANK YOU VERY MUCH!, and the Hotel clerk's stat raise is a paid
     * service at 100 gold per 4 points.
     *
     * How it was missed: a grep of cmd_O's own bytes for the gold displacements 0x58/0x59
     * came back empty and the conclusion was drawn that the routine does not touch gold.
     * True of the 0x18E bytes in that file, wrong about the routine — the deduction is one
     * call away, in a file whose name says what it does. */
    const uint8_t price[2] = { hundreds, 0x00 };   /* DS:27 = high, DS:28 = low */
    if (!u2_spend_price(ctx, price))
        return U2_OFFER_BROKE;

    /* Bit 7 of the role byte decides everything, and the whole special path is additionally
     * gated on sector 32 -- `[DS:4A]==2 && [DS:49]==3`. Outside that one town a quest role
     * prints the same THANK YOU a plain creature gets, which is a byte-level anchor for
     * sector 32 being the Hotel California town. */
    const uint8_t role = st->mon_role[slot];
    if (!(role & 0x80u) || st->sector_second != 2 || st->sector_first != 3) {
        seam_str_out(ctx, U2_S_OFFER_THANK_YOU);
        return U2_OFFER_THANK_YOU;
    }

    switch (role) {
    case 0x81:
        /* ENILNO IS YOURS! DS:7F is weapons_owned[9], the QUICK SWORD -- the one weapon no
         * shoppe sells and no steal can take, which is why this is the only source. */
        if (hundreds < 5) {
            seam_str_out(ctx, U2_S_OFFER_THANK_YOU);
            return U2_OFFER_BELOW;
        }
        seam_str_out(ctx, U2_S_OFFER_ENILNO);
        st->weapons_owned[9] = 1;
        return U2_OFFER_QUICKSWORD;

    case 0x82:
        if (hundreds < 5) {
            seam_str_out(ctx, U2_S_OFFER_THANK_YOU);
            return U2_OFFER_BELOW;
        }
        if (st->blessing == 0) {                 /* DS:6C, set by Father Antos */
            seam_str_out(ctx, U2_S_OFFER_EARN_RING);
            return U2_OFFER_EARN_RING;
        }
        seam_str_out(ctx, U2_S_OFFER_RING_IS_YOURS);
        st->items_held[0] = 1;                   /* DS:D6, the RING slot */
        return U2_OFFER_RING;

    case 0x83: {
        seam_str_out(ctx, U2_S_OFFER_HERE_TAKE_THIS);
        const int i = (u2_rand_byte(ctx) & 7u) + 1;   /* DAGGER..PHASER, never QUICK SWORD */
        st->weapons_owned[i] = u2_bcd_inc_sat(st->weapons_owned[i], U2_SATURATE(ctx));
        return U2_OFFER_WEAPON;
    }

    default: {
        /* The grind. One roll, one stat, four BCD adds, and deliberately no ceiling guard:
         * that is why a stat past 99 wraps and why 95 is the last safe stop. */
        const uint8_t roll = u2_rand_byte(ctx);
        const int idx = roll & 7u;
        if (idx >= 6) {                       /* the 2 of 8 that refuses */
            seam_str_out(ctx, U2_S_OFFER_THANK_YOU);
            return U2_OFFER_REFUSED;
        }
        int unused = 0;
        uint8_t out = 0;
        (void)u2_offer_raise_for(roll, hundreds, *stats[idx], &unused, &out);
        /* The raise itself is left byte-faithful inside u2_offer_raise_for — its unit test
         * pins the 99 -> 04 carry chain the original actually performs, and that has to stay
         * true for the original-fidelity run. The ceiling is applied here, at the store, so
         * enhanced mode stops at 99 while fidelity still wraps. A stat that came back lower
         * than it went in is the wrap; hold the old value instead. */
        if (U2_SATURATE(ctx) && out < *stats[idx])
            out = *stats[idx];
        *stats[idx] = out;
        seam_str_out(ctx, U2_S_OFFER_ALAKAZAM);
        return U2_OFFER_RAISE;
    }
    }
}
