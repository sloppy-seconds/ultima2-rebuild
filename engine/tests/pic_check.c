/*
 * tests/pic_check.c — the PIC unpack, pinned against the original's own picture.
 *
 * Why the assertions are row and quadrant censuses rather than a whole-image one:
 * every candidate layout for these bytes is a permutation of the same 16,000 bytes, so
 * the global colour census is IDENTICAL for the correct CGA interleave, for the two
 * fields swapped, and for a naive non-interleaved read (all three give
 * 52433/6061/4370/1136). A test written on the global census would pass on a decode that
 * drew the picture upside-combed. What separates them is where the pixels land: row 4
 * carries eight magenta pixels and row 5 carries none under the correct layout, and
 * swapping the fields moves those eight to row 5. That is asserted here by name, and the
 * quadrant censuses close the remaining permutations.
 *
 * The numbers themselves were derived from original/picdra with the layout proven against
 * a live capture of the original's splash (100% colour-class agreement over 4000 sampled
 * pixels; see captures/spice86/splash-look and core/pic.h). They are counts, not art, and
 * nothing here reproduces the picture.
 *
 * Build and run from engine/:
 *   cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o /tmp/pic_check \
 *      tests/pic_check.c src/core/pic.c src/core/frame.c src/debug/trace.c && /tmp/pic_check
 */

#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/font.h"
#include "core/frame.h"
#include "core/pic.h"

static int checks, fails;

#define CHECK(cond, ...) do {                                          \
        ++checks;                                                      \
        if (cond) { printf("  ok    "); }                              \
        else { ++fails; printf("  FAIL  "); }                          \
        printf(__VA_ARGS__);                                           \
        printf("\n");                                                  \
    } while (0)

static void census(const u2_frame *f, int y0, int y1, int x0, int x1, int out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            out[f->px[y][x] & 3]++;
}

static int census_is(const int got[4], int k, int c, int m, int w)
{
    return got[0] == k && got[1] == c && got[2] == m && got[3] == w;
}

int main(void)
{
    u2_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.data_dir = "../original";

    printf("pic_check\n");

    /* A missing file must be a clean refusal, not a black rectangle. This is the whole
     * reason the caller branches before drawing: the terminal build and any player who
     * has not pointed --data-dir at the game both land here. */
    u2_frame f;
    u2_frame_init(&f, 1);   /* heap-backed store; see frame_check.c */
    u2_frame_clear(&f, U2_CGA_WHITE);
    CHECK(u2_pic_blit(&ctx, "picdoesnotexist", &f) != 0,
          "a missing picture is refused rather than blitted");
    CHECK(f.px[0][0] == U2_CGA_WHITE, "a refused blit leaves the frame untouched");

    ctx.data_dir = ".";
    CHECK(u2_pic_blit(&ctx, "picdra", &f) != 0,
          "an empty data dir is refused, not read as a zero-length picture");
    ctx.data_dir = "../original";

    u2_frame_clear(&f, U2_CGA_BLACK);
    if (u2_pic_blit(&ctx, "picdra", &f) != 0) {
        printf("  SKIP  original/picdra not present (gitignored by policy); "
               "the census checks below need it\n");
        printf("PIC CHECK: PASS (%d checks)\n", checks);
        return 0;
    }
    CHECK(1, "picdra unpacked");

    int c[4];
    census(&f, 0, U2_SCREEN_H, 0, U2_SCREEN_W, c);
    CHECK(census_is(c, 52433, 6061, 4370, 1136),
          "full census black/cyan/magenta/white = 52433/6061/4370/1136, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);

    /* The discriminator. Under the correct even/odd interleave the dragon's magenta
     * starts on scanline 4; swap the two fields and it lands on 5 instead, while every
     * global count stays identical. */
    int r4[4], r5[4];
    census(&f, 4, 5, 0, U2_SCREEN_W, r4);
    census(&f, 5, 6, 0, U2_SCREEN_W, r5);
    CHECK(r4[2] == 8 && r5[2] == 0,
          "scanline 4 holds the 8 magenta pixels and scanline 5 none "
          "(got %d and %d: a field swap moves them to 5)", r4[2], r5[2]);

    /* Quadrants close the remaining permutations the global census cannot see. */
    census(&f, 0, 100, 0, 160, c);
    CHECK(census_is(c, 13765, 635, 1597, 3), "quadrant y0-99 x0-159 = 13765/635/1597/3, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);
    census(&f, 0, 100, 160, 320, c);
    CHECK(census_is(c, 13625, 442, 1933, 0), "quadrant y0-99 x160-319 = 13625/442/1933/0, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);
    census(&f, 100, 200, 0, 160, c);
    CHECK(census_is(c, 12245, 2882, 420, 453), "quadrant y100-199 x0-159 = 12245/2882/420/453, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);
    census(&f, 100, 200, 160, 320, c);
    CHECK(census_is(c, 12798, 2102, 420, 680), "quadrant y100-199 x160-319 = 12798/2102/420/680, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);

    /* The other five must unpack to the same geometry without error, because the demo
     * path picks among them at runtime and a short file would otherwise show as a
     * half-drawn screen. */
    static const char *const others[] = { "picspa", "picmin", "picdng", "pictwn", "picout" };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) {
        u2_frame_clear(&f, U2_CGA_BLACK);
        const int rc = u2_pic_blit(&ctx, others[i], &f);
        census(&f, 0, U2_SCREEN_H, 0, U2_SCREEN_W, c);
        const int total = c[0] + c[1] + c[2] + c[3];
        CHECK(rc == 0 && total == U2_SCREEN_W * U2_SCREEN_H,
              "%s unpacks to a full 320x200 (rc=%d, %d pixels)", others[i], rc, total);
    }

    /* The text layer over a picture must not paint a background.
     *
     * This is the gate for the defect that made the demonstration's couplets sit on black
     * instead of on the starfield: u2_pic_blit wrote the art, then u2_font_draw_vram_rows
     * filled every one of the 2000 cells with text_bg and deleted it. Both halves are
     * asserted, because a check that only tests the new function cannot tell which of the
     * two it was given.
     */
    {
        u2_vram v;
        memset(&v, 0, sizeof v);
        for (int r = 0; r < U2_ROWS; r++)
            for (int c = 0; c < U2_COLS; c++) {
                v.cell[r][c].ch = ' ';
                v.cell[r][c].attr = 0x07;
            }
        static const char cap[] = "BATTLE STRANGE CREATURES";
        for (int i = 0; cap[i]; i++)
            v.cell[23][i].ch = (uint8_t)cap[i];

        u2_frame_clear(&f, U2_CGA_BLACK);
        u2_frame_set_text_fg(&f, U2_CGA_WHITE);
        u2_pic_blit(&ctx, "pictwn", &f);
        int ink0 = 0;
        for (int y = 0; y < U2_SCREEN_H; y++)
            for (int x = 0; x < U2_SCREEN_W; x++)
                if (f.px[y][x]) ink0++;

        u2_font_draw_vram_rows(&f, &v, 0, U2_ROWS - 1);
        int ink_plain = 0;
        for (int y = 0; y < U2_SCREEN_H; y++)
            for (int x = 0; x < U2_SCREEN_W; x++)
                if (f.px[y][x]) ink_plain++;
        CHECK(ink_plain < ink0 / 4,
              "the plain text layer wipes the picture it was drawn over (%d ink left of %d)"
              " -- this is the bug, pinned so a regression cannot call it a fix", ink_plain, ink0);

        u2_pic_blit(&ctx, "pictwn", &f);
        u2_font_draw_vram_rows_overlay(&f, &v, 0, U2_ROWS - 1);
        int ink_ov = 0;
        for (int y = 0; y < U2_SCREEN_H; y++)
            for (int x = 0; x < U2_SCREEN_W; x++)
                if (f.px[y][x]) ink_ov++;
        CHECK(ink_ov >= ink0,
              "the overlay text layer leaves the picture intact and adds the caption"
              " (%d ink vs %d before)", ink_ov, ink0);
    }

    /* PICCAS is named in the executable at file offset 0x6ba and has no file in the pack.
     * Asserting it stays absent keeps that fact honest: if a future pack adds it, this goes
     * red and someone re-reads the load sites instead of never noticing. */
    CHECK(u2_pic_blit(&ctx, "piccas", &f) != 0,
          "piccas is still absent from the pack, as the executable's reference to it has never found");

    printf("\nPIC CHECK: %s (%d checks, %d failures)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
