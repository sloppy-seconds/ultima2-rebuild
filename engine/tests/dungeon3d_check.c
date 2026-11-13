/*
 * tests/dungeon3d_check.c — the enhanced dungeon draws a filled perspective corridor,
 * not the CGA wireframe.
 *
 * This is the gate for docs/enhanced-dungeon-3d.md § 7a step 1 (flat-shaded corridor).
 * It is a unit gate because driving the engine in enhanced mode needs the enhanced
 * layout, and the discriminator (a filled corridor vs a sparse wireframe) is fully
 * exercised by rendering into a frame and measuring it.
 *
 * Three parts:
 *   1. direct render: build a corridor scan (rock on both sides, an end cap at depth
 *      3) and call u2_dungeon_render3d. Assert the window is filled, the side walls
 *      and the end cap carry the wall colour, the top band is ceiling and the bottom
 *      band is floor, and nothing spills past the map window.
 *   2. dispatch: on an all-open world, u2_dungeon_render with enhanced_layout set must
 *      fill the window (the 3D path); with it clear it must stay sparse (the wireframe).
 *      This is what proves the dispatch actually routes enhanced to the 3D renderer.
 *   3. --selftest: the fill predicate must reject a blank frame and a single-line frame,
 *      so the gate can go red where it enforces (AGENTS.md § 3).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/ctx.h"
#include "core/state.h"
#include "core/frame.h"
#include "core/layout.h"
#include "game/dungeon.h"
#include "game/overworld.h"

#define WIN (U2_ENH_MAP_COLS * 32)   /* 448 */

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* Fraction of the map window that is non-black. A filled corridor is near 1.0; the
 * wireframe is a handful of lines and sits well under 0.3. */
static double ink_fraction(const u2_frame *f)
{
    long ink = 0;
    for (int y = 0; y < WIN; y++)
        for (int x = 0; x < WIN; x++)
            if (f->px[y][x] != 0) ink++;
    return (double)ink / ((double)WIN * WIN);
}

static int px(const u2_frame *f, int x, int y) { return f->px[y][x]; }

/* Count distinct palette indices present in a rectangle. A flat fill yields 1; a
 * textured wall yields more. Used to tell a texture-mapped wall from a solid one. */
static int distinct_in_rect(const u2_frame *f, int x0, int y0, int x1, int y1)
{
    int seen[256] = {0}, n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int c = f->px[y][x];
            if (!seen[c]) { seen[c] = 1; n++; }
        }
    return n;
}

static void setup(u2_ctx *ctx, u2_overworld *ow, int enhanced)
{
    memset(ctx, 0, sizeof *ctx);
    memset(ow, 0, sizeof *ow);
    ow->x = 5; ow->y = 5;
    ctx->state.face_dx = 1;
    ctx->state.face_dy = 0;
    ctx->enhanced_layout = enhanced;
}

static void enh_frame(u2_overworld *ow)
{
    u2_frame_free(&ow->frame);
    if (u2_frame_init_size(&ow->frame, U2_ENH_W, U2_ENH_H, U2_ENH_SCALE) != 0) {
        printf("frame init failed\n"); exit(2);
    }
    u2_frame_clear(&ow->frame, U2_CGA_BLACK);
}

/* ---- 1. direct render of a corridor scan ---------------------------------- */
static void test_direct(void)
{
    u2_ctx ctx; u2_overworld ow;
    setup(&ctx, &ow, 1);
    enh_frame(&ow);

    u2_dg_scan s;
    memset(&s, 0, sizeof s);
    for (int d = 0; d < U2_DG_SAMPLE_DEPTH; d++) {
        s.left[d] = U2_DG_ROCK;
        s.right[d] = U2_DG_ROCK;
        s.ctr[d] = (d >= 3) ? U2_DG_ROCK : 0x00;   /* end cap at depth 3 */
    }
    u2_dungeon_render3d(&ctx, &ow, &s);

    const double ink = ink_fraction(&ow.frame);
    CHECK(ink > 0.6, "direct: corridor should fill the window (ink=%.3f)", ink);
    CHECK(px(&ow.frame, 20, 224) == U2_CGA_WHITE,  "direct: left wall should be wall colour");
    CHECK(px(&ow.frame, 428, 224) == U2_CGA_WHITE, "direct: right wall should be wall colour");
    CHECK(px(&ow.frame, 224, 224) == U2_CGA_WHITE, "direct: end cap should be wall colour");
    CHECK(px(&ow.frame, 224, 5) == U2_CGA_CYAN,    "direct: top band should be ceiling colour");
    CHECK(px(&ow.frame, 224, 440) == U2_CGA_MAGENTA, "direct: bottom band should be floor colour");

    /* Nothing drawn past the map window's right edge (the panel starts at 448). */
    int maxx = 0;
    for (int y = 0; y < U2_ENH_H; y++)
        for (int x = 0; x < U2_ENH_W; x++)
            if (ow.frame.px[y][x] != 0 && x > maxx) maxx = x;
    CHECK(maxx < WIN, "direct: ink must stay inside the map window (maxx=%d)", maxx);

    u2_frame_free(&ow.frame);
}

/* ---- 2. the dispatch routes enhanced to the 3D path ---------------------- */
static void test_dispatch(void)
{
    u2_ctx ctx; u2_overworld ow;

    setup(&ctx, &ow, 1);
    enh_frame(&ow);
    u2_dungeon_render(&ctx, &ow, 1);
    const double enh_ink = ink_fraction(&ow.frame);
    u2_frame_free(&ow.frame);

    setup(&ctx, &ow, 0);
    enh_frame(&ow);
    u2_dungeon_render(&ctx, &ow, 1);
    const double cls_ink = ink_fraction(&ow.frame);
    u2_frame_free(&ow.frame);

    CHECK(enh_ink > 0.6, "dispatch: enhanced render should be a filled corridor (ink=%.3f)", enh_ink);
    CHECK(cls_ink < 0.3, "dispatch: classic render should stay a sparse wireframe (ink=%.3f)", cls_ink);
    CHECK(enh_ink > cls_ink + 0.3, "dispatch: enhanced must fill far more than the wireframe (%.3f vs %.3f)",
          enh_ink, cls_ink);
}

/* ---- 3. a wall texture is sampled across the wall quads ------------------- */
static void test_texture(void)
{
    u2_ctx ctx; u2_overworld ow;
    static uint8_t tex[32 * 32];
    for (int v = 0; v < 32; v++)
        for (int u = 0; u < 32; u++)
            tex[v * 32 + u] = ((u + v) & 1) ? U2_CGA_WHITE : U2_CGA_CYAN;

    u2_dg_scan s;
    memset(&s, 0, sizeof s);
    for (int d = 0; d < U2_DG_SAMPLE_DEPTH; d++) {
        s.left[d] = U2_DG_ROCK; s.right[d] = U2_DG_ROCK;
        s.ctr[d] = (d >= 3) ? U2_DG_ROCK : 0x00;
    }

    /* Flat (no theme wall): the near left-wall column is one solid colour. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    u2_dungeon_render3d(&ctx, &ow, &s);
    const int flat = distinct_in_rect(&ow.frame, 18, 60, 24, 380);
    u2_frame_free(&ow.frame);

    /* Textured (theme wall copied into ctx): the same column now alternates, so it
     * carries more than one index. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    ctx.dng_wall_set = 1;
    memcpy(ctx.dng_wall, tex, sizeof tex);
    u2_dungeon_render3d(&ctx, &ow, &s);
    const int texd = distinct_in_rect(&ow.frame, 18, 60, 24, 380);
    u2_frame_free(&ow.frame);

    CHECK(flat == 1, "texture: flat wall column should be one colour (distinct=%d)", flat);
    CHECK(texd >= 2, "texture: textured wall column should vary (distinct=%d)", texd);
}

/* ---- 4. selftest: the fill predicate can go red --------------------------- */
static int selftest(void)
{
    int bad = 0;
    u2_frame f;
    if (u2_frame_init_size(&f, U2_ENH_W, U2_ENH_H, U2_ENH_SCALE) != 0) return 1;

    /* A blank frame must NOT read as filled. */
    u2_frame_clear(&f, U2_CGA_BLACK);
    if (ink_fraction(&f) > 0.6) { printf("selftest: blank frame passed the fill predicate\n"); bad = 1; }

    /* A single line must NOT read as filled. */
    u2_frame_clear(&f, U2_CGA_BLACK);
    for (int x = 0; x < WIN; x++) f.px[224][x] = U2_CGA_WHITE;
    if (ink_fraction(&f) > 0.6) { printf("selftest: single-line frame passed the fill predicate\n"); bad = 1; }

    /* A uniform rectangle must read as one colour, so the texture predicate (>=2) can
     * go red: a flat wall would fail it. */
    u2_frame_clear(&f, U2_CGA_WHITE);
    if (distinct_in_rect(&f, 0, 0, 64, 64) != 1) {
        printf("selftest: uniform rect did not read as one colour\n"); bad = 1;
    }

    u2_frame_free(&f);
    return bad;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--selftest"))
        return selftest();

    if (argc > 2 && !strcmp(argv[1], "--dump")) {
        u2_ctx ctx; u2_overworld ow;
        setup(&ctx, &ow, 1);
        enh_frame(&ow);
        u2_dg_scan s;
        memset(&s, 0, sizeof s);
        for (int d = 0; d < U2_DG_SAMPLE_DEPTH; d++) {
            s.left[d] = U2_DG_ROCK; s.right[d] = U2_DG_ROCK;
            s.ctr[d] = (d >= 3) ? U2_DG_ROCK : 0x00;
        }
        static uint8_t tex[32 * 32];
        for (int v = 0; v < 32; v++)
            for (int u = 0; u < 32; u++)
                tex[v * 32 + u] = ((u / 4 + v / 4) & 1) ? U2_CGA_WHITE : U2_CGA_CYAN;
        ctx.dng_wall_set = 1;
        memcpy(ctx.dng_wall, tex, sizeof tex);
        /* Optional 3rd arg: a real DNGWALL.TILE (1024 theme-palette indices) to use
         * instead of the checkerboard, for eyeballing the baked FM Towns brick. */
        if (argc > 3) {
            FILE *tf = fopen(argv[3], "rb");
            if (tf) {
                size_t n = fread(ctx.dng_wall, 1, U2_DNG_WALL_PX, tf);
                fclose(tf);
                if (n == U2_DNG_WALL_PX) printf("dump: loaded wall tile from %s\n", argv[3]);
            }
        }
        u2_dungeon_render3d(&ctx, &ow, &s);
        uint8_t pal[16][3]; u2_frame_palette_ega(pal);
        u2_frame_write_bmp16(&ow.frame, argv[2], pal);
        printf("wrote %s\n", argv[2]);
        u2_frame_free(&ow.frame);
        return 0;
    }

    test_direct();
    test_dispatch();
    test_texture();

    printf("dungeon3d_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
