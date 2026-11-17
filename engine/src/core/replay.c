/*
 * replay.c — verifiable playthrough report (see core/replay.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/replay.h"
#include "core/sha256.h"
#include "debug/dump_state.h"

void u2_replay_begin(u2_ctx *ctx)
{
    ctx->replay = calloc(1, sizeof(u2_replay));
}

void u2_replay_record(u2_ctx *ctx)
{
    u2_replay *r = ctx->replay;
    char screen[U2_COLS * U2_ROWS + U2_ROWS + 1];
    char state[U2_STATE_JSON_MAX];
    int i;

    if (!r || r->n >= U2_REPLAY_MAX_STEPS) {
        if (r) r->truncated = 1;
        return;
    }
    i = r->n;
    u2_screen_text(ctx, screen, (int)sizeof screen);
    /* Hash the FROZEN evidence scope, not the debug serializer. Coupling the two meant that adding a
     * diagnostic field to the turn log silently invalidated every recorded CP2 replay. */
    u2_state_digest_json(ctx, state, (int)sizeof state);
    sha256_hex(screen, strlen(screen), r->screen_sha[i]);
    sha256_hex(state, strlen(state), r->state_sha[i]);
    r->keys_at[i] = ctx->keys_consumed;
    r->rolls[i] = ctx->rng_rolls;
    r->n = i + 1;
}

void u2_replay_set_keys(u2_ctx *ctx, const char *path)
{
    u2_replay *r = ctx->replay;
    char line[128];
    FILE *f;

    if (!r || !path) return;
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, (int)sizeof line, f)) {
        char *hash = strchr(line, '#');
        char *tok;
        if (hash) *hash = '\0';
        tok = strtok(line, " \t\r\n");
        if (!tok) continue;
        if (r->n_keys < U2_REPLAY_MAX_KEYS) {
            snprintf(r->keys[r->n_keys], 16, "%s", tok);
            r->n_keys++;
        } else {
            r->truncated = 1;
            break;
        }
    }
    fclose(f);
}

static void emit_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(f, "\\%c", *p);
        else if (*p < 0x20 || *p >= 0x7F) fprintf(f, "\\u%04X", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

int u2_replay_write(u2_ctx *ctx, const char *path)
{
    u2_replay *r = ctx->replay;
    char state[U2_STATE_JSON_MAX];
    char ver[32];
    FILE *f;

    if (!r) return -1;
    f = fopen(path, "w");
    if (!f) return -1;

    u2_engine_version(ver, (int)sizeof ver);
    u2_state_json(ctx, state, (int)sizeof state);

    fputs("{\n", f);
    fputs("  \"report\": \"u2-playthrough\",\n", f);
    fputs("  \"version\": 1,\n", f);
    /* Which field set state_sha was computed over. Verification treats an absent field as scope 1, so
     * the evidence recorded before the split still verifies. Bumping this is a baseline move. */
    fputs("  \"digest_version\": 1,\n", f);
    /* Which presentation mode produced this run, so a replay is not at the mercy of
     * whatever the default happens to be this month. Enhanced became the default on
     * 2026-11-17, and the committed CP2 evidence predates that, so it was recorded
     * under original rules and must replay under them. An absent field therefore
     * means classic, not "unknown": every report written before this line existed
     * was written when original mode was the only default. */
    fprintf(f, "  \"mode\": \"%s\",\n",
            ctx->fidelity == U2_FIDELITY_ENHANCED ? "enhanced" : "classic");
    fprintf(f, "  \"engine_version\": "); emit_str(f, ver); fprintf(f, ",\n");
    fprintf(f, "  \"content_source\": ");
    emit_str(f, ctx->content_source ? ctx->content_source : "none");
    fprintf(f, ",\n");
    fprintf(f, "  \"seed\": %u,\n", ctx->seed);
    fputs("  \"save_dir\": \"fresh\",\n", f);
    fputs("  \"keys\": [", f);
    for (int i = 0; i < r->n_keys; i++) {
        if (i) fputs(", ", f);
        emit_str(f, r->keys[i]);
    }
    fputs("],\n", f);
    fputs("  \"steps\": [\n", f);
    for (int i = 0; i < r->n; i++) {
        fprintf(f, "    {\"at_key\": %d, \"screen\": \"%s\", \"state\": \"%s\", \"rolls\": %u}%s\n",
                r->keys_at[i], r->screen_sha[i], r->state_sha[i], r->rolls[i],
                i + 1 < r->n ? "," : "");
    }
    fputs("  ],\n", f);
    fprintf(f, "  \"steps_truncated\": %s,\n", r->truncated ? "true" : "false");
    fprintf(f, "  \"final_state\": %s\n", state);
    fputs("}\n", f);

    fclose(f);
    return 0;
}
