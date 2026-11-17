/* sound.c — the outdoor step thud and the refusal buzz. See sound.h for what is
 * wired and, more importantly, what is not.
 *
 * Every number here is measured against the original under Spice86, in
 * captures/spice86/step-sound-20261117/. Two instruments, both independent of the
 * disassembly:
 *
 *   - the register dump at asm/5CD2_speaker_freq while the tone played, which
 *     gives the divisor (BX) and the sequence record pointer (BP);
 *   - polling Spice86's own PC-speaker state, which gives the span the gate stayed
 *     up, in emulated cycles, because the gate is exactly what asm/5CB2 raises and
 *     asm/5CE4 lowers.
 *
 * Cycle-to-millisecond basis, stated once because it is the soft part of every
 * duration below: the delay loops are CPU cycles, and dividing them by a nominal
 * 4.77 MHz is what the original hardware would have done. Spice86 models no 8088
 * bus wait states, so each figure here is a LOWER bound on the real duration — the
 * same caveat already recorded against U2_ROCKET_STEP_MS and G86. A lower bound
 * that is a little short is a slightly brisk game; it is not a wrong sound. */
#include "game/sound.h"

#include "game/sound.h"
#include "game/feedback.h"
#include "core/seam.h"

/* asm/5D09's sequence 0, record at CS:5D5D: {count 0x05, divisor 0x4000}.
 * Observed live as BP=0x5D5D, BX=0x4000, CX=0x0500 at asm/5CD2, on every step.
 * 1193182 / 0x4000 = 72.8 Hz, which is a thud rather than a note. */
#define U2_STEP_DIVISOR  0x4000

/* Measured 64,424..66,734 cycles per thud across 12 steps, spread under 4%:
 * 65,000 cycles at 4.77 MHz is 13.6 ms. */
#define U2_STEP_MS       14

/* The two thumps start 642,175 cycles apart, so the silence between them is that
 * minus the note itself: (642175 - 65000) / 4770000 = 121 ms. This is not a
 * stylistic choice — played back to back the pair is one 27 ms buzz, and the
 * original is audibly dun-dun, not duuun. */
#define U2_STEP_GAP_MS   121

/* asm/5C95: BX=0x500 (1193182 / 1280 = 932 Hz) with CX=0x4000 delay units,
 * confirmed live on a refused cmd_X and again on four refused steps:
 * 833,165..835,133 cycles, i.e. 175 ms at 4.77 MHz. */
#define U2_REFUSE_DIVISOR 0x0500
#define U2_REFUSE_MS      175

void u2_sound_step(u2_ctx *ctx)
{
    seam_tone(ctx, U2_STEP_DIVISOR, U2_STEP_MS);
    seam_silence(ctx, U2_STEP_GAP_MS);
    seam_tone(ctx, U2_STEP_DIVISOR, U2_STEP_MS);
}

void u2_sound_refusal(u2_ctx *ctx)
{
    seam_tone(ctx, U2_REFUSE_DIVISOR, U2_REFUSE_MS);
}

int u2_sound_audible(const u2_ctx *ctx)
{
    return ctx->sound_off == 0;
}

int u2_sound_toggle(u2_ctx *ctx)
{
    /* xorb $0xff, DS:54D -- asm/4F7B, the only write site in the game. XOR rather
     * than a boolean flip because that is the instruction: the flag has always been
     * either 0 or 0xFF, and keeping the byte shape means a future read of the real
     * DS byte and a read of this field agree. */
    ctx->sound_off ^= 0xff;
    /* The original prints nothing here; asm/4F7B is five bytes and none of them is
     * output. Classic mode therefore stays silent, and this is the citation AGENTS.md
     * § 3 asks for at a silent mutation site. Enhanced announces through the one seam
     * that invents text, which resolves to nothing in original mode anyway. */
    u2_feedback(ctx, ctx->sound_off ? "[sound off]" : "[sound on]");
    return ctx->sound_off != 0;
}
