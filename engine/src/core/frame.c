/*
 * frame.c — the framebuffer and the tile blit.
 *
 * Dependency-free by design. Everything a display backend needs to be thin is
 * decided here: geometry, the palette mapping, clipping, and the do-not-draw
 * rule from asm/4ABB. A backend that only moves bytes cannot get the game's
 * rendering semantics wrong, which is the point of the split.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/frame.h"

int u2_frame_init(u2_frame *f, int scale)
{
    if (!f) return -1;
    /* The struct must be zeroed or previously freed: this does NOT free an
     * existing store, because a frame declared as an uninitialised stack local
     * has a garbage `px` and free()ing it is a crash, not a cleanup. A caller
     * that re-sizes an existing frame calls u2_frame_free first, and
     * u2_frame_ensure() does both. */
    int s = scale > 1 ? scale : 1;
    if (s > U2_FRAME_MAX_SCALE) s = U2_FRAME_MAX_SCALE;
    const size_t rows = (size_t)U2_SCREEN_H * s;
    if (rows > U2_FRAME_MAX_H) return -1;
    /* One block, rows U2_FRAME_MAX_W wide so `px[y][x]` stays valid at any
     * scale. calloc rather than malloc: a frame that is read before it is drawn
     * must read as black, not as whatever the heap had. */
    uint8_t *mem = calloc(rows, U2_FRAME_MAX_W);
    if (!mem) return -1;
    f->px = (uint8_t (*)[U2_FRAME_MAX_W])mem;
    f->scale = s;
    f->w = U2_SCREEN_W * s;
    f->h = U2_SCREEN_H * s;
    f->text_fg = U2_CGA_WHITE;
    f->text_bg = U2_CGA_BLACK;
    f->presents = 0;
    return 0;
}

int u2_frame_init_size(u2_frame *f, int w, int h, int scale)
{
    if (!f) return -1;
    if (w <= 0 || h <= 0 || w > U2_FRAME_MAX_W || h > U2_FRAME_MAX_H) return -1;
    const int s = scale > 1 ? scale : 1;
    /* The logical canvas is w/s by h/s, and it has to be at least the classic
     * 320x200 or the text grid the font API bounds-checks against would be
     * smaller than the screen it is drawing. */
    if (w / s < U2_SCREEN_W || h / s < U2_SCREEN_H) return -1;
    /* Same one-block, calloc'd layout as u2_frame_init: a frame read before it is
     * drawn must read as black. */
    uint8_t *mem = calloc((size_t)h, U2_FRAME_MAX_W);
    if (!mem) return -1;
    f->px = (uint8_t (*)[U2_FRAME_MAX_W])mem;
    f->scale = s;
    f->w = w;
    f->h = h;
    f->text_fg = U2_CGA_WHITE;
    f->text_bg = U2_CGA_BLACK;
    f->presents = 0;
    return 0;
}

void u2_frame_free(u2_frame *f)
{
    if (!f) return;
    free(f->px);
    f->px = NULL;
    f->w = f->h = f->scale = 0;
}

/* Init only when the scale actually changed, so a flow that draws several
 * pictures in a row does not allocate and leak one frame per picture. */
int u2_frame_ensure(u2_frame *f, int scale)
{
    const int s = scale > 1 ? scale : 1;
    if (f && f->px && f->scale == s) return 0;
    if (f && f->px) u2_frame_free(f);
    return u2_frame_init(f, s);
}

void u2_frame_palette(uint8_t out[4][3])
{
    /* CGA's four mode-4 colours. High intensity is used for cyan and magenta
     * because that is what the composite output reads as on a modern display;
     * a backend wanting the true 0xAA values can build its own table, which is
     * exactly why the core carries indices and not RGB. */
    static const uint8_t pal[4][3] = {
        {   0,   0,   0 },   /* 0 black   */
        {   0, 170, 170 },   /* 1 cyan    */
        { 170,   0, 170 },   /* 2 magenta */
        { 255, 255, 255 }    /* 3 white   */
    };
    memcpy(out, pal, sizeof pal);
}

void u2_frame_palette_ega(uint8_t out[16][3])
{
    /* The standard EGA palette. These are the 0xAA-scaled RGB values the EGA
     * adapter drives (the six levels are 0x00/0xAA/0xFF), which is what the fan
     * pack's EGA art was authored to look right on. The first four entries are
     * deliberately NOT the CGA table's values: EGA's index 1 is blue and 2 is
     * red, where CGA mode 4 uses cyan and magenta. A CGA frame rendered through
     * this table would show blue grass and red walls, which is why the art mode
     * picks the table and the two are never mixed (docs/ega-graphics-plan.md § 4). */
    static const uint8_t pal[16][3] = {
        {   0,   0,   0 },   /*  0 black        */
        {   0,   0, 170 },   /*  1 blue         */
        {   0, 170,   0 },   /*  2 green        */
        {   0, 170, 170 },   /*  3 cyan         */
        { 170,   0,   0 },   /*  4 red          */
        { 170,   0, 170 },   /*  5 magenta      */
        { 170,  85,   0 },   /*  6 brown        */
        { 170, 170, 170 },   /*  7 light grey   */
        {  85,  85,  85 },   /*  8 grey         */
        {  85,  85, 255 },   /*  9 light blue   */
        {  85, 255,   0 },   /* 10 light green  */
        {  85, 255, 255 },   /* 11 light cyan   */
        { 255,  85,  85 },   /* 12 light red    */
        { 255,  85, 255 },   /* 13 light magenta*/
        { 255, 255,  85 },   /* 14 yellow       */
        { 255, 255, 255 }    /* 15 white        */
    };
    memcpy(out, pal, sizeof pal);
}

/* The four CGA mode-4 colours expressed as EGA palette indices: black, cyan,
 * magenta, white. See u2_frame::cga_on_ega. */
static const uint8_t k_cga_to_ega[4] = { 0, 3, 5, 15 };

uint8_t u2_frame_cga_idx(const u2_frame *f, uint8_t c)
{
    if (!f || !f->cga_on_ega || c > 3) return c;
    return k_cga_to_ega[c];
}

void u2_frame_clear(u2_frame *f, uint8_t colour)
{
    if (!f || !f->px) return;
    /* & 15, not & 3: the frame carries 0..15 indices whenever an EGA or v2
     * theme is drawing, and a clear colour of 14 (yellow) is a legitimate one
     * there. A CGA caller cannot tell the difference, because CGA only ever
     * passes 0..3. */
    const uint8_t c = (uint8_t)(colour & 15);
    for (int y = 0; y < f->h; y++)
        memset(f->px[y], c, (size_t)f->w);
    f->presents = 0;
}

void u2_frame_blit_tile(u2_frame *f, const u2_tiles *t, int tile, int x, int y)
{
    if (!f || !t || !f->px) return;
    if (tile < 0 || tile >= U2_TILE_COUNT) return;

    for (int row = 0; row < U2_TILE_H; row++) {
        const int dy = y + row;
        if (dy < 0 || dy >= U2_SCREEN_H) continue;         /* clipped */
        const uint8_t *src = t->px[tile] + row * U2_TILE_STRIDE;
        for (int col = 0; col < U2_TILE_W; col++) {
            const int dx = x + col;
            if (dx < 0 || dx >= U2_SCREEN_W) continue;     /* clipped */
            const uint8_t b = src[col >> 2];
            /* put_logical, not put: a CGA tile is 16x16 in a layout that may be
             * composited at 2x, and a 16x16 island in a 32x32 cell is what a
             * `put` would draw. The dungeon and town art is CGA even under a
             * large-tile theme, so this path really does run at scale 2. */
            u2_frame_put_logical(f, dx, dy,
                                 u2_frame_cga_idx(f,
                                     (uint8_t)((b >> (6 - 2 * (col & 3))) & 3)));
        }
    }
}

void u2_frame_blit_tile_ega(u2_frame *f, const u2_tiles_ega *t, int tile, int x, int y)
{
    if (!f || !t || !f->px) return;
    if (tile < 0 || tile >= u2_tiles_ega_count(t)) return;

    /* Geometry comes from the theme, not from constants: a v1 theme is 16x16 with
     * an 8-byte stride and a v2 theme may be 32x32 with a 16-byte one, and the
     * compiler will not stop a hardcoded stride from being quietly wrong for one
     * of them.
     *
     * `s` is how many physical pixels one tile pixel is worth in THIS frame: the
     * frame scale divided by the atlas's native scale. For a 32x32 theme in the
     * 640x400 EGA frame that is 1 and the tile is blitted 1:1, every pixel it has.
     * For a 16x16 theme in the same frame it is 2, and each tile pixel becomes a
     * 2x2 block — the same nearest-neighbour doubling the SDL window was already
     * doing one step later, moved into the frame so the frame size can be a
     * property of the art mode instead of the theme. A frame is never SMALLER
     * than a theme's native scale; the loader refuses that case rather than
     * decimating the art. */
    const int tw = u2_tiles_ega_tile_w(t);
    const int th = u2_tiles_ega_tile_h(t);
    const int stride = u2_tiles_ega_stride(t);
    const int s = u2_tiles_ega_present(t, f->scale);

    for (int row = 0; row < th; row++) {
        const int dy = y + row * s;
        if (dy >= f->h) continue;                          /* clipped */
        const uint8_t *src = t->px[tile] + row * stride;
        for (int col = 0; col < tw; col++) {
            const int dx = x + col * s;
            if (dx >= f->w) break;                         /* rest of the row is off-screen */
            const uint8_t b = src[col >> 1];
            const uint8_t v = (uint8_t)((col & 1) ? (b & 0xF) : (b >> 4));
            /* The theme's transparent pen, if it declared one. Skipping the write
             * leaves whatever the frame already holds at that pixel, which is how
             * terrain shows through a sprite drawn over it. -1 matches no index, so
             * a theme that declares nothing draws every pixel as before. */
            if ((int)v == t->transparent_index) continue;
            for (int j = 0; j < s; j++)
                for (int i = 0; i < s; i++)
                    u2_frame_put(f, dx + i, dy + j, v);
        }
    }
}

void u2_frame_pixel(u2_frame *f, int x, int y, uint8_t colour)
{
    if (!f) return;
    u2_frame_put(f, x, y, (uint8_t)(colour & 3));
}

void u2_frame_line(u2_frame *f, int x0, int y0, int x1, int y1, uint8_t colour)
{
    if (!f) return;
    int dx = x1 - x0, dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    const int sx = x1 > x0 ? 1 : -1;
    const int sy = y1 > y0 ? 1 : -1;

    /* Same branch test as asm/67B3's `cmp 0x484,%al ; jae`: whichever delta is
     * not the major axis carries the error term. */
    int err;
    if (dx >= dy) {
        err = -dx;
        for (int x = x0, y = y0;; x += sx) {
            u2_frame_pixel(f, x, y, colour);
            if (x == x1) break;
            err += 2 * dy;
            if (err >= 0) { y += sy; err -= 2 * dx; }
        }
    } else {
        err = -dy;
        for (int x = x0, y = y0;; y += sy) {
            u2_frame_pixel(f, x, y, colour);
            if (y == y1) break;
            err += 2 * dx;
            if (err >= 0) { x += sx; err -= 2 * dy; }
        }
    }
}

/* Scale-aware twins of the two primitives above, for content whose coordinates
 * live in the 320x200 CGA layout but must fill a doubled frame. Each logical
 * pixel becomes a `scale`x`scale` block, so a wireframe drawn in CGA space fills
 * the map window of a 640x400 EGA frame instead of sitting in its top-left
 * quarter. At scale 1 they are byte-for-byte the raw writers, which is what keeps
 * the CGA path untouched. The colour is masked to the four CGA indices exactly as
 * u2_frame_pixel does, because the dungeon draws CGA colour indices that the EGA
 * palette resolves through its first four (CGA-compatible) entries. */
void u2_frame_pixel_scaled(u2_frame *f, int x, int y, uint8_t colour, int scale)
{
    if (!f) return;
    if (scale < 1) scale = 1;
    const uint8_t c = (uint8_t)(colour & 3);
    for (int j = 0; j < scale; j++)
        for (int i = 0; i < scale; i++)
            u2_frame_put(f, x * scale + i, y * scale + j, c);
}

void u2_frame_line_scaled(u2_frame *f, int x0, int y0, int x1, int y1,
                          uint8_t colour, int scale)
{
    if (!f) return;
    int dx = x1 - x0, dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    const int sx = x1 > x0 ? 1 : -1;
    const int sy = y1 > y0 ? 1 : -1;

    int err;
    if (dx >= dy) {
        err = -dx;
        for (int x = x0, y = y0;; x += sx) {
            u2_frame_pixel_scaled(f, x, y, colour, scale);
            if (x == x1) break;
            err += 2 * dy;
            if (err >= 0) { y += sy; err -= 2 * dx; }
        }
    } else {
        err = -dy;
        for (int x = x0, y = y0;; y += sy) {
            u2_frame_pixel_scaled(f, x, y, colour, scale);
            if (y == y1) break;
            err += 2 * dx;
            if (err >= 0) { x += sx; err -= 2 * dy; }
        }
    }
}

/* Rational fit writers. The dungeon's logical canvas is 320x200; the enhanced map
 * window is 448x448, which is 1.4x the dungeon width, so an integer block scale
 * either under-fills (1x) or overflows into the side panel (2x). These map each
 * logical point through ox + x*num/den and stamp a thick x thick block, which
 * fills the window at a fractional ratio while keeping the doubled frame's line
 * weight (thick 2). The map is linear, so mapping a line's endpoints and
 * Bresenham-ing in physical space keeps it straight. Colour masks to the four
 * CGA indices exactly as the scaled writers do. */
void u2_frame_pixel_fit(u2_frame *f, int x, int y, uint8_t colour,
                        int ox, int oy, int num, int den, int thick)
{
    if (!f) return;
    if (den < 1) den = 1;
    if (thick < 1) thick = 1;
    const uint8_t c = (uint8_t)(colour & 3);
    const int px = ox + (int)((long)x * num / den);
    const int py = oy + (int)((long)y * num / den);
    for (int j = 0; j < thick; j++)
        for (int i = 0; i < thick; i++)
            u2_frame_put(f, px + i, py + j, c);
}

void u2_frame_line_fit(u2_frame *f, int x0, int y0, int x1, int y1, uint8_t colour,
                       int ox, int oy, int num, int den, int thick)
{
    if (!f) return;
    if (den < 1) den = 1;
    if (thick < 1) thick = 1;
    /* Map endpoints to physical, then Bresenham there with a thick x thick stamp
     * (the enhanced frame is scale 2, so thick 2 matches the doubled line weight). */
    const int px0 = ox + (int)((long)x0 * num / den);
    const int py0 = oy + (int)((long)y0 * num / den);
    const int px1 = ox + (int)((long)x1 * num / den);
    const int py1 = oy + (int)((long)y1 * num / den);
    int dx = px1 - px0, dy = py1 - py0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    const int sx = px1 > px0 ? 1 : -1;
    const int sy = py1 > py0 ? 1 : -1;
    const uint8_t c = (uint8_t)(colour & 3);
    int err;
    if (dx >= dy) {
        err = -dx;
        for (int x = px0, y = py0;; x += sx) {
            for (int j = 0; j < thick; j++) for (int i = 0; i < thick; i++)
                u2_frame_put(f, x + i, y + j, c);
            if (x == px1) break;
            err += 2 * dy;
            if (err >= 0) { y += sy; err -= 2 * dx; }
        }
    } else {
        err = -dy;
        for (int x = px0, y = py0;; y += sy) {
            for (int j = 0; j < thick; j++) for (int i = 0; i < thick; i++)
                u2_frame_put(f, x + i, y + j, c);
            if (y == py1) break;
            err += 2 * dx;
            if (err >= 0) { x += sx; err -= 2 * dy; }
        }
    }
}

void u2_frame_draw_view(u2_frame *f, const u2_view *v, const u2_tiles *t,
                        int overlay_art, int overlay_col, int overlay_row)
{
    if (!f || !v || !t) return;

    int vcols, vrows;
    u2_view_shape(v, &vcols, &vrows, NULL, NULL);

    for (int row = 0; row < vrows; row++) {
        for (int col = 0; col < vcols; col++) {
            /* asm/4ABB: `or cl,cl; js skip`. A suppressed cell keeps whatever
             * was already in the frame, which is why the caller clears first.
             * The staging byte halves to reach the art record — see
             * u2_view_art(); skipping that step doubles every tile index. */
            const int staging = u2_view_at(v, col, row);
            if (staging < 0) continue;
            u2_frame_blit_tile(f, t, u2_view_art(staging),
                               col * U2_TILE_W, row * U2_TILE_H);
        }
    }

    if (overlay_art >= 0 && overlay_col >= 0 && overlay_row >= 0 &&
        overlay_col < vcols && overlay_row < vrows) {
        u2_frame_blit_tile(f, t, overlay_art,
                           overlay_col * U2_TILE_W, overlay_row * U2_TILE_H);
    }
    f->presents++;
}

void u2_frame_draw_view_ega(u2_frame *f, const u2_view *v, const u2_tiles_ega *t,
                            int overlay_art, int overlay_col, int overlay_row)
{
    if (!f || !v || !t) return;

    const int pres = u2_tiles_ega_present(t, f->scale);
    int vcols, vrows;
    u2_view_shape(v, &vcols, &vrows, NULL, NULL);
    const int cell_w = u2_tiles_ega_tile_w(t) * pres;
    const int cell_h = u2_tiles_ega_tile_h(t) * pres;
    const int composites = (t->transparent_index >= 0);

    for (int row = 0; row < vrows; row++) {
        for (int col = 0; col < vcols; col++) {
            /* Same staging->art mapping as the CGA composite (see above); only
             * the atlas and the blit differ, so the two paths cannot drift on
             * which tile a cell selects. The atlas then maps art -> pool entry,
             * which is identity for every v1 theme and a table for a v2 one. */
            const int staging = u2_view_at(v, col, row);
            if (staging < 0) continue;
            const int art = u2_view_art(staging);
            const int tile = u2_tiles_ega_tile(t, art);
            if (tile < 0) continue;   /* the theme does not cover this art */

            /* Composite when the theme declared a transparent pen: ground first,
             * then the occupant letting the pen through. The DOS model is one art
             * per cell -- the creature stamp, and 0x78 for the player, replaces the
             * terrain byte in the grid -- so without this there is nothing behind a
             * sprite for the pen to reveal, and keying just shows the cleared frame.
             * A theme with no pen takes this path not at all, which is what keeps
             * every v1 theme exactly as it was. */
            if (composites) {
                const int g = u2_view_ground_at(v, col, row);
                if (g >= 0 && u2_view_art(g) != art) {
                    const int gt = u2_tiles_ega_tile(t, u2_view_art(g));
                    if (gt >= 0)
                        u2_frame_blit_tile_ega(f, t, gt, col * cell_w, row * cell_h);
                }
            }
            u2_frame_blit_tile_ega(f, t, tile, col * cell_w, row * cell_h);
        }
    }

    if (overlay_art >= 0 && overlay_col >= 0 && overlay_row >= 0 &&
        overlay_col < vcols && overlay_row < vrows) {
        /* The overlay is an ART index and has to go through the theme's map like
         * every other selection in this function. Passing it straight to the atlas
         * was invisible for every v1 theme, because their map is identity by
         * definition, and wrong the moment a v2 theme had a real table: art 60 is
         * the avatar, pool index 60 is a capital O, and the player became an O.
         * A theme that does not cover the avatar draws no marker rather than
         * guessing, which is the same rule the terrain loop above follows. */
        const int tile = u2_tiles_ega_tile(t, overlay_art);
        if (tile >= 0)
            u2_frame_blit_tile_ega(f, t, tile,
                                   overlay_col * cell_w, overlay_row * cell_h);
    }
    f->presents++;
}

/* Big-endian-ish 16/32-bit writers for the two container formats. */
static void put16le(FILE *f, unsigned v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
}

static void put32le(FILE *f, unsigned long v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f);
    fputc((int)((v >> 24) & 0xff), f);
}

int u2_frame_write_ppm(const u2_frame *f, const char *path,
                       const uint8_t pal[4][3])
{
    if (!f || !f->px || !path || !pal) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    fprintf(fp, "P6\n%d %d\n255\n", f->w, f->h);
    for (int y = 0; y < f->h; y++) {
        for (int x = 0; x < f->w; x++) {
            const uint8_t *rgb = pal[f->px[y][x] & 3];
            fputc(rgb[0], fp); fputc(rgb[1], fp); fputc(rgb[2], fp);
        }
    }
    const int err = ferror(fp);
    if (fclose(fp) != 0 || err) return -1;
    return 0;
}

int u2_frame_write_ppm16(const u2_frame *f, const char *path,
                         const uint8_t pal[16][3])
{
    if (!f || !f->px || !path || !pal) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    fprintf(fp, "P6\n%d %d\n255\n", f->w, f->h);
    for (int y = 0; y < f->h; y++) {
        for (int x = 0; x < f->w; x++) {
            const uint8_t *rgb = pal[f->px[y][x] & 15];
            fputc(rgb[0], fp); fputc(rgb[1], fp); fputc(rgb[2], fp);
        }
    }
    const int err = ferror(fp);
    if (fclose(fp) != 0 || err) return -1;
    return 0;
}

int u2_frame_write_bmp(const u2_frame *f, const char *path,
                       const uint8_t pal[4][3])
{
    if (!f || !f->px || !path || !pal) return -1;

    const unsigned row_bytes = (unsigned)f->w * 3;  /* 960 at 1x, already padded */
    const unsigned pixels = row_bytes * (unsigned)f->h;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    fputc('B', fp); fputc('M', fp);
    put32le(fp, 14 + 40 + pixels);
    put32le(fp, 0);
    put32le(fp, 14 + 40);
    put32le(fp, 40);
    put32le(fp, (unsigned)f->w);
    put32le(fp, (unsigned)f->h);
    put16le(fp, 1);
    put16le(fp, 24);
    put32le(fp, 0);                 /* BI_RGB, uncompressed */
    put32le(fp, pixels);
    put32le(fp, 2835);              /* 72 dpi */
    put32le(fp, 2835);
    put32le(fp, 0);
    put32le(fp, 0);

    for (int y = f->h - 1; y >= 0; y--) {            /* BMP is bottom-up */
        for (int x = 0; x < f->w; x++) {
            const uint8_t *rgb = pal[f->px[y][x] & 3];
            fputc(rgb[2], fp); fputc(rgb[1], fp); fputc(rgb[0], fp);  /* BGR */
        }
    }
    const int err = ferror(fp);
    if (fclose(fp) != 0 || err) return -1;
    return 0;
}

int u2_frame_write_bmp16(const u2_frame *f, const char *path,
                         const uint8_t pal[16][3])
{
    if (!f || !f->px || !path || !pal) return -1;

    const unsigned row_bytes = (unsigned)f->w * 3;  /* 960 at 1x, already padded */
    const unsigned pixels = row_bytes * (unsigned)f->h;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    fputc('B', fp); fputc('M', fp);
    put32le(fp, 14 + 40 + pixels);
    put32le(fp, 0);
    put32le(fp, 14 + 40);
    put32le(fp, 40);
    put32le(fp, (unsigned)f->w);
    put32le(fp, (unsigned)f->h);
    put16le(fp, 1);
    put16le(fp, 24);
    put32le(fp, 0);                 /* BI_RGB, uncompressed */
    put32le(fp, pixels);
    put32le(fp, 2835);              /* 72 dpi */
    put32le(fp, 2835);
    put32le(fp, 0);
    put32le(fp, 0);

    for (int y = f->h - 1; y >= 0; y--) {            /* BMP is bottom-up */
        for (int x = 0; x < f->w; x++) {
            const uint8_t *rgb = pal[f->px[y][x] & 15];
            fputc(rgb[2], fp); fputc(rgb[1], fp); fputc(rgb[0], fp);  /* BGR */
        }
    }
    const int err = ferror(fp);
    if (fclose(fp) != 0 || err) return -1;
    return 0;
}
