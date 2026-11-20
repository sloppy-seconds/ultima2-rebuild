/* flash.c — the wound flash: XOR a ring onto the victim's cell, tone, XOR back.
 * See flash.h and asm/5255_wound_video_flash / asm/4C1E_video_xor_patch. */
#include "game/flash.h"

#include "core/frame.h"
#include "core/seam.h"
#include "core/tiles.h"
#include "gen_content.h"      /* U2_RING_XOR, generated from asm/5287_data_mixed.asm */

/* The mask is 16 records of 4 bytes at 2bpp — one CGA scanline each, leftmost
 * pixel in the top two bits — so exactly one 16x16 tile cell. XORing the 2-bit
 * palette index reproduces the original's plane XOR: 3 inverts (white<->black,
 * the disc and the turned-black victim), 0 leaves alone, and the partials
 * (0x0f, 0xf0, 0x3f, 0xfc, 0xc0, 0x03) carve the ring's edge. Content-blind by
 * design: over bright terrain it comes out mottled there too. */
static void ring_xor(u2_frame *f, int col, int row)
{
    const int x0 = col * U2_TILE_W;
    const int y0 = row * U2_TILE_H;

    for (int y = 0; y < 16; y++) {
        if (y0 + y < 0 || y0 + y >= U2_SCREEN_H)
            continue;
        for (int b = 0; b < 4; b++) {
            const uint8_t pat = U2_RING_XOR[y * 4 + b];
            for (int q = 0; q < 4; q++) {
                const int x = x0 + b * 4 + q;
                if (x < 0 || x >= U2_SCREEN_W)
                    continue;
                /* The ring mask is authored in the 1x tile cell, so it XORs
                 * through the layout helper and covers the whole cell at any
                 * frame scale. */
                u2_frame_xor_logical(f, x, y0 + y,
                                     (uint8_t)((pat >> (6 - 2 * q)) & 3u));
            }
        }
    }
}

/* The ring's on-screen duration, in ms. MEASURED: the capture's poller saw the ring up
 * for 148-180 ms at ~130 ms granularity (captures/spice86/wound-flash-20261102), so 150
 * is inside the measurement rather than a round guess about it.
 */
#define U2_WOUND_FLASH_MS 150

void u2_flash_cell(u2_ctx *ctx, int col, int row)
{
    ctx->diag_flashes++;

    /* The tone is the original's feedback and stays wired: divisor 0x800 is asm/5EF2's
     * own immediate. It is NOT the timer any more, and the wound loop's other tone
     * (asm/5EAC, divisor 0x300) is still not wired because its duration has no byte and
     * no measurement, and a guessed tone is worse than none. NOT modelled either: the
     * DS:54D sound flag that gates asm/5DA9 — the engine does not carry that byte;
     * choosing the audio backend is the user's volume knob.
     */
    if (ctx->frame) {
        ring_xor(ctx->frame, col, row);
        seam_present_frame(ctx);
    }

    seam_tone(ctx, 0x800, U2_WOUND_FLASH_MS);

    /* The hold belongs to the clock, not the tone. In the original the speaker write is
     * followed by a busy loop, so the tone really is the timer; here the SDL backend
     * queues samples and returns, and a muted run returns before it reaches the backend
     * at all -- so pacing on the tone made the ring appear and vanish inside one frame,
     * and it did that silently for anyone playing with sound off. Same number, different
     * owner. A scripted run pays nothing: the script clock only advances virtual time.
     */
    if (ctx->clock && ctx->clock->sleep_ms)
        ctx->clock->sleep_ms(ctx, U2_WOUND_FLASH_MS);

    if (ctx->frame) {
        ring_xor(ctx->frame, col, row);   /* the undo: XOR is its own inverse */
        seam_present_frame(ctx);
    }
}
