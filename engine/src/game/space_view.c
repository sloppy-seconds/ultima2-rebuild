/* space_view.c — the in-space screen: star field, ship cross, readout values.
 *
 * See space_view.h for why this is a separate translation unit from space.c.
 *
 * GRADE DISCIPLINE ON THIS FILE. Three of the four things it draws are measured and
 * one is not, and the difference is stated at each site rather than averaged into a
 * single "space screen" claim:
 *
 *   ship sprite   MEASURED. asm/6CC4_space_ship_erase erases a plus of exactly 6 + 7
 *                 pixels about DS:47B/DS:47C; the draw is the mirror of the erase.
 *   ship origin   MEASURED. DS:47B/47C read 0x80/0x40 in orbit
 *                 (captures/spice86/w6-orbit-dive).
 *   readout       MEASURED. asm/6999 writes FUEL from DS:E5 and the triple from the
 *                 world table at file 0x7212; arrows leave the triple alone.
 *   star field   PARTLY DECODED. The rectangle it is plotted into, the origin inset,
 *                the fact that it is white-only, and the centre of the radiating variant
 *                are all MEASURED (see space_view.h: asm/4B76's origin pair 16/16, and
 *                the white-pixel extents and centroid of frames 014/049/050/096/097).
 *                What is NOT decoded is the per-star algorithm, the star count for the
 *                warp field, and the animation rate -- the frames loop samples every
 *                4-5 s and the warp lasts under a second, so no two frames bracket a
 *                single star. The positions below are a deterministic hash inside the
 *                measured rectangle. Do not cite them as evidence about the original.
 */
#include "game/space_view.h"

#include "core/ctx.h"
#include "core/state.h"     /* u2_bcd_dec1, the item and world bytes */
#include "game/overworld.h"  /* U2_ITEM_TRI_LITHIUM */
#include "game/space.h"      /* u2_space_worlds, U2_WORLD_COUNT */

/* The star field occupies the asm/4B76 view: 256x128 inset at (16,16). See space_view.h
 * -- the extents are measured off five capture frames, not chosen. */
#define U2_SPACE_ORBIT_STARS  64    /* 77 white px in frames 226/227/228 minus the
                                     * 13-px ship cross = ~64 stars. Measured. */
#define U2_SPACE_WARP_RAYS    64    /* STRUCTURE measured (dots lie on rays out of the
                                     * view centre), COUNT not decoded. */
#define U2_SPACE_WARP_PER_RAY 24

/* A plain xorshift, not the game's RNG: the game's generator is a 16-bit LCG whose
 * output is a gameplay-observable quantity, and reusing it here would make a purely
 * cosmetic choice perturb the sequence every byte-level test depends on. Keeping the
 * two apart is what lets the star field be wrong without taking anything else down.
 */
static uint32_t star_hash(uint32_t s)
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

/* Plot in view coordinates, i.e. add the asm/4B76 origin pair. Everything the space
 * screen draws goes through here so the measured inset cannot be bypassed by accident. */
static void view_plot(u2_frame *f, int vx, int vy)
{
    if (vx < 0 || vx >= U2_SPACE_VIEW_COLS || vy < 0 || vy >= U2_SPACE_VIEW_ROWS)
        return;
    u2_frame_pixel(f, U2_SPACE_VIEW_X + vx, U2_SPACE_VIEW_Y + vy, U2_CGA_WHITE);
}

void u2_space_scene_from(const u2_ctx *ctx, u2_space_scene *out)
{
    const u2_state *st = &ctx->state;

    /* The triple is a property of the world byte, not of the ship: measured, an arrow
     * key moved the sprite and left all three bytes alone. Deep space (0x0A) is a
     * state rather than a world, and it has no table row, so it falls back to the
     * last-known triple the caller already carries in the scene. */
    if (st->world < U2_WORLD_COUNT) {
        const u2_space_world *w = &u2_space_worlds[st->world];
        out->xeno = w->xeno;
        out->yako = w->yako;
        out->zabo = w->zabo;
    }

    out->fuel = u2_bcd_dec1(st->items_held[U2_ITEM_TRI_LITHIUM]);
    out->warping = 0;   /* the warp animation's trigger and duration are NOT decoded -- see file header */
    out->ship_x = st->space_ship_x;
    out->ship_y = st->space_ship_y;
    out->star_seed = ((uint32_t)st->world << 16) ^
                     ((uint32_t)out->xeno << 8) ^ (uint32_t)out->yako;
}

void u2_space_scene_draw(u2_frame *f, const u2_space_scene *s)
{
    u2_frame_clear(f, U2_CGA_BLACK);

    uint32_t h = star_hash(s->star_seed ? s->star_seed : 1u);

    if (s->warping) {
        /* Radiating field: dots along rays out of the view centre. The centre and the
         * rectangle are measured (space_view.h); the ray count, the per-ray spacing and
         * the animation rate are NOT decoded -- see the file header. */
        for (int r = 0; r < U2_SPACE_WARP_RAYS; r++) {
            h = star_hash(h);
            const int angle_num = (int)(h % 1024u);          /* 0..1023 of a turn */
            for (int d = 4; d < U2_SPACE_VIEW_COLS / 2; d += 3) {
                h = star_hash(h);
                if ((h & 3u) == 0) continue;                 /* sparse along the ray */
                /* integer ray march: dx,dy from a coarse angle, radius d */
                const int dx = d * (int)(((angle_num % 512u) * 2 - 512)) / 512;
                const int dy = d * (int)(((angle_num % 256u) * 2 - 256)) / 256;
                view_plot(f, U2_SPACE_CENTRE_X + dx, U2_SPACE_CENTRE_Y + dy);
            }
        }
    } else {
        /* Orbit field: scattered points across the view. ~64 of them, measured. */
        for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++) {
            h = star_hash(h);
            const int x = (int)(h % U2_SPACE_VIEW_COLS);
            const int y = (int)((h >> 16) % U2_SPACE_VIEW_ROWS);
            view_plot(f, x, y);
        }
    }

    /* The ship: the plus asm/6CC4 erases, drawn back. Vertical stroke x fixed, y
     * from -3 to +2; horizontal stroke y fixed, x from -3 to +3. These are SCREEN
     * coordinates (DS:47B/47C read 0x80/0x40 on the screen, not in the view). */
    for (int d = U2_SPACE_SHIP_V_FROM; d <= U2_SPACE_SHIP_V_TO; d++)
        u2_frame_pixel(f, s->ship_x, s->ship_y + d, U2_CGA_WHITE);
    for (int d = U2_SPACE_SHIP_H_FROM; d <= U2_SPACE_SHIP_H_TO; d++)
        u2_frame_pixel(f, s->ship_x + d, s->ship_y, U2_CGA_WHITE);
}
