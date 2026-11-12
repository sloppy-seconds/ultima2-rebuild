/*
 * victory.c — CP8: the endgame sequence. See victory.h for what is evidence.
 *
 * Order of operations is the original's, from the head of asm/72A0 (file 0x74A0):
 *
 *   74A1  movb $0x28,DS:31        narrow the print margin to 40 columns
 *   74A6  call str_out            "\r\r     MINAX IS DEAD!!\rALL HER WORKS SHALL DIE!\r"
 *   74DA  64 x 64 fill of 0x74    (art 0x1D, solid black) through the grid pointer
 *   7517  call str_out            "\rYOU FEEL A STRANGE FORCE!"
 *   7544  db "MAPX30  "           an FCB-style filename, inline
 *   754F  call str_out            "\r\rYOU HAVE SAVED THE UNIVERSE,\rAND COMPLETED ULTIMA ][!
 *                                 SEEK\rNOW TO CONQUER WICKED EXODUS,\r"
 *   75AE  call str_out            "FOUND IN ULTIMA ]I[-D ]II[-P!"
 *   75CF  DS:12 = 0xFF            view_mode
 *   75D4  DS:13 = 0x28            player_stamp, and 0x28 is the ROCKET stamp (asm/2FAD)
 *   75D9  DS:00 = DS:01 = 0       player position
 *   75E1  loop with a delay, `jmp -0x2a` back: an ending animation that never returns
 *
 * The repaint and the flight ARE implemented -- in the overworld loop, not here
 * ---------------------------------------------------------------------------
 * The 64x64 repaint at 74DA and the trailing flight loop at 75E1 are reproduced by
 * victory_fade() and victory_flight() in game/overworld.c, because the loop owns the grid
 * and the renderer and this function (called from the kill path) does not. What the capture
 * captures/spice86/victory-live/ settled, and what those functions encode:
 *   - L_005417 is a RANDOM-BYTE generator, not a cell-addressing call. The fade paints
 *     random cells (masked &0x3f) with the black tile 0x74, so ~63% of the grid ends black
 *     with repeats -- the "slow fade." It is NOT a systematic sweep of a named sector.
 *   - L_0061a6 beside the "MAPX30" filename is NOT a sector save: 0 content files changed
 *     across the whole ending. The repaint is in-memory grid only and is never persisted,
 *     so the engine writes nothing to disk here (and correctly so).
 *   - The flight loop is non-terminating in the original (flies until quit). The engine
 *     bounds it (U2_VICTORY_FLIGHT_STEPS) and ends the run, the 1d owner decision.
 * G75 items (a)/(b)/(c) are closed by that capture; see asm/72A0's header.
 */
#include "game/victory.h"

#include "core/seam.h"
#include "gen_content.h"

void u2_victory(u2_ctx *ctx)
{
    /* Set before printing, not after: a caller watching the log should be able to correlate
     * the first victory line with the flag being up, and a gate that reads the flag after the
     * last message would pass even if the print seam were the thing that broke. */
    ctx->victory = 1;

    seam_str_out(ctx, U2_S_MINAX_DEAD);
    /* asm/72A0's 64x64 fade runs in the overworld loop (victory_fade), not here: this
     * function is on the kill path and has no grid or renderer. See the header note. */
    seam_str_out(ctx, U2_S_STRANGE_FORCE);
    seam_str_out(ctx, U2_S_WIN_UNIVERSE);
    seam_str_out(ctx, U2_S_WIN_FOUND_IN);

    /* The three writes the original makes after the text, in its order. */
    ctx->state.view_mode    = 0xFF;   /* DS:12 */
    ctx->state.player_stamp = 0x28;   /* DS:13, the rocket stamp */
    ctx->state.sector_x     = 0;      /* DS:00 */
    ctx->state.sector_y     = 0;      /* DS:01 */
}
