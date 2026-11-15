/*
 * timer.c — the delay seam.
 * spec: none. This seam has no counterpart in the binary, and the file used to say
 *       otherwise. It cited `asm/52CA_timed_wait.asm`, which was renamed on
 *       2026-10-21 to `asm/52CA_dos_call_wrapper.asm` on the evidence that it stashes
 *       a caller's function number at cs:0x4F04 and reaches three of the four
 *       `int 0x21` sites in the image — it is the generic DOS FCB call wrapper, and
 *       the AH/CX/DX it stages are a file function number and an FCB transfer
 *       count, not a delay. The bytes were read correctly and labelled wrongly, and
 *       the engine's prose outlived the rename by three weeks.
 *
 *       What IS true, and is the reason this file exists: the original has no delay
 *       primitive at all. It paces by the cost of the work in the loop — one whole
 *       sector render per march step — so its animation rate was machine-dependent
 *       and is not recoverable from the program. Every figure the engine passes here
 *       is a stated stand-in naming its own measurement.
 *
 * There is no tick source here on purpose. The bytes at DS:0x23F..0x244 that an
 * earlier reading called "tick counters" are the rand_byte shift register
 * (see core/rng.h), so nothing in the engine derives randomness from a clock.
 * The clock exists only to make seam_wait_ms wait.
 */
#include "core/timer.h"

void u2_wait_ms(u2_ctx *ctx, uint32_t ms)
{
    if (ctx->clock->sleep_ms)
        ctx->clock->sleep_ms(ctx, ms);
}
