/*
 * tests/frame_check.c — the guarantees of core/frame.c.
 *
 * The framebuffer is the seam every display backend sits behind, so a mistake
 * here propagates to all of them. Tiles are synthetic filler; no game content.
 *
 * Build and run by tests/run_cp4.sh.
 */

#define _POSIX_C_SOURCE 200809L   /* mkdtemp, unlink, rmdir under -std=c11 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/frame.h"
#include "core/view.h"
#include "core/world.h"

static int fails, checks;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        checks++;                                                     \
        if (cond) printf("  ok    %s\n", msg);                        \
        else      { printf("  FAIL  %s\n", msg); fails++; }           \
    } while (0)

static long filesize(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fclose(f);
    return s;
}

/* A tiles struct where tile k is solid colour k, plus one checkerboard. */
static void synth_tiles(u2_tiles *t)
{
    memset(t, 0, sizeof *t);
    for (int k = 0; k < U2_TILE_COUNT; k++) {
        const uint8_t c = (uint8_t)(k & 3);
        uint8_t packed = (uint8_t)((c << 6) | (c << 4) | (c << 2) | c);
        memset(t->px[k], packed, U2_TILE_BYTES);
    }
    /* tile 8: left half cyan, right half white, so column mapping is visible */
    for (int r = 0; r < U2_TILE_H; r++)
        for (int b = 0; b < U2_TILE_STRIDE; b++) {
            const uint8_t cyan = 0x55, white = 0xff;
            t->px[8][r * U2_TILE_STRIDE + b] = (b < 2) ? cyan : white;
        }
}

int main(void)
{
    printf("frame_check\n");
    u2_tiles t;
    synth_tiles(&t);
    u2_frame f;
    u2_frame_init(&f, 1);   /* the frame is heap-backed now; a stack frame with no
                             * store is a refused draw, not a 320x200 one */

    /* --- clear and palette ----------------------------------------------- */
    u2_frame_clear(&f, U2_CGA_BLACK);
    int all0 = 1;
    for (int y = 0; y < U2_SCREEN_H && all0; y++)
        for (int x = 0; x < U2_SCREEN_W && all0; x++)
            if (f.px[y][x] != 0) all0 = 0;
    CHECK(all0, "clear fills all 64000 pixels");

    uint8_t pal[4][3];
    u2_frame_palette(pal);
    CHECK(pal[0][0] == 0 && pal[0][1] == 0 && pal[0][2] == 0, "palette 0 is black");
    CHECK(pal[3][0] == 255 && pal[3][1] == 255 && pal[3][2] == 255, "palette 3 is white");

    /* --- the blit -------------------------------------------------------- */
    u2_frame_clear(&f, 0);
    u2_frame_blit_tile(&f, &t, 1, 0, 0);
    CHECK(f.px[0][0] == 1 && f.px[15][15] == 1, "a solid tile blits 16x16 at the origin");
    CHECK(f.px[16][0] == 0, "the blit stops at 16 rows, it does not bleed");
    CHECK(f.px[0][16] == 0, "the blit stops at 16 columns");

    u2_frame_clear(&f, 0);
    u2_frame_blit_tile(&f, &t, 2, 32, 48);
    CHECK(f.px[48][32] == 2 && f.px[63][47] == 2, "a tile blits at an offset position");
    CHECK(f.px[47][32] == 0 && f.px[48][31] == 0, "nothing lands outside the requested box");

    /* 2bpp unpacking through the blit, not just the accessor. */
    u2_frame_clear(&f, 0);
    u2_frame_blit_tile(&f, &t, 8, 0, 0);
    int halves = (f.px[0][0] == 1 && f.px[0][15] == 3 &&
                  f.px[0][7] == 1 && f.px[0][8] == 3);
    CHECK(halves, "byte 0 holds the left 8 px, byte 2 the right 8, top bits first");

    /* Clipping: a tile half off each edge must not corrupt the frame. */
    u2_frame_clear(&f, 0);
    u2_frame_blit_tile(&f, &t, 3, U2_SCREEN_W - 8, U2_SCREEN_H - 8);
    CHECK(f.px[U2_SCREEN_H - 1][U2_SCREEN_W - 1] == 3, "a corner-clipped tile still draws its visible part");
    CHECK(f.px[0][0] == 0, "clipping did not wrap into the opposite corner");
    u2_frame_blit_tile(&f, &t, 3, -8, -8);
    CHECK(f.px[0][0] == 3, "a negatively offset tile draws its visible bottom-right");
    u2_frame_blit_tile(&f, &t, -1, 0, 0);
    u2_frame_blit_tile(&f, &t, U2_TILE_COUNT, 0, 0);
    CHECK(1, "an out-of-range tile index is ignored rather than reading past the table");

    /* --- compositing the viewport ---------------------------------------- */
    u2_world w;
    memset(&w, 0, sizeof w);
    w.map_loaded = 1;
    for (int i = 0; i < U2_SECTOR_TILES; i++) w.map[i] = 0;   /* index 0 */
    u2_view v;
    u2_view_fill(&v, &w, 32, 32, 0xFF, NULL, 0);

    u2_frame_clear(&f, 0);
    u2_frame_draw_view(&f, &v, &t, -1, 0, 0);
    CHECK(f.px[0][0] == 0 && f.px[159][319] == 0, "the 20x10 window covers rows 0..159");
    CHECK(f.px[160][0] == 0, "row 160 is below the map window and untouched");

    /* Suppressed cells must keep the frame's prior contents, per asm/4ABB. */
    u2_frame_clear(&f, 2);
    uint8_t under[U2_VIEW_TILES];
    memset(under, 0, sizeof under);
    u2_view_fill(&v, &w, 32, 32, 0xFF, under, 0);   /* all index 0, gate clear */
    u2_frame_draw_view(&f, &v, &t, -1, 0, 0);
    int kept = 1;
    for (int y = 0; y < U2_MAP_PX_H && kept; y++)
        for (int x = 0; x < U2_SCREEN_W && kept; x++)
            if (f.px[y][x] != 2) kept = 0;
    CHECK(kept, "every cell suppressed by the underlay leaves the frame as it was");

    /* The overlay draws on top. */
    u2_frame_clear(&f, 0);
    u2_frame_draw_view(&f, &v, &t, 3, 9, 5);
    CHECK(f.px[5 * 16][9 * 16] == 3, "the overlay tile draws over the terrain");

    /* --- the file writers ------------------------------------------------ */
    char dir[] = "/var/tmp/u2frameXXXXXX";
    if (!mkdtemp(dir)) { printf("  FAIL  mkdtemp\n"); return 1; }
    char ppm[600], bmp[600];
    snprintf(ppm, sizeof ppm, "%s/f.ppm", dir);
    snprintf(bmp, sizeof bmp, "%s/f.bmp", dir);

    u2_frame_clear(&f, 1);
    CHECK(u2_frame_write_ppm(&f, ppm, &pal[0][0]) == 0, "ppm written");
    /* P6 header "P6\n320 200\n255\n" is 15 bytes, then 320*200*3. */
    CHECK(filesize(ppm) == 15 + (long)U2_SCREEN_W * U2_SCREEN_H * 3,
          "ppm is the exact uncompressed size, no compression needed");
    FILE *pf = fopen(ppm, "rb");
    int hdr_ok = pf && fgetc(pf) == 'P' && fgetc(pf) == '6';
    if (pf) fclose(pf);
    CHECK(hdr_ok, "ppm carries a P6 magic");

    CHECK(u2_frame_write_bmp(&f, bmp, &pal[0][0]) == 0, "bmp written");
    CHECK(filesize(bmp) == 54 + (long)U2_SCREEN_W * U2_SCREEN_H * 3,
          "bmp is 54-byte header plus uncompressed BGR rows");
    unsigned char b[58];
    FILE *bf = fopen(bmp, "rb");
    if (bf) { size_t got = fread(b, 1, sizeof b, bf); (void)got; fclose(bf); }
    CHECK(b[0] == 'B' && b[1] == 'M', "bmp carries the BM magic");
    /* bottom-up: the first stored row is screen row 199, solid cyan -> BGR */
    CHECK(b[54] == 170 && b[55] == 170 && b[56] == 0,
          "bmp stores BGR and starts from the bottom row");

    unlink(ppm); unlink(bmp); rmdir(dir);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
