/*
 * font.c — 8x8 glyph blitting into the framebuffer.
 *
 * The single text-to-pixels mechanism for the framebuffer (kickoff CP4b,
 * task 0: "Blit into u2_frame. One rendering mechanism for the framebuffer.").
 * Both the game-side compositing (status band) and the SDL backend's cell
 * path go through these two functions, so a glyph cannot disagree with itself
 * depending on which screen drew it.
 *
 * The glyph data is vendored Public Domain (font8x8_basic.h, included here
 * and nowhere else). Nothing in this file is derived from the game binary.
 */

#include <string.h>

#include "core/font.h"
#include "core/font8x8_basic.h"

_Static_assert(sizeof font8x8_basic / sizeof font8x8_basic[0] == 128,
               "font8x8_basic must cover U+0000..U+007F");

void u2_font_draw_cell(u2_frame *f, int row, int col, uint8_t ch,
                       uint8_t fg, uint8_t bg)
{
    if (!f || !f->px || row < 0 || col < 0) return;
    /* Bounds come from the frame's LOGICAL size, not from the 320x200 constants.
     * A scaled classic frame keeps the 40x25 grid it is defined by (640/2/8 = 40),
     * and the enhanced frame is a native 640x480 at scale 1, which is 80x60 cells
     * and is what lets the panel hold readable 8px text. Hardcoding the constants
     * here would clip the enhanced panel at a quarter of its width and look like a
     * drawing bug rather than a bounds bug. */
    const int s = f->scale > 1 ? f->scale : 1;
    if (row >= (f->h / s) / U2_FONT_CELL_H || col >= (f->w / s) / U2_FONT_CELL_W)
        return;
    const unsigned char *glyph = font8x8_basic[ch & 0x7F];
    const int x0 = col * U2_FONT_CELL_W;
    const int y0 = row * U2_FONT_CELL_H;

    for (int y = 0; y < U2_FONT_CELL_H; y++) {
        const unsigned char bits = glyph[y];
        for (int x = 0; x < U2_FONT_CELL_W; x++) {
            /* LSB is the leftmost pixel (font8x8 convention). The text layer is
             * laid out in the 1x grid, so it goes through put_logical and each
             * glyph dot becomes a scale x scale block: a 2x theme keeps the same
             * 80x25 text grid at twice the pixels instead of shrinking into the
             * top-left corner of a window four times the area. */
            u2_frame_put_logical(f, x0 + x, y0 + y,
                u2_frame_cga_idx(f, (bits & (1u << x)) ? fg : bg));
        }
    }
}

int u2_font_draw_text(u2_frame *f, int row, int col, const char *s,
                      uint8_t fg, uint8_t bg)
{
    if (!f || !s) return 0;
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++, n++) {
        /* Control bytes and the engine's pseudo-key codes never render: a
         * stray 0x0d from a game string must not punch holes in a screen. */
        const uint8_t ch = (*p >= 32 && *p < 127) ? *p : ' ';
        u2_font_draw_cell(f, row, col + n, ch, fg, bg);
    }
    return n;
}

void u2_font_draw_vram_rows(u2_frame *f, const u2_vram *v, int top_row,
                            int bottom_row)
{
    if (!f || !v) return;
    if (top_row < 0) top_row = 0;
    if (bottom_row >= U2_ROWS) bottom_row = U2_ROWS - 1;
    for (int r = top_row; r <= bottom_row; r++)
        for (int c = 0; c < U2_COLS; c++)
            /* The frame's own pair, not the CGA constants: a theme that brings an
             * RGB table can make index 3 black (FM Towns does), and text drawn in
             * the background colour is text that does not exist. */
            u2_font_draw_cell(f, r, c, v->cell[r][c].ch,
                              f->text_fg ? f->text_fg : U2_CGA_WHITE,
                              f->text_bg);
}
