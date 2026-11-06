/*
 * renderer_term.c — ANSI terminal backend.
 *
 * Paints the 80x25 grid with cursor addressing; the grid itself is owned by
 * core/vram.c so this stays a pure painter (seam discipline). The only attribute
 * honoured is the engine's own highlight bit (U2_ATTR_HILITE, core/vram.h), which
 * becomes ANSI reverse video. Full palette-accurate attribute mapping is still CP9
 * work; this header used to claim it had already happened, and it had not.
 */
#include <stdio.h>
#include <stdlib.h>
#include "core/ctx.h"

static int term_init(u2_ctx *ctx)
{
    /* Clear once; from then on every flush rewrites cells in place. */
    fputs("\033[2J\033[H", stdout);
    ctx->vram.visible = 0;
    return 0;
}

static void term_cell(u2_ctx *ctx, int row, int col, uint8_t ch, uint8_t attr)
{
    (void)ctx;
    /* Reverse video for the engine's highlight bit. The glyph is written either
     * way, so a terminal that eats the escape still gets the text. */
    if (attr & U2_ATTR_HILITE)
        printf("\033[%d;%dH\033[7m%c\033[27m", row + 1, col + 1, (char)ch);
    else
        printf("\033[%d;%dH%c", row + 1, col + 1, (char)ch);
}

static void term_cursor(u2_ctx *ctx, int row, int col, int visible)
{
    (void)ctx;
    printf("\033[%d;%dH", row + 1, col + 1);
    fputs(visible ? "\033[?25h" : "\033[?25l", stdout);
}

static void term_flush(u2_ctx *ctx)
{
    (void)ctx;
    fflush(stdout);
}

static void term_shutdown(u2_ctx *ctx)
{
    (void)ctx;
    fputs("\033[?25h\n", stdout);
    fflush(stdout);
}

const u2_renderer u2_renderer_term = {
    .name = "term",
    .init = term_init,
    .cell = term_cell,
    .cursor = term_cursor,
    .flush = term_flush,
    .shutdown = term_shutdown,
};
