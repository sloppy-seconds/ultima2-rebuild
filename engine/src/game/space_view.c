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
#include "core/layout.h"     /* U2_ENH_MAP_ROWS, the enhanced map area */
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
 * screen draws goes through here so the measured inset cannot be bypassed by accident.
 *
 * The view is a 256x128 rectangle measured off the original's own 320x200 screen, where
 * it is 80% of the width and 64% of the height. On the 1024x512 enhanced canvas those
 * same pixels are 25% and 25%, which is what the owner meant by "squished" (frames
 * 2026-11-20). So the plot carries a transform: `scale` of 1 with the measured origin
 * reproduces the original exactly, and the enhanced caller passes 2 with an origin that
 * centres the doubled field in the map area. Every star, every warp streak and the ship
 * cross go through it, so there is one place where the geometry is decided. */
static void view_plot(u2_frame *f, const u2_space_scene *s, int vx, int vy)
{
    if (vx < 0 || vx >= U2_SPACE_VIEW_COLS || vy < 0 || vy >= U2_SPACE_VIEW_ROWS)
        return;
    const int sc = s->scale > 0 ? s->scale : 1;
    const int px = s->ox + vx * sc;
    const int py = s->oy + vy * sc;
    for (int dy = 0; dy < sc; dy++)
        for (int dx = 0; dx < sc; dx++)
            u2_frame_pixel(f, px + dx, py + dy, U2_CGA_WHITE);
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
     * stack, so a field added later MUST be initialised here or it arrives as stack
     * garbage. That is what happened when warp_x/warp_y were added: the draw took the
     * table branch with wild pointers and the scripted launch in the space gate stopped
     * reaching orbit, with no hint of the cause. */
    out->warp_source = U2_WARP_FROM_HASH;
    out->warp_x = NULL;
    out->warp_y = NULL;
    out->warping = 0;   /* The scene builder stays stateless so a gate can build a scene from
                         * state alone. The warp is driven by the caller: commands.c
                         * space_warp_field() / space_warp_engage() rebuild the scene with
                         * this overridden for the duration of asm/6DFD's redraw loops. See
                         * that function's note for what is bytes and what is invented. */
    out->ship_x = st->space_ship_x;
    out->ship_y = st->space_ship_y;
    /* The original's own geometry. A caller that wants the field bigger or moved
     * overrides these three before drawing; nothing else in the file reads them. */
    out->scale = 1;
    out->ox = U2_SPACE_VIEW_X;
    out->oy = U2_SPACE_VIEW_Y;
    /* The field's 256x128 rectangle was measured on the original's 320x200 screen,
     * where it is most of the picture. On the 1024x512 enhanced canvas the same
     * pixels are a stamp in the corner -- owner, 2026-11-20: "space is squished".
     * Doubled it fills the map area's height and runs 64 px under the panel, which is
     * opaque chrome drawn after it, so the overlap is hidden rather than clipped.
     *
     * This moved out of flight.c's paint path on 2026-11-23 (issue #17) because it is a
     * property of the canvas the ctx describes, not of the frame being painted: every
     * caller that draws an enhanced scene needs the same doubling, and a caller that
     * forgot would quietly draw the corner stamp again. A scene built from an enhanced
     * ctx is now drawable as-is, which is what "the builder owns the geometry" means.
     * The warp triple stays the caller's -- see the note above -- and so does
     * keep_frame, which is the sweep's accumulate flag rather than anything in ctx. */
    if (ctx->enhanced_layout) {
        out->scale = 2;
        out->ox = 0;
        out->oy = (U2_ENH_MAP_ROWS * 32 - U2_SPACE_VIEW_ROWS * out->scale) / 2;
    }
    out->star_seed = ((uint32_t)st->world << 16) ^
                     ((uint32_t)out->xeno << 8) ^ (uint32_t)out->yako;
}

void u2_space_scene_use_table(u2_space_scene *s, const uint8_t *x, const uint8_t *y)
{
    s->warp_source = U2_WARP_FROM_TABLE;
    s->warp_x = x;
    s->warp_y = y;
}

void u2_space_warp_seed(uint8_t *sx, uint8_t *sy, int n, uint32_t seed)
{
    /* asm/6D3E_sub_6d3e is the original's initialiser and it is exact: 64 entries, x from
     * one random byte, y from a second masked with `and $0x7f`. The walk counts DOWN from
     * 0x3f, which is why the table is filled back to front there; order does not matter
     * here because the field is unordered. What matters and is reproduced is the 0x7f mask,
     * because that is what keeps every y bucket inside the eight-entry y table. */
    uint32_t h = star_hash(seed ? seed : 1u);
    for (int i = 0; i < n; i++) {
        h = star_hash(h);
        sx[i] = (uint8_t)(h % 256u);
        sy[i] = (uint8_t)((h >> 16) % 128u);
    }
}

/* Leg boundaries live in the header: the warp driver splits its two calls at the end of
 * the accumulate window and so needs U2_SWEEP_DESCEND and U2_SWEEP_ZERO too. */

int u2_space_warp_sweep_frames(void)
{
    return U2_SWEEP_DESCEND + U2_SWEEP_ZERO + U2_SWEEP_ONE
         + U2_SWEEP_ASCEND + U2_SWEEP_TOP_HOLD;
}

u2_warp_sweep u2_space_warp_sweep(int frame)
{
    u2_warp_sweep s;
    if (frame < 0) frame = 0;

    if (frame < U2_SWEEP_DESCEND) {
        /* 127 decrements spread over 192 frames. Interpolated, so the value is monotone
         * and lands exactly on 1 at the end of the leg; the original's own cadence inside
         * this leg is not recoverable from what was sampled. */
        s.value = 128 - (frame * 127) / U2_SWEEP_DESCEND;
        s.accumulate = 0;
    } else if (frame < U2_SWEEP_DESCEND + U2_SWEEP_ZERO) {
        s.value = 0;
        s.accumulate = 1;          /* asm/6B3D skips the erase here: this is the trail */
    } else if (frame < U2_SWEEP_DESCEND + U2_SWEEP_ZERO + U2_SWEEP_ONE) {
        s.value = 1;
        s.accumulate = 0;
    } else if (frame < U2_SWEEP_DESCEND + U2_SWEEP_ZERO + U2_SWEEP_ONE + U2_SWEEP_ASCEND) {
        int f = frame - (U2_SWEEP_DESCEND + U2_SWEEP_ZERO + U2_SWEEP_ONE);
        s.value = 2 * (f + 1);
        s.accumulate = 0;
    } else {
        s.value = 128;
        s.accumulate = 0;
    }
    return s;
}

/* asm/6B3D reads two step tables off the CS segment. Both were read live at CS:0x17D:0x6cac
 * and CS:0x17D:0x6cbc (linear 0x0847C / 0x0848C) and both are constant across a title screen
 * and a live orbit.
 *
 * The x table is 16 entries, value = index + 1.
 *
 * The y table is EIGHT entries, value = index + 1 -- and the bytes after it are the program's
 * own instructions: `a0 7b 04 a2 79 04 a0 7c`. The y bucket is never below 12 for a star
 * seeded by asm/6D3E (`y = rand & 0x7f`, ship at 0x40), so **every y step the original takes
 * comes from reading past the end of its own table into its own code.** That is a bug in the
 * game, but it is a deterministic bug: the code bytes do not change, so the steps are fixed
 * at 121, 4, 160, 124 for buckets 12, 13, 14, 15. This is G89's y half, closed by reading
 * the table width instead of inferring it -- the listing cannot say where a table ends.
 */
static const uint8_t u2_warp_step_y[16] = {
    1, 2, 3, 4, 5, 6, 7, 8,                 /* cs:0x6cbc..0x6cc3, the table proper */
    0xa0, 0x7b, 0x04, 0xa2,                 /* cs:0x6cc4..0x6cc7, code */
    0x79, 0x04, 0xa0, 0x7c                  /* cs:0x6cc8..0x6ccb, code */
};

int u2_space_warp_advance(uint8_t *sx, uint8_t *sy, int n, int ship_x, int ship_y)
{
    /* asm/6B3D's four arms, transcribed. Two things the first version of this function got
     * backwards, both of which matter:
     *
     * 1. THE ARMS WERE SWAPPED. `L_006d55 jb L_006d7f` branches when old < ship, so the
     *    FALL-THROUGH is the old >= ship case, and the fall-through is the `sbb` (subtract)
     *    arm. The engine had >= as the add arm.
     * 2. THE BUCKET IS THE COMPLEMENT OF THE DISTANCE, NOT THE DISTANCE. `L_006d57
     *    xor $0xff,%al` then four `rcr` with `clc` before each (so a logical shift, not an
     *    arithmetic one) gives di = (~(old - ship)) >> 4 on the >= arm, and the < arm skips
     *    the xor, giving di = ((old - ship) mod 256) >> 4.
     *
     * The stores are still not all gated the same way, and that asymmetry is what separates
     * a field that runs forever from one that drains:
     *
     *   x, old >= ship : L_006d6e sbb ; cmc ; jb L_006d79 ; L_006d79 mov %al,0x4c5
     *                    store AFTER the branch -> an underflow is refused.
     *   x, old <  ship : L_006d93 adc ; L_006d98 mov %al,0x4c5 ; L_006d9b jae
     *                    store BEFORE the branch -> it wraps and is kept.
     *   y, old >= ship : L_006dc7 sbb ; cmc ; L_006dcd mov %al,0x4c6 ; jns
     *                    store BEFORE the branch -> kept even when the result goes negative.
     *   y, old <  ship : L_006de9 adc ; jns L_006df3 ; L_006df3 mov %al,0x4c6
     *                    store AFTER the branch -> refused once the result reaches 0x80.
     *
     * Net effect, with the ship at 0x80/0x40: every star's x is pulled toward the ship's
     * column (down by 8..16 from above, up by 9..16 from below), and every star's y is
     * scattered by a step of 4, 121, 124 or 160 depending on which code byte its bucket
     * lands on. Whether the plotted position is the table value or a ship-relative one is a
     * separate question from how the table moves; this function is only the table.
     */
    int moved = 0;
    for (int i = 0; i < n; i++) {
        int old = sx[i], d, di, step, nv;
        /* x: MEASURED, not transcribed. The arms below used to be a static reading of
         * asm/6B3D and they pulled every star TOWARD the ship's column, which is the
         * opposite of a warp. The rule here was fitted instead of read, from the
         * capture that already existed: captures/spice86/g90-warp-table/table.txt
         * carries 300 consecutive frames of DS:4CD.., and pairing each frame with the
         * next gives 11941 changed star samples. Bucketing by
         * `((old - centre) & 0xff) >> 4` and taking the dominant displacement in each
         * bucket gives, with no ambiguity (500-750 samples on the dominant value per
         * bucket against single-digit noise on the rest):
         *
         *   bucket   0  1  2  3  4  5  6  7   8  9 10 11 12 13 14 15
         *   old-new -1 -2 -3 -4 -5 -6 -7 -8  +8 +7 +6 +5 +4 +3 +2 +1
         *
         * i.e. new = old + (bucket+1) on the near side and old - (16-bucket) on the far
         * side: every star moves AWAY from the centre by an amount proportional to its
         * distance from it, 1 px next to the middle and 8 px at the edge. That is a
         * radial expansion, and it is the zoom the owner described.
         *
         * The centre is the ship's screen position, and the same capture reads DS:47B
         * and DS:47C as 0x80 and 0x40 in every one of 120 frames: the crosshair does
         * not move, the field moves under it. See the cross below. */
        d = (old - ship_x) & 0xff;
        di = d >> 4;
        if (di < 8) {
            step = di + 1;
            sx[i] = (uint8_t)((old + step) & 0xff);
        } else {
            step = 16 - di;
            sx[i] = (uint8_t)((old - step) & 0xff);
        }

        old = sy[i];
        if (old >= ship_y) {
            d = (old - ship_y) & 0xff;
            di = ((~d) & 0xff) >> 4;
            step = u2_warp_step_y[di];
            sy[i] = (uint8_t)((old - step) & 0xff);
        } else {
            d = (old - ship_y) & 0xff;
            di = d >> 4;
            step = u2_warp_step_y[di];
            nv = (old + step) & 0xff;
            if (nv < 0x80) sy[i] = (uint8_t)nv;
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

    if (s->warping && s->warp_source == U2_WARP_FROM_TABLE && s->warp_x && s->warp_y) {
        /* The stateful field: positions come from the caller's tables, which it has
         * already advanced with u2_space_warp_advance(). This is the path the original
         * takes, and the picture it produces is the one the frames show -- sparse dots
         * strung out along rays, because a star plots one pixel per pass at wherever its
         * own distance put it, not one pixel per pass along a shared ray. */
        for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++)
            view_plot(f, s, s->warp_x[i], s->warp_y[i]);
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
                view_plot(f, s, U2_SPACE_CENTRE_X + dx, U2_SPACE_CENTRE_Y + dy);
            }
        }
    } else {
        /* Orbit field: scattered points across the view. ~64 of them, measured. */
        for (int i = 0; i < U2_SPACE_ORBIT_STARS; i++) {
            h = star_hash(h);
            const int x = (int)(h % U2_SPACE_VIEW_COLS);
            const int y = (int)((h >> 16) % U2_SPACE_VIEW_ROWS);
            view_plot(f, s, x, y);
        }
    }

    /* The ship: the plus asm/6CC4 erases, drawn back. Vertical stroke x fixed, y
     * from -3 to +2; horizontal stroke y fixed, x from -3 to +3. These are SCREEN
     * coordinates (DS:47B/47C read 0x80/0x40 on the screen, not in the view), so the
     * transform below converts screen to view once and then scales: at scale 1 with
     * the measured origin this reproduces the old absolute plot byte for byte. */
    const int sc = s->scale > 0 ? s->scale : 1;
    /* The cross is at the CENTRE of the field and does not move. Measured, not
     * inferred: g90-warp-table/ship.txt reads DS:47B and DS:47C as 0x80 and 0x40 — the
     * middle of the 256x128 field — in all 120 frames it captured, while the star
     * tables in the same capture are rewriting themselves every frame. So the original
     * pins the crosshair and scrolls the field under it, and the engine had it the
     * other way round: a static hash field with a travelling cross.
     *
     * s->ship_x and s->ship_y are still what the step is measured against, because the
     * centre IS the ship's screen position; they are simply no longer where the cross
     * is drawn. What a direction key does to the field in the orbit view is still
     * open and is NOT implemented here. */
    const int svx = U2_SPACE_CENTRE_X;
    const int svy = U2_SPACE_CENTRE_Y;
    for (int d = U2_SPACE_SHIP_V_FROM; d <= U2_SPACE_SHIP_V_TO; d++)
        for (int k = 0; k < sc; k++)
            u2_frame_pixel(f, s->ox + svx * sc + k, s->oy + (svy + d) * sc, U2_CGA_WHITE);
    for (int d = U2_SPACE_SHIP_H_FROM; d <= U2_SPACE_SHIP_H_TO; d++)
        for (int k = 0; k < sc; k++)
            u2_frame_pixel(f, s->ox + (svx + d) * sc, s->oy + svy * sc + k, U2_CGA_WHITE);
}
