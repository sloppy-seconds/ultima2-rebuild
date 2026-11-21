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
    CHECK(census_is(c, 52436, 6070, 4352, 1142),
          "full census black/cyan/magenta/white = 52436/6070/4352/1142, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);

    /* The discriminator, and it is NOT a census. Both candidate layouts produce the
     * same 11,567 inked pixels for picdra, which is exactly why the field-interleave
     * reading survived as long as it did: the proof behind it was a colour census, and
     * a census cannot tell them apart. What tells them apart is position. The art is
     * 320x100 displayed doubled, so every scanline pair is the same row, and the old
     * reading (odd scanlines from offset 8000) would differ from this on 97 of the
     * 200 rows. Measured against a live capture of the original on 2026-11-21:
     * displayed row 2n equals file row n on 187 of 187 sampled rows across two
     * pictures, while row 2n+1 matched file row 100+n on 2 of 100. */
    int r4[4], r5[4];
    census(&f, 4, 5, 0, U2_SCREEN_W, r4);
    census(&f, 5, 6, 0, U2_SCREEN_W, r5);
    CHECK(r4[2] == 8 && r5[2] == 8,
          "scanlines 4 and 5 hold the same 8 magenta pixels, because the art is doubled"
          " (got %d and %d: reading the odd field from offset 8000 gives 8 and 0)",
          r4[2], r5[2]);

    int pairs = 0;
    for (int n = 0; n < U2_SCREEN_H / 2; n++) {
        int same = 1;
        for (int x = 0; x < U2_SCREEN_W; x++)
            if (f.px[2 * n][x] != f.px[2 * n + 1][x]) { same = 0; break; }
        pairs += same;
    }
    CHECK(pairs == U2_SCREEN_H / 2,
          "every scanline pair is identical (%d of %d): the picture is 320x100 doubled,"
          " so only the first 8000 bytes are pixels -- the old field reading fails this"
          " on 97 of 200 rows", pairs, U2_SCREEN_H / 2);

    /* The control that makes the check above a check: if the file's second half were a
     * copy of its first, the two layouts would be indistinguishable and the doubling
     * assertion would be vacuous. It is not a copy. (What it IS -- a horizontally
     * shifted variant of the first half -- is unexplained; see pic.c.) */
    {
        FILE *fp = fopen("../original/picdra", "rb");
        int halves_differ = 0;
        if (fp) {
            static unsigned char h1[8000], h2[8000];
            if (fread(h1, 1, 8000, fp) == 8000 && fread(h2, 1, 8000, fp) == 8000)
                halves_differ = (memcmp(h1, h2, 8000) != 0);
            fclose(fp);
        }
        CHECK(halves_differ,
              "the file's second half is not a copy of its first, so the doubled reading"
              " and the old field reading really are different layouts");
    }

    /* Quadrants close the remaining permutations the global census cannot see. */
    census(&f, 0, 100, 0, 160, c);
    CHECK(census_is(c, 13078, 1218, 1698, 6), "quadrant y0-99 x0-159 = 13078/1218/1698/6, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);
    census(&f, 0, 100, 160, 320, c);
    CHECK(census_is(c, 14142, 0, 1858, 0), "quadrant y0-99 x160-319 = 14142/0/1858/0, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);
    census(&f, 100, 200, 0, 160, c);
    CHECK(census_is(c, 10988, 4628, 308, 76), "quadrant y100-199 x0-159 = 10988/4628/308/76, got %d/%d/%d/%d",
          c[0], c[1], c[2], c[3]);
    census(&f, 100, 200, 160, 320, c);
    CHECK(census_is(c, 14228, 224, 488, 1060), "quadrant y100-199 x160-319 = 14228/224/488/1060, got %d/%d/%d/%d",
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
            for (int col = 0; col < U2_COLS; col++) {
                v.cell[r][col].ch = ' ';
                v.cell[r][col].attr = 0x07;
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

    /* CGA art on an EGA palette. The pack does not carry an EGA copy of every
     * picture in every install, and when one is missing the CGA original is shown
     * instead -- but the backend is then resolving indices against the 16-colour
     * EGA table, where CGA's cyan and magenta are NOT indices 1 and 2. Drawn raw,
     * white comes out cyan and magenta comes out green, which is what the owner
     * reported on 2026-11-20 as "the demo pictures are messed up in EGA mode".
     * u2_frame::cga_on_ega is the existing mechanism (black 0, cyan 3, magenta 5,
     * white 15); this pins that the picture path actually uses it, and that the
     * classic path is untouched.
     */
    {
        int base_c[16];
        for (int i = 0; i < 16; i++) base_c[i] = 0;
        f.cga_on_ega = 0;
        u2_pic_blit(&ctx, "pictwn", &f);
        for (int y = 0; y < U2_SCREEN_H; y++)
            for (int x = 0; x < U2_SCREEN_W; x++) base_c[f.px[y][x]]++;

        int rem_c[16];
        for (int i = 0; i < 16; i++) rem_c[i] = 0;
        f.cga_on_ega = 1;
        u2_pic_blit(&ctx, "pictwn", &f);
        for (int y = 0; y < U2_SCREEN_H; y++)
            for (int x = 0; x < U2_SCREEN_W; x++) rem_c[f.px[y][x]]++;
        f.cga_on_ega = 0;

        CHECK(rem_c[0] == base_c[0] && rem_c[3] == base_c[1]
              && rem_c[5] == base_c[2] && rem_c[15] == base_c[3],
              "CGA art on an EGA palette lands on the EGA indices for the same colours"
              " (0/3/5/15 held %d/%d/%d/%d, CGA census was %d/%d/%d/%d)",
              rem_c[0], rem_c[3], rem_c[5], rem_c[15],
              base_c[0], base_c[1], base_c[2], base_c[3]);
        CHECK(rem_c[1] == 0 && rem_c[2] == 0,
              "no raw CGA index survives the remap (index 1 held %d, index 2 held %d)"
              " -- these are EGA blue and EGA red, and a picture drawn with them is the bug",
              rem_c[1], rem_c[2]);
        CHECK(base_c[1] + base_c[2] + base_c[3] > 0,
              "the control: pictwn really does use the CGA indices being remapped"
              " (%d/%d/%d), so the check above could have failed",
              base_c[1], base_c[2], base_c[3]);
    }

    /* PICCAS is named in the executable at file offset 0x6ba and has no file in the pack.
     * Asserting it stays absent keeps that fact honest: if a future pack adds it, this goes
     * red and someone re-reads the load sites instead of never noticing. */
    CHECK(u2_pic_blit(&ctx, "piccas", &f) != 0,
          "piccas is still absent from the pack, as the executable's reference to it has never found");

    printf("\nPIC CHECK: %s (%d checks, %d failures)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
