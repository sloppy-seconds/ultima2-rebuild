/*
 * The play API. See core/api.h for the contract and docs/play-api.md for the reasoning.
 *
 * Three things this file deliberately does NOT do:
 *
 *   - It does not run a game loop. The observation is emitted by the input backend
 *     (platform/input_api.c) right before it blocks, so the state described is the state the
 *     next key is delivered into. A second loop would be a second engine.
 *   - It does not own a key vocabulary. `{"key":"..."}` is parsed by u2_key_from_token(),
 *     the same function --script uses, so a token means one thing in both.
 *   - It does not re-render the screen to produce `message`. The tap records what the game
 *     asked to print; the grid records what is on screen. They are different observations of
 *     different things and are named separately rather than allowed to silently disagree.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/api.h"
#include "core/keys.h"
#include "core/demo.h"
#include "debug/dump_state.h"
#include "game/creation.h"

#define LINE_MAX_LEN 4096
#define SCR_MAX      4096
#define JSON_MAX     U2_STATE_JSON_MAX

struct u2_api {
    int ready_emitted;
    /* One `await` per thing the caller must answer. Without this the engine would emit a
     * fresh observation on every poll, and a command that consumes two keys (ATTACK-- then a
     * direction) would look like two questions rather than one question and a follow-up. */
    int await_emitted;
};

int u2_api_active(const u2_ctx *ctx) { return ctx && ctx->api != NULL; }

/* ---- the text tap (A6) -------------------------------------------------- */

/* ---- JSON helpers ------------------------------------------------------- */

/* Explicit escaping, same reason dump_state.c gives for hand-rolling: a stray quote in a
 * game string would otherwise corrupt the transcript. */
static void emit_escaped(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout);  break;
        case '\r': fputs("\\r", stdout);  break;
        case '\t': fputs("\\t", stdout);  break;
        default:
            if (c < 0x20) printf("\\u%04x", c);
            else fputc((char)c, stdout);
        }
    }
}

/* Find "key":"value" and copy the value, unescaping as it goes. Returns 1 on a hit. */
static int json_str(const char *line, const char *key, char *out, size_t outsz)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    /* Keep scanning past matches not followed by a colon. A single strstr found `"key"`
     * inside `"cmd":"key"` and gave up -- the substring-search defect this repo has hit
     * before (sha inside save-sha, art inside any word that happens to carry it). */
    const char *p = line;
    for (;;) {
        p = strstr(p, pat);
        if (!p) return 0;
        p += strlen(pat);
        const char *q = p;
        while (*q && (unsigned char)*q <= ' ') q++;
        if (*q == ':') { p = q + 1; break; }
    }
    while (*p && (unsigned char)*p <= ' ') p++;
    if (*p != '"') return 0;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            default:  c = *p;   /* \" \\ \/ all arrive as the literal */
            }
        }
        if (n + 1 >= outsz) break;
        out[n++] = c;
        p++;
    }
    out[n] = '\0';
    return 1;
}

/* Find "key": <integer> . */
static int json_num(const char *line, const char *key, long *out)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p && (unsigned char)*p <= ' ') p++;
    if (*p != ':') return 0;
    p++;
    while (*p && (unsigned char)*p <= ' ') p++;
    char *end = NULL;
    const long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = v;
    return 1;
}

/* "stats":[10,10,10,10,10,40] -> up to 6 ints. Returns how many were read. */
static int json_int_array(const char *line, const char *key, int *out, int max)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        char *end = NULL;
        const long v = strtol(p, &end, 10);
        if (end == p) break;
        out[n++] = (int)v;
        p = end;
    }
    return n;
}

/* ---- the observation ---------------------------------------------------- */

static void emit_screen(const u2_ctx *ctx)
{
    static char scr[SCR_MAX];
    const int n = u2_screen_text(ctx, scr, (int)sizeof scr);
    fputc('[', stdout);
    if (n > 0) {
        int first = 1;
        const char *row = scr;
        for (int i = 0; i <= n; i++) {
            if (i == n || scr[i] == '\n') {
                if (!first) fputc(',', stdout);
                first = 0;
                fputc('"', stdout);
                emit_escaped(row, (size_t)(i - (row - scr)));
                fputc('"', stdout);
                row = scr + i + 1;
            }
        }
    }
    fputc(']', stdout);
}

/* strnlen is not portable at -std=c11 without a feature macro, and the name field is not
 * guaranteed NUL-terminated (it is a 16-byte fixed field), so bound it by hand. */
static size_t bounded_len(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static void emit_char(const u2_ctx *ctx)
{
    static const char *names[6] = { "str", "agi", "sta", "cha", "wis", "int" };
    const uint8_t *st[6] = { &ctx->state.stat_str, &ctx->state.stat_agi, &ctx->state.stat_sta,
                             &ctx->state.stat_cha, &ctx->state.stat_wis, &ctx->state.stat_int };
    /* Its own block rather than extra fields inside u2_state_json: that serializer is the
     * debug dump and CP2's evidence is read through it, so growing it is a bigger decision
     * than this API needs. A bot needs its own character, and this is it. */
    printf("\"char\":{\"name\":\"");
    emit_escaped((const char *)ctx->state.name, bounded_len((const char *)ctx->state.name, 16));
    printf("\",\"sex\":\"%c\",\"race\":%u,\"class\":%u,\"stats\":{",
           ctx->state.sex ? ctx->state.sex : '?',
           (unsigned)ctx->state.race, (unsigned)ctx->state.pclass);
    for (int i = 0; i < 6; i++)
        printf("%s\"%s\":%u", i ? "," : "", names[i],
               (unsigned)(((*st[i] >> 4) * 10) + (*st[i] & 0x0F)));
    printf("}}");
}

static void emit_obs(u2_ctx *ctx)
{
    static char js[JSON_MAX];
    char digest[65];

    u2_demo_digest(ctx, digest);

    const int jn = u2_state_json(ctx, js, (int)sizeof js);

    /* `rolls`, not `turns`: there is no turn counter on the state block (ow->turns lives in
     * the overworld struct and does not exist at the title or in creation), and rng_rolls is
     * already inside the digest scope, so a caller can correlate it with the digest. */
    /* Mode, and the signal it is read from. There is no phase field on the state block, so
     * this is derived: a name means a character exists, and sector_second is the settlement
     * digit. `title` therefore means "no character yet", which is what the caller can act
     * on; the finer modes (dungeon, space) are not distinguished here and are named as a
     * gap in docs/play-api.md rather than guessed at from the grid. */
    const char *mode = !ctx->state.name[0] ? "title"
                     : (ctx->state.sector_second ? "site" : "world");
    printf("{\"u2api\":%d,\"mode\":\"%s\",\"awaiting\":\"key\",\"rolls\":%u,\"digest\":\"%s\","
           "\"message\":\"",
           U2_API_PROTO, mode, (unsigned)ctx->rng_rolls, digest);
    emit_escaped(ctx->msg_tap.text, ctx->msg_tap.len);
    if (ctx->msg_tap.truncated) emit_escaped("...[truncated]", 14);
    fputc('"', stdout);
    printf(",\"screen\":");
    emit_screen(ctx);
    printf(",");
    emit_char(ctx);
    printf(",\"state\":%s}", jn > 0 ? js : "{}");
    fflush(stdout);

    ctx->msg_tap.len = 0;
    ctx->msg_tap.text[0] = '\0';
    ctx->msg_tap.truncated = 0;
}

void u2_api_observe(u2_ctx *ctx, const char *event, const char *reason)
{
    if (!u2_api_active(ctx)) return;
    if (ctx->api) {
        struct u2_api *a = ctx->api;
        a->await_emitted = (strcmp(event, "await") == 0);
    }
    printf("{\"u2api\":%d,\"event\":\"%s\"", U2_API_PROTO, event);
    if (reason && *reason) printf(",\"reason\":\"%s\"", reason);
    printf(",\"obs\":");
    /* emit_obs prints the observation's own braces and no newline, so the whole event is
     * exactly one line. A protocol record split across lines is the hazard-1 failure in
     * miniature: the caller reads a prefix and sees garbage rather than an error. */
    emit_obs(ctx);          /* prints the observation's own { } and no newline */
    printf("}\n");          /* closes the event */
    fflush(stdout);
}

void u2_api_error(u2_ctx *ctx, const char *code, const char *message)
{
    if (!u2_api_active(ctx)) return;
    printf("{\"u2api\":%d,\"event\":\"error\",\"code\":\"%s\",\"message\":\"",
           U2_API_PROTO, code ? code : "error");
    emit_escaped(message ? message : "", message ? strlen(message) : 0);
    printf("\"}\n");
    fflush(stdout);
}

/* ---- commands ----------------------------------------------------------- */

/* Allocate the session without speaking. main calls this BEFORE the backends are wired,
 * because the input backend's init checks for a session; the first observation has to wait
 * until afterwards, because u2_state_json reads the renderer, input and clock names out of
 * ctx and segfaults on the nulls that precede them. That ordering is the whole reason
 * attach and begin are two calls rather than one. */
int u2_api_attach(u2_ctx *ctx)
{
    if (ctx->api) return 0;
    struct u2_api *a = calloc(1, sizeof *a);
    if (!a) return -1;
    ctx->api = a;
    ctx->msg_tap.on = 1;
    return 0;
}

int u2_api_begin(u2_ctx *ctx)
{
    if (!ctx->api && u2_api_attach(ctx) != 0) return -1;
    u2_api_observe(ctx, "ready", NULL);
    return 0;
}

void u2_api_end(u2_ctx *ctx, const char *reason)
{
    struct u2_api *a = ctx ? ctx->api : NULL;
    if (!a) return;
    u2_api_observe(ctx, "end", reason ? reason : "end");
    ctx->api = NULL;
    free(a);
}

static int do_create(u2_ctx *ctx, const char *line)
{
    u2_character c;
    char why[128] = "";
    char name[64] = "";
    long v = 0;

    memset(&c, 0, sizeof c);
    json_str(line, "name", name, sizeof name);
    c.name = name;

    char sex[8] = "";
    if (json_str(line, "sex", sex, sizeof sex) && sex[0])
        c.sex = (unsigned char)sex[0];
    else
        c.sex = 'M';

    if (json_num(line, "race", &v))  c.race = (int)('0' + (int)v);
    else                             c.race = '1';
    if (json_num(line, "class", &v)) c.cls = (int)('0' + (int)v);
    else                             c.cls = '1';

    const int got = json_int_array(line, "stats", c.stat, 6);
    if (got != 6) {
        u2_api_error(ctx, "stats_required", "stats must be an array of six integers");
        return 0;
    }

    if (u2_creation_apply(ctx, &c, why, (int)sizeof why) != 0) {
        u2_api_error(ctx, "create_refused", why);
        return 0;
    }
    return 1;
}

int u2_api_command(u2_ctx *ctx, u2_key *out)
{
    struct u2_api *a = ctx->api;
    char line[LINE_MAX_LEN];

    for (;;) {
        /* The engine speaks first (A2), and only once per question. */
        if (!a->await_emitted) u2_api_observe(ctx, "await", NULL);

        if (!fgets(line, (int)sizeof line, stdin))
            return U2_API_END;              /* caller gone: end cleanly, do not spin */

        /* Echo nothing that is not ours. A blank line is a keepalive, not a command. */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '#') continue;

        char cmd[32] = "";
        if (!json_str(line, "cmd", cmd, sizeof cmd)) {
            u2_api_error(ctx, "bad_command", "no \"cmd\" string in the request");
            continue;
        }

        if (strcmp(cmd, "quit") == 0) {
            if (a) a->ready_emitted = 0;
            return U2_API_END;
        }
        if (strcmp(cmd, "state") == 0) {
            u2_api_observe(ctx, "await", NULL);
            continue;
        }
        if (strcmp(cmd, "create") == 0) {
            if (do_create(ctx, line)) u2_api_observe(ctx, "created", NULL);
            continue;                       /* the loop re-emits await via the guard */
        }
        if (strcmp(cmd, "key") == 0) {
            char tok[32] = "";
            if (!json_str(line, "key", tok, sizeof tok)) {
                u2_api_error(ctx, "key_required", "a \"key\" command needs a \"key\" token");
                continue;
            }
            u2_key k;
            if (!u2_key_from_token(tok, &k)) {
                u2_api_error(ctx, "unknown_key", tok);
                continue;
            }
            if (out) *out = k;
            a->await_emitted = 0;
            return U2_API_KEY;
        }

        char detail[96];
        snprintf(detail, sizeof detail, "unknown cmd '%s'", cmd);
        u2_api_error(ctx, "unknown_command", detail);
    }
}
