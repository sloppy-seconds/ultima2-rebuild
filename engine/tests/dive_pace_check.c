/*
 * tests/dive_pace_check.c — the rocket's descent cadence: the geometry is measured, the
 * clock is not, and this file keeps those two facts from being confused again.
 *
 * WHY THIS EXISTS. Owner recollection, 2026-11-15: the landing sequence looks slower in the
 * original than in the engine (docs/needs-gameplay.md G86). Chasing it found that the engine
 * had three definitions of the pace figure in two names, one of them duplicated in the same
 * file with a comment asserting the two differed, and that the seam it was attributed to
 * (asm/52CA) is the DOS FCB call wrapper, not a delay — the original has no delay primitive
 * at all. So the pace is not a program feature the engine can copy; it is the cost of one
 * whole sector render per march step, on hardware whose speed the engine cannot inherit.
 *
 * That splits the assertion in two, and they are NOT the same grade:
 *
 *   GEOMETRY — observed. captures/spice86/dive-sweep-rate/dive1.csv is 11,885 samples of
 *   DS:00/DS:01 across a live dive. The distinct positions are y += 2 every step, x += 1
 *   every fourth step, starting at (0,0), wrapping at 64. That is a fact about the original
 *   and the engine must reproduce it exactly. Cases 1-3.
 *
 *   CLOCK — arithmetic, and labelled as such. 492,074 emulated cycles per step divided by a
 *   nominal 4.77 MHz gives 103 ms. The division is the weak half: Spice86 counts CPU cycles
 *   and does not model 8088 bus wait states, so real hardware spent more wall-clock time per
 *   cycle than the nominal clock implies, and a figure derived this way is a LOWER BOUND on
 *   what a player saw. That is the whole of why the owner's memory can be right while every
 *   number here is also right. Cases 4-6 pin what the figure is rather than defending it.
 *
 * Build and run by tests/run_cp7.sh.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/state.h"
#include "game/space.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* The observed sequence, verbatim from dive-sweep-rate README § 1, first 14 distinct
 * positions. Transcribed, not derived — if the engine's step disagrees with this it has
 * drifted from the capture, which is the whole point of the file. */
static const struct { int x, y; } OBSERVED[] = {
    {0,0}, {0,2}, {0,4}, {0,6}, {1,8}, {1,10}, {1,12}, {1,14},
    {2,16}, {2,18}, {2,20}, {2,22}, {3,24}, {3,26},
};

int main(void)
{
    /* ---- 1. the first 14 steps match the capture, position for position --------- */
    uint8_t x = 0, y = 0;
    for (unsigned i = 0; i < sizeof OBSERVED / sizeof OBSERVED[0]; i++) {
        CHECK(x == OBSERVED[i].x && y == OBSERVED[i].y,
              "step %u: engine is at (%u,%u), the capture saw (%d,%d)",
              i, x, y, OBSERVED[i].x, OBSERVED[i].y);
        u2_space_descent_step(&x, &y);
    }

    /* ---- 2. one north-to-south sweep: y wraps at 64 with +2 per step, so 32 ------ */
    x = 0; y = 0;
    int steps = 0;
    do { u2_space_descent_step(&x, &y); steps++; } while (!(y == 0) && steps < 1000);
    CHECK(steps == 32, "one north-to-south sweep is 32 steps, took %d", steps);

    /* ---- 3. x advances 8 cells per sweep, so the FULL cycle is 256 steps -------- */
    /* This is the number the aiming window is quoted against: 256 steps at 103 ms is
     * 26.4 s, which is what docs/needs-gameplay.md G63 means by "about every 26 s".
     * Getting this wrong by a factor of 8 is how the first draft of this file failed —
     * it looped back to (0,0) and called 32 the answer. */
    x = 0; y = 0;
    for (int i = 0; i < 32; i++) u2_space_descent_step(&x, &y);
    CHECK(y == 0 && x == 8, "after one sweep y has wrapped and x has advanced 8 "
          "(got (%u,%u))", x, y);

    x = 0; y = 0;
    steps = 0;
    do { u2_space_descent_step(&x, &y); steps++; } while (!(x == 0 && y == 0) && steps < 5000);
    CHECK(steps == 256, "the full cycle back to the origin is 256 steps, took %d", steps);
    CHECK(256 * U2_DIVE_STEP_MS / 1000 == 26,
          "and that cycle is the ~26 s aiming window G63 quotes (got %d s)",
          256 * U2_DIVE_STEP_MS / 1000);

    /* ---- 4. the two pace constants are ONE borrowed figure, not two ------------- */
    CHECK(U2_DIVE_STEP_MS == U2_PLANE_STEP_MS,
          "the dive and plane pace figures are the same borrowed number (%d vs %d) — if "
          "they are now different, one was measured and this note is stale",
          U2_DIVE_STEP_MS, U2_PLANE_STEP_MS);

    /* ---- 5. the figure is a lower bound, and the file says so ------------------- */
    /* 492,074 cycles / 4.77 MHz = 103.2 ms. Recomputed here rather than remembered, so a
     * constant that drifts away from its own derivation is caught. */
    const long cycles_per_step = 492074;
    const long nominal_hz = 4770000;
    const long implied_ms = cycles_per_step * 1000 / nominal_hz;
    CHECK(implied_ms == U2_DIVE_STEP_MS,
          "U2_DIVE_STEP_MS is %d but 492074 cycles at 4.77 MHz is %ld ms — the constant "
          "no longer equals its stated derivation", U2_DIVE_STEP_MS, implied_ms);

    /* ---- 6. what the sweep costs at that figure --------------------------------- */
    const int sweep_ms = 32 * U2_DIVE_STEP_MS;
    CHECK(sweep_ms == 3296, "one sweep is %d ms at the stated figure", sweep_ms);
    printf("  note  sweep = %d ms at the nominal-clock figure; this is a LOWER BOUND, "
           "Spice86 counts cycles and models no bus wait states (G86)\n", sweep_ms);

    printf("dive_pace_check: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
