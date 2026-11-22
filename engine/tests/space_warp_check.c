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
#include <stdlib.h>
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

/* The ship cross exactly as asm/6D83 draws it: vertical stroke x fixed at ship_x, y from
 * ship_y-3 to ship_y+2; horizontal stroke y fixed at ship_y, x from ship_x-3 to ship_x+3.
 * True only when all thirteen land lit at the given VIEW position, so a stray star cannot
 * satisfy it. */
static int cross_at(const u2_frame *f, const u2_space_scene *s, int vx, int vy)
{
    const int sc = s->scale > 0 ? s->scale : 1;
    for (int d = U2_SPACE_SHIP_V_FROM; d <= U2_SPACE_SHIP_V_TO; d++)
        for (int k = 0; k < sc; k++) {
            int px = s->ox + vx * sc + k, py = s->oy + (vy + d) * sc;
            if (px < 0 || py < 0 || px >= f->w || py >= f->h || !f->px[py][px]) return 0;
        }
    for (int d = U2_SPACE_SHIP_H_FROM; d <= U2_SPACE_SHIP_H_TO; d++)
        for (int k = 0; k < sc; k++) {
            int px = s->ox + (vx + d) * sc, py = s->oy + vy * sc + k;
            if (px < 0 || py < 0 || px >= f->w || py >= f->h || !f->px[py][px]) return 0;
        }
    return 1;
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

    /* --- the cross is drawn AT THE SHIP, and the presets are the asm's ------------
     * asm/6999 is the orbit command loop; its arrow arms (L_006cb9..L_006d3c) print the
     * word, call asm/6CC4 to erase at the OLD position, and store a FIXED PRESET into
     * DS:47B/DS:47C. There is no step. asm/6D83 then draws the sprite at that pair, so the
     * cross lands on the preset and the field sits relative to it. Every assertion here
     * fails if the cross is pinned back to the centre or the presets become a delta. */
    u2_space_scene ss;
    /* The enhanced block above leaves ctx.enhanced_layout set, and an enhanced scene is
     * scale 2 -- which clips against this 320x200 frame and makes every position
     * assertion below about clipping rather than about the sprite. The presets are screen
     * coordinates in both modes, so classic is the honest place to pin them. */
    ctx.enhanced_layout = 0;
    ctx.state.space_ship_x = U2_SPACE_SHIP_X0;
    ctx.state.space_ship_y = U2_SPACE_SHIP_Y0;
    u2_space_scene_from(&ctx, &ss);
    ss.warping = 0; ss.keep_frame = 0;
    u2_frame_clear(&f, 0);
    u2_space_scene_draw(&f, &ss);
    CHECK(cross_at(&f, &ss, U2_SPACE_SHIP_X0, U2_SPACE_SHIP_Y0),
          "at the entry preset the cross sits at the field centre, which is all that "
          "g90-warp-table ever saw and what got misread as a pin");

    ctx.state.space_ship_x = U2_SPACE_SHIP_LEFT_X;
    ctx.state.space_ship_y = U2_SPACE_SHIP_Y0;
    u2_space_scene_from(&ctx, &ss);
    ss.warping = 0; ss.keep_frame = 0;
    u2_frame_clear(&f, 0);
    u2_space_scene_draw(&f, &ss);
    CHECK(cross_at(&f, &ss, U2_SPACE_SHIP_LEFT_X, U2_SPACE_SHIP_Y0),
          "LEFT stores 0x20/0x40 (asm/6999 L_006ce4) and the cross is drawn there");
    CHECK(!cross_at(&f, &ss, U2_SPACE_SHIP_X0, U2_SPACE_SHIP_Y0),
          "and the centre carries no cross once the ship has moved: the sprite travels, "
          "it is not pinned");

    ctx.state.space_ship_x = U2_SPACE_SHIP_RIGHT_X;
    ctx.state.space_ship_y = U2_SPACE_SHIP_Y0;
    u2_space_scene_from(&ctx, &ss);
    ss.warping = 0; ss.keep_frame = 0;
    u2_frame_clear(&f, 0);
    u2_space_scene_draw(&f, &ss);
    CHECK(cross_at(&f, &ss, U2_SPACE_SHIP_RIGHT_X, U2_SPACE_SHIP_Y0),
          "RIGHT stores 0xDF/0x40 (asm/6999 L_006d0a region) and the cross is drawn there");

    ctx.state.space_ship_x = U2_SPACE_SHIP_X0;
    ctx.state.space_ship_y = U2_SPACE_SHIP_CLIMB_Y;
    u2_space_scene_from(&ctx, &ss);
    ss.warping = 0; ss.keep_frame = 0;
    u2_frame_clear(&f, 0);
    u2_space_scene_draw(&f, &ss);
    CHECK(cross_at(&f, &ss, U2_SPACE_SHIP_X0, U2_SPACE_SHIP_CLIMB_Y),
          "CLIMB stores 0x80/0x10 and the cross is drawn there");

    /* --- the field is state, and it converges on the cross ------------------------
     * asm/6B3D rewrites DS:4CD/DS:50D in place and the store puts the new value into both
     * the table and the plot coordinates, so a table entry IS a screen coordinate and the
     * motion lives in the step, not in the draw. A hash scatter cannot express this, which
     * is why the engine grew real tables. */
    uint8_t ox[U2_SPACE_ORBIT_STARS], oy[U2_SPACE_ORBIT_STARS];
    u2_space_orbit_init(ox, oy, 0x517u);
    int ymask_ok = 1;
    for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++)
        if (oy[i] > 0x7f) ymask_ok = 0;
    CHECK(ymask_ok, "asm/6D3E masks the y byte to 0..127 and the init reproduces it");

    uint8_t bx[U2_SPACE_ORBIT_STARS], by[U2_SPACE_ORBIT_STARS];
    memcpy(bx, ox, sizeof bx); memcpy(by, oy, sizeof by);
    long d_before = 0;
    for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++) {
        int dx = abs((int)bx[i] - 0x20), dy = abs((int)by[i] - 0x40);
        d_before += dx + dy;
    }
    u2_space_orbit_pull(ox, oy, 0x20, 0x40);
    long d_after = 0;
    for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++) {
        int dx = abs((int)ox[i] - 0x20), dy = abs((int)by[i] - 0x40);
        d_after += dx + dy;
    }
    CHECK(memcmp(bx, ox, sizeof bx) != 0 || memcmp(by, oy, sizeof by) != 0,
          "one pass actually moves stars: the field is stepped, not recomputed from a seed");
    CHECK(d_after < d_before,
          "and the whole field is closer to the cross than it was (%ld -> %ld): the pull is "
          "toward the ship on both axes, which is what asm/6B3D's sbb/adc arms do",
          d_before, d_after);

    /* Convergence, not a one-shot shift: repeated passes keep closing the distance. This is
     * the property the owner described -- the field keeps moving after a turn -- and it is
     * the one a static offset cannot fake. The exact per-bucket magnitudes (1..16 on x,
     * 1..8 on y) are implemented from the live CS read but are NOT pinned by exact-value
     * tests here: which arm a star takes depends on a byte-compare/borrow boundary that the
     * listing supports more than one reading of, and encoding a guessed expectation as a
     * check would be worse than leaving it open. A live read of the tables across one pass
     * pins it; see G92. */
    for (int pass = 0; pass < 8; pass++) u2_space_orbit_pull(ox, oy, 0x20, 0x40);
    long d_later = 0;
    for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++) {
        int dx = abs((int)ox[i] - 0x20), dy = abs((int)oy[i] - 0x40);
        d_later += dx + dy;
    }
    CHECK(d_later < d_after,
          "and eight more passes close it further (%ld -> %ld): the field converges on the "
          "cross over time rather than moving once", d_after, d_later);

    ctx.state.space_ship_x = U2_SPACE_SHIP_X0;
    ctx.state.space_ship_y = U2_SPACE_SHIP_DIVE_Y;
    u2_space_scene_from(&ctx, &ss);
    ss.warping = 0; ss.keep_frame = 0;
    u2_frame_clear(&f, 0);
    u2_space_scene_draw(&f, &ss);
    CHECK(cross_at(&f, &ss, U2_SPACE_SHIP_X0, U2_SPACE_SHIP_DIVE_Y),
          "DIVE stores 0x80/0x6F and the cross is drawn there");

    u2_frame_free(&f);
    printf("space_warp_check: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
