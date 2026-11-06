/*
 * dump_state.c — screen grid + JSON state dump (see debug/dump_state.h).
 *
 * The text builders (u2_screen_text, u2_state_json) are separate from the
 * printers so core/replay.c can digest exactly what a human sees, without a
 * FILE in the way.
 */
#include <stdio.h>
#include <string.h>
#include "debug/dump_state.h"

void u2_engine_version(char *out, int outsz)
{
    snprintf(out, outsz, "%s", U2_ENGINE_VERSION);
}

void u2_dump_screen(const u2_ctx *ctx, FILE *out)
{
    char line[U2_COLS + 1];
    /* ASCII only: the grid is parsed by scripts and by the replay digests. */
    fputc('+', out);
    for (int i = 0; i < U2_COLS; i++) fputc('-', out);
    fputs("+\n", out);
    for (int r = 0; r < U2_ROWS; r++) {
        int n = u2_vram_row_text(&ctx->vram, r, line, (int)sizeof line);
        fprintf(out, "|%s", line);
        for (int i = n; i < U2_COLS; i++) fputc(' ', out);
        fputs("|\n", out);
    }
    fputc('+', out);
    for (int i = 0; i < U2_COLS; i++) fputc('-', out);
    fputs("+\n", out);
}

void u2_dump_hilite(const u2_ctx *ctx, FILE *out)
{
    for (int r = 0; r < U2_ROWS; r++)
        for (int c = 0; c < U2_COLS; c++)
            if (ctx->vram.cell[r][c].attr & U2_ATTR_HILITE)
                fprintf(out, "HILITE %d %d\n", r, c);
}

int u2_screen_text(const u2_ctx *ctx, char *out, int outsz)
{
    char line[U2_COLS + 1];
    int used = 0;

    for (int r = 0; r < U2_ROWS && used < outsz - 1; r++) {
        int n = u2_vram_row_text(&ctx->vram, r, line, (int)sizeof line);
        int written = snprintf(out + used, (size_t)(outsz - used), "%s%s",
                               line, r + 1 < U2_ROWS ? "\n" : "");
        if (written < 0) break;
        used += written;
        (void)n;
    }
    return used;
}

/* The behavioural evidence digest — scope version 1, FROZEN.
 *
 * This is deliberately a SEPARATE serializer from u2_state_json(), and the reason is a bug that
 * nearly went the other way. replay.c hashed whatever u2_state_json() emitted, so the debug turn log
 * and the committed CP2 evidence shared one field set: adding a diagnostic field silently changed
 * every recorded state hash and invalidated the evidence. A feature whose whole purpose is to let
 * telemetry grow (docs/plan-cross-game-replay.md) cannot be coupled to a serializer that must not
 * grow, so the two concerns are now split.
 *
 * u2_state_json() is diagnostics: it may gain fields freely, and does.
 * u2_state_digest_json() is evidence: it is frozen at the field set CP2 was recorded against.
 * Changing it is a baseline move and needs owner approval, exactly like a CP2 re-record would.
 * The output below must stay byte-identical to the serializer as of the split, or the committed
 * evidence stops reproducing.
 */
int u2_state_digest_json(const u2_ctx *ctx, char *out, int outsz)
{
    const u2_state *s = &ctx->state;
    char ver[32];

    u2_engine_version(ver, (int)sizeof ver);
    return snprintf(out, (size_t)outsz,
        "{\"engine\":\"%s\",\"mode\":\"%s\","
        "\"platform\":{\"renderer\":\"%s\",\"input\":\"%s\",\"audio\":\"%s\",\"clock\":\"%s\"},"
        "\"screen\":{\"cols\":%d,\"rows\":%d,\"cells_written\":%llu,"
        "\"cursor\":{\"row\":%d,\"col\":%d,\"visible\":%d}},"
        "\"state\":{\"player_x\":%u,\"player_y\":%u,"
        "\"cursor\":{\"row\":%u,\"col\":%u},"
        "\"margins\":{\"left\":%u,\"right\":%u},"
        "\"text_attr\":%u,\"story_flag\":%u,\"sector_second\":%u,"
        "\"gold_bcd\":[%u,%u],\"gold\":%d,\"spell_pick\":%u,"
        "\"rng\":{\"state\":[%u,%u,%u,%u,%u,%u],\"rolls\":%u},"
        "\"map_base\":%u},"
        "\"content\":\"%s\"}",
        ver,
        ctx->mode == U2_MODE_HEADLESS ? "headless" : "term",
        ctx->renderer->name, ctx->input->name, ctx->audio->name, ctx->clock->name,
        U2_COLS, U2_ROWS, (unsigned long long)ctx->vram.writes,
        ctx->vram.row, ctx->vram.col, ctx->vram.visible,
        s->player_x, s->player_y, s->cursor_row, s->cursor_col,
        s->left_margin, s->right_margin, s->text_attr, s->story_flag, s->sector_second,
        s->gold_lo, s->gold_hi, u2_bcd16(s->gold_lo, s->gold_hi), s->spell_pick,
        s->rng_state[0], s->rng_state[1], s->rng_state[2],
        s->rng_state[3], s->rng_state[4], s->rng_state[5],
        ctx->rng_rolls, s->map_base,
        ctx->content_source ? ctx->content_source : "none");
}

/* Raw DS bytes, in memory order, under the SAME field names tools/u2_record.py uses for the original.
 *
 * This exists so a cross-game diff can compare bytes instead of interpretations. The decoded fields
 * above are convenient for a human and useless as a comparison basis: u2_bcd16() takes (lo, hi) while
 * the display prints hundreds-first, and food is a 3-byte BCD that the 16-bit decoder truncates. Every
 * such convention is a place where two correct sides look different. Comparing raw bytes removes the
 * whole class. The debug serializer may carry this because the evidence digest is a separate, frozen
 * function - see u2_state_digest_json.
 */
static int emit_raw(const u2_state *s, char *out, int outsz)
{
    int used = 0;
    uint8_t pos[2]    = { s->sector_x, s->sector_y };
    uint8_t sector[2] = { s->sector_first, s->sector_second };
    uint8_t hp[2]     = { s->hp, s->unk_52 };
    uint8_t food[3]   = { s->food, s->food_hi, s->food_lo };
    uint8_t exp[2]    = { s->exp_lo, s->exp_hi };
    uint8_t gold[2]   = { s->gold_lo, s->gold_hi };
    uint8_t para[4]   = { s->counter_26A, s->paral_legs, s->paral_arms, s->paral_sleep };
    /* DS:64 torches and DS:26 the light counter, side by side because cmd_I writes both and
     * the LIGHT spell writes the same constant into the second. */
    uint8_t light[2]  = { s->torches, s->moved_flag };

    struct { const char *name; const uint8_t *p; int n; } f[] = {
        { "pos", pos, 2 }, { "sector", sector, 2 }, { "hp", hp, 2 },        { "food", food, 3 }, { "exp", exp, 2 }, { "gold", gold, 2 },
        { "items", s->items_held, 16 },
        /* The travel mode, which is the same byte as the sprite stamped into the player's
         * cell (DS:13). A turn log that cannot say whether the player was aboard anything
         * cannot explain a step that cost 38 food or a step the rocket refused. */
        { "stamp", &s->player_stamp, 1 },
        /* `light` is torches then the light counter; `floor` is the byte DESCEND and KLIMB
         * move, and the one that underflows to 0xFF on the way out of the bottom of a tower
         * (captures/spice86/d12-ladder-ds25). A turn log that cannot name these cannot tell
         * an IGNITE TORCH that spent a torch from one that only printed its own name. */
        { "light", light, 2 },
        { "floor", &s->site_floor, 1 },
        { "mon_x", s->mon_x, 32 },     { "mon_y", s->mon_y, 32 },
        { "mon_hp", s->mon_hp, 32 },   { "mon_type", s->mon_type, 32 },
        { "mon_kind", s->mon_kind, 32 }, { "mon_role", s->mon_role, 32 },
        { "wander_x", s->mon_wander_x, 32 }, { "wander_y", s->mon_wander_y, 32 },
        { "rng", s->rng_state, 6 },    { "paralysis", para, 4 },
    };

    /* No leading comma: the creatures block this follows already ends with one. */
    used += snprintf(out + used, (size_t)(outsz - used), "\"raw\":{");
    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) {
        used += snprintf(out + used, (size_t)(outsz - used), "\"%s\":\"", f[i].name);
        for (int b = 0; b < f[i].n && used < outsz - 16; b++)
            used += snprintf(out + used, (size_t)(outsz - used), "%s%02x",
                             b ? " " : "", f[i].p[b]);
        used += snprintf(out + used, (size_t)(outsz - used), "\"%s",
                         i + 1 < sizeof f / sizeof f[0] ? "," : "},");
    }
    return used;
}

int u2_state_json(const u2_ctx *ctx, char *out, int outsz)
{
    const u2_state *s = &ctx->state;
    char ver[32];

    /* The creature table, built first because snprintf has no append.
     *
     * Every slot that has a type, as [x, y, type, role, hp]. This exists because a turn log that
     * cannot say where the creatures were is only a picture: the frame shows a sprite, and only the
     * state can say whether two sprites mean two creatures or one creature and a stale stamp. Hex
     * for the bytes, because they are bytes and not counts.
     */
    char mons[2048];
    char raw[1600];
    int used = 0;

    emit_raw(s, raw, (int)sizeof raw);
    used += snprintf(mons + used, sizeof mons - (size_t)used, "\"creatures\":[");
    for (int i = 0; i < 32 && used < (int)sizeof mons - 48; i++) {
        if (!s->mon_type[i]) continue;
        used += snprintf(mons + used, sizeof mons - (size_t)used,
                         "%s[%d,%d,\"0x%02X\",\"0x%02X\",%u]",
                         used > 15 ? "," : "", s->mon_x[i], s->mon_y[i],
                         s->mon_type[i], s->mon_role[i], s->mon_hp[i]);
    }
    snprintf(mons + used, sizeof mons - (size_t)used, "],");

    u2_engine_version(ver, (int)sizeof ver);
    return snprintf(out, (size_t)outsz,
        "{\"engine\":\"%s\",\"mode\":\"%s\","
        "\"platform\":{\"renderer\":\"%s\",\"input\":\"%s\",\"audio\":\"%s\",\"clock\":\"%s\"},"
        "\"screen\":{\"cols\":%d,\"rows\":%d,\"cells_written\":%llu,"
        "\"cursor\":{\"row\":%d,\"col\":%d,\"visible\":%d}},"
        "\"state\":{%s%s\"ghosts\":%d,\"flashes\":%u,\"unstamped\":%d,\"unstamped_at_load\":%d,"
        /* pos is DS:00/01, the original's own player-position mirror. The field below was NAMED
         * player_x/player_y but reported DS:23/24, which is the staged candidate cell the step gate
         * reads — it sat at 0 in every turn log, so a recording could not say where the player was
         * standing. Renamed `staged` rather than dropped; the rename is the honest fix. */
        "\"pos\":{\"x\":%u,\"y\":%u},"
        "\"sector\":\"%u%u\",\"hp\":%u,\"food\":%u,"
        "\"staged\":{\"x\":%u,\"y\":%u},"
        "\"cursor\":{\"row\":%u,\"col\":%u},"
        "\"margins\":{\"left\":%u,\"right\":%u},"
        "\"text_attr\":%u,\"story_flag\":%u,\"sector_second\":%u,"
        "\"gold_bcd\":[%u,%u],\"gold\":%d,\"spell_pick\":%u,"
        "\"rng\":{\"state\":[%u,%u,%u,%u,%u,%u],\"rolls\":%u},"
        "\"map_base\":%u},"
        "\"content\":\"%s\"}",
        ver,
        ctx->mode == U2_MODE_HEADLESS ? "headless" : "term",
        ctx->renderer->name, ctx->input->name, ctx->audio->name, ctx->clock->name,
        U2_COLS, U2_ROWS, (unsigned long long)ctx->vram.writes,
        ctx->vram.row, ctx->vram.col, ctx->vram.visible,
        mons, raw,
        ctx->diag_ghosts,
        ctx->diag_flashes,
        ctx->diag_unstamped,
        ctx->diag_unstamped_at_load,
        s->sector_x, s->sector_y,
        s->sector_first, s->sector_second,
        u2_bcd16(s->unk_52, s->hp), u2_bcd16(s->food, s->food_hi),
        s->player_x, s->player_y,
        s->cursor_row, s->cursor_col,
        s->left_margin, s->right_margin, s->text_attr, s->story_flag, s->sector_second,
        s->gold_lo, s->gold_hi, u2_bcd16(s->gold_lo, s->gold_hi), s->spell_pick,
        s->rng_state[0], s->rng_state[1], s->rng_state[2],
        s->rng_state[3], s->rng_state[4], s->rng_state[5],
        ctx->rng_rolls, s->map_base,
        ctx->content_source ? ctx->content_source : "none");
}

void u2_dump_state_json(const u2_ctx *ctx, FILE *out)
{
    char buf[U2_STATE_JSON_MAX];

    u2_state_json(ctx, buf, (int)sizeof buf);
    fputs(buf, out);
    fputc('\n', out);
}
