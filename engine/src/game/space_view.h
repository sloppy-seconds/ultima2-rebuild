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
/* 77 white px in frames 226/227/228 minus the 13-px ship cross = ~64 stars. Measured.
 * Also the width of the original's DS:4CD / DS:50D tables, which is corroborating rather
 * than assumed: the two 64-entry tables are 0x50D - 0x4CD = 64 apart. */
#define U2_SPACE_ORBIT_STARS 64

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
    /* Where and how big to draw. The field's own geometry is a measured 256x128
     * rectangle inset 16 px on the original's 320x200 screen; these say where that
     * rectangle lands on THIS frame and how many physical pixels one of its pixels
     * is worth. scale=1, ox=16, oy=16 is the original, and u2_space_scene_from always
     * sets exactly that; the enhanced caller overrides before drawing. */
    int scale;
    int ox;
    int oy;
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
    /* Which star field the warp draws, and the table it draws from when the answer is
     * the table.
     *
     * These are the engine's version of the original's DS:4CD / DS:50D tables: 64
     * entries, advanced once per redraw by u2_space_warp_advance() and written back.
     * asm/6B3D reads a star's OLD position out of the table, derives a new one from
     * its distance from the ship, and stores it, so the field is stateful and cannot
     * be regenerated from a seed and a frame index. In U2_WARP_FROM_HASH the tables
     * are not consulted at all.
     *
     * The mode used to be carried by the pointers: NULL meant "use the hash field".
     * That was an in-band signal, and it bit — when these fields were added the scene
     * builder did not initialise them, so every caller on the stack handed the draw
     * wild pointers, the table branch ran, and the scripted launch in the space gate
     * stopped reaching orbit with no hint of a cause. A named mode means a forgotten
     * initialisation is a wrong picture that says which mode it used, not a dereference
     * of garbage. Set the table with u2_space_scene_use_table; do not assign the
     * pointers.
     *
     * Nothing in the shipped engine sets the table mode. The step rule the tables exist
     * for is decoded and unit-tested, but running it to completion empties the middle of
     * the field, so the renderer stays on the hash field until the missing piece is
     * found — see the note in commands.c space_paint_mode() and G89 in
     * docs/needs-gameplay.md. */
    enum { U2_WARP_FROM_HASH = 0, U2_WARP_FROM_TABLE } warp_source;
    const uint8_t *warp_x;
    const uint8_t *warp_y;
    /* Star field seed. NOT decoded from the original -- see u2_space_scene_from(). */
    uint32_t star_seed;
} u2_space_scene;

/* asm/6B3D's per-star motion, applied in place to `n` stars.
 *
 * Both axes run the same rule, and it is radial: a star steps AWAY from the ship by an
 * amount that GROWS with its distance from the ship, which is what makes far stars
 * outrun near ones and turns a drift into rays.
 *
 *   d = (uint8_t)(old - ship);  step = (d >> 4) + 1   (cs:0x6cac is 1..16)
 *   old >= ship : new = old + step, skipped if it overflows past 255
 *   old <  ship : new = old - step, skipped if old < step
 *
 * The two arms are `L_006d7f` (`adc`, add) and `L_006d57` (`xor $0xff` then `sbb`,
 * subtract); the skips are the `jae` after the add and the `cmc; jb` after the
 * subtract. The subtract arm's bucket is `(ship - old - 1) >> 4`, because the original
 * complements the wrapped difference rather than measuring it the other way.
 *
 * y is the same shape against `cs:0x6cbc`, which reads live as 1..8 and then code. Only
 * buckets 0..7 are settled, so a star whose y distance from the ship puts it in bucket 8
 * or higher is left where it is rather than stepped by a value read out of the
 * program's own instructions. That is a known divergence, not a guess: see G89.
 *
 * Returns the number of stars that moved. */
int u2_space_warp_advance(uint8_t *sx, uint8_t *sy, int n, int ship_x, int ship_y);

/* Fill a warp field with `n` starting positions from `seed`. The original's initialiser is
 * asm/6D3E: 64 entries, x from one random byte, y from a second masked `and $0x7f`. The
 * distribution here is still the engine's own hash rather than the game's RNG — the values
 * are not recoverable — but the SHAPE is the original's, and the 0x7f mask in particular is
 * load-bearing: it is what keeps every y bucket inside the eight-entry y table. */
void u2_space_warp_seed(uint8_t *sx, uint8_t *sy, int n, uint32_t seed);

/* One frame of the warp's sweep counter, as measured on the original (G90, closed
 * 2026-11-16, captures/spice86/g90-warp-table/full-sweep.txt). asm/6B3D gates the erase of
 * each star's old pixel on this byte being non-zero:
 *
 *       mov 0x4c7,%al ; or %al,%al ; je L_006e14      ; zero -> do not erase
 *
 * so the frames where `value` is 0 are exactly the frames that ACCUMULATE, and that window
 * is what draws the streaks. Measured shape over ~600 frames:
 *
 *   descend   128 -> 1   ~192 frames
 *   hold      0          128 frames     <- accumulate: erase skipped every star, every frame
 *   hold      1          128 frames
 *   ascend    2,4,..,126  63 frames     (step +2)
 *   hold      128        ~81 frames
 *
 * The leg lengths and the zero-window length are measured. The descend rate is fitted: the
 * leg is ~192 frames for 127 decrements, which is not an integer, so `value` is interpolated
 * rather than stepped and that approximation is labelled rather than hidden.
 *
 * Out of range it returns the final hold, so a caller that walks past the end settles. */
#define U2_SWEEP_DESCEND   192      /* 128 -> 1 */
#define U2_SWEEP_ZERO      128      /* held at 0: the accumulate window */
#define U2_SWEEP_ONE       128      /* held at 1 */
#define U2_SWEEP_ASCEND     63      /* 2,4,...,126 */
#define U2_SWEEP_TOP_HOLD   81      /* held at 128 */

typedef struct { int value; int accumulate; } u2_warp_sweep;
u2_warp_sweep u2_space_warp_sweep(int frame);
int u2_space_warp_sweep_frames(void);

/* Build the scene from live state. Pure: reads, allocates nothing, draws nothing.
 * It always returns U2_WARP_FROM_HASH and warping=0, so a gate can build a scene from
 * state alone; a caller that is animating a warp overrides those, and a caller that has
 * real tables says so with u2_space_scene_use_table rather than by assigning pointers. */
void u2_space_scene_from(const u2_ctx *ctx, u2_space_scene *out);

/* Draw the warp field from the caller's own star tables instead of the hash field.
 * `n` is not a parameter because the original's tables are 64 entries and the renderer
 * walks exactly that many; passing a shorter array is a bug this cannot detect, so the
 * length is stated in the type's contract instead of left to be remembered.
 *
 * The tables are not copied and must outlive the scene. */
void u2_space_scene_use_table(u2_space_scene *s, const uint8_t *x, const uint8_t *y);

/* Paint the scene into the map-window region of the frame.
 *
 * A caller MUST composite the text layer back over the frame after this and before it
 * presents. The field clear takes the whole frame, not the view rectangle, so a scene
 * drawn and presented as-is loses the `CMD:` prompt and the FUEL/XENO/YAKO/ZABO readout
 * — which is a real regression this project has already shipped once (owner, 2026-11-10:
 * "we make it to space but the cmd> etc disappears"). The requirement lives here rather
 * than only in the one caller's comment because the API invites the two-call sequence
 * and the second call is not visible from the signature. Folding it in for good is the
 * space screen owning its own paint, which is #17. */
void u2_space_scene_draw(u2_frame *f, const u2_space_scene *s);

#endif /* U2_SPACE_VIEW_H */
