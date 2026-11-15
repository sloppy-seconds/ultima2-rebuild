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
#include "core/pool_names.h"
#include "core/keys.h"
#include "core/demo.h"
#include "core/save.h"
#include "debug/dump_state.h"
#include "game/creation.h"
#include "game/gates.h"

#define LINE_MAX_LEN 4096
#define SCR_MAX      4096
#define JSON_MAX     U2_STATE_JSON_MAX

struct u2_api {
    int ready_emitted;
    /* One `await` per thing the caller must answer. Without this the engine would emit a
     * fresh observation on every poll, and a command that consumes two keys (ATTACK-- then a
     * direction) would look like two questions rather than one question and a follow-up. */
    int await_emitted;
    /* One key held across a handoff. A `create` arrives on a line the reader has already
     * consumed, and the key that follows it belongs to the world, not to the flow that was
     * polling when the create landed. The line cannot be unread, so the key is parked here
     * and handed out first once the world loop is running. One slot is enough because the
     * flows stop on `enter_world` as soon as they see a no-key poll, so at most one key can
     * be in flight. */
    int pending_key;
    u2_key pending;
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

/* The death signature, measured rather than guessed. `asm/0C62_death.asm` is nine zero
 * stores over DS:51..59 followed by the "<name> IS DEAD!" tail, and it is screen-proven at
 * captures/spice86/cp5-weak-fight/death1.png, which reads H.P., FOOD, EXP and GOLD all at
 * 0000. So the signature is the whole ledger zeroed, not HP alone -- and the distinction
 * matters, because a bot that watches only HP would call the moment a hit lands at exactly
 * zero on the high pair while food and gold are intact, which is a wounded character and not
 * a corpse.
 *
 * The name test is not decoration. A process that has not created a character yet also has
 * nine zero bytes, and reporting that as `dead` would tell a caller the run is over before it
 * started. Death is "the ledger was zeroed on somebody", so there has to be a somebody. */
static int char_is_dead(const u2_state *st)
{
    if (!st->name[0]) return 0;
    return !st->hp && !st->unk_52 && !st->food && !st->food_hi && !st->food_lo
        && !st->exp_lo && !st->exp_hi && !st->gold_lo && !st->gold_hi;
}

/* One owned-count pool as "NAME":count pairs. Keyed by name rather than by index because
 * the whole point of this block is that a caller should not have to know that armour counts
 * are indexed one below the menu digit while weapon counts are not -- that off-by-one is
 * recorded in state.h precisely because it is the kind of thing a caller gets wrong silently.
 * Names come from the generated pools, the same tables the panel and the Z screen read, so
 * there is no fourth copy. U2_POOL_NAME is not used here because it token-pastes `tbl##_N`
 * and so needs a literal table name; the caller passes that table's own _N, which makes
 * every index in range by construction. */
static void emit_counts(const char *const *tbl, int n, const uint8_t *counts)
{
    for (int i = 0; i < n; i++)
        printf("%s\"%s\":%u", i ? "," : "", tbl[i], (unsigned)counts[i]);
}

static void emit_gate(const u2_ctx *ctx)
{
    /* A bot cannot walk to a door it cannot see, and until now the observation carried no
     * gate at all: `state` is the debug dump and it has never emitted DS:6E or DS:6F. The
     * cell comes from ctx, which the draw path writes, so the API reports the door that was
     * actually rendered rather than a second reading of the rotation table. The rest is
     * derived from the state block by the same pure functions the engine uses, so there is
     * one rule and not two. */
    const int slot = u2_gate_slot(ctx->state.gate_index);
    const int era = ctx->state.sector_first;
    const int dest = u2_gate_partner_era(era, slot);
    int dx = 0, dy = 0;
    if (dest >= 0)
        (void)u2_gate_cell(dest, slot, &dx, &dy);
    printf("\"gate\":{\"present\":%s,\"x\":%d,\"y\":%d,\"slot\":%d,\"countdown\":%u,"
           "\"era\":%u,\"world\":%u,\"dest_era\":%d,\"dest_x\":%d,\"dest_y\":%d}",
           ctx->gate_present ? "true" : "false", ctx->gate_x, ctx->gate_y, slot,
           (unsigned)ctx->state.gate_countdown, (unsigned)era, (unsigned)ctx->state.world,
           dest, dx, dy);
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
    /* +1 on race and class: the state block stores them 0..3 (asm/5A7A's `sub $0x31`),
     * and the create command takes them 1..4, so reporting the raw byte would make a
     * caller send 1 and read back 0. The observation and the command speak one numbering. */
    printf("\",\"sex\":\"%c\",\"race\":%u,\"class\":%u,\"stats\":{",
           ctx->state.sex ? ctx->state.sex : '?',
           (unsigned)ctx->state.race + 1, (unsigned)ctx->state.pclass + 1);
    for (int i = 0; i < 6; i++)
        printf("%s\"%s\":%u", i ? "," : "", names[i],
               (unsigned)(((*st[i] >> 4) * 10) + (*st[i] & 0x0F)));
    printf("}");

    /* Decoded counters. The caller should not have to know these are packed decimal; the
     * engine already has one accessor that says which byte pair each field reads, and using
     * it here is what keeps the API's numbers agreeing with the panel's. FOOD is the
     * DISPLAYED four digits -- the original hides a third byte that a step actually charges,
     * which is why a step looks like it costs nothing until the shown number moves. */
    uint8_t msb, lsb;
    u2_state_bcd_pair(&ctx->state, U2_BCD_HP,   &msb, &lsb); printf(",\"hp\":%d", u2_bcd16(msb, lsb));
    u2_state_bcd_pair(&ctx->state, U2_BCD_FOOD, &msb, &lsb); printf(",\"food\":%d", u2_bcd16(msb, lsb));
    u2_state_bcd_pair(&ctx->state, U2_BCD_EXP,  &msb, &lsb); printf(",\"exp\":%d", u2_bcd16(msb, lsb));
    u2_state_bcd_pair(&ctx->state, U2_BCD_GOLD, &msb, &lsb); printf(",\"gold\":%d", u2_bcd16(msb, lsb));
    printf(",\"dead\":%s", char_is_dead(&ctx->state) ? "true" : "false");

    /* What is equipped, as both the index and the name. A policy comparing `wield.index`
     * against a goal predicate wants the number; a human reading a log wants the name, and
     * making the caller look the name up is how a second table gets written. */
    printf(",\"wield\":{\"index\":%u,\"name\":\"%s\"},\"worn\":{\"index\":%u,\"name\":\"%s\"}",
           (unsigned)ctx->state.readied_weapon,
           U2_POOL_NAME(U2_T_WEAPONS, ctx->state.readied_weapon),
           (unsigned)ctx->state.worn_armour,
           U2_POOL_NAME(U2_T_ARMOUR, ctx->state.worn_armour));

    printf(",\"weapons\":{");
    emit_counts(U2_T_WEAPONS, (int)(sizeof ctx->state.weapons_owned / sizeof ctx->state.weapons_owned[0]),
                ctx->state.weapons_owned);
    printf("},\"items\":{");
    emit_counts(U2_T_ITEMS, (int)(sizeof ctx->state.items_held / sizeof ctx->state.items_held[0]),
                ctx->state.items_held);
    /* Armour counts are offset by one: armour_owned[0] is CLOTH, not SKIN, because the Z
     * loop reads `0x96(%di)` with di = the menu digit and the digits start at 1. Handled
     * here so no caller has to. SKIN gets a key with a zero count rather than a synthetic
     * one, because no byte tracks it -- and the same is true of HANDS in the weapon pool,
     * whose slot is present and zero. Both digits are exempt from the ownership gate
     * (cmd_R's note records the original walking digit 0 past the ownership test), so a
     * zero here means "nothing to consume", not "refused". Inventing a 1 would be the
     * engine asserting a state the bytes do not hold. */
    printf("},\"armour\":{\"SKIN\":0,");
    for (int i = 0; i < (int)(sizeof ctx->state.armour_owned / sizeof ctx->state.armour_owned[0]); i++)
        printf("%s\"%s\":%u", i ? "," : "",
               U2_POOL_NAME(U2_T_ARMOUR, i + 1), (unsigned)ctx->state.armour_owned[i]);
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
    /* Mode, and the signal each value is read from. There is no phase field on the state
     * block, so this is derived: no name means no character yet, `enter_world` is the
     * engine's own request-to-play flag, and `sector_second` is the settlement digit. The
     * middle state exists because the play API can create a character without the world
     * having been entered, and reporting that as `world` was a lie a caller would act on.
     * Dungeon and space are still NOT distinguished, and are named as a gap in
     * docs/play-api.md rather than guessed at from the grid. */
    const char *mode = !ctx->state.name[0] ? "title"
                     : (!ctx->enter_world ? "created"
                      : (ctx->state.sector_second ? "site" : "world"));
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
    /* Only once there is a world to have a door in. At the title or in creation the era and
     * world bytes are both zero, and reporting era 0 there would read as a real location. */
    if (ctx->enter_world)
        printf(","), emit_gate(ctx);
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

/* One observation line, on stdout, for a caller that is not running a session.
 *
 * This exists so the DS decode has exactly one implementation. The bot has to look at the
 * original's memory as well as the engine's (docs/bot-driver.md), and the obvious way to do
 * that is a Python parser for the DS block — which would be a second hand-maintained copy of
 * every field offset and every packed-decimal rule in state.h, drifting silently and showing
 * it as a bot acting on a wrong field. So the bot ships raw bytes at the engine instead and
 * gets this function's output for them, and the two targets cannot disagree about what a
 * byte means because there is only one decoder.
 *
 * It is the same emit_obs() the live API prints, not a parallel formatter: same `char` block,
 * same `state` block, same digest. A caller that reads one can read the other.
 */
int u2_api_emit_observation(u2_ctx *ctx)
{
    if (!ctx->api && u2_api_attach(ctx) != 0) return -1;
    emit_obs(ctx);
    fputc('\n', stdout);
    return 0;
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
    /* Finish the handoff the interactive flow would have made. Without this the character
     * exists on disk and in memory while the title flow keeps cycling its splash, so a
     * caller that created a character and then sent game keys was driving the menu, not the
     * world -- and the observation said `mode: world` the whole time, because mode is derived
     * from the name (docs/play-api.md deviation 2). `enter_world` is the engine's own
     * request-to-play signal, set by the enhanced-mode creation tail at title.c; reusing it
     * is deliberate, because inventing a second "the API wants in" flag would be a second
     * answer to one question. */
    ctx->enter_world = 1;
    return 1;
}

/* {"cmd":"load"} -- the menu's own P, expressed declaratively.
 *
 * `create` is "press C and fill the form" without the form; this is "press P" without the
 * menu. Both are menu ACTIONS, never menu navigation, and neither gives the caller a power
 * the original lacks: u2_save_load() is the same routine title.c runs for the P key, and it
 * is already cross-verified against the real game by tools/save_cross_load.py.
 *
 * The guard is the point. The original cannot load mid-session -- a load means exiting to the
 * menu and coming back -- so `load` is refused once the world has started. That is what keeps
 * this a menu verb rather than a new game capability, and it is the owner ruling of
 * 2026-11-09: no mid-session load until a real multi-slot save/load system exists
 * (docs/save-scoped-worlds.md D5, which defers slot selection). Recovery for a bot is
 * therefore: end the process, relaunch with --api --save-dir X, send {"cmd":"load"}.
 *
 * world_started rather than enter_world is the test, because enter_world stays set for the
 * whole session once a handoff has happened and so cannot tell "still at the menu" from
 * "handed off long ago" -- the same distinction that made the create handoff work.
 */
static int do_load(u2_ctx *ctx)
{
    /* enter_world, not world_started. The menu offers C *or* P, so once any handoff has
     * happened -- a create, or an earlier load -- this is no longer a menu action. Testing
     * world_started instead let `create` then `load` through, because a caller that sends an
     * invalid key never starts the world loop and world_started stays 0 forever. */
    if (ctx->enter_world) {
        u2_api_error(ctx, "load_only_at_menu",
                     "the original can only load at the menu; end the process and relaunch "
                     "with --save-dir to reload a save");
        return 0;
    }

    const u2_save_result r = u2_save_load(ctx);
    if (r != U2_SAVE_OK) {
        /* BLANK is the original's own "no character yet" test -- a slot that exists but
         * holds nothing. NOT_READY is the insert-disk analogue. Naming them apart matters:
         * a caller that cannot tell an empty slot from a broken one will retry forever. */
        if (r == U2_SAVE_BLANK)
            u2_api_error(ctx, "load_no_character", "the save slot holds no character");
        else if (r == U2_SAVE_NOT_READY)
            u2_api_error(ctx, "load_not_ready", "the save path is unusable or absent");
        else
            u2_api_error(ctx, "load_failed", "could not read the save record");
        return 0;
    }

    /* Same handoff as create: the flag is what makes the title flow unwind, and returning
     * U2_API_NONE from the dispatch is what keeps the caller's next keys for the world. */
    ctx->enter_world = 1;
    return 1;
}

int u2_api_command(u2_ctx *ctx, u2_key *out)
{
    struct u2_api *a = ctx->api;
    char line[LINE_MAX_LEN];

    for (;;) {
        /* A key parked during a handoff goes out before anything is read or emitted, so the
         * world loop receives the caller's keys in the order they were sent. */
        if (a->pending_key) {
            a->pending_key = 0;
            if (out) *out = a->pending;
            a->await_emitted = 0;
            return U2_API_KEY;
        }
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
            if (do_create(ctx, line)) {
                u2_api_observe(ctx, "created", NULL);
                /* Hand the session back instead of reading on. This is the whole fix for
                 * "the API accepts every key, answers await, reports mode world, and does
                 * nothing": `create` is delivered from INSIDE seam_key_poll, so the flow that
                 * was polling keeps going after the flag is set, and the title flow's stop test
                 * only runs on the no-key arm. Without this return the title flow ate the
                 * caller's first game keys as menu keys, then swallowed the rest, and the
                 * world loop was entered with stdin already spent. Measured before the fix:
                 * 40 direction keys, `rolls` never left 0, and the overworld's key poll was
                 * reached zero times.
                 *
                 * Returning NONE (not KEY, not END) is what makes the unwind honest: NONE is
                 * "no key right now", so seam_key_poll leaves input_exhausted alone and the
                 * title flow's own predicate sees enter_world and returns. The caller's keys
                 * stay in stdin for the world, which is where they belong. U2_API_NONE existed
                 * in api.h for exactly this and had never been returned. */
                if (ctx->enter_world)
                    return U2_API_NONE;
            }
            continue;                       /* the loop re-emits await via the guard */
        }
        if (strcmp(cmd, "load") == 0) {
            if (do_load(ctx))
                u2_api_observe(ctx, "loaded", NULL);
            /* Identical unwind to create. If the load set enter_world, the flow that is
             * polling must see it on a no-key arm, so answer NONE and hand the session
             * back rather than reading on and eating the caller's next key as a menu key. */
            if (ctx->enter_world)
                return U2_API_NONE;
            continue;
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
            /* Hold the key until the world has taken over. A `create` is delivered from
             * inside a poll that some flow is already sitting in, and the title flow's stop
             * test lives in its no-key arm — so a key released immediately is read by that
             * flow as a menu key and lost. Parking it and answering NONE makes every flow
             * that consults flow_stopped() unwind, and the key is handed to the world loop
             * when it asks. Without this the API accepted every key, answered `await`,
             * reported mode world and took no turn at all: measured as `rolls` stuck at 0
             * across forty direction keys. */
            if (ctx->enter_world && !ctx->world_started) {
                a->pending_key = 1;
                a->pending = k;
                return U2_API_NONE;
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
