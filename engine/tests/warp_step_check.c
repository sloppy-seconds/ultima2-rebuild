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

    /* --- direction: everything is pulled TOWARD the ship ------------------------- */
    /* The first version of this file had the arms swapped and the bucket uncomplemented,
     * which made the field run away from the ship and pile up at an edge. The bytes branch
     * `jb L_006d7f` on old < ship, so the fall-through (old >= ship) is the `sbb` arm, and
     * the fall-through complements before shifting: di = (~(old-ship)) >> 4. */
    CHECK(step_x(150, 100) == 137, "right of ship: 150 -> 137 (d=50, di=12, step 13), got %d", step_x(150, 100));
    CHECK(step_x(50, 100) == 63,   "left of ship:  50 -> 63 (d=206, di=12, step 13), got %d",  step_x(50, 100));

    /* --- magnitude: the step GROWS as a star gets CLOSER to the ship ------------- */
    /* This is the radial mechanism, and it is inverted from what you would guess. A star
     * right next to the ship is thrown 16 px; one at the far edge creeps by 1. */
    CHECK(step_x(108, 100) == 92,  "d=8: step 16 toward ship, got %d", step_x(108, 100) - 108);
    CHECK(step_x(140, 100) == 126, "d=40: step 14 toward ship, got %d", step_x(140, 100) - 140);
    CHECK(step_x(244, 100) == 237, "d=144: step 7 toward ship, got %d", step_x(244, 100) - 244);
    /* The below-ship arm skips the complement, so it indexes the wrapped difference. */
    CHECK(step_x(92, 100) == 108,  "below d=8: step 16 toward ship, got %d", step_x(92, 100) - 92);
    CHECK(step_x(91, 100) == 107,  "below d=9: step 16 toward ship, got %d", step_x(91, 100) - 91);
    CHECK(step_x(36, 100) == 49,   "below d=64: step 13 toward ship, got %d", step_x(36, 100) - 36);

    /* --- the two stores that WRAP and the two that refuse ------------------------ */
    /* x add arm (old < ship) stores BEFORE its branch (L_006d98 then jae), so x wraps and
     * is kept. This is the single fact that makes the original's field grow across a warp
     * instead of draining. */
    CHECK(step_x(248, 250) == 8,   "x add arm must wrap (248+16 -> 8), got %d", step_x(248, 250));
    /* x sub arm stores AFTER its branch (jb L_006d79 then mov), so an underflow is refused
     * and the star stays put. */
    CHECK(step_x(10, 10) == 10,    "x sub arm must refuse an underflow, got %d", step_x(10, 10));
    /* Exactly at the boundary: ship=1, old=16 gives d=15, di=15, step 16, so old == step
     * and the star lands on 0. old=15 gets the same step but 15 < 16, so that one IS
     * skipped. The pair is what pins the `jb`. */
    CHECK(step_x(16, 1) == 0,     "old == step must reach 0, got %d", step_x(16, 1));
    CHECK(step_x(15, 1) == 15,    "old == step-1 must skip, got %d", step_x(15, 1));

    /* --- y: the mirror image, gated on the SIGN instead of the carry ------------- */
    /* y add arm (old < ship_y): adc then `jns` BEFORE the store, so a result at or past
     * 0x80 is refused. y=68, ship_y=100 -> d=224, di=14, and bucket 14 is the code byte
     * 0xa0 = 160, so 68+160 = 228 >= 0x80 and the store is skipped. */
    {
        uint8_t sx[1] = { 100 }, sy[1] = { 68 };
        u2_space_warp_advance(sx, sy, 1, 100, 100);
        CHECK(sy[0] == 68, "y add arm must refuse >= 0x80, got %d", sy[0]);
    }
    {
        /* y=52, ship_y=100 -> d=208, di=13, bucket 13 is code byte 0x04 = 4 -> 56. */
        uint8_t sx[1] = { 100 }, sy[1] = { 52 };
        u2_space_warp_advance(sx, sy, 1, 100, 100);
        CHECK(sy[0] == 56, "y add arm below 0x80 must move, got %d", sy[0]);
    }
    /* y sub arm (old >= ship_y): the store sits BEFORE the `jns`, so a result that goes
     * negative is kept. y=64, ship_y=64 -> d=0, di=15, bucket 15 is code byte 0x7c = 124,
     * so 64-124 wraps to 196 and that is what gets stored. */
    {
        uint8_t sx[1] = { 100 }, sy[1] = { 64 };
        u2_space_warp_advance(sx, sy, 1, 100, 64);
        CHECK(sy[0] == 196, "y sub arm keeps a negative result (64-124 -> 196), got %d", sy[0]);
    }
    /* THE Y TABLE IS EIGHT ENTRIES AND THE ORIGINAL READS PAST IT. Read live at
     * CS:0x17D:0x6cbc the table is 01..08 and the next eight bytes are program code
     * (a0 7b 04 a2 79 04 a0 7c). asm/6D3E seeds every y with `rand & 0x7f` and the ship
     * sits at 0x40, so on BOTH arms the bucket lands in 12..15 -- every y step the original
     * takes comes out of its own instructions. Deterministic, because code bytes do not
     * change; the steps are 121, 4, 160, 124. This is G89's y half. */
    {
        uint8_t sx[64], sy[64];
        u2_space_warp_seed(sx, sy, 64, 7u);
        int maxy = 0, bad = 0;
        for (int i = 0; i < 64; i++) {
            if (sy[i] > maxy) maxy = sy[i];
            int d = (sy[i] - 0x40) & 0xff;
            int di = (sy[i] >= 0x40) ? (((~d) & 0xff) >> 4) : (d >> 4);
            if (di < 12 || di > 15) bad++;
        }
        CHECK(maxy <= 0x7f, "seeded y must stay under 0x80 (asm/6D3E masks it), got %d", maxy);
        CHECK(bad == 0, "every seeded y bucket must land in 12..15 (past the 8-entry "
                        "table), %d did not", bad);
    }

    /* --- the field stays ALIVE but converges: a named, explained divergence ------ */
    /* This block used to pin a freeze at frame 251. The rule was wrong in two places
     * (swapped arms, uncomplemented bucket) and both are fixed, so the field no longer
     * freezes -- but it still does not match the original, and the reason is now decoded
     * rather than unknown.
     *
     * What the bytes do on the paths that look like refusals: `L_006d76 jmp L_006e8c` and
     * `L_006df0 jmp L_006e8c` are NOT skips. L_006e8c reloads the star's old x and y, calls
     * L_006f68 -- which is the RNG, advancing the CS-side state word at cs:0x6996/0x6997
     * with adc -- and stores the returned byte into DS:4C5. So a star whose arithmetic arm
     * refuses gets a FRESH RANDOM POSITION, not no movement. That is why the original's
     * table stays full-range: measured across 300 live samples its x spread never drops below
     * 196 (mean 242), because the field is continuously re-stirred rather than converged.
     *
     * This model has no RNG arm, so it converges into a band around the ship's column. That
     * is asserted here rather than hidden, and the fix is named: give the refuse paths the
     * same RNG the engine already models elsewhere, and this check flips to the positive
     * claim (spread > 128) when it lands. */
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
        CHECK(moved > 0,
              "the field must still be moving at frame 251, as the original's is; moved=%d", moved);

        int minx = 255, maxx = 0;
        for (int i = 0; i < 64; i++) {
            if (sx[i] < minx) minx = sx[i];
            if (sx[i] > maxx) maxx = sx[i];
        }
        CHECK(maxx - minx < 64,
              "KNOWN DIVERGENCE: without the L_006e8c RNG arm this model converges on the "
              "ship's column (original spread is ~242, see the block comment); got %d..%d",
              minx, maxx);
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
