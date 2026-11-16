/* warp_step_check.c — the warp's per-star motion, asm/6B3D's radial step.
 *
 * The claim under test is the one G89 turned up: a star steps AWAY from the ship, and the
 * step GROWS with its distance from the ship. That is what makes far stars outrun near
 * ones, and it is the difference between rays and a uniform drift. The engine used to
 * fake this by growing a radius along a fixed ray, which is why its warp drew solid
 * streaks where the original draws sparse dots.
 *
 * Every expectation here is read off asm/6B3D_space_field_draw.asm:
 *   L_006d50  sbb 0x47b,%al ; cmc ; jb L_006d7f   -> old >= ship takes the ADD arm
 *   L_006d7f  four clc;rcr                        -> di = (old - ship) >> 4
 *   L_006d93  adc %cs:0x6cac(%di),%al ; jae       -> new = old + di + 1, skip on overflow
 *   L_006d57  xor $0xff ; four clc;rcr            -> di = (ship - old - 1) >> 4
 *   L_006d6e  sbb %cs:0x6cac(%di),%al ; cmc ; jb  -> new = old - di - 1, skip if old < step
 *   cs:0x6cac live = 01 02 03 ... 10              -> table[i] == i + 1
 *   cs:0x6cbc live = 01 02 ... 08, then code      -> y table is 8 deep; beyond that, unknown
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "game/space_view.h"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL: "); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* One star, one axis, one step. Returns the star's new x with y parked so it cannot move. */
static int step_x(int old, int ship)
{
    uint8_t sx[1] = { (uint8_t)old }, sy[1] = { (uint8_t)ship };
    u2_space_warp_advance(sx, sy, 1, ship, ship);
    return sx[0];
}

int main(void)
{
    printf("warp_step: asm/6B3D's radial step\n");

    /* --- direction: away from the ship, on both arms ---------------------------- */
    /* d = 50 -> di = 3 -> step 4 on both arms (the left arm measures ship-old-1 = 49,
     * which is still bucket 3). */
    CHECK(step_x(150, 100) == 154, "right of ship: expected 150 -> 154, got %d", step_x(150, 100));
    CHECK(step_x(50, 100) == 46,   "left of ship:  expected 50 -> 46, got %d",  step_x(50, 100));

    /* --- magnitude: the step grows with distance -------------------------------- */
    /* d = 8  -> di = 0  -> step 1 */
    CHECK(step_x(108, 100) == 109, "d=8: expected step 1, got %d", step_x(108, 100) - 108);
    /* d = 40 -> di = 2  -> step 3 */
    CHECK(step_x(140, 100) == 143, "d=40: expected step 3, got %d", step_x(140, 100) - 140);
    /* d = 144 -> di = 9 -> step 10 */
    CHECK(step_x(244, 100) == 254, "d=144: expected step 10, got %d", step_x(244, 100) - 244);
    /* The left arm measures ship - old - 1, so d = 8 there is step 1 too, and d = 9 is
     * still step 1: the arms are deliberately asymmetric because the original complements
     * a wrapped difference instead of taking an absolute value. */
    CHECK(step_x(92, 100) == 91,   "left d=8: expected step 1, got %d", 92 - step_x(92, 100));
    CHECK(step_x(91, 100) == 90,   "left d=9: expected step 1, got %d", 91 - step_x(91, 100));
    CHECK(step_x(36, 100) == 32,   "left d=64: expected step 4, got %d", 36 - step_x(36, 100));

    /* --- the two stores that WRAP and the two that refuse ------------------------ */
    /* x add arm stores BEFORE its branch (L_006d98 then jae), so x wraps and is kept.
     * This is the single fact that makes the original's field grow across a warp instead
     * of draining: a star at 250 with step 10 lands at 4, not stuck at 250. */
    CHECK(step_x(250, 100) == 4,   "x add arm must wrap (250+10 -> 4), got %d", step_x(250, 100));
    /* x subtract arm stores AFTER its branch (jb L_006d79 then mov), so an underflow is
     * refused and the star stays put. */
    CHECK(step_x(1, 200) == 1,     "x sub arm must refuse an underflow, got %d", step_x(1, 200));
    /* Exactly on the boundary: ship=200, old=12 gives di = (200-12-1)>>4 = 11 and step 12,
     * so old == step and the star lands on 0 rather than being skipped. old=11 gives the
     * same step 12 but 11 < 12, so that one IS skipped. The pair is what pins the `jb`. */
    CHECK(step_x(12, 200) == 0,     "old == step must reach 0, got %d", step_x(12, 200));
    CHECK(step_x(11, 200) == 11,    "old == step-1 must skip, got %d", step_x(11, 200));

    /* --- y: the mirror image, gated on the SIGN instead of the carry ------------- */
    /* y add arm: adc then `jns` before the store, so a result at or past 0x80 is refused. */
    {
        uint8_t sx[1] = { 100 }, sy[1] = { 127 };
        u2_space_warp_advance(sx, sy, 1, 100, 100);
        /* di = (127-100)>>4 = 1, step 2, so 127+2 = 129 which is >= 0x80 -> refused. */
        CHECK(sy[0] == 127, "y add arm must refuse >= 0x80, got %d", sy[0]);
    }
    {
        uint8_t sx[1] = { 100 }, sy[1] = { 125 };
        u2_space_warp_advance(sx, sy, 1, 100, 100);
        /* di = 1, step 2 -> 127, still under 0x80, so this one moves. */
        CHECK(sy[0] == 127, "y add arm below 0x80 must move, got %d", sy[0]);
    }
    /* y sub arm: the store sits before the `jns`, so it wraps and is kept. */
    {
        uint8_t sx[1] = { 100 }, sy[1] = { 1 };
        u2_space_warp_advance(sx, sy, 1, 100, 100);
        /* di = (100-1-1)>>4 = 6, step 7, so 1-7 wraps to 250 */
        CHECK(sy[0] == 250, "y sub arm must wrap (1-7 -> 250), got %d", sy[0]);
    }
    /* The y table is eight deep and that is enough, because asm/6D3E seeds every y with
     * `rand & 0x7f` and the ship sits at 0x40, so no y distance reaches bucket 8. A field
     * seeded the way the original seeds it therefore never runs off the table. */
    {
        uint8_t sx[64], sy[64];
        u2_space_warp_seed(sx, sy, 64, 7u);
        int maxy = 0;
        for (int i = 0; i < 64; i++) if (sy[i] > maxy) maxy = sy[i];
        CHECK(maxy <= 0x7f, "seeded y must stay under 0x80 (asm/6D3E masks it), got %d", maxy);
    }

    /* --- a CHARACTERIZATION test, pinning a known divergence --------------------- */
    /* This one asserts something the model gets WRONG, on purpose.
     *
     * Measured on the original (captures/spice86/g90-warp-table/): the live star table
     * stays full-range across ~300 of the warp's ~509 frames and the screen's white-pixel
     * count climbs from ~77 to ~6200 and back. This model does not: after 250 frames every
     * star has fallen into the sub-arm's refuse case and the field is frozen. That is why
     * the rule is implemented and tested but NOT wired into the renderer.
     *
     * Pinning the freeze rather than leaving it unasserted is deliberate: it goes red the
     * moment someone fixes the model, which forces them to look at G90 and update this
     * comment instead of shipping a half-fix that looks finished. If this fails, the
     * divergence is closed — delete this block and wire the field in space_paint_mode(). */
    {
        uint8_t sx[64], sy[64];
        u2_space_warp_seed(sx, sy, 64, 12345u);
        for (int f = 0; f < 250; f++)
            u2_space_warp_advance(sx, sy, 64, 128, 64);
        uint8_t px[64], py[64];
        memcpy(px, sx, 64); memcpy(py, sy, 64);
        u2_space_warp_advance(sx, sy, 64, 128, 64);
        int moved = 0;
        for (int i = 0; i < 64; i++) if (sx[i] != px[i] || sy[i] != py[i]) moved++;
        CHECK(moved == 0,
              "KNOWN DIVERGENCE (G90): the model freezes by frame 251 but the original "
              "does not; if this now moves, wire the field in and delete this check (moved=%d)",
              moved);
    }

    /* --- seeding is deterministic ------------------------------------------------- */
    {
        uint8_t a[64], b[64], c[64];
        u2_space_warp_seed(a, a + 32, 32, 999u);
        u2_space_warp_seed(b, b + 32, 32, 999u);
        u2_space_warp_seed(c, c + 32, 32, 1000u);
        CHECK(memcmp(a, b, 64) == 0, "same seed must give the same field");
        CHECK(memcmp(a, c, 64) != 0, "different seed must give a different field");
    }

    printf("  ok  %d checks.\n", checks);
    printf("warp_step: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
