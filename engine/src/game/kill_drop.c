/* kill_drop.c — the kill-drop dispatch, asm/2CE5..2D8B.
 *
 * This lives in its own translation unit rather than inside combat.c for a reason worth
 * stating: the CP5 controls (tests/combat_model_*.c) link combat_check.c against MODEL
 * implementations that replace combat.c wholesale, so anything combat_check.c needs from
 * combat.c has to be re-declared by every model. The drop is not part of the C1/C2/C6
 * contract those models exist to test, so it is kept out of the file they substitute.
 */
#include <stdint.h>

#include "core/ctx.h"
#include "core/rng.h"
#include "game/combat.h"

/* asm/0ADB spawn arms set type 0xFC with HP 0x20; ai.c carries the same constant
 * privately for the contact-path theft. Repeated rather than hoisted because the two uses
 * are different events and a shared name would imply a shared decision the bytes do not
 * show. */
#define U2_AI_TYPE_THIEF 0xFC


/* asm/0FCD's thief type. ai.c carries the same constant privately; it is repeated here
 * rather than hoisted because the two uses are different events (theirs is the contact-path
 * theft, ours is the drop on death) and the shared name would imply a shared decision that
 * the bytes do not show. Both cite asm/0ADB's spawn arms, which set type 0xFC with HP 0x20. */
#define U2_AI_TYPE_THIEF 0xFC

/* Test surface for the equivalence the 0x60 arm depends on: two single increments must equal
 * one `adc $2,%al ; daa`. Both forms are implemented independently here -- bcd_add2 does the
 * DAA in one step from the x86 rule, not by looping -- so the check compares two derivations
 * rather than restating one. */
uint8_t u2_bcd_add2_test(uint8_t v)
{
    unsigned sum = (unsigned)v + 2u;
    /* AF is the half-carry out of bit 3: set when the low nibbles overflow. */
    const int af = ((v & 0xFu) + 2u) > 0x0Fu;
    if (af || (sum & 0xFu) > 9u)
        sum += 6u;
    if (sum > 0x99u)
        sum += 0x60u;
    return (uint8_t)(sum & 0xFFu);
}

/* asm/2CD3..2D8B. Which a kill grants, given the TYPE of what died -- four arms, tested in
 * this order because that is the order the bytes test them:
 *
 *   2CD3  cmp $0x60,%al ; jne 2CE5     -> keys += 2
 *   2CE5  cmp $0xfc,%al ; jne 2D22     -> tools (25%) + a random item
 *   2D22  cmp $0xf0 -> HELM (25%) + torches ; cmp $0xf8 -> WAND or STAFF
 *   2D8B  nop                          -> everything else grants nothing
 *
 * The thief arm (asm/2CE9..2D17) is a UNIFORM draw over the item array, and that is the
 * whole reason the ANKH is obtainable at all: no shoppe sells it, no chest holds it, and
 * nothing else in the binary writes DS:DD. asm/2D03 is `and $0xf,%al ; je skip`, so the
 * index is rand&15 and index 0 is excluded -- slot 0 is the RING, which asm/3C2E pays for
 * gold instead, so keeping it out of the random pool is deliberate. Slots 1..15 are equally
 * likely, which puts ANKH (7) and TRI-LITHIUM (15) at 1/15 each when the drop fires.
 *
 * The third-party guides describe this as "thieves sometimes drop an item" and are right
 * about the mechanic and the rarity. They are corroborated here by bytes and by the
 * executed flow in captures/spice86/baseline-fight, which reaches 0x2d07 by FALL-THROUGH
 * from 0x2d05 -- which is also why no `call` to asm/2D07 exists anywhere and the routine
 * looked orphaned to every static pass over the tree. */
void u2_combat_kill_drop(u2_ctx *ctx, uint8_t dead_type)
{
    if (dead_type == 0x60u) {
        /* asm/2CD8 is `clc ; mov 0x65,%al ; adc $2,%al ; daa ; mov %al,0x65` -- keys += 2,
         * packed decimal, no clamp, so 98 -> 00. Two bcd_inc1 calls are byte-equivalent to
         * the single `adc $2 ; daa` including across the wrap and across a nibble carry
         * (0x09 +2 = 0x11 either way; 0x99 +2 = 0x01 either way), which combat_check walks
         * rather than assumes.
         *
         * The identity of type 0x60 is the GUARD, and it is documented and observed rather
         * than guessed: state.h records the type byte as "0x60 Guard, 0x68 Merchant, 0xF0
         * Fighter" and notes it is NOT the art index, and the bestiary's gamercorner row
         * for GUARD carries the special "Drops 2 keys" -- which is exactly what asm/2CD8
         * does. It is now also seen in play: 0x60 fills live mon_type slots in 105 of the
         * 3055 captured DS dumps (210 slot-occurrences), every one of them in sector 03,
         * alongside 0x68 Merchant at the same count. Sector 03 is the castle town, which is
         * where captures/spice86/castle-03-live was taken and where Townsfolk belong.
         * The type>>2 == art_index fold that would have been the lazy route is explicitly
         * retracted in asm/0ADB's header as "a hypothesis about the renderer". */
        ctx->state.keys = u2_bcd_inc_sat(ctx->state.keys, U2_SATURATE(ctx));
        ctx->state.keys = u2_bcd_inc_sat(ctx->state.keys, U2_SATURATE(ctx));
        return;
    }
    if (dead_type == U2_AI_TYPE_THIEF) {
        /* asm/2CE9/2CF2: one roll, `cmp $0x40,%al`, so 64/256 = 25%. asm/2CF6 then adds 1
         * to DS:66, which state.h names `tools` (asm/277B spends them in a dungeon). */
        if (u2_rand_byte(ctx) < 0x40u)
            ctx->state.tools = u2_bcd_inc_sat(ctx->state.tools, U2_SATURATE(ctx));
        /* asm/2D00/2D03: a SECOND roll picks the slot. Two rolls, not one -- reading this
         * as a single draw gets the joint distribution wrong. */
        const unsigned idx = (unsigned)(u2_rand_byte(ctx) & 0x0Fu);
        if (idx != 0)
            ctx->state.items_held[idx] = u2_bcd_inc_sat(ctx->state.items_held[idx],
                                                        U2_SATURATE(ctx));
        return;
    }
    if (dead_type == 0xF0u) {
        /* asm/2D26..2D3A: 25% for a HELM (DS:DB = items_held[5]), then asm/2D3D..2D52 adds
         * (rand & 3) + 1 to DS:64, which is `torches`. */
        if (u2_rand_byte(ctx) < 0x40u)
            ctx->state.items_held[5] = u2_bcd_inc_sat(ctx->state.items_held[5], U2_SATURATE(ctx));
        for (unsigned n = 1u + (u2_rand_byte(ctx) & 3u); n; --n)
            ctx->state.torches = u2_bcd_inc_sat(ctx->state.torches, U2_SATURATE(ctx));
        return;
    }
    if (dead_type == 0xF8u) {
        /* asm/2D64..2D7C: `and $0x1,%al` then `inc %di` over the item base, so the slot is
         * 1 or 2 -- WAND or STAFF. asm/0AD3 makes 0xF8 the magic-missile caster, and a
         * caster leaving behind a wand or a staff is the consistency check that this arm
         * is read correctly rather than merely plausibly. */
        const unsigned idx = 1u + (unsigned)(u2_rand_byte(ctx) & 1u);
        ctx->state.items_held[idx] = u2_bcd_inc_sat(ctx->state.items_held[idx], U2_SATURATE(ctx));
    }
    /* Every other type: asm/2D8B is a bare nop and the tail jmp to the reward follows with
     * no grant at all. Silence here is the original's, and it is cited by the byte that
     * proves it. */
}
