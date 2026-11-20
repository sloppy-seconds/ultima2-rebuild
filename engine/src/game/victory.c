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
 *   - The flight loop is non-terminating in the original (flies until quit). The engine now
 *     matches that when a person is playing, and bounds it to U2_VICTORY_FLIGHT_STEPS only
 *     headlessly so a scripted gate can finish. Either way the run then ends: the original
 *     offers nothing after this point, so there is no menu to return to.
 *   - `L_0061a6` beside the `MAPX30  ` filename IS file I/O, and it is a READ. The caller's
 *     own bytes say so (AH=0x27, INT 21h FCB random block read; CX=0x1000 = 4096 records =
 *     one 64x64 grid; filename "MAPX30  "), and sector 30 is Earth, 1990 A.D. So the ending
 *     fades the chamber to black, reads the 1990 wilderness, and flies the rocket over that.
 *     The earlier "not a sector save" finding stands and was never the same claim: the
 *     capture md5'd every content file before and after and saw 0 changes, and a read cannot
 *     change an md5, so that test could not have seen the load even though it was running.
 *
 * Where each half lives: this function is the flag, the first line of text and the snapshot.
 * The fade, the other three strings, the sector read and the rocket-mode writes are in
 * game/overworld.c, because the loop owns the grid and the renderer and this runs on the
 * kill path. Splitting them that way is not a convenience: the original interleaves the
 * prints with the fade and the read, and printing them all here made the ending arrive as
 * one block of text with the avatar already redrawn as a rocket.
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
    /* Only the first line lives here. asm/72A0 prints the other three after the fade and
     * after the sector read, so they belong to the loop that owns the grid and the renderer:
     * see the victory arm in game/overworld.c.
     */

    /* Snapshot the four runtime fields the ending borrows. The borrowing happens in the
     * overworld loop (victory_rocket_mode), but the snapshot belongs here because this is the
     * last point at which the character is still the one that walked into the castle.
     * Guarded on the flag rather than unconditional, because a second call would otherwise
     * overwrite the saved values with the ending's own.
     */
    if (!ctx->victory_saved) {
        ctx->victory_saved       = 1;
        ctx->victory_saved_view  = ctx->state.view_mode;
        ctx->victory_saved_stamp = ctx->state.player_stamp;
        ctx->victory_saved_x     = ctx->state.sector_x;
        ctx->victory_saved_y     = ctx->state.sector_y;
    }
}

void u2_victory_restore(u2_ctx *ctx)
{
    /* Put back what the ending borrowed. All four live outside the saved record, so this is
     * the only way they come home; u2_save_load covers the record span and nothing here.
     */
    ctx->state.view_mode    = ctx->victory_saved_view;
    ctx->state.player_stamp = ctx->victory_saved_stamp;
    ctx->state.sector_x     = ctx->victory_saved_x;
    ctx->state.sector_y     = ctx->victory_saved_y;
}
