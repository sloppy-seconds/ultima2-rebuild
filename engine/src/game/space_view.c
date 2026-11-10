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
 *   star field    NOT DECODED -- an ENHANCEMENT, not fidelity. The original's stars
 *                 are drawn by a routine this project has not identified, and the
 *                 radiating variant during hyperwarp has not been decoded either.
 *                 These positions are a deterministic hash of a seed, chosen because
 *                 it looks like the capture frames, not because any byte says so.
 *                 Do not cite them as evidence about the original.
 */
#include "game/space_view.h"

#include "core/ctx.h"
#include "core/state.h"     /* u2_bcd_dec1, the item and world bytes */
#include "game/overworld.h"  /* U2_ITEM_TRI_LITHIUM */
#include "game/space.h"      /* u2_space_worlds, U2_WORLD_COUNT */

/* The star field occupies the map window, which is everything above the text rows.
 * frame.h: the screen is 320x200 and the overworld composites its text into the
 * bottom rows, so the window is y 0..U2_SPACE_WINDOW_H-1. */
#define U2_SPACE_WINDOW_W U2_SCREEN_W
#define U2_SPACE_WINDOW_H 160
#define U2_SPACE_STARS    140

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
    out->ship_x = st->space_ship_x;
    out->ship_y = st->space_ship_y;
    out->star_seed = ((uint32_t)st->world << 16) ^
                     ((uint32_t)out->xeno << 8) ^ (uint32_t)out->yako;
}

void u2_space_scene_draw(u2_frame *f, const u2_space_scene *s)
{
    u2_frame_clear(f, U2_CGA_BLACK);

    /* Stars -- see the file header: enhancement, not fidelity. */
    uint32_t h = star_hash(s->star_seed ? s->star_seed : 1u);
    for (int i = 0; i < U2_SPACE_STARS; i++) {
        h = star_hash(h);
        const int x = (int)(h % U2_SPACE_WINDOW_W);
        const int y = (int)((h >> 16) % U2_SPACE_WINDOW_H);
        u2_frame_pixel(f, x, y, U2_CGA_WHITE);
    }

    /* The ship: the plus asm/6CC4 erases, drawn back. Vertical stroke x fixed, y
     * from -3 to +2; horizontal stroke y fixed, x from -3 to +3. */
    for (int d = U2_SPACE_SHIP_V_FROM; d <= U2_SPACE_SHIP_V_TO; d++)
        u2_frame_pixel(f, s->ship_x, s->ship_y + d, U2_CGA_WHITE);
    for (int d = U2_SPACE_SHIP_H_FROM; d <= U2_SPACE_SHIP_H_TO; d++)
        u2_frame_pixel(f, s->ship_x + d, s->ship_y, U2_CGA_WHITE);
}
