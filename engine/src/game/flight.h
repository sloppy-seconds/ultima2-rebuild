/* The flight screens: the plane, the rocket, and the orbit / deep-space loop.
 *
 * Two functions, because that is all the command dispatch needs. LAUNCH (L) picks one by
 * the player's stamp and nothing else in the engine reaches in — the rest of this module
 * is a loop with its own pacing, and its own idea of when to present a frame.
 *
 * Extracted from commands.c on 2026-11-22 (#17), which is where they had been living as
 * file-statics beside a 26-letter jump table they had no part in.
 */
#ifndef U2_GAME_FLIGHT_H
#define U2_GAME_FLIGHT_H

#include "core/ctx.h"
#include "game/commands.h"      /* u2_cmd_result */
#include "game/overworld.h"     /* u2_overworld */

/* asm/38B0's plane arm: the plane's own direction loop, landing on a valid cell. */
u2_cmd_result u2_flight_launch_plane(u2_ctx *ctx, u2_overworld *ow);

/* asm/38B0's rocket arm: the ascent, the orbit screen, and the descent back to the sector
 * the rocket lifted from. This is the entry that owns the space loop. */
u2_cmd_result u2_flight_launch_rocket(u2_ctx *ctx, u2_overworld *ow);

#endif /* U2_GAME_FLIGHT_H */
