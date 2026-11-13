#ifndef U2_FRAME_H
#define U2_FRAME_H

/*
 * Framebuffer — the pixel side of the engine, and the seam that should have
 * existed from the start.
 *
 * Why this is a core module and not a renderer detail: the original runs the
 * whole game in video mode 4 (asm/4A3E: INT 10h AH=00h AL=04h, 320x200), and
 * it draws *everything* there, including its text — the site-name banners use
 * the custom A-Z tiles from the tile table, not the CGA ROM font. A character
 * grid is therefore a simplification of the original, not a faithful model of
 * it, and any display backend that wants to be honest needs pixels.
 *
 * So pixel generation lives here, in dependency-free C11, and a backend only
 * moves bytes to a screen: SDL2 uploads them as a texture, kitty or sixel
 * encodes them, PPM writes them to a file, the null backend counts them. That
 * split is what keeps the game portable instead of terminal-specific.
 *
 * Geometry, all proven in docs/phase4-implementation.md § CP4:
 *   - screen 320x200, one CGA row = 80 bytes at 2bpp
 *   - the map window is 20x10 tiles at rows 0..19, full width (asm/5AE1 fill
 *     loop bounds, asm/4ABB flush loop bounds, and the screenshots, three
 *     independent confirmations)
 *   - a tile is 16x16 at 2bpp, leftmost pixel in the top two bits
 *   - the original stages tile IDs then flushes to video; this mirrors that by
 *     compositing into a frame the caller then presents
 */

#include <stdint.h>
#include "core/tiles.h"
#include "core/tiles_ega.h"
#include "core/view.h"

#define U2_SCREEN_W   320
#define U2_SCREEN_H   200
#define U2_SCREEN_ROW 80                       /* bytes per row at 2bpp */

/* The map window occupies the top 160 pixel rows; the original leaves the rest
 * for the status block and the command line, and row 24 blank. */
#define U2_MAP_PX_H   (U2_VIEW_ROWS * U2_TILE_H)   /* 160 */

/* CGA mode 4 palette indices. The engine carries indices, not RGB, so a
 * backend can honour a different palette (composite RGB, the fan pack's
 * CGACOMP.PAL, or a scaled-up 4-tone approximation) without the core knowing. */
#define U2_CGA_BLACK    0
#define U2_CGA_CYAN     1
#define U2_CGA_MAGENTA  2
#define U2_CGA_WHITE    3

/* The frame carries one byte per pixel. In the CGA path that byte is 0..3; in
 * the EGA path (`--ega`) it is 0..15. The storage is identical either way —
 * widening CGA to EGA is a palette and value-range change, not a framebuffer
 * rewrite — so a backend reads `px[y][x]` as an index into whichever palette the
 * art mode selected. The CGA constants above stay valid in both modes because
 * EGA's first four entries are the CGA-compatible black/cyan/magenta/white
 * positions; a CGA frame is a legal (if 4-colour) EGA frame. */
#define U2_PALETTE_CGA_ENTRIES 4
#define U2_PALETTE_EGA_ENTRIES 16

/* A frame is a PHYSICAL pixel buffer whose extent follows the active theme: a
 * 16x16-tile theme gets 320x200, a 32x32-tile theme gets 640x400, and the map
 * window is the top VIEW_ROWS*tile_h of it in both cases. `scale` is the ratio
 * to the 1x layout, and it is what the text layer multiplies by so the status
 * band stays the same proportion of the screen instead of shrinking into the
 * corner. Tile blits write physical pixels 1:1 and keep every pixel the art
 * has; a blit that went through the logical space would decimate a 32x32 tile
 * to 16x16 and throw away the reason for the theme. */
#define U2_FRAME_MAX_SCALE 2
/* 1024x512: the enhanced layout's ceiling. The width is what buys a readable
 * 16px panel beside a 14-column map (docs/enhanced-layout-plan.md § 9a showed that
 * 640 cannot hold both), and the height is deliberately short rather than a full
 * 4:3, because a taller frame would leave dead space under a map we are not
 * enlarging. The stride is the maximum, so this sizes every frame's row buffer. */
#define U2_FRAME_MAX_W     1024
#define U2_FRAME_MAX_H     512

typedef struct u2_frame {
    /* One byte per pixel, a palette index: 0..3 in the CGA path, 0..15 in an
     * EGA or v2-theme path. Heap-backed by u2_frame_init; `w` and `h` are the
     * physical extent in use and rows are U2_FRAME_MAX_W bytes wide. */
    uint8_t (*px)[U2_FRAME_MAX_W];
    int      w, h;          /* physical pixels: U2_SCREEN_W/H * scale */
    int      scale;         /* 1 or 2 */
    /* The two indices the text layer draws with. Defaults are the CGA pair (3 on
     * 0), but a theme that ships its own RGB table can make 3 an unusable colour:
     * the FM Towns palette has index 3 as BLACK, so status text drawn with the CGA
     * pair is black on black and simply is not there. The theme loader picks the
     * brightest entry of whatever palette is active. */
    uint8_t  text_fg, text_bg;
    unsigned presents;      /* frames composited, for tests and diagnostics */
} u2_frame;

/* Allocate the backing store for one frame at the given scale (0 or 1 means 1).
 * Returns 0 on success. A frame that was never inited has px == NULL, and every
 * writer below refuses rather than dereferencing it. */
int u2_frame_init(u2_frame *f, int scale);
/* A frame at an explicit pixel size and an explicit scale, for the enhanced
 * layout. The scale matters twice over: it is what makes text draw at 16px rather
 * than 8, and it is what makes a 16px theme present at 32px so every theme fills
 * the same map cell instead of a v1 theme occupying a quarter of the space. */
int u2_frame_init_size(u2_frame *f, int w, int h, int scale);
void u2_frame_free(u2_frame *f);

/* Init only if the frame is not already at this scale, freeing the old store
 * first. For frames that outlive one theme cycle: a static splash frame or an
 * overworld re-sized by Ctrl-T. Like init it assumes the struct is zeroed or
 * inited, never uninitialised garbage. */
int u2_frame_ensure(u2_frame *f, int scale);

/* Point the text layer at the palette's brightest ink. `fg` of 0 means "leave the
 * CGA default", which is what every theme without its own RGB table wants. */
static inline void u2_frame_set_text_fg(u2_frame *f, uint8_t fg)
{
    if (f && fg) f->text_fg = fg;
}

/* Write one PHYSICAL pixel. This is what a tile blit uses. */
static inline void u2_frame_put(u2_frame *f, int x, int y, uint8_t colour)
{
    if (!f || !f->px || x < 0 || y < 0 || x >= f->w || y >= f->h) return;
    f->px[y][x] = colour;
}

/* Write one pixel of the 1x LAYOUT — a glyph dot, a cursor, a status rule — as
 * a scale x scale block. Anything whose coordinates come from the text grid
 * goes through this; anything whose coordinates come from tile geometry does
 * not. Mixing them is how a 2x theme draws its text in the top-left quarter. */
static inline void u2_frame_put_logical(u2_frame *f, int x, int y, uint8_t colour)
{
    if (!f || !f->px) return;
    const int s = f->scale > 1 ? f->scale : 1;
    for (int j = 0; j < s; j++)
        for (int i = 0; i < s; i++)
            u2_frame_put(f, x * s + i, y * s + j, colour);
}

/* XOR one 1x-layout pixel, for the death-ring mask, which is a plane XOR and not
 * a write. Same block rule as put_logical, or the ring would cover a quarter of
 * the cell it belongs to in a large-tile theme. */
static inline void u2_frame_xor_logical(u2_frame *f, int x, int y, uint8_t bits)
{
    if (!f || !f->px || x < 0 || y < 0) return;
    const int s = f->scale > 1 ? f->scale : 1;
    for (int j = 0; j < s; j++)
        for (int i = 0; i < s; i++) {
            const int px = x * s + i, py = y * s + j;
            if (px < f->w && py < f->h) f->px[py][px] ^= bits;
        }
}

/* The four palette entries as 8-bit RGB, in CGA's canonical order. A backend
 * may substitute its own; this is the default, not an assertion about hardware. */
void u2_frame_palette(uint8_t out[4][3]);

/* The sixteen EGA palette entries as 8-bit RGB, in the standard EGA order
 * (black, blue, green, cyan, red, magenta, brown, light grey, grey, light blue,
 * light green, light cyan, light red, light magenta, yellow, white). This is the
 * hardware palette the fan pack's EGA art was authored against. The v1 themes
 * carry no RGB table of their own — what EGATHEME.10 and .C64 ship as EGACOLOR
 * is a colour-index remap for the fan driver's own primitives, not a palette
 * (docs/ega-graphics.md § 3.1) — but a format-v2 theme may name a PALETTE.RGB,
 * and when it does the renderer uses that instead of this table. */
void u2_frame_palette_ega(uint8_t out[16][3]);

void u2_frame_clear(u2_frame *f, uint8_t colour);

/* Blit one tile at a pixel position. Out-of-bounds pixels are clipped rather
 * than wrapped, so a partially off-screen tile cannot corrupt the frame. */
void u2_frame_blit_tile(u2_frame *f, const u2_tiles *t, int tile, int x, int y);

/* The EGA twin of the tile blit. Same geometry and clipping, but the source is
 * a 4bpp `u2_tiles_ega` tile (high nibble = left pixel) and the values written
 * into the frame are 0..15 rather than 0..3. The CGA blit above is untouched. */
void u2_frame_blit_tile_ega(u2_frame *f, const u2_tiles_ega *t, int tile, int x, int y);

/*
 * One pixel, and a Bresenham line built from it.
 *
 * These exist for the dungeon wireframe, and they mirror two byte-proven
 * routines rather than inventing a rasteriser:
 *
 *   asm/4B76  plot. x += [DS:4BF], y += [DS:4C1], then OR the plane mask into
 *             video, which sets BOTH plane bits and therefore always writes
 *             colour 3 (white). asm/4BAA is the same address arithmetic with
 *             the mask inverted, and it is how the original erases: colour 0.
 *             The live capture has the origin pair at 16,16, so the view is
 *             inset by one tile-width on the left and top.
 *   asm/67B3  line. A two-branch integer Bresenham: the deltas are taken as
 *             absolute values with the step sign kept separately in [DS:47D]/
 *             [DS:47E], and the branch is chosen by `cmp |dx|,|dy|` — one loop
 *             steps x as the major axis, the other y. The loop count is the
 *             major delta and each iteration plots one pixel.
 *
 *             This is a conventional Bresenham, not a transcription. The
 *             original's accumulator is a single byte that wraps, and a
 *             byte-wrapped accumulator drifts on shallow slopes (a 4x1 line
 *             steps y twice). Reproducing the drift would be reproducing a
 *             rounding bug nobody can distinguish from intent, so the engine
 *             keeps the structure and uses the standard error term. Grade:
 *             inferred, and it is a rendering choice, not a behavioural one.
 *
 * Out-of-bounds pixels are dropped, not wrapped: the original writes straight
 * into video and a coordinate off the edge would corrupt the frame buffer of
 * whatever else is resident, which is a bug worth not reproducing.
 */
void u2_frame_pixel(u2_frame *f, int x, int y, uint8_t colour);
void u2_frame_line(u2_frame *f, int x0, int y0, int x1, int y1, uint8_t colour);

/* Scale-aware twins: each logical pixel becomes a `scale`x`scale` block, so the
 * dungeon wireframe (authored in 320x200 CGA space) fills a doubled frame's map
 * window instead of its top-left quarter. At scale 1 they equal the raw writers.
 * See the comment at their definition in frame.c. */
void u2_frame_pixel_scaled(u2_frame *f, int x, int y, uint8_t colour, int scale);
void u2_frame_line_scaled(u2_frame *f, int x0, int y0, int x1, int y1,
                          uint8_t colour, int scale);

/* Rational fit twins: map a logical point (x,y) to physical (ox + x*num/den,
 * oy + y*num/den) and stamp a `thick`x`thick` block there. This is for a target
 * window that is NOT an integer multiple of the logical layout — the enhanced
 * map window is 448 px wide against the dungeon's 320 px logical width, so no
 * integer block scale fills it without spilling into the side panel. A line is
 * Bresenham'd in physical space after mapping its endpoints, so a straight line
 * stays straight under the linear map. See the comment at their definition. */
void u2_frame_pixel_fit(u2_frame *f, int x, int y, uint8_t colour,
                        int ox, int oy, int num, int den, int thick);
void u2_frame_line_fit(u2_frame *f, int x0, int y0, int x1, int y1, uint8_t colour,
                       int ox, int oy, int num, int den, int thick);

/*
 * Composite the map window from a filled viewport. This is the engine's
 * equivalent of asm/4ABB's flush loop: walk the 20x10 staging grid, skip the
 * cells flagged do-not-draw, and blit each tile at (col*16, row*16).
 *
 * `overlay` is the player marker drawn after the terrain, because the original
 * composites the character on top rather than baking it into the sector. It may
 * be -1 to draw no marker. It is an ART index.
 */
void u2_frame_draw_view(u2_frame *f, const u2_view *v, const u2_tiles *t,
                        int overlay_art, int overlay_col, int overlay_row);

/* The EGA twin of the view composite: same do-not-draw rule and overlay, but it
 * blits from a `u2_tiles_ega` atlas so the map window lands in 16 colours.
 * `overlay_art` is an ART index, not an atlas index: like every other tile
 * selection in this function it goes through the theme's map before it can
 * address the atlas. */
void u2_frame_draw_view_ega(u2_frame *f, const u2_view *v, const u2_tiles_ega *t,
                            int overlay_art, int overlay_col, int overlay_row);

/* Write the frame as a binary PPM (P6) mapped through `pal`. Uncompressed, so
 * it needs no zlib and no dependency — which is what makes "look at the real
 * tiles today" possible without SDL2. Returns 0 on success. */
int u2_frame_write_ppm(const u2_frame *f, const char *path,
                       const uint8_t pal[4][3]);

/* Same, as an uncompressed 24-bit BMP, for viewers that prefer it. */
int u2_frame_write_bmp(const u2_frame *f, const char *path,
                       const uint8_t pal[4][3]);

/* The 16-colour variants of the two writers, for a frame composed in the EGA
 * art mode. They index `pal` with the full 0..15 range; the 4-entry writers above
 * mask to 0..3 and are the CGA path, left byte-for-byte untouched. */
int u2_frame_write_ppm16(const u2_frame *f, const char *path,
                         const uint8_t pal[16][3]);
int u2_frame_write_bmp16(const u2_frame *f, const char *path,
                         const uint8_t pal[16][3]);

#endif /* U2_FRAME_H */
