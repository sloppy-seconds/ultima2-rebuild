#ifndef U2_DUMP_STATE_H
#define U2_DUMP_STATE_H

/*
 * State dump — the automation substrate.
 *
 * --dump-state prints the screen grid as plain text and the game state as JSON
 * on stdout, which is what a playthrough harness parses. No third-party JSON
 * writer: the schema is small and hand-rolled, and the escaping is explicit so
 * a stray quote cannot corrupt a transcript.
 *
 * The text builders return the same bytes the printers emit, so core/replay.c
 * digests exactly what an agent sees.
 */

#include <stdio.h>
#include "core/ctx.h"

#define U2_ENGINE_VERSION "0.1-cp2"
#define U2_STATE_JSON_MAX 8192   /* sized for the creature table plus the raw block; see u2_state_json */

void u2_dump_screen(const u2_ctx *ctx, FILE *out);
/* One `HILITE <row> <col>` line per cell carrying U2_ATTR_HILITE, 0-based, in
 * row-major order. The grid dump prints characters only, so an inverted cell and
 * a plain one are identical there and the highlight is unobservable to a text
 * assertion. This is the channel that makes it assertable headlessly, without
 * driving a terminal and parsing its escapes. */
void u2_dump_hilite(const u2_ctx *ctx, FILE *out);
void u2_dump_state_json(const u2_ctx *ctx, FILE *out);
int  u2_screen_text(const u2_ctx *ctx, char *out, int outsz);
int  u2_state_json(const u2_ctx *ctx, char *out, int outsz);
/* The behavioural evidence digest, scope version 1. Separate from u2_state_json on purpose: the
 * debug serializer grows freely, the evidence digest must not, because replay.c hashes this and CP2's
 * committed evidence is only reproducible while its scope is frozen. Changing the output of this
 * function is a baseline move needing owner approval. */
int  u2_state_digest_json(const u2_ctx *ctx, char *out, int outsz);
void u2_engine_version(char *out, int outsz);

#endif /* U2_DUMP_STATE_H */
