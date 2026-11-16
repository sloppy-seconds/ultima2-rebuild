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
 *                The star COUNT is now corroborated two ways: measured off five frames as
 *                ~64, and read out of the binary as two 64-entry tables (DS:4CD for x,
 *                DS:50D for y, stride 64) in asm/6B3D_space_field_draw.
 *                What is NOT decoded is the per-star position algorithm and what the
 *                field's mode >= 2 branch draws. The warp TRIGGER and DURATION are read
 *                out of asm/6DFD (four bounded redraw loops, DS:4C7 as the motion mode,
 *                DS:4C8 as the frame counter, delays 0x16/0x18/0x1a) — see that header.
 *                An earlier note here said the warp "lasts under a second"; that was
 *                inferred from a frame loop sampling every 4-5 s and the bytes contradict
 *                it, so it is gone rather than softened. The positions below are still a
 *                deterministic hash inside the measured rectangle. Do not cite them as
 *                evidence about the original.
 */
#include "game/space_view.h"

#include "core/ctx.h"
#include "core/state.h"     /* u2_bcd_dec1, the item and world bytes */
#include "game/overworld.h"  /* U2_ITEM_TRI_LITHIUM */
#include "game/space.h"      /* u2_space_worlds, U2_WORLD_COUNT */

/* The star field occupies the asm/4B76 view: 256x128 inset at (16,16). See space_view.h
 * -- the extents are measured off five capture frames, not chosen. The star COUNT lives
 * in space_view.h now, because the warp tables in u2_overworld are sized by it and a
 * second copy would be free to disagree. */
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
    /* The builder sets every field it knows about, and callers declare the struct on the
     * stack, so a pointer field added later MUST be initialised here or it arrives as
     * stack garbage. That is what happened when warp_x/warp_y were added: the draw took the
     * table branch with wild pointers and the scripted launch in the space gate stopped
     * reaching orbit, with no hint of the cause. */
    out->warp_x = NULL;
    out->warp_y = NULL;
    out->warping = 0;   /* The scene builder stays stateless so a gate can build a scene from
                         * state alone. The warp is driven by the caller: commands.c
                         * space_warp_field() / space_warp_engage() rebuild the scene with
                         * this overridden for the duration of asm/6DFD's redraw loops. See
                         * that function's note for what is bytes and what is invented. */
    out->ship_x = st->space_ship_x;
    out->ship_y = st->space_ship_y;
    out->star_seed = ((uint32_t)st->world << 16) ^
                     ((uint32_t)out->xeno << 8) ^ (uint32_t)out->yako;
}

void u2_space_warp_seed(uint8_t *sx, uint8_t *sy, int n, uint32_t seed)
{
    uint32_t h = star_hash(seed ? seed : 1u);
    for (int i = 0; i < n; i++) {
        h = star_hash(h);
        sx[i] = (uint8_t)(h % 256u);
        sy[i] = (uint8_t)((h >> 16) % 200u);   /* the view is 128 tall; the tables are bytes */
    }
}

int u2_space_warp_advance(uint8_t *sx, uint8_t *sy, int n, int ship_x, int ship_y)
{
    /* One axis of asm/6B3D's step rule. `ship` is DS:47B for x and DS:47C for y.
     * `ytable` is non-zero to use the eight-entry y table, whose upper half is unknown. */
    int moved = 0;
    for (int i = 0; i < n; i++) {
        int old = sx[i];
        unsigned d = (unsigned)(uint8_t)(old - ship_x);
        int di = (int)(d >> 4);
        if (di > 15) di = 15;                       /* cannot happen with uint8_t, pinned */
        int step = di + 1;                          /* cs:0x6cac[i] == i + 1 */
        if (old >= ship_x) {
            int nv = old + step;
            if (nv <= 0xff) sx[i] = (uint8_t)nv;    /* adc overflow -> jae not taken -> skip */
        } else {
            /* The original complements the wrapped difference, so its bucket here is
             * (ship - old - 1) >> 4, not (ship - old) >> 4. Kept as it is written. */
            di = (ship_x - old - 1) >> 4;
            step = di + 1;
            if (old >= step) sx[i] = (uint8_t)(old - step);
        }

        old = sy[i];
        d = (unsigned)(uint8_t)(old - ship_y);
        di = (int)(d >> 4);
        if (di < 8) {                               /* cs:0x6cbc is only 8 entries deep */
            step = di + 1;
            if (old >= ship_y) {
                int nv = old + step;
                if (nv <= 0xff) sy[i] = (uint8_t)nv;
            } else {
                di = (ship_y - old - 1) >> 4;
                if (di < 8) {
                    step = di + 1;
                    if (old >= step) sy[i] = (uint8_t)(old - step);
                }
            }
        }
        moved++;
    }
    return moved;
}

void u2_space_scene_draw(u2_frame *f, const u2_space_scene *s)
{
    /* The trail, and the whole of it: mode-0 frames of asm/6B3D do not erase the star's
     * old position and the warp path never clears, so the previous frame is the canvas.
     * Clearing here is what made the engine's warp a shimmer instead of streaks. */
    if (!s->keep_frame)
        u2_frame_clear(f, U2_CGA_BLACK);

    uint32_t h = star_hash(s->star_seed ? s->star_seed : 1u);

    if (s->warping && s->warp_x && s->warp_y) {
        /* The stateful field: positions come from the caller's tables, which it has
         * already advanced with u2_space_warp_advance(). This is the path the original
         * takes, and the picture it produces is the one the frames show -- sparse dots
         * strung out along rays, because a star plots one pixel per pass at wherever its
         * own distance put it, not one pixel per pass along a shared ray. */
        for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++)
            view_plot(f, s->warp_x[i], s->warp_y[i]);
    } else if (s->warping) {
        /* Radiating field: dots along rays out of the view centre. The centre and the
         * rectangle are measured (space_view.h); the ray count, the per-ray spacing and
         * the animation rate are NOT decoded -- see the file header.
         *
         * The radius grows with warp_frame and wraps, which is the engine's stand-in for
         * the tables asm/6B3D walks: the rays themselves are stable across frames (the
         * seed is, and the hash chain per ray does not depend on the radius), so what
         * accumulates when the frame is kept is a line along each ray rather than noise.
         * Wrapping is what the original's byte arithmetic does too. */
        const int span = U2_SPACE_VIEW_COLS / 2 - 4;
        for (int r = 0; r < U2_SPACE_WARP_RAYS; r++) {
            h = star_hash(h);
            const int angle_num = (int)(h % 1024u);          /* 0..1023 of a turn */
            for (int k = 0; k < span / 3; k++) {
                const int d = 4 + (k * 3 + s->warp_frame) % span;
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
