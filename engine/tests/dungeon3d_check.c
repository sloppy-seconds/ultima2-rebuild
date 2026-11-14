/*
 * tests/dungeon3d_check.c — the enhanced dungeon draws a filled, textured,
 * raycast 3D view of the grid — not the CGA wireframe, not a flat corridor,
 * and not any theme's art.
 *
 * Gate for docs/enhanced-dungeon-3d.md after the 2026-11-14 owner ruling:
 * procedural art (dungeon_assets.c), DDA raycaster into u2_frame, instant
 * movement. The FM Towns blit route is archived on branch
 * archive/fmtowns-dungeon-route.
 *
 * Parts:
 *   1. direct render of a built room: the window fills, walls are textured
 *      (several indices), the ceiling band is dark, nothing spills past the
 *      map window.
 *   2. the door system renders by the render-stop rule: 0xC0 draws as a
 *      passage (plank threshold visible), 0xE0 draws exactly like rock —
 *      a fake wall must not look fake.
 *   3. sprites: a creature stamp draws its creature (a colour no static
 *      geometry uses), a chest draws wood, and neither appears when absent.
 *   4. dispatch: enhanced routes to the 3D path, classic stays the wireframe.
 *   5. assets: deterministic across re-init, every texture carries more than
 *      one index, sprites carry both transparent and opaque pixels.
 *   6. --selftest: the predicates can go red (AGENTS.md § 3).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/ctx.h"
#include "core/state.h"
#include "core/frame.h"
#include "core/layout.h"
#include "core/world.h"
#include "game/dungeon.h"
#include "game/dungeon_assets.h"
#include "game/overworld.h"

#define WIN (U2_ENH_MAP_COLS * 32)   /* 448 */

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* Fraction of the map window that is non-black. A raycast view is near 1.0;
 * the wireframe is a handful of lines and sits well under 0.3. */
static double ink_fraction(const u2_frame *f)
{
    long ink = 0;
    for (int y = 0; y < WIN; y++)
        for (int x = 0; x < WIN; x++)
            if (f->px[y][x] != 0) ink++;
    return (double)ink / ((double)WIN * WIN);
}

static int px(const u2_frame *f, int x, int y) { return f->px[y][x]; }

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

/* Count pixels of one index in the map window. */
static long count_index(const u2_frame *f, int colour)
{
    long n = 0;
    for (int y = 0; y < WIN; y++)
        for (int x = 0; x < WIN; x++)
            if (f->px[y][x] == colour) n++;
    return n;
}

/* Count pixels whose palette index is >= lo. Concept sprite art lives in the
 * 16..255 sub-palette; the procedural geometry and EGA panel stay in 0..15, so
 * this distinguishes "a concept sprite drew" from "only static geometry drew". */
static long count_index_ge(const u2_frame *f, int lo)
{
    long n = 0;
    for (int y = 0; y < WIN; y++)
        for (int x = 0; x < WIN; x++)
            if (f->px[y][x] >= lo) n++;
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

/* A room the player stands in, facing +x: end wall at x=8, side walls along
 * y=3 and y=7 from x=5..8. All rock (0x80). The cell at (8,5) is `centre`. */
static void build_room(u2_overworld *ow, uint8_t centre)
{
    for (int y = 3; y <= 7; y++)
        for (int x = 5; x <= 8; x++)
            u2_world_dungeon_put(&ow->world, 0, x, y, 0x00);
    for (int y = 3; y <= 7; y++) u2_world_dungeon_put(&ow->world, 0, 8, y, U2_DG_ROCK);
    for (int x = 5; x <= 8; x++) {
        u2_world_dungeon_put(&ow->world, 0, x, 3, U2_DG_ROCK);
        u2_world_dungeon_put(&ow->world, 0, x, 7, U2_DG_ROCK);
    }
    u2_world_dungeon_put(&ow->world, 0, 8, 5, centre);
}

/* ---- 1. the room renders as a filled, textured view --------------------- */
static void test_room(void)
{
    u2_ctx ctx; u2_overworld ow;
    setup(&ctx, &ow, 1);
    enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_dungeon_render(&ctx, &ow, 1);

    const double ink = ink_fraction(&ow.frame);
    CHECK(ink > 0.6, "room: view should fill the window (ink=%.3f)", ink);

    /* The end cap is stone, not flat: several indices in the centre band. */
    const int d = distinct_in_rect(&ow.frame, 180, 150, 270, 300);
    CHECK(d >= 3, "room: end cap should be textured (distinct=%d)", d);

    /* The ceiling band near the horizon is dark rock: low indices only. */
    int ceil_ok = 1;
    for (int x = 100; x < 350; x += 7) {
        const int c = px(&ow.frame, x, 120);
        if (c != 0 && c != 7 && c != 8) ceil_ok = 0;
    }
    CHECK(ceil_ok, "room: ceiling band should stay dark stone");

    /* Nothing drawn past the map window's right edge (the panel starts at 448). */
    int maxx = 0;
    for (int y = 0; y < U2_ENH_H; y++)
        for (int x = 0; x < U2_ENH_W; x++)
            if (ow.frame.px[y][x] != 0 && x > maxx) maxx = x;
    CHECK(maxx < WIN, "room: ink must stay inside the map window (maxx=%d)", maxx);

    u2_frame_free(&ow.frame);
}

/* ---- 2. doors render by the render-stop rule ----------------------------- */
static void build_doorwall(u2_overworld *ow, uint8_t centre)
{
    /* A corridor at x=5, a wall at x=6 whose gap at (6,5) is `centre`, and an
     * open room beyond at x=7. The player stands at (5,5) facing east, so the
     * centre tile is one cell ahead — close enough that the wood frame stays at
     * shade level 0 and its brown survives to be counted. */
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
        u2_world_dungeon_put(&ow->world, 0, x, y, U2_DG_ROCK);
    for (int y = 4; y <= 6; y++) {
        u2_world_dungeon_put(&ow->world, 0, 5, y, 0x00);
        u2_world_dungeon_put(&ow->world, 0, 7, y, 0x00);
    }
    u2_world_dungeon_put(&ow->world, 0, 6, 5, centre);
}

static void test_doors(void)
{
    u2_ctx ctx; u2_overworld ow;

    /* 0xC0 doorway one cell ahead: draws as a stone wall with a framed wooden
     * opening. The frame's wood (index 6) is the marker; rock carries none. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_doorwall(&ow, U2_DG_DOOR);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index(&ow.frame, 6) > 0,
          "door: 0xC0 should draw a framed wooden opening");
    u2_frame_free(&ow.frame);

    /* Standing INSIDE the doorway: the wall is a full cell thick, so the framed
     * opening reappears on the far face one cell ahead. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_doorwall(&ow, U2_DG_DOOR);
    ow.x = 6; ow.y = 5;   /* stand in the door cell */
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index(&ow.frame, 6) > 0,
          "door: standing in the doorway should show the frame on the far face");
    u2_frame_free(&ow.frame);

    /* rock one cell ahead: a plain wall, no wood frame. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_doorwall(&ow, U2_DG_ROCK);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index(&ow.frame, 6) == 0, "door: rock should carry no wood frame");
    u2_frame_free(&ow.frame);

    /* 0xE0 fake wall: must draw EXACTLY like rock — solid, no frame, fills view. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_doorwall(&ow, U2_DG_SECRET);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index(&ow.frame, 6) == 0,
          "secret: 0xE0 must draw as solid rock, not a framed door");
    const double ink = ink_fraction(&ow.frame);
    CHECK(ink > 0.6, "secret: fake wall should still fill the view (ink=%.3f)", ink);
    u2_frame_free(&ow.frame);
}

/* ---- 3. sprites ---------------------------------------------------------- */
static void test_sprites(void)
{
    u2_ctx ctx; u2_overworld ow;

    /* Baseline: the bare rock room draws only EGA indices (0..15). Concept
     * creature art lives in the 16..255 sub-palette, so none of that range
     * appears until a creature is present. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index_ge(&ow.frame, 16) == 0,
          "sprite: bare room should show no concept-sprite colours");
    u2_frame_free(&ow.frame);

    /* A stamp-2 creature one cell ahead must appear as concept art (indices
     * 16..255), a range nothing else in the room produces. One cell (not two)
     * keeps it inside shade level 0 now that the camera pull-back adds half a
     * cell of distance, so the sprite shows its bright base colours. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_world_dungeon_put(&ow.world, 0, 6, 5, 0x02);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index_ge(&ow.frame, 16) > 50, "sprite: creature stamp should draw");
    u2_frame_free(&ow.frame);

    /* A chest stays procedural wood (brown 6): the chest concept is a multi-chest
     * sheet, so the billboard is still the procedural chest. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_world_dungeon_put(&ow.world, 0, 6, 5, U2_DG_CHEST);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index(&ow.frame, 6) > 50, "sprite: chest should draw");
    u2_frame_free(&ow.frame);
}

/* ---- 3b. ladders hang on the far wall ------------------------------------ */
/* The ladder's rungs are pale yellow (14), a colour a bare rock room carries
 * nowhere, so yellow's presence proves the ladder drew — and its absence in the
 * control proves the check can fail. Both ladder bits must draw it. */
static void test_ladders(void)
{
    u2_ctx ctx; u2_overworld ow;

    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index(&ow.frame, 14) == 0, "ladder: bare room should show no rung yellow");
    u2_frame_free(&ow.frame);

    /* Up-ladder one cell ahead (rock end cap behind it) must draw its rungs. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_world_dungeon_put(&ow.world, 0, 6, 5, U2_DG_UP);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index(&ow.frame, 14) > 20, "ladder: up-ladder should draw rungs");
    u2_frame_free(&ow.frame);

    /* Down-ladder likewise. */
    setup(&ctx, &ow, 1); enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_world_dungeon_put(&ow.world, 0, 6, 5, U2_DG_DOWN);
    u2_dungeon_render(&ctx, &ow, 1);
    CHECK(count_index(&ow.frame, 14) > 20, "ladder: down-ladder should draw rungs");
    u2_frame_free(&ow.frame);
}

/* ---- 4. the dispatch routes enhanced to the 3D path ---------------------- */
static void test_dispatch(void)
{
    u2_ctx ctx; u2_overworld ow;

    setup(&ctx, &ow, 1);
    enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_dungeon_render(&ctx, &ow, 1);
    const double enh_ink = ink_fraction(&ow.frame);
    u2_frame_free(&ow.frame);

    setup(&ctx, &ow, 0);
    enh_frame(&ow);
    build_room(&ow, U2_DG_ROCK);
    u2_dungeon_render(&ctx, &ow, 1);
    const double cls_ink = ink_fraction(&ow.frame);
    u2_frame_free(&ow.frame);

    CHECK(enh_ink > 0.6, "dispatch: enhanced render should fill the view (ink=%.3f)", enh_ink);
    CHECK(cls_ink < 0.3, "dispatch: classic render should stay a sparse wireframe (ink=%.3f)", cls_ink);
    CHECK(enh_ink > cls_ink + 0.3, "dispatch: enhanced must fill far more than the wireframe (%.3f vs %.3f)",
          enh_ink, cls_ink);
}

/* ---- 5. the asset set is sane and deterministic --------------------------- */
static void test_assets(void)
{
    static uint8_t snap[U2_DGA_TEX_COUNT][U2_DGA_TEX_PX];
    for (int id = 0; id < U2_DGA_TEX_COUNT; id++)
        memcpy(snap[id], u2_dga_tex(id, 0), U2_DGA_TEX_PX);

    /* Re-init must reproduce byte-identical art: the generator is seeded and
     * integer-only, so a differing byte means a nondeterministic generator. */
    u2_dga_init();
    int same = 1;
    for (int id = 0; id < U2_DGA_TEX_COUNT; id++)
        if (memcmp(snap[id], u2_dga_tex(id, 0), U2_DGA_TEX_PX) != 0) same = 0;
    CHECK(same, "assets: re-init must reproduce identical textures");

    /* Every texture carries more than one index (a blank texture is a bug). */
    for (int id = 0; id < U2_DGA_TEX_COUNT; id++) {
        int seen[256] = {0}, n = 0;
        for (int i = 0; i < U2_DGA_TEX_PX; i++)
            if (!seen[u2_dga_tex(id, 0)[i]]) { seen[u2_dga_tex(id, 0)[i]] = 1; n++; }
        CHECK(n >= 2, "assets: texture %d should carry >= 2 indices (got %d)", id, n);
    }

    /* Every sprite has both transparent and opaque pixels, sampled at its own
     * edge (32 for procedural art, 128 for a concept-derived sprite). */
    for (int id = 0; id < U2_DGA_SP_COUNT; id++) {
        int trans = 0, opaque = 0;
        const int edge = u2_dga_sprite_edge(id);
        for (int i = 0; i < edge * edge; i++) {
            const uint8_t c = u2_dga_sprite(id, 0)[i];
            if (c == U2_DGA_TRANSPARENT) trans++; else opaque++;
        }
        CHECK(trans > 0 && opaque > 0, "assets: sprite %d should mix clear and drawn", id);
    }

    /* The concept-derived sprite (stamp 1) is high-resolution and draws from the
     * expanded palette (frame indices 16..255), not the EGA 0..15. This is the
     * pipeline the owner locked in 2026-11-14: 256-colour + 128px source. */
    CHECK(u2_dga_sprite_edge(U2_DGA_SP_MON1) == 128,
          "assets: the concept sprite is 128px");
    CHECK(u2_dga_palette_ready(), "assets: the concept sub-palette is loaded");
    int hi = 0;
    for (int i = 0; i < 128 * 128; i++) {
        uint8_t c = u2_dga_sprite(U2_DGA_SP_MON1, 0)[i];
        if (c != U2_DGA_TRANSPARENT && c >= 16) hi++;
    }
    CHECK(hi > 1000, "assets: the concept sprite draws expanded-palette indices");

    /* Shade levels darken: level 3 must have no index level 0 does not shade to. */
    int lit_white = 0, dark_white = 0;
    for (int i = 0; i < U2_DGA_TEX_PX; i++) {
        if (u2_dga_tex(U2_DGA_WALL_STONE, 0)[i] == 15) lit_white = 1;
        if (u2_dga_tex(U2_DGA_WALL_STONE, 3)[i] == 15) dark_white = 1;
    }
    CHECK(lit_white && !dark_white, "assets: far shade must remove white from the wall");
}

/* ---- 6. selftest: the predicates can go red -------------------------------- */
/* The CGA-on-EGA remap: the enhanced layout always draws on the 16-colour EGA
 * palette, so when the overworld art fell back to CGA its four indices must land
 * on the EGA indices of the same colour (black 0, cyan 3, magenta 5, white 15),
 * and every index above 3 — the dungeon's greys and creature accents — must pass
 * through untouched. A frame that never sets the flag must be pure identity. */
static void test_cga_on_ega(void)
{
    u2_frame f;
    if (u2_frame_init_size(&f, 320, 200, 1) != 0) { fails++; return; }

    f.cga_on_ega = 0;
    for (int c = 0; c < 16; c++)
        CHECK(u2_frame_cga_idx(&f, c) == c,
              "cga_on_ega=0: index %d must be identity", c);

    f.cga_on_ega = 1;
    CHECK(u2_frame_cga_idx(&f, 0) == 0,  "cga_on_ega: CGA black -> EGA black");
    CHECK(u2_frame_cga_idx(&f, 1) == 3,  "cga_on_ega: CGA cyan -> EGA cyan");
    CHECK(u2_frame_cga_idx(&f, 2) == 5,  "cga_on_ega: CGA magenta -> EGA magenta");
    CHECK(u2_frame_cga_idx(&f, 3) == 15, "cga_on_ega: CGA white -> EGA white");
    /* The dungeon writes these directly and must not be recoloured by the flag. */
    CHECK(u2_frame_cga_idx(&f, 7) == 7,  "cga_on_ega: EGA lt grey passes through");
    CHECK(u2_frame_cga_idx(&f, 8) == 8,  "cga_on_ega: EGA dk grey passes through");
    CHECK(u2_frame_cga_idx(&f, 15) == 15,"cga_on_ega: EGA white passes through");

    u2_frame_free(&f);
}

static int selftest(void)
{
    int bad = 0;
    u2_frame f;
    if (u2_frame_init_size(&f, U2_ENH_W, U2_ENH_H, U2_ENH_SCALE) != 0) return 1;

    u2_frame_clear(&f, U2_CGA_BLACK);
    if (ink_fraction(&f) > 0.6) { printf("selftest: blank frame passed the fill predicate\n"); bad = 1; }

    u2_frame_clear(&f, U2_CGA_BLACK);
    for (int x = 0; x < WIN; x++) f.px[224][x] = U2_CGA_WHITE;
    if (ink_fraction(&f) > 0.6) { printf("selftest: single-line frame passed the fill predicate\n"); bad = 1; }

    u2_frame_clear(&f, U2_CGA_WHITE);
    if (distinct_in_rect(&f, 0, 0, 64, 64) != 1) {
        printf("selftest: uniform rect did not read as one colour\n"); bad = 1;
    }
    if (count_index(&f, 10) != 0) {
        printf("selftest: colour predicate found green where there is none\n"); bad = 1;
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
        build_room(&ow, U2_DG_ROCK);
        u2_world_dungeon_put(&ow.world, 0, 7, 5, 0x02);          /* creature */
        u2_world_dungeon_put(&ow.world, 0, 6, 4, U2_DG_CHEST);   /* chest    */
        u2_world_dungeon_put(&ow.world, 0, 6, 6, U2_DG_DOWN);    /* ladder   */
        u2_dungeon_render(&ctx, &ow, 1);
        uint8_t pal[16][3]; u2_frame_palette_ega(pal);
        u2_frame_write_bmp16(&ow.frame, argv[2], pal);
        printf("wrote %s\n", argv[2]);
        u2_frame_free(&ow.frame);
        return 0;
    }

    test_room();
    test_doors();
    test_sprites();
    test_ladders();
    test_dispatch();
    test_assets();
    test_cga_on_ega();

    printf("dungeon3d_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
