/*
 * input_api.c — the play API's input backend. See core/api.h and docs/play-api.md.
 *
 * This is the whole of A3: a key sent to the API arrives through `u2_input`, past the same
 * seam, in the same alphabet game logic sees, exactly like input_script.c and
 * input_demo.c before it. There is no parallel API code path in the game logic, and there
 * cannot be one from here — the backend only answers the question the loop was already
 * going to ask.
 *
 * Both poll() and wait() block. That is deliberate and it is the design, not a shortcut:
 * in an API session the caller owns time, so "no key right now" has no meaning — there is
 * no clock advancing between polls, only the next command. The consequence is spelled out
 * in core/api.h: the idle auto-PASS does not fire, and the overworld loop asks
 * u2_api_blocks_input() rather than assuming it may spin.
 */
#include <stdio.h>

#include "core/api.h"
#include "platform/platform.h"

static int api_input_init(u2_ctx *ctx)
{
    if (!u2_api_active(ctx)) {
        fprintf(stderr, "u2: the API input backend needs a session; --api was not parsed\n");
        return -1;
    }
    return 0;
}

static int api_input_poll(u2_ctx *ctx, u2_key *out)
{
    return u2_api_command(ctx, out) == U2_API_KEY ? 1 : -1;
}

static int api_input_wait(u2_ctx *ctx, u2_key *out)
{
    return u2_api_command(ctx, out) == U2_API_KEY ? 1 : -1;
}

static void api_input_shutdown(u2_ctx *ctx)
{
    (void)ctx;      /* the session itself is finalised by u2_api_end in main */
}

const u2_input u2_input_api = {
    .name     = "api",
    .init     = api_input_init,
    .poll     = api_input_poll,
    .wait     = api_input_wait,
    .shutdown = api_input_shutdown,
};
