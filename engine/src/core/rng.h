#ifndef U2_RNG_H
#define U2_RNG_H

/*
 * Random byte — the shift-register PRNG.
 *
 * spec: asm/5217_rand_byte.asm + asm/522B_rand_byte_shift.asm + asm/5236_rand_byte_tail.asm.
 *   (Those last two were sub_522b and sub_5236 until 2026-11-15, when the split was named; the
 *   spec line above is the reason the rename had to touch this header in the same commit.)
 * Phase 3 split this routine across three "function" boundaries; read together
 * (runtime 0x5217..0x523D) it is:
 *
 *   push ax; push bx
 *   stc
 *   al  = DS:0x240
 *   al += DS:0x243 + 1        (adc, carry in from stc)
 *   al += DS:0x244 + carry    (adc)
 *   DS:0x23F = al             the new value
 *   bx = 4
 * 1: al = DS:0x23F(bx)        shift the window one byte left ...
 *    DS:0x240(bx) = al        ... five times (jns runs bx = 4..0)
 *    dec bx ; jns 1b
 *   pop bx ; pop ax
 *   al = DS:0x23F
 *   or al, al                 callers branch on the zero flag
 *   ret
 *
 * ...and "the zero flag" is only half the story, which cost a decode. The same `or` sets SF from
 * bit 7 of the returned byte, and the PRAYER arm of asm/3139 branches on the SIGN (`js`), not on
 * zero. So a caller testing "is the roll zero" and a caller testing "is the high bit set" are
 * reading different properties of one return value, and u2_rand_byte() returns the byte itself so
 * both are expressible. Bit 7 measured fair: 50.0% of 200000 draws from a live register state
 * (captures/spice86/magic-casts-2026-11-15), which is what makes PRAYER's 50% a measured fact.
 *
 * So the state is the six bytes DS:0x23F..0x244 and it is rewritten on every
 * call: a feedback shift register, not a clock sampler. docs/phase4-plan.md §1
 * described those bytes as tick counters; the decode says otherwise, and
 * nothing else in the binary writes them (only asm/0100_sub_0100.asm seeds
 * them). Consequences that matter for this project:
 *
 *   - rolls are a pure function of (seed, number of rolls so far), so monster
 *     spawns, encounters and combat rolls replay exactly for the same inputs;
 *   - the original's seed is the BIOS keyboard-flag byte at 0x40:6C, which is
 *     normally zero at startup, so the shipped game is itself effectively
 *     deterministic per boot;
 *   - no wall-clock source is needed for reproducibility, and none is used.
 *
 * The engine keeps the state in the DS mirror (u2_state.rng_state) so a dump
 * shows exactly what the original's memory would have shown.
 */

#include <stdint.h>
#include "core/ctx.h"

/* One rand_byte() call: advances the register and returns the new byte. */
uint8_t u2_rand_byte(u2_ctx *ctx);

/* The same, tagged with the call site for --trace-rolls. Callers do not pass the tag: the macro
 * below fills __func__, so the whole tree keeps calling u2_rand_byte(ctx) unchanged and every site
 * is still named. __func__ is the enclosing function, which is coarse (two rolls in one function
 * share a label) but honest, and it costs nothing to maintain. */
uint8_t u2_rand_byte_at(u2_ctx *ctx, const char *site);
#define u2_rand_byte(c) u2_rand_byte_at((c), __func__)

/* Seed the register the way asm/0100_sub_0100.asm does, from the 0x40:6C byte.
 * The engine calls this at startup with u2_rng_default_seed(); --seed overrides
 * it so a run can be given a different-but-still-reproducible dice sequence. */
void u2_rng_seed(u2_ctx *ctx, uint8_t bios_kbd_flags);

/* The value the original would have read at 0x40:6C on a cold boot. */
#define U2_RNG_DEFAULT_SEED 0x00

/* Load a captured register verbatim, replacing whatever the seed produced.
 *
 * This exists because asm/0100's seeding cannot express a mid-game state: it writes six copies of one
 * byte and then increments two of them, so the reachable startup states are 256 out of 2^48. A
 * recording taken from the original at turn N holds six arbitrary bytes, and resuming there needs a
 * raw load.
 *
 * That is what makes cross-game replay possible at all. The original's idle loop advances the register
 * on the BIOS timer, so the register at any turn is a function of emulated time as well as commands;
 * replaying a seed and a key script from boot only reproduces a session if the pacing matches tick for
 * tick. Resuming from the recorded register sidesteps the question entirely — each turn starts from the
 * state the original actually had. See docs/plan-cross-game-replay.md § 6b/§ 6c.
 *
 * ctx->rng_rolls is a diagnostic counter, not part of the original's state, and is reset: after a load
 * the engine has not rolled anything yet.
 */
void u2_rng_load(u2_ctx *ctx, const uint8_t state[6]);

/* Number of rolls performed (diagnostics; not part of the original's state). */
uint32_t u2_rng_rolls(const u2_ctx *ctx);

#endif /* U2_RNG_H */
