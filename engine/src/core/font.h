#ifndef U2_FONT_H
#define U2_FONT_H

/*
 * Engine text font — 8x8 bitmap glyphs blitted into the framebuffer.
 *
 * Owner decision 2026-10-20 (docs/phase4-kickoff-cp4b.md): the text
 * *mechanism* is ours to choose — readable text in the right place with the
 * right values, not a reproduction of the original's BIOS plumbing. The data
 * is dhepper/font8x8 (Public Domain), vendored in font8x8_basic.h; see
 * THIRD-PARTY-LICENSES. Px437 was assessed and is a visual reference only.
 *
 * What stays the game's own: A-Z for in-world site-name banners uses the
 * extracted tile letters (art 32-57, core/tiles.h), not this font. This
 * module is for the status band, menu and creation text only.
 *
 * Geometry: the grid is 40x25 cells of 8x8 px, which is exactly the 320x200
 * mode-4 screen (platform.h), so a cell (row, col) maps to pixel origin
 * (col*8, row*8) with no scaling and no fractional cells.
 *
 * Bit order (font8x8 convention): one byte per pixel row, LSB = leftmost
 * pixel, row 0 at the top.
 */

#include <stdint.h>
#include "core/frame.h"
#include "core/vram.h"

#define U2_FONT_CELL_W 8
#define U2_FONT_CELL_H 8

/* Draw one glyph at cell (row, col). ch >= 128 draws as ch & 0x7F; the
 * engine's own pseudo-key codes (U2_KEY_*) must never reach a renderer —
 * callers pass printable text. Pixels outside the frame are clipped. */
void u2_font_draw_cell(u2_frame *f, int row, int col, uint8_t ch,
                       uint8_t fg, uint8_t bg);

/* Draw a NUL-terminated string starting at cell (row, col), advancing one
 * cell per byte. Characters outside 32..126 are drawn as space so control
 * bytes can never corrupt a screen. Returns the number of cells consumed. */
int u2_font_draw_text(u2_frame *f, int row, int col, const char *s,
                      uint8_t fg, uint8_t bg);

/*
 * Composite the text rows of the cell grid into the framebuffer — the one
 * framebuffer decision from the CP4b kickoff: map pixels and text share a
 * frame, so a screenshot of the window is a screenshot of the game.
 *
 * The overworld calls this for rows 20..24 after drawing the map window:
 * the status block and the CMD message window live in the vram grid (where
 * the terminal renderer already paints them), and this is the pixel side of
 * the same storage. Attributes are not honoured yet: mode-4 text colour is
 * white-on-black everywhere the engine prints, and real attribute mapping is
 * CP9 work.
 */
void u2_font_draw_vram_rows(u2_frame *f, const u2_vram *v, int top_row,
                            int bottom_row);

/* The same grid, drawn as glyph dots only, with NO background fill.
 *
 * u2_font_draw_vram_rows is a text screen: it paints every cell's background, which is
 * what makes a text screen opaque. Over a picture that is fatal -- it fills the whole
 * frame with text_bg and the picture is gone, which is how the demonstration's couplets
 * came to sit on black instead of on the starfield. The original's printer writes glyph
 * pixels and nothing else, so the art shows through the spaces. Use this variant
 * wherever text is composited onto art (title.c's show_pic is the only such caller).
 */
void u2_font_draw_vram_rows_overlay(u2_frame *f, const u2_vram *v, int top_row,
                                    int bottom_row);

#endif /* U2_FONT_H */
