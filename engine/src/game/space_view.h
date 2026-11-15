/* space_view.h — what the in-space screen is made of, and how it is painted.
 *
 * Split out of game/space.c on purpose. space.c is the numeric model and its test
 * harness (tests/space_check.c) compiles it alone, with no frame and no SDL; a draw
 * function living there would drag core/frame.h into a harness that has every right
 * not to pay for it. Same reason kill_drop.c is its own translation unit.
 *
 * The scene is a plain struct built from state, so a gate can assert the readout and
 * the ship position WITHOUT rendering anything, and the renderer stays a thin
 * projection of it. That ordering is the whole point: the numbers are decided here and
 * the pixels follow, never the reverse.
 */
#ifndef U2_SPACE_VIEW_H
#define U2_SPACE_VIEW_H

#include <stdint.h>

#include "core/frame.h"

typedef struct u2_ctx u2_ctx;

/* The space field is plotted through asm/4B76_pixel_plot, whose origin pair
 * DS:4BF/DS:4C1 is 16/16, so the drawable view is a 256x128 rectangle inset 16 pixels
 * from the top-left of the 320x200 screen. MEASURED, not inferred: the five warp frames
 * in captures/spice86/g39-rocket-flight/frames/ (014, 049, 050, 096, 097) have white
 * pixels spanning exactly x 16..271 and y 16..143 -- 256 columns and 128 rows from that
 * origin, and nothing outside it. asm/4B76 can only ever write colour 3 (it ORs both
 * plane bits), so the field is white on black by construction, not by taste. */
#define U2_SPACE_VIEW_X 16
#define U2_SPACE_VIEW_Y 16
#define U2_SPACE_VIEW_COLS 256
#define U2_SPACE_VIEW_ROWS 128

/* The radiating field is centred on the view centre. Measured: the centroid of the white
 * pixels in frames 014/049/096/097 is (127,63), (127,63), (124,64), (125,62) in view
 * coordinates -- i.e. (128,64) = W/2, H/2, within measurement noise. */
#define U2_SPACE_CENTRE_X (U2_SPACE_VIEW_COLS / 2)
#define U2_SPACE_CENTRE_Y (U2_SPACE_VIEW_ROWS / 2)

/* The ship sprite, measured rather than drawn to taste. asm/6CC4_space_ship_erase
 * calls asm/4BAA_pixel_erase thirteen times in two perpendicular sweeps centred on
 * the ship's screen position DS:47B (x) / DS:47C (y):
 *
 *   vertical   x fixed at DS:47B, y from DS:47C-3 to DS:47C+2   (6 pixels)
 *   horizontal y fixed at DS:47C, x from DS:47B-3 to DS:47B+3   (7 pixels)
 *
 * i.e. a plus/cross, which is what the capture frames show. The draw is the mirror of
 * the erase, so these offsets ARE the sprite and not an approximation of it. */
#define U2_SPACE_SHIP_V_FROM (-3)
#define U2_SPACE_SHIP_V_TO   (+2)
#define U2_SPACE_SHIP_H_FROM (-3)
#define U2_SPACE_SHIP_H_TO   (+3)

/* Where the ship sits when space is entered. Measured in
 * captures/spice86/w6-orbit-dive: DS:47B/DS:47C read 0x80 / 0x40 in orbit, and did
 * not change on the dive. */
#define U2_SPACE_SHIP_X0 0x80
#define U2_SPACE_SHIP_Y0 0x40

/* The right-hand readout, in the order the original prints it. asm/6999_space_status
 * writes the three coordinates into DS:4CA/4CB/4CC from the world table, and the fuel
 * it labels is DS:E5 (TRI-LITHIUM). */
typedef struct u2_space_scene {
    uint8_t fuel;
    uint8_t xeno;
    uint8_t yako;
    uint8_t zabo;
    int ship_x;
    int ship_y;
    /* Non-zero while the hyperwarp animation is playing. The warp field and the orbit
     * field are visibly different screens in the capture -- the orbit field is ~64
     * scattered points, the warp field is 1,900..6,000 lit pixels arranged radially --
     * so they cannot be the same drawing. */
    int warping;
    /* Outward-motion counter for the warp field, 0 when static.
     *
     * The original's stars live in two 64-entry tables (DS:4CD/DS:50D) that asm/6B3D
     * recomputes every call, so each frame they sit further out along their ray and the
     * frame after that further still. This is the engine's stand-in for those tables:
     * the field is generated from a hash rather than stored, so the frame index is what
     * makes a star's radius grow. See asm/6B3D_space_field_draw's header.
     *
     * It only produces anything visible when `keep_frame` is also set — a cleared frame
     * with moved stars is a shimmer, not a streak. */
    int warp_frame;
    /* Non-zero to draw INTO the frame that is already there instead of clearing first.
     *
     * This is the trail. asm/6B3D's mode byte decides it: `mov 0x4c7,%al ; or %al,%al ;
     * je L_006e14` skips the `call L_004daa` that would clear the star's old position,
     * so in mode 0 the old pixel stays lit while the moved star plots a new one further
     * out, and nothing in asm/6DFD's warp path clears the screen between redraws. The
     * accumulated result is a line radiating from the ship, which is what the warp
     * actually looks like. Mode 1 erases its own old pixel per star, so it shows one
     * clean dot and no trail — the engine reproduces that by clearing, which is the same
     * picture arrived at differently, and the difference is recorded rather than hidden. */
    int keep_frame;
    /* Star field seed. NOT decoded from the original -- see u2_space_scene_from(). */
    uint32_t star_seed;
} u2_space_scene;

/* Build the scene from live state. Pure: reads, allocates nothing, draws nothing. */
void u2_space_scene_from(const u2_ctx *ctx, u2_space_scene *out);

/* Paint the scene into the map-window region of the frame. */
void u2_space_scene_draw(u2_frame *f, const u2_space_scene *s);

#endif /* U2_SPACE_VIEW_H */
