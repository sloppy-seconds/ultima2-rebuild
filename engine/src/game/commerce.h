#ifndef U2_COMMERCE_H
#define U2_COMMERCE_H

/*
 * Commerce — TRANSACT, the vendor selector, and the shops it reaches.
 *
 * The original has no goods table and no price table (docs/commerce.md § 1: measured
 * negative over every printable string in the executable). What it has instead is a
 * position test and one arithmetic routine:
 *
 *   - cmd_T (asm/4050) aims a two-cell ray in the typed direction. The cell next to the
 *     player must be a shop sign (high-bit tile) or 0x7C, and the cell beyond it must be
 *     0x68, the merchant marker. Anything else is FUNNY, NO RESPONSE!.
 *   - On a hit it tail-jumps to asm/1590, which is not a menu-versus-line fork at all: it
 *     is a quadrant switch on the player's own position, and a site-kind test inside each
 *     quadrant. Eight destinations, four catalogues for a town (kind 2) and four for a
 *     village (any other kind). See u2_commerce_shop_for().
 *   - Every shop then computes its price with asm/20AB_bcd_price_from_stats — one routine,
 *     called with a per-shop index — writes the pair into DS:27/28, prints it, and charges
 *     the SAME pair through asm/2052_gold_spend_price. There is no second pricing path.
 *
 * The two facts that used to make this look unbuildable were both bad observations, not
 * bad bytes: the grocery quote is 0020 and not 0200, and an axe is 20 and not 25. Both
 * were re-measured live against the printer and the charge on 2026-10-29
 * (captures/spice86/g23-food-live, captures/spice86/g23-weapons) and asm/20AB reproduces
 * both. docs/commerce.md § 3b carries the corrections.
 */

#include "core/ctx.h"
#include "game/overworld.h"

/* The eight asm/1590 destinations, named for what is standing there. */
typedef enum u2_shop {
    U2_SHOP_NONE = 0,     /* the ray missed: FUNNY, NO RESPONSE! */
    U2_SHOP_ARMOUR,       /* asm/1D1D  east half, north half, town   */
    U2_SHOP_GROCERY,      /* asm/1799  east half, north half, village */
    U2_SHOP_WEAPONS,      /* asm/1E4F  west half, north half, town   */
    U2_SHOP_CLUE,         /* asm/1AE0  west half, north half, village */
    U2_SHOP_TRANSPORT,    /* asm/1F75  east half, south half, town   */
    U2_SHOP_CLERIC,       /* asm/1841  east half, south half, village */
    U2_SHOP_PUB,          /* asm/15E4  west half, south half, town   */
    U2_SHOP_WIZARD        /* asm/19AF  west half, south half, village */
} u2_shop;

/* The ray's outcome, so a caller (and a test) can tell the three ways this ends apart
 * without reading printed text. */
typedef enum u2_transact_result {
    U2_TRANSACT_NO_DEALER = 0,  /* FUNNY, NO RESPONSE! — the ray found nothing, or the
                                   cursor held a creature and the talking half is not
                                   implemented here (see commerce.c's GAP note) */
    U2_TRANSACT_OPEN,           /* a shop was reached and its menu printed */
    U2_TRANSACT_REFUSED,        /* a shop was reached and declined the transaction */
    U2_TRANSACT_TREATED         /* the cursor held a type-0x6C creature and the tribute ran:
                                   gold taken and HP raised. Distinct from OPEN because a
                                   test must be able to tell "the tribute fired" from "a
                                   shop menu printed" without grepping for text. */
} u2_transact_result;

/*
 * asm/1590_sub_1590 + its tiling fragments 1598..15E0, read as the one routine they are.
 * Pure function of the player's position and the site kind, which is why it is separate:
 * the quadrant is the player's X/Y (DS:00/01), not the vendor's, and not the sign's.
 *
 *   x >= 0x20, y <  0x20 -> town: armour,   else grocery
 *   x <  0x20, y <  0x20 -> town: weapons,  else the clue seller
 *   x >= 0x20, y >= 0x20 -> town: transport, else the cleric shrine
 *   x <  0x20, y >= 0x20 -> town: the pub,   else the wizard shoppe
 *
 * Corroboration, not inference: run over the fifty sign/NPC pairings in the shipped data
 * (tools/shop_access.py) the sign text names the predicted shop wherever the sign names a
 * shop at all — WEAPONS, ARMOURY, TRANSPORT, PUB, CLERGY, ORACLE, SEER, WIZARDRY, DELI,
 * COOK all land on their own catalogue, in six different sectors of both kinds.
 */
u2_shop u2_commerce_shop_for(int x, int y, int site_kind);

/*
 * The gold-for-HP tribute ladder, as a pure function so the gate can enumerate all 256
 * inputs rather than sample them.
 *
 * asm/42AA..42CB is four cmp/jb rungs on [DS:51] — the HIGH packed-decimal byte of HP — each
 * preloading BL with its own value. The result is a raise in hundreds, and it is INVERSE to
 * need: the more wounded the player, the larger the gift.
 *
 *   hp_high <  0x50 -> 3   (+300)
 *   hp_high <  0x75 -> 2   (+200)
 *   hp_high <  0x99 -> 1   (+100)
 *   else            -> 0
 *
 * The last rung is an overflow guard, not a surcharge. At 0x99 a further +100 would carry
 * past a packed-decimal byte, so returning 0 is the thing that stops HP ever exceeding
 * 9999. Observed rung by rung on 2026-10-29 in captures/spice86/lord-british-tribute, 54
 * offers from HP 1 to HP 9901.
 */
uint8_t u2_tribute_raise_for(uint8_t hp_high);

/*
 * asm/20AB_bcd_price_from_stats, mirrored instruction for instruction.
 *
 * Seeds two packed-decimal pairs to 0004, takes the loop count as the bit length of
 * int + charisma added as raw BCD bytes, and walks the pairs a Fibonacci step at a time:
 * P1 <- P1 + P2, P2 <- old P1. The result is P1, the pair every shop prints and charges.
 * Reachable values are 4, 8, 12, 20, 32, 52, 84, 136, 220, 356, 576, 932, ... so a higher
 * int + charisma means a shorter walk and a LOWER price — which is the direction the owner
 * remembered charisma moving prices, and the direction a third-party grocery table agrees.
 *
 * price[0] is DS:27 (the high byte, printed first) and price[1] is DS:28.
 */
void u2_price_from_stats(uint8_t index, uint8_t stat_int, uint8_t stat_cha,
                         uint8_t price[2]);

/* asm/2052_gold_spend_price: subtract the price pair from gold. Returns 1 when the gold
 * moved and 0 when the player could not pay, in which case the refusal has been printed and
 * the original abandons the shop entirely (asm/2052's failure arm pops its own return
 * address and tail-jumps to the command loop) — so a caller must not print a success line
 * after a 0. */
int u2_spend_price(u2_ctx *ctx, const uint8_t price[2]);

/*
 * The whole cmd_T body after the direction is known: the guards, the ray, the dispatcher
 * and the shop. `dx`/`dy` are the aim deltas the original leaves in DS:19/1A (asm/2362's
 * aim gate: decb/incb on one byte, the other left zero).
 *
 * Prints the shop's own text, or FUNNY, NO RESPONSE!. Returns the outcome for the caller's
 * bookkeeping; the engine's command epilogue is not this function's business.
 */
u2_transact_result u2_commerce_transact(u2_ctx *ctx, u2_overworld *ow, int dx, int dy);

/*
 * asm/3F37 + asm/3F5D — cmd_S, STEAL.
 *
 * The same two-cell ray as TRANSACT, aimed at the same merchant marker, through the same
 * asm/2362 gate: cmd_S's call to it sits at asm/3F36 as a bare forced-keep `e8`, which is how
 * it stayed invisible for as long as cmd_T's did. What differs is what happens on a hit. There
 * is no creature scan, no menu and no price: the game rolls, and then the quadrant the PLAYER
 * is standing in decides what category of goods falls off the shelf into the inventory, at
 * random within that category.
 *
 *   village (kind 1), north-east -> food, one unit, packed decimal
 *   town    (kind 2), north-east -> armour, random of 4 (CLOTH..PLATE)
 *   town    (kind 2), north-west -> weapons, random of 8 (DAGGER..PHASER)
 *   anything else                -> NO LUCK!
 *
 * So the south halves of a town cannot be robbed, and neither can a clue seller, a shrine, a
 * pub or a magic shoppe: the game does not have those arms. The high-end goods are out of
 * reach too — REFLECT and POWER armour and the QUICK SWORD are never selected by the mask.
 *
 * Prints its own line either way. Returns the outcome so a test can assert on the branch
 * rather than on the text.
 */
typedef enum u2_steal_result {
    U2_STEAL_NO_LUCK = 0,
    U2_STEAL_FOOD,
    U2_STEAL_ARMOUR,
    U2_STEAL_WEAPONS
} u2_steal_result;

u2_steal_result u2_commerce_steal(u2_ctx *ctx, u2_overworld *ow, int dx, int dy);

/*
 * The loot half of asm/3F5D on its own, so it can be enumerated the way u2_commerce_shop_for
 * is. Two of the three outcomes are chosen by a roll *within* the category, which is not a
 * pure question; which category at all is a pure question, and it is this one:
 *
 *   kind 1 (village), north-east -> food
 *   kind 2 (town),    north-east -> armour
 *   kind 2 (town),    north-west -> weapons
 *   everything else              -> nothing stealable
 *
 * Note it is NOT the same partition as the shop selector: the selector has eight cells and
 * this has three filled ones. A village's north-west is the clue seller and a town's south
 * half is the transport shoppe, and asm/3F5D has no arm for either, so they come back NONE
 * rather than defaulting to something plausible.
 */
u2_steal_result u2_steal_loot_for(int x, int y, int site_kind);

/*
 * asm/3C2E — cmd_O, OFFER GOLD.
 *
 * The handler the third-party walkthrough calls Hotel California, and the only place in the
 * game that grants the two endgame items. Its shape is not what the word "offer" suggests:
 *
 *   - It aims the SAME one-cell creature scan TRANSACT uses. asm/3C2E stores the found slot
 *     into DS:1F and then tests it against zero, and slot 0 is never a possible result of
 *     that scan — so "nothing there" and "slot 0" are the same value, which is the property
 *     asm/23E6 already has.
 *   - The amount is read AFTER the target is known, as ONE digit of hundreds (DS:27). One
 *     digit is not a shortcut: both thresholds the bytes gate on are `cmp $0x5`, and the
 *     walkthrough's two quoted figures were 500 and 900 — 9 being the largest single digit.
 *     **No gold is ever deducted.** There is no write to the gold pair anywhere in asm/3C2E;
 *     the amount is read, compared and used as an increment, and kept. An offer is free.
 *   - Bit 7 of the target's role byte decides everything. Clear means a plain creature and
 *     the answer is THANK YOU VERY MUCH! whatever was offered. Set means a quest role, and
 *     then the WHOLE special path is gated on sector 32 — `[DS:4A]==2 && [DS:49]==3` — so
 *     outside that one town none of the quest arms can fire at all.
 *   - Inside sector 32 the role is compared one at a time: 0x81 gives the QUICK SWORD
 *     (DS:7F=1) for >=500, 0x82 gives the RING (DS:D6=1) for >=500 **and only if DS:6C is
 *     non-zero** (the Father Antos blessing flag; without it the line is EARN THE RING!),
 *     0x83 gives one random weapon from DAGGER..PHASER, and anything else with bit 7 set
 *     reaches the stat raise.
 *   - The stat raise is one roll: `r = rng & 7`. It fires when `r < 6` — 6 of 8, so **75% up
 *     and 25% THANK YOU** — and the same `r` indexes the stat, DS:4B+r, so all six stats are
 *     equally likely conditional on firing. The gain is four BCD adds of the amount, so
 *     **+4 per 100 gold**, written back with **no ceiling guard**, which is why a stat past
 *     99 wraps and why "stop at 95" is arithmetic rather than superstition: 95+4 = 99.
 *
 * Results are an enum rather than text so a test can assert on the arm taken.
 */
typedef enum u2_offer_result {
    U2_OFFER_NOBODY = 0,    /* OFFER TO WHOM? — the ray found no creature */
    U2_OFFER_THANK_YOU,     /* plain recipient: bit 7 clear, or set but not sector 32 */
    U2_OFFER_BELOW,         /* a quest role, but the amount was under 5 hundreds */
    U2_OFFER_QUICKSWORD,    /* ENILNO IS YOURS! — DS:7F set */
    U2_OFFER_EARN_RING,     /* EARN THE RING! — the blessing flag DS:6C is zero */
    U2_OFFER_RING,          /* THE RING IS YOURS! — DS:D6 set */
    U2_OFFER_WEAPON,        /* HERE TAKE THIS! — one weapon count goes up */
    U2_OFFER_RAISE,         /* ALAKAZAM! — a stat went up by 4 x hundreds */
    U2_OFFER_REFUSED,       /* the 2 of 8 that prints THANK YOU instead of raising */
    U2_OFFER_BROKE          /* YOU DONT HAVE THAT MUCH! — asm/2052 could not subtract the
                             * amount from gold, and asm/2070 abandons cmd_O rather than
                             * returning to it, so nothing else prints */
} u2_offer_result;

/* Runs the whole handler after the direction is known: the creature scan, the amount
 * prompt (read here, because asm/3C2E asks for it only once a recipient exists), and the
 * arm. The raise's roll comes from the engine's own generator, so a test pins it by seeding
 * rather than by stubbing; the arithmetic underneath it is enumerable through the pure
 * helper below. */
u2_offer_result u2_commerce_offer(u2_ctx *ctx, u2_overworld *ow, int dx, int dy);

/* The pure half of the raise, so the gate can enumerate all 256 rolls and all 256 amounts
 * without touching the RNG or the state: which stat index the roll selects, whether it
 * fires at all, and what the new packed-decimal value is. */
int u2_offer_raise_for(uint8_t roll, uint8_t hundreds, uint8_t cur_bcd,
                       int *stat_index, uint8_t *new_bcd);

#endif /* U2_COMMERCE_H */
