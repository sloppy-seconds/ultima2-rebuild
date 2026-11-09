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


/* Packed-decimal add one, with the original's missing ceiling guard. asm/2D07 is
 * `clc; mov 0xd6(%di),%al; adc $1,%al; daa; mov %al,0xd6(%di)` and there is no compare
 * anywhere in it, so a count at 99 goes 99 -> 00 and keeps going. That is not a bug we are
 * reproducing out of laziness: asm/2D07's own header records "no clamp, so a slot wraps
 * 99 -> 00", and the same absence is what lets cmd_O push a stat past 99.
 *
 * Adding n by calling this n times is byte-equivalent to one DAA add of n, including across
 * the 99 boundary (0x99 +4 in one step is 0x03, and four steps are 0x00,0x01,0x02,0x03).
 * Verified, not assumed -- combat_check walks both forms. */
static void bcd_inc1(uint8_t *p)
{
    unsigned v = (unsigned)*p + 1u;
    if ((v & 0xFu) > 9u)
        v += 6u;
    if (v > 0x99u)
        v += 0x60u;              /* and the carry falls off the end, by design */
    *p = (uint8_t)(v & 0xFFu);
}

/* asm/0FCD's thief type. ai.c carries the same constant privately; it is repeated here
 * rather than hoisted because the two uses are different events (theirs is the contact-path
 * theft, ours is the drop on death) and the shared name would imply a shared decision that
 * the bytes do not show. Both cite asm/0ADB's spawn arms, which set type 0xFC with HP 0x20. */
#define U2_AI_TYPE_THIEF 0xFC

/* asm/2CE5..2D8B. Which item a kill grants, given the type of what died.
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
    if (dead_type == U2_AI_TYPE_THIEF) {
        /* asm/2CE9/2CF2: one roll, `cmp $0x40,%al`, so 64/256 = 25%. asm/2CF6 then adds 1
         * to DS:66, which state.h names `tools` (asm/277B spends them in a dungeon). */
        if (u2_rand_byte(ctx) < 0x40u)
            bcd_inc1(&ctx->state.tools);
        /* asm/2D00/2D03: a SECOND roll picks the slot. Two rolls, not one -- reading this
         * as a single draw gets the joint distribution wrong. */
        const unsigned idx = (unsigned)(u2_rand_byte(ctx) & 0x0Fu);
        if (idx != 0)
            bcd_inc1(&ctx->state.items_held[idx]);
        return;
    }
    if (dead_type == 0xF0u) {
        /* asm/2D26..2D3A: 25% for a HELM (DS:DB = items_held[5]), then asm/2D3D..2D52 adds
         * (rand & 3) + 1 to DS:64, which is `torches`. */
        if (u2_rand_byte(ctx) < 0x40u)
            bcd_inc1(&ctx->state.items_held[5]);
        for (unsigned n = 1u + (u2_rand_byte(ctx) & 3u); n; --n)
            bcd_inc1(&ctx->state.torches);
        return;
    }
    if (dead_type == 0xF8u) {
        /* asm/2D64..2D7C: `and $0x1,%al` then `inc %di` over the item base, so the slot is
         * 1 or 2 -- WAND or STAFF. asm/0AD3 makes 0xF8 the magic-missile caster, and a
         * caster leaving behind a wand or a staff is the consistency check that this arm
         * is read correctly rather than merely plausibly. */
        const unsigned idx = 1u + (unsigned)(u2_rand_byte(ctx) & 1u);
        bcd_inc1(&ctx->state.items_held[idx]);
    }
    /* Every other type: asm/2D8B is a bare nop and the tail jmp to the reward follows with
     * no item at all. Silence here is the original's, and it is cited by the byte that
     * proves it. */
}
