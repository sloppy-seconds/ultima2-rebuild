/*
 * tests/dungeon_scale_check.c — the dungeon wireframe fills the map window of a
 * doubled frame, and stays CGA-sized in CGA.
 *
 * The dungeon renderer authors its geometry in the 320x200 CGA layout (DG_ORIGIN
 * 16, a view rect about 256x128). Before this change it drew those coordinates as
 * raw physical pixels, so in classic EGA (a 640x400 frame, scale 2) the wireframe
 * landed in the top-left quarter of the screen — the bug Paul reported. The fix
 * routes the line and sprite writers through u2_frame_*_scaled, which multiply the
 * logical coordinates by the frame scale and draw scale-thick, so the same view
 * fills the doubled frame's map window.
 *
 * This is a unit gate rather than a CP7 headless case because driving the engine in
 * EGA mode needs EGA theme art present (the engine refuses to start --ega without
 * it), which the cleanroom test tree does not carry. The dungeon draws no theme art,
 * so the scaling is fully exercised by calling u2_dungeon_render directly into a
 * scale-1 and a scale-2 frame and measuring the ink bounding box.
 *
 * The scale decision lives in dg_scale: classic EGA scales by the frame scale, CGA
 * by 1, and enhanced by 1 (its map window is narrower than twice the dungeon's
 * logical width; the enhanced dungeon is deferred to docs/enhanced-layout-plan.md
 * § 10). This gate covers the CGA and classic-EGA arms, which is the change.
 *
 * Build and run by tests/run_cp7.sh. Pass --selftest to prove the fill predicate
 * can go red: it feeds two scale-1 boxes and requires the predicate to reject them.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/ctx.h"
#include "core/state.h"
#include "core/frame.h"
#include "game/dungeon.h"
#include "game/overworld.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

typedef struct { int minx, miny, maxx, maxy; } bbox;

/* Build a minimal lit dungeon: player at (5,5) facing east on an all-open floor
 * (a memset world reads 0x00 = open), so dg_draw reaches the near depth frame and
 * draws the screen-filling rectangle. No sprites (strokes not loaded). */
static void setup(u2_ctx *ctx, u2_overworld *ow)
{
    memset(ctx, 0, sizeof *ctx);
    memset(ow, 0, sizeof *ow);
    ow->x = 5; ow->y = 5;
    ctx->state.face_dx = 1;
    ctx->state.face_dy = 0;
    ctx->enhanced_layout = 0;      /* classic path: scale follows the frame */
}

static void render_into(u2_ctx *ctx, u2_overworld *ow, int scale)
{
    u2_frame_free(&ow->frame);
    if (u2_frame_init(&ow->frame, scale) != 0) {
        printf("  FAIL  could not init a scale-%d frame\n", scale);
        exit(1);
    }
    u2_dungeon_render(ctx, ow, 1);   /* lit = 1, so the wireframe is drawn */
}

static bbox ink_bbox(const u2_frame *f)
{
    bbox b = { 1 << 30, 1 << 30, -1, -1 };
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++)
            if (f->px[y][x] != 0) {
                if (x < b.minx) b.minx = x;
                if (y < b.miny) b.miny = y;
                if (x > b.maxx) b.maxx = x;
                if (y > b.maxy) b.maxy = y;
            }
    if (b.maxx < 0) { b.minx = b.miny = b.maxx = b.maxy = 0; }
    return b;
}

static int bw(const bbox *b) { return b->maxx - b->minx + 1; }
static int bh(const bbox *b) { return b->maxy - b->miny + 1; }

/* The predicate the gate enforces: the doubled frame's ink spans at least 1.8x the
 * single frame's in both axes (a true 2x fill, with slack for line-thickness and
 * the origin inset). Returns 0 when it does NOT fill, which is the failure shape. */
static int fills_doubled(const bbox *one, const bbox *two)
{
    return bw(two) * 10 >= bw(one) * 18 && bh(two) * 10 >= bh(one) * 18;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        /* Red control: two identical scale-1 boxes must NOT satisfy the fill
         * predicate. If this ever returns "fills", the gate is toothless. */
        bbox a = { 16, 16, 271, 143 };
        bbox same = { 16, 16, 271, 143 };
        if (fills_doubled(&a, &same)) {
            printf("SELFTEST BROKE: fill predicate accepted two equal boxes\n");
            return 1;
        }
        bbox dbl = { 32, 32, 543, 287 };
        if (!fills_doubled(&a, &dbl)) {
            printf("SELFTEST BROKE: fill predicate rejected a genuine 2x box\n");
            return 1;
        }
        printf("dungeon_scale_check: selftest ok\n");
        return 0;
    }

    u2_ctx ctx; u2_overworld ow;

    /* --- CGA (scale 1): the wireframe stays in the CGA layout -------------- */
    setup(&ctx, &ow);
    render_into(&ctx, &ow, 1);
    bbox cga = ink_bbox(&ow.frame);
    CHECK(bw(&cga) >= 200 && bw(&cga) <= 272,
          "CGA dungeon ink spans the CGA view (width %d, expected 200..272)", bw(&cga));
    CHECK(bh(&cga) >= 100 && bh(&cga) <= 144,
          "CGA dungeon ink spans the CGA view (height %d, expected 100..144)", bh(&cga));

    /* --- classic EGA (scale 2): the same view fills the doubled frame ------ */
    setup(&ctx, &ow);
    render_into(&ctx, &ow, 2);
    bbox ega = ink_bbox(&ow.frame);
    CHECK(fills_doubled(&cga, &ega),
          "EGA dungeon fills the doubled frame (cga %dx%d -> ega %dx%d, want >=1.8x)",
          bw(&cga), bh(&cga), bw(&ega), bh(&ega));
    /* It must actually reach across the 640-wide window, not just be bigger. */
    CHECK(bw(&ega) >= 480,
          "EGA dungeon ink reaches across the map window (width %d, want >=480)", bw(&ega));
    /* And it must not have grown past the doubled CGA layout (would spill). */
    CHECK(bw(&ega) <= 544 && bh(&ega) <= 288,
          "EGA dungeon ink stays within the doubled CGA layout (%dx%d, max 544x288)",
          bw(&ega), bh(&ega));

    u2_frame_free(&ow.frame);

    printf("dungeon_scale_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
