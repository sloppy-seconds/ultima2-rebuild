#ifndef U2_API_H
#define U2_API_H

/*
 * The play API. docs/play-api.md is the plan; this header is the contract.
 *
 * Line-delimited JSON over stdin/stdout. The engine speaks first and every turn ends
 * with an `await`, because the number of keys a command needs is not knowable from
 * outside the engine (ATTACK-- wants one more, a cmd_Y prompt wants a word and a CR),
 * so a caller-initiated request/response would deadlock or guess.
 *
 * There is no second game loop. The observation is emitted by the input backend
 * immediately before it blocks for the caller's key, which is the same rendezvous the
 * demo recorder taps (docs/demo-record-playback.md D12): the state an observation
 * describes is the state the next key is delivered into, so the two can never disagree.
 *
 * stdout is the machine channel and stays that way. Every diagnostic in the engine
 * already goes to stderr; a single stray printf to stdout corrupts a protocol line
 * mid-way and the caller sees garbage instead of an error, so tests/run_api.sh asserts
 * that EVERY line of a whole session parses.
 */

#include "core/ctx.h"

#define U2_API_PROTO   1
#define U2_API_MSGMAX  512     /* the drained message text; see the truncation marker */

typedef enum u2_api_result {
    U2_API_KEY = 1,       /* a key is in *out */
    U2_API_NONE = 0,      /* nothing to deliver (never returned today; kept so a
                           * caller that tests only for 1 is correct) */
    U2_API_END = -1       /* the caller quit, closed stdin, or sent something fatal */
} u2_api_result;

/* Allocate the session so a backend can check for it. Speaks nothing. */
int  u2_api_attach(u2_ctx *ctx);

/* Emit `ready`. Call only after the backends are wired: the observation reads the
 * renderer, input and clock names off ctx, and they are null before then. */
int  u2_api_begin(u2_ctx *ctx);

/* Emit an observation with the given event name ("await", "end", "ready"). */
void u2_api_observe(u2_ctx *ctx, const char *event, const char *reason);

/* An error event naming a rule, then the session continues. */
void u2_api_error(u2_ctx *ctx, const char *code, const char *message);

/*
 * Block for the caller's next command.
 *   U2_API_KEY  a key was delivered into *out
 *   U2_API_END  the caller quit or stdin closed
 * A `state` command re-emits an observation and keeps waiting, so it never returns.
 */
int  u2_api_command(u2_ctx *ctx, u2_key *out);

/* The text tap (docs/play-api.md A6) is written by seam.c into ctx->msg_tap and drained
 * by the observation. Captured at the string emitter, not re-rendered from the grid, so
 * the message and the picture are different observations of different things. Positioned
 * text loses its layout in the tap -- `screen` is the picture; `message` is the stream. */

/* Non-zero when this run is an API session. The seam asks this before tapping, so a
 * normal run pays nothing. */
int  u2_api_active(const u2_ctx *ctx);

/* Finalise. Idempotent. */
void u2_api_end(u2_ctx *ctx, const char *reason);

#endif /* U2_API_H */
