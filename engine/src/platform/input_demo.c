/*
 * input_demo.c — the playback input backend. See core/demo.h.
 *
 * This is the whole of D12's claim that there is no parallel demo code path: the
 * keys a demo replays arrive through `u2_input`, past the same seam, in the same
 * alphabet game logic sees. `u2_input_script` is the template, and the file is
 * small because everything that is not delivery lives in core/demo.c.
 *
 * The one thing a script never has to say is "no key, but the world owes a turn":
 * an idle auto-PASS is a record with no key behind it, so poll() answers 0 and the
 * overworld loop's idle arm decides what to do about it (D4). That is why 0 is a
 * legal answer here and not a stall.
 *
 * Pacing (D11) lives in this file and nowhere else, because pacing is presentation
 * and presentation must not be able to reach state. Records are driven from the
 * file, not from the clock, so a pause here cannot change what happens — only when
 * the player sees it. W8 is the gate that measures that claim instead of asserting
 * it, and W15's control forces the pace on in headless to prove it is applied.
 */
#include <stdio.h>
#include "core/ctx.h"
#include "core/demo.h"

/* Roughly one BIOS tick per record. Not tuned: the owner ruling was "watchable,
 * and the speed can be tuned later", and tuning a number before the first demo
 * exists is the part the plan declines to guess about. */
#define U2_DEMO_PACE_MS 55

static int demo_input_init(u2_ctx *ctx)
{
    if (!ctx->demo) {
        fprintf(stderr, "u2: the playback input backend needs a demo; --playback "
                        "was not parsed\n");
        return -1;
    }
    return 0;
}

static int paced(u2_ctx *ctx)
{
    return ctx->mode != U2_MODE_HEADLESS || ctx->demo_play_pace;
}

static int demo_input_poll(u2_ctx *ctx, u2_key *out)
{
    const int r = u2_demo_next_key(ctx, out);

    if (r == 1 && paced(ctx) && ctx->clock && ctx->clock->sleep_ms)
        ctx->clock->sleep_ms(ctx, U2_DEMO_PACE_MS);
    return r;
}

static int demo_input_wait(u2_ctx *ctx, u2_key *out)
{
    const int r = u2_demo_next_key(ctx, out);

    if (r == 1) {
        if (paced(ctx) && ctx->clock && ctx->clock->sleep_ms)
            ctx->clock->sleep_ms(ctx, U2_DEMO_PACE_MS);
        return 1;
    }
    if (r == 0) {
        /* The next record is `kind=auto` and something is BLOCKING for a key. That
         * cannot happen for a well-formed demo: an idle auto-PASS only exists where
         * the recording was in the overworld's poll loop, and a blocking prompt is
         * not that. Answering "no key" rather than spinning is the loud version — the
         * caller ends its flow, and u2_demo_finish reports the records left over. */
        return 0;
    }
    ctx->exit_code = 0;
    return 0;
}

const u2_input u2_input_demo = {
    .name = "demo",
    .init = demo_input_init,
    .poll = demo_input_poll,
    .wait = demo_input_wait,
    .shutdown = NULL,
};
