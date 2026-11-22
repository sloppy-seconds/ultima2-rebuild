/* Which star field the warp draws, and how a caller says which one.
 *
 * The mode used to be carried by a pointer: warp_x == NULL meant "use the hash field".
 * That is an in-band signal, and it had already bitten once — the scene builder did not
 * initialise the pointers, every stack caller handed the draw garbage, the table branch
 * ran, and the scripted launch in the space gate stopped reaching orbit with no hint of
 * the cause. The mode is named now, so a forgotten initialisation picks a mode and says
 * which, instead of dereferencing nothing.
 *
 * This check is the other half of that fix. The table path existed and nothing drew
 * through it, so nothing could tell a working table branch from one that plotted
 * nowhere. Each assertion below fails if the branch is removed, if the guard on `warping`
 * is dropped, or if the builder stops defaulting to the hash field.
 */
#include <stdio.h>
#include <string.h>

#include "core/frame.h"
#include "core/layout.h"      /* U2_ENH_MAP_ROWS, the enhanced map area */
#include "core/state.h"
#include "game/space_view.h"

static int checks, fails;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* Lit pixels inside the field rectangle. Used for comparison, never as an exact expected
 * count: the scene draw also plots the ship cross and the FUEL/XENO/YAKO/ZABO readout, so
 * a full field measures 75 lit pixels for a 64-star table and the remainder belongs to
 * chrome, not stars. */
static int lit_in_field(const u2_frame *f, const u2_space_scene *s)
{
    const int sc = s->scale > 0 ? s->scale : 1;
    int n = 0;
    for (int vy = 0; vy < U2_SPACE_VIEW_ROWS; vy++)
        for (int vx = 0; vx < U2_SPACE_VIEW_COLS; vx++) {
            int px = s->ox + vx * sc, py = s->oy + vy * sc;
            if (px >= 0 && py >= 0 && px < f->w && py < f->h && f->px[py][px]) n++;
        }
    return n;
}

/* How many of the table's own mapped positions are lit. This is the direct question —
 * did the renderer read the table — and it is the only assertion that cannot be satisfied
 * by a field that happens to light a similar number of pixels. */
static int table_hits(const u2_frame *f, const u2_space_scene *s,
                      const uint8_t *tx, const uint8_t *ty)
{
    const int sc = s->scale > 0 ? s->scale : 1;
    int n = 0;
    for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++) {
        int px = s->ox + tx[i] * sc, py = s->oy + ty[i] * sc;
        if (px >= 0 && py >= 0 && px < f->w && py < f->h && f->px[py][px]) n++;
    }
    return n;
}

/* 64 distinct, in-range positions, so a correct table draw lights exactly 64 pixels. */
static void fill_table(uint8_t *tx, uint8_t *ty)
{
    for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++) {
        tx[i] = (uint8_t)(i * 4);            /* 0..252, inside the 256-wide field */
        ty[i] = (uint8_t)(i * 2);            /* 0..126, inside the 128-tall field */
    }
}

int main(void)
{
    u2_ctx ctx;
    u2_frame f;
    uint8_t tx[U2_SPACE_ORBIT_STARS], ty[U2_SPACE_ORBIT_STARS];
    fill_table(tx, ty);

    memset(&ctx, 0, sizeof ctx);
    u2_state_init(&ctx.state);
    if (u2_frame_init(&f, 1) != 0) { printf("frame init failed\n"); return 1; }

    /* --- the builder's default is the mode, not a NULL ---------------------- */
    u2_space_scene sc;
    u2_space_scene_from(&ctx, &sc);
    CHECK(sc.warp_source == U2_WARP_FROM_HASH,
          "scene_from selects the hash field by name, not by leaving a pointer empty");
    CHECK(sc.warp_x == NULL && sc.warp_y == NULL,
          "and it still clears the tables, so a stack scene cannot carry a stale address");

    /* --- the table mode draws the table ------------------------------------- */
    sc.warping = 1;
    sc.keep_frame = 0;                 /* clear first, so the count is this draw alone */
    u2_space_scene_use_table(&sc, tx, ty);
    CHECK(sc.warp_source == U2_WARP_FROM_TABLE, "use_table sets the mode");
    u2_space_scene_draw(&f, &sc);
    int table_lit = lit_in_field(&f, &sc);
    CHECK(table_hits(&f, &sc, tx, ty) == U2_SPACE_ORBIT_STARS,
          "every one of the 64 positions the table was given is lit — this is the branch "
          "that had never been drawn through");
    /* Spot-check the mapping, not just the hits: view_plot is ox + vx*scale, so star 0 at
     * field (0,0) lands at (16,16) and star 63 at field (252,126) lands at (268,142). */
    CHECK(f.px[sc.oy + 0][sc.ox + 0] != 0, "star 0 plots at its mapped pixel");
    CHECK(f.px[sc.oy + 126][sc.ox + 252] != 0, "the last star plots at its mapped pixel");
    CHECK(f.px[sc.oy + 1][sc.ox + 2] == 0,
          "and nothing is drawn between them — the field is the table, not a fill");

    /* --- the mode actually switches the source ------------------------------ */
    sc.warp_source = U2_WARP_FROM_HASH;
    u2_space_scene_draw(&f, &sc);
    int hash_lit = lit_in_field(&f, &sc);
    CHECK(hash_lit != table_lit,
          "the same scene in hash mode lights a different number of pixels (%d vs %d) — if "
          "these are equal the mode is not consulted and the branch is dead",
          hash_lit, table_lit);
    CHECK(table_hits(&f, &sc, tx, ty) < U2_SPACE_ORBIT_STARS,
          "and hash mode does not reproduce the table's positions, so the two fields are "
          "genuinely different sources rather than one field drawn twice");

    /* --- the guard: a table without a warp is ignored ----------------------- */
    /* warping = 0 is not "no field". It is the orbit field, which the header records as a
     * visibly different screen from the warp field (~64 scattered points against a radial
     * streak). So the assertion is that the table is not consulted, not that the screen is
     * empty — an empty screen here would be its own bug. */
    u2_space_scene_use_table(&sc, tx, ty);
    sc.warping = 0;
    u2_space_scene_draw(&f, &sc);
    int orbit_lit = lit_in_field(&f, &sc);
    CHECK(table_hits(&f, &sc, tx, ty) < U2_SPACE_ORBIT_STARS,
          "a table set but not warping does not draw the table — the mode does not "
          "override the warp flag, so a static orbit keeps its own drawing path");
    sc.warp_source = U2_WARP_FROM_HASH;
    u2_space_scene_draw(&f, &sc);
    CHECK(lit_in_field(&f, &sc) == orbit_lit && orbit_lit > 0,
          "and it draws the same non-empty field the hash mode draws at rest (%d), so the "
          "table being ignored falls back to the orbit field rather than to a blank screen",
          orbit_lit);

    /* --- the builder owns the canvas geometry (issue #17) -------------------
     * The enhanced doubling used to live in flight.c's paint path, which meant a scene
     * built from an enhanced ctx was only correct after a caller remembered to fix it.
     * These assertions are what make the move load-bearing: without them the whole
     * block can be deleted and the suite stays green, which is how a refactor that
     * silently un-does itself passes review. */
    ctx.enhanced_layout = 0;
    u2_space_scene classic;
    u2_space_scene_from(&ctx, &classic);
    CHECK(classic.scale == 1 && classic.ox == U2_SPACE_VIEW_X
          && classic.oy == U2_SPACE_VIEW_Y,
          "a classic ctx gets the original's 1:1 rectangle at its measured offset");

    ctx.enhanced_layout = 1;
    u2_space_scene enh;
    u2_space_scene_from(&ctx, &enh);
    CHECK(enh.scale == 2 && enh.ox == 0,
          "an enhanced ctx gets the doubled field with no caller involved");
    CHECK(enh.oy == (U2_ENH_MAP_ROWS * 32 - U2_SPACE_VIEW_ROWS * enh.scale) / 2,
          "and it is centred in the map area rather than pinned to its top");
    CHECK(enh.oy >= 0
          && enh.oy + U2_SPACE_VIEW_ROWS * enh.scale <= U2_ENH_MAP_ROWS * 32,
          "the doubled field stays inside the map area, so it is offset, not clipped");

    /* Drawable as-is is the actual claim. A scene built from an enhanced ctx must put
     * stars on the frame with nothing but the builder and the draw. */
    enh.keep_frame = 0;
    u2_frame_clear(&f, 0);
    u2_space_scene_draw(&f, &enh);
    CHECK(lit_in_field(&f, &enh) > 0,
          "and the enhanced scene draws a non-empty field straight from ctx, which is "
          "what 'the builder owns the geometry' has to mean to be worth anything");

    u2_frame_free(&f);
    printf("space_warp_check: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
