/*
 * helm_map_check.c — the magical helm's VIEW must draw the sector, not just say it did.
 *
 * Why a pixel assertion and not a message assertion: the engine spent the helm and
 * printed `VIEW / WITH MAGICAL HELM!` for months while showing nothing, and every
 * message-level test stayed green. The standing rule is that a gate asserts the visible
 * outcome, and the visible outcome here is a picture. captures/spice86/vhelm-sector-map-live
 * is the geometry and the density ladder this checks against.
 *
 * Controls, both run in CI (see the two refusal cases): with no helm, and with the site
 * digit at 4 or above, the frame's map band must stay exactly as it was. A draw that
 * never fired and a draw that drew black are told apart by the land half being lit.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/frame.h"
#include "platform/platform.h"
#include "core/state.h"
#include "core/vram.h"
#include "core/world.h"
#include "game/commands.h"
#include "game/overworld.h"

#include "u2_scenario.h"   /* the arming: scripted input, null clock, vram/state/frame init */

static int checks, fails;
#define CHECK(cond, ...) do {                                        \
        ++checks;                                                    \
        if (cond) { printf("  ok    "); }                            \
        else { ++fails; printf("  FAIL  "); }                        \
        printf(__VA_ARGS__); printf("\n");                           \
    } while (0)

/* One key, then the input is finished: the map waits for a keypress, so a check that
 * never supplies one would hang rather than fail. The script and the null clock come
 * from u2_scenario.h with everything else the arming needs. */

/* A sector that is land in its top half and water in its bottom half, so "the map was
 * drawn" has an unambiguous shape: lit pixels above the midline, none below it. */
static void arm(u2_ctx *ctx, u2_overworld *ow, uint8_t helm, uint8_t site)
{
    u2_sc_arm(ctx, ow, " ");
    for (int y = 0; y < U2_SECTOR_EDGE; y++)
        for (int x = 0; x < U2_SECTOR_EDGE; x++)
            ow->world.map[y * U2_SECTOR_EDGE + x] = (y < 32) ? 0x0C : 0x00;
    ow->world.map_loaded = 1;
    ctx->state.items_held[5] = helm;      /* DS:DB, the HELM count */
    ctx->state.sector_second = site;      /* DS:4A, the site-kind digit */
    ctx->state.sector_x = 4; ctx->state.sector_y = 4;
}

/* Count lit pixels in the map band, split at the vertical middle of the drawn picture.
 * Geometry from the capture: 64x64 cells at 4x2 logical pixels from (17,16). */
static void count_band(u2_frame *f, int *upper, int *lower)
{
    const int oy = 16, mid = oy + 32 * 2;
    *upper = *lower = 0;
    for (int y = oy; y < oy + 64 * 2; y++)
        for (int x = 17; x < 17 + 64 * 4; x++)
            if (f->px[y][x] != 0) {
                if (y < mid) (*upper)++; else (*lower)++;
            }
}

int main(void)
{
    u2_ctx ctx;
    u2_overworld ow;

    /* --- the view, with a helm and a town ---------------------------------- */
    arm(&ctx, &ow, 1, 2);
    int before_up, before_down;
    count_band(&ow.frame, &before_up, &before_down);
    u2_cmd_dispatch(&ctx, &ow, 'V');
    int up, down;
    count_band(&ow.frame, &up, &down);
    CHECK(ctx.state.items_held[5] == 0, "the view costs one helm (DS:DB 1 -> 0)");
    CHECK(up > 200, "the land half of the sector is lit in the map band (%d px)", up);
    CHECK(down == 0, "the water half is black, so the band is a map and not noise (%d px)", down);
    CHECK(before_up == 0 && before_down == 0,
          "the frame was empty before the press, so the lit pixels are the view");
    u2_frame_free(&ow.frame);

    /* --- control: no helm --------------------------------------------------- */
    arm(&ctx, &ow, 0, 2);
    u2_cmd_dispatch(&ctx, &ow, 'V');
    count_band(&ow.frame, &up, &down);
    CHECK(up == 0 && down == 0, "with no helm nothing is drawn (VIEW WHAT?)");
    u2_frame_free(&ow.frame);

    /* --- control: tower or dungeon ------------------------------------------ */
    arm(&ctx, &ow, 3, 4);
    u2_cmd_dispatch(&ctx, &ow, 'V');
    count_band(&ow.frame, &up, &down);
    CHECK(up == 0 && down == 0, "site digit 4 is refused and the helm is kept");
    CHECK(ctx.state.items_held[5] == 3, "and the count is untouched (%d)", ctx.state.items_held[5]);
    u2_frame_free(&ow.frame);

    /* --- the charge is one per press, not one to zero ------------------------ */
    arm(&ctx, &ow, 7, 0);                 /* site 0 = wilderness, which the gate allows */
    u2_cmd_dispatch(&ctx, &ow, 'V');
    CHECK(ctx.state.items_held[5] == 6, "7 helms -> 6 after one view");
    u2_sc_replay(&ctx, " ");
    u2_cmd_dispatch(&ctx, &ow, 'V');
    CHECK(ctx.state.items_held[5] == 5, "and 5 after two — the charge is per press");
    u2_frame_free(&ow.frame);

    printf("helm_map_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
