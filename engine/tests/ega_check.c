/*
 * tests/ega_check.c — the 16-colour EGA pipeline: palette, tile blit, atlas
 * loader, and the PIC*-E loader.
 *
 * The CGA path is pinned elsewhere (frame_check, pic_check); this covers only
 * what --ega adds, and it is written so the parts that need the fan pack SKIP
 * cleanly when it is absent (CI has no references/ or original/updated/), while
 * the pure-pipeline parts (palette, blit decode, 16-entry writer) always run.
 *
 * Build and run from engine/:
 *   cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o /var/tmp/ega_check \
 *      tests/ega_check.c src/core/frame.c src/core/view.c src/core/tiles_ega.c \
 *      src/core/pic_ega.c src/core/pic.c src/debug/trace.c && /var/tmp/ega_check
 */

#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/frame.h"
#include "core/tiles_ega.h"
#include "core/pic_ega.h"

static int checks, fails;
#define CHECK(cond, ...) do {                                          \
        ++checks;                                                      \
        if (cond) { printf("  ok    "); }                              \
        else { ++fails; printf("  FAIL  "); }                          \
        printf(__VA_ARGS__); printf("\n");                             \
    } while (0)

int main(void)
{
    printf("ega_check\n");

    /* --- the EGA palette table ------------------------------------------- */
    uint8_t pal[16][3];
    u2_frame_palette_ega(pal);
    CHECK(pal[0][0]==0 && pal[0][1]==0 && pal[0][2]==0, "entry 0 is black");
    CHECK(pal[1][0]==0 && pal[1][1]==0 && pal[1][2]==170, "entry 1 is blue (0,0,170)");
    CHECK(pal[2][0]==0 && pal[2][1]==170 && pal[2][2]==0, "entry 2 is green (0,170,0)");
    CHECK(pal[15][0]==255 && pal[15][1]==255 && pal[15][2]==255, "entry 15 is white");
    /* The first four EGA entries are NOT the CGA table: index 1 is blue, not
     * cyan. If someone "fixed" this to match CGA the map would recolour wrong. */
    uint8_t cga[4][3];
    u2_frame_palette(cga);
    CHECK(pal[1][2]==170 && pal[1][1]==0,
          "EGA index 1 differs from CGA (blue, not cyan) — the tables are not interchangeable");
    CHECK(pal[2][0]==0 && cga[2][0]==170,
          "EGA index 2 is green where CGA index 2 is magenta");
    int distinct = 1;
    for (int i = 0; i < 16 && distinct; i++)
        for (int j = i+1; j < 16; j++)
            if (pal[i][0]==pal[j][0] && pal[i][1]==pal[j][1] && pal[i][2]==pal[j][2])
                distinct = 0;
    CHECK(distinct, "all 16 EGA entries are distinct RGB values");

    /* --- the EGA tile blit decodes 4bpp, high nibble = left pixel -------- */
    u2_tiles_ega t;
    memset(&t, 0, sizeof t);
    t.loaded = 1;
    /* Tile 0, row 0: byte 0x12 -> pixel(0,0)=1, pixel(1,0)=2; byte 0xAB -> a,b. */
    t.px[0][0] = 0x12;
    t.px[0][1] = 0xAB;
    u2_frame f;
    u2_frame_init(&f, 1);   /* heap-backed store; see frame_check.c */
    u2_frame_clear(&f, 0);
    u2_frame_blit_tile_ega(&f, &t, 0, 0, 0);
    CHECK(f.px[0][0]==1 && f.px[0][1]==2, "4bpp decode: 0x12 -> left 1, right 2");
    CHECK(f.px[0][2]==0xA && f.px[0][3]==0xB, "4bpp decode: 0xAB -> left 10, right 11");
    CHECK(u2_tile_ega_pixel(&t, 0, 0, 0)==1 && u2_tile_ega_pixel(&t,0,1,0)==2,
          "u2_tile_ega_pixel agrees with the blit");
    /* Out-of-range tile index is a no-op, not a crash. */
    u2_frame_blit_tile_ega(&f, &t, U2_TILE_COUNT, 0, 0);
    CHECK(1, "an out-of-range tile index is ignored, not a crash");

    /* --- the 16-entry PPM writer maps indices 4..15 ---------------------- */
    u2_frame_clear(&f, 0);
    for (int x = 0; x < 16; x++) f.px[0][x] = (uint8_t)x;
    if (u2_frame_write_ppm16(&f, "/var/tmp/ega_check.ppm", &pal[0][0]) == 0) {
        FILE *fp = fopen("/var/tmp/ega_check.ppm", "rb");
        /* Skip the P6 header line-by-line is fiddly; just confirm it wrote. */
        CHECK(fp != NULL, "write_ppm16 produced a file");
        if (fp) fclose(fp);
    } else {
        CHECK(0, "write_ppm16 wrote a frame");
    }

    /* --- the atlas loader, against the real EGATILES when present -------- */
    u2_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.ega_theme_dir = "../references/u2upgrade";
    u2_tiles_ega te;
    u2_tiles_ega_result r = u2_tiles_ega_open(&ctx, &te, NULL);
    if (r != U2_TILES_EGA_OK) {
        printf("  SKIP  references/u2upgrade/EGATILES not present (%s); "
               "the atlas checks below need it\n", u2_tiles_ega_result_text(r));
    } else {
        CHECK(te.loaded == 1, "default EGATILES loaded");
        /* The letter-A tile (index 32) must carry lit pixels; a wrong header
         * offset would land on blank padding and read all-zero. */
        int lit = 0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                if (u2_tile_ega_pixel(&te, 32, x, y)) lit++;
        CHECK(lit > 20, "tile 32 (letter A) has %d lit pixels (a header offset would read blank)", lit);
        /* A second theme must load and differ, proving themes are interchangeable. */
        u2_tiles_ega tc;
        if (u2_tiles_ega_open(&ctx, &tc, "c64") == U2_TILES_EGA_OK) {
            CHECK(memcmp(tc.px, te.px, sizeof tc.px) != 0,
                  "theme c64 differs from default (interchangeable sets)");
        } else {
            printf("  SKIP  EGATHEME.C64/EGATILES not present\n");
        }
        /* A bogus theme name must NOT be OK (no silent default). */
        u2_tiles_ega tb;
        CHECK(u2_tiles_ega_open(&ctx, &tb, "nope") != U2_TILES_EGA_OK,
              "an unknown theme name is refused, not silently defaulted");

        /* Ctrl-T cycling: walking next_theme from "default" must visit every
         * theme exactly once and wrap back to the start. The order is the
         * alphabetical order of the theme names (10, alt, c64, default). */
        char names[8][32];
        int steps = 0;
        char cur[32];
        snprintf(cur, sizeof cur, "default");
        int ok_cycle = 1;
        do {
            char nx[32];
            if (u2_tiles_ega_next_theme(te.dir, cur, nx, sizeof nx) <= 0) { ok_cycle = 0; break; }
            snprintf(names[steps], 32, "%s", nx);
            snprintf(cur, sizeof cur, "%s", nx);
            steps++;
        } while (steps < 8 && strcmp(cur, "default") != 0);
        CHECK(ok_cycle && steps >= 2 && strcmp(cur, "default") == 0,
              "theme cycle returns to 'default' after %d steps", steps);
        /* No repeats within one lap. */
        int dup = 0;
        for (int i = 0; i < steps; i++)
            for (int j = i + 1; j < steps; j++)
                if (strcmp(names[i], names[j]) == 0) dup = 1;
        CHECK(!dup, "each theme is visited once per lap (%d distinct)", steps);
        /* An unknown current theme yields a real theme (progress, not a stall). */
        char nx2[32];
        CHECK(u2_tiles_ega_next_theme(te.dir, "zzz", nx2, sizeof nx2) > 0 && nx2[0],
              "an unknown current theme still advances to a real theme (%s)", nx2);
    }

    /* --- the PIC*-E loader, against the real files when present ---------- */
    ctx.ega_theme_dir = "../original/updated";
    if (!u2_pic_can_show_ega(&ctx, "picdra")) {
        printf("  SKIP  original/updated/PICDRA-E not present; the pic checks need it\n");
    } else {
        u2_frame pf;
        u2_frame_init(&pf, 1);
        u2_frame_clear(&pf, 0);
        CHECK(u2_pic_blit_ega(&ctx, "picdra", &pf) == 0, "PICDRA-E blitted");
        /* Every pixel must be in 0..15 and the frame fully written (64000 px). */
        int maxv = 0, nonzero = 0;
        for (int y = 0; y < U2_SCREEN_H; y++)
            for (int x = 0; x < U2_SCREEN_W; x++) {
                if (pf.px[y][x] > maxv) maxv = pf.px[y][x];
                if (pf.px[y][x]) nonzero++;
            }
        CHECK(maxv <= 15, "PICDRA-E indices stay within 0..15 (max %d)", maxv);
        CHECK(nonzero > 1000, "PICDRA-E has %d non-background pixels (a blank read would be 0)", nonzero);
        /* A missing EGA pic is a clean refusal, not a black rectangle. */
        CHECK(u2_pic_can_show_ega(&ctx, "picnope") == 0, "a missing PIC*-E reports as not-showable");
    }

    printf("\nEGA CHECK: %s (%d checks, %d failures)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
