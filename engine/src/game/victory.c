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
 * What is deliberately NOT implemented, and why
 * ---------------------------------------------
 * The 64x64 repaint at 74DA is measured -- the bounds, the fill byte and the fact that it
 * goes through the grid pointer at [DS:06] are all bytes -- but WHICH grid it lands on is not.
 * The loop runs before the inline "MAPX30" filename appears, and the two calls it uses on the
 * way (L_005417 for the cell coordinates, L_0061a6 beside the filename) are inferred as
 * cell-addressing and file I/O rather than observed. So the engine reproduces the ending that
 * is proven and leaves the repaint out rather than guessing which sector gets blacked out and
 * persisting a wrong world. That is docs/needs-gameplay.md G75 items (a) and (b), and both
 * are agent-drivable: force a monx slot's type to 0x40 with tools/u2_edit.py, kill it, and the
 * gate runs against the original without a playthrough.
 *
 * The trailing animation loop is not reproduced either. The engine ends the run instead,
 * which is the behaviour a caller can act on; the original hangs, and G75(c) is the open
 * question of whether that hang is the intended ending or a dead program with a nice message.
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
    /* asm/72A0's 64x64 repaint would go here. See the note above -- omitted on purpose. */
    seam_str_out(ctx, U2_S_STRANGE_FORCE);
    seam_str_out(ctx, U2_S_WIN_UNIVERSE);
    seam_str_out(ctx, U2_S_WIN_FOUND_IN);

    /* The three writes the original makes after the text, in its order. */
    ctx->state.view_mode    = 0xFF;   /* DS:12 */
    ctx->state.player_stamp = 0x28;   /* DS:13, the rocket stamp */
    ctx->state.sector_x     = 0;      /* DS:00 */
    ctx->state.sector_y     = 0;      /* DS:01 */
}
