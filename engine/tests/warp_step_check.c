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

    /* --- the two skips ----------------------------------------------------------- */
    /* Add arm overflows past 255: `jae` is not taken and the star is left alone. */
    CHECK(step_x(250, 100) == 250, "overflow must skip, got %d", step_x(250, 100));
    /* Subtract arm underflows: old < step, so `cmc; jb` falls to the skip. */
    CHECK(step_x(1, 200) == 1,     "underflow must skip, got %d", step_x(1, 200));
    /* Exactly on the boundary: ship=200, old=12 gives di = (200-12-1)>>4 = 11 and step 12,
     * so old == step and the star lands on 0 rather than being skipped. old=11 gives the
     * same step 12 but 11 < 12, so that one IS skipped. The pair is what pins the `jb`. */
    CHECK(step_x(12, 200) == 0,     "old == step must reach 0, got %d", step_x(12, 200));
    CHECK(step_x(11, 200) == 11,    "old == step-1 must skip, got %d", step_x(11, 200));

    /* --- the y table is only eight deep ------------------------------------------ */
    /* y distance 8 -> di 0 -> step 1, settled by the live read. */
    {
        uint8_t sx[1] = { 100 }, sy[1] = { 108 };
        u2_space_warp_advance(sx, sy, 1, 100, 100);
        CHECK(sy[0] == 109, "y d=8: expected 108 -> 109, got %d", sy[0]);
    }
    /* y distance 190 -> di = 11, past the end of the table. Left alone, NOT stepped by a
     * byte lifted out of the program's own instructions. This is the half of G89 that is
     * still open, and the test pins the refusal so a later session cannot quietly guess.
     * (The first version of this case used a distance of 200 measured the wrong way, which
     * wrapped to bucket 3 and so asserted nothing at all; the selftest caught it.) */
    {
        uint8_t sx[1] = { 100 }, sy[1] = { 200 };
        u2_space_warp_advance(sx, sy, 1, 100, 10);
        CHECK(sy[0] == 200, "y bucket >= 8 must be left alone, got %d", sy[0]);
    }

    /* --- the field stays inside a byte ------------------------------------------- */
    {
        uint8_t sx[64], sy[64];
        u2_space_warp_seed(sx, sy, 64, 12345u);
        for (int f = 0; f < 509; f++)
            u2_space_warp_advance(sx, sy, 64, 128, 64);
        int spread = 0;
        for (int i = 1; i < 64; i++) {
            int d = (int)sx[i] - (int)sx[i - 1];
            spread += d < 0 ? -d : d;
        }
        CHECK(spread > 0, "509 frames must not collapse the field to one point");
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
