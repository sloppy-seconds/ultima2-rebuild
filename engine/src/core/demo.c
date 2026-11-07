/*
 * demo.c — record and playback of a playable session. See core/demo.h; the
 * decisions are D1..D13 in docs/demo-record-playback.md and the D-numbers below
 * point at them rather than restating them, because a restated decision is a
 * second copy that can drift and then lie.
 *
 * Shape: one struct, two directions. Recording appends and flushes per record so
 * a hard kill loses at most the open record (D8); playback parses the whole file
 * up front so a truncated file is refused before a single key is delivered rather
 * than discovered at record 4000.
 */
#define _POSIX_C_SOURCE 200809L   /* mkdir, stat, localtime under -std=c11 */

#include "core/demo.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "core/compat.h"
#include "core/save.h"
#include "core/sha256.h"
#include "core/state.h"
#include "debug/dump_state.h"

const int U2_DEMO_EXCLUDE[U2_DEMO_EXCLUDE_COUNT] = {
    0x74, 0x75, 0x550, 0x551, 0x552, 0x553
};

#define U2_DEMO_MAX_KEYS_PER_REC 8
/* There used to be a U2_DEMO_MAX_OVERLAYS of 3 here, meaning one sector's three files.
 * Both lists are heap-grown now; see overlay_reserve and world_reserve. */

/* A sector delta is spelled `x,y:old->new` per differing byte, ascending by y*64+x,
 * joined by ';'. The cap is generous on purpose: a sector a player really edited is a
 * handful of cells, and anything past this is a corrupt or adversarial file rather than
 * a game. Over-cap is reported, never silently truncated — a truncated delta that still
 * verifies is the vacuous-pass shape AGENTS.md § 3 exists to stop. */
#define U2_DEMO_DELTA_MAX 2048

/* One `overlay` / `overlay-slot` line: a sector file whose bytes reached the world.
 * `checked` is per entry, not global: the comparison has to happen once per FILE, and a
 * single latch made it happen once per RUN, so only the starting sector was ever
 * verified. */
struct demo_overlay {
    char name[24];
    char sha[65];
    int  from_slot;
    int  checked;
};

/* One `@world` line: what a viewer's content must hash to, and what this save changed. */
struct demo_world {
    char name[24];
    char base[65];
    char delta[U2_DEMO_DELTA_MAX];
};
/* Sized for the longest line the writer can legitimately emit, which is an `@world`:
 * prefix + name + base + ` delta=` + a full U2_DEMO_DELTA_MAX delta. The value used to be
 * 768, which a six-cell real delta did not exceed but a larger one would have, and emit()
 * used vsnprintf's return as a length — so a too-long line would have fwrite'd past the
 * end of its own stack buffer and hashed the garbage into the framing digest. emit() now
 * clamps as well, so this constant is a size and not a silent trap. */
#define U2_DEMO_LINEBUFSZ (U2_DEMO_DELTA_MAX + 256)
/* The saved record is 256 bytes at DS:36 (core/save.h). Named once here so the
 * two places that digest it cannot disagree about the span. */
#define U2_DEMO_RECORD_BYTES 256

typedef struct u2_demo_rec {
    int      kind;                    /* 0 = key, 1 = auto */
    u2_key   keys[U2_DEMO_MAX_KEYS_PER_REC];
    int      n_keys;
    uint32_t rolls;
    char     sha[65];
} u2_demo_rec;

typedef struct u2_demo {
    u2_demo_state state;
    u2_demo_mode  mode;

    /* ---- recording ---- */
    FILE      *file;
    char       path[600];
    sha256_ctx framing;               /* running digest over every byte written */
    int        n_written;
    int        pending_start;
    int        pending_stop;
    int        started;               /* header + checkpoint are on disk */
    int        finished;
    /* How the recording ended, and it changes what @end's state digest MEANS.
     * A stop toggle leaves the run running, so the digest is the state at a quiescent
     * point and playback compares it at the matching poll. A quit leaves the run over,
     * so the digest is the state at exit and playback must compare it at exit. The
     * first version assumed the first in both cases, which made the owner's own
     * requirement — "quitting the game while recording should stop the recording
     * gracefully" — produce a demo that could never verify: it reported DEMO
     * INCOMPLETE on a file that was perfectly well formed. */
    int        stopped_by_toggle;
    int        told_title;            /* the "will start when you reach the world" note */
    /* How many times the toggle has been pressed since the recorder was armed. The
     * cancel rule needs a count rather than a state test: "pending and not started" is
     * the shape of a FIRST press that has not yet reached a boundary just as much as it
     * is the shape of a second one, and testing the state alone made the first Ctrl-R
     * arm the recording and cancel it in the same breath. */
    int        toggles;

    /* ---- parsed file ---- */
    u2_demo_rec *recs;
    int        n_recs;
    int        cap_recs;
    int        format_version;
    int        digest_version;
    int        span;
    int        n_excl;
    int        excl[U2_DEMO_EXCLUDE_COUNT];
    char       engine[64];
    char       content[128];
    char       art[16];
    char       theme[40];
    int        enhanced;
    int        sector_hi, sector_lo;
    char       save_sha[65];
    char       checkpoint_sha[65];
    char       end_state_sha[65];
    char       end_frame_sha[65];
    uint8_t    checkpoint[U2_DEMO_SPAN];
    int        has_checkpoint;
    /* The `rolls=` the checkpoint line carried. The digest head embeds ctx->rng_rolls, so
     * a playback that restores the mirror but not this counter digests a different string
     * over identical bytes and diverges at every record. */
    uint32_t   ckpt_rolls;
    int        complete;              /* @end was seen */

    struct demo_overlay *overlay;
    int        n_overlay;
    int        cap_overlay;
    /* The § 13.3 world lines: the content-layer digest a viewer must match, plus the
     * cells this save changed. Carried ALONGSIDE `overlay` rather than replacing it, so
     * a v1 file still plays with v1 semantics and a v2 file is a superset of it. */
    struct demo_world *world;
    int        n_world;
    int        cap_world;
    /* How many `@world` lines write_header already put on disk. The header is written at
     * the checkpoint, which is the START of a session, and a walk visits sectors after
     * that moment; their lines go in the epilogue instead. Split, not moved: the header
     * position is what a v2 reader of a single-sector demo sees, and the framing digest
     * is a running hash over everything above `@end`, so lines emitted later are covered
     * by it without changing what the digest means. */
    int        n_world_written;
    int        overlay_failed;
    int        slot_note;             /* the slot-overlay caveat was printed once */

    /* ---- playback cursor ---- */
    int        cur_rec;
    int        cur_key;
    int        verified_through;      /* last record whose digest was checked */
    int        diverged;              /* first bad record index, -1 = none */
    int        applied;               /* checkpoint has been mounted */
    int        reported;
    int        closing_checked;       /* the @end state digest was compared */
    int        stop_at_exit;          /* the recording ended by quitting, not by toggle */
} u2_demo;

/* Both lists are heap-grown rather than fixed. The old cap of three was one sector's
 * three files, and a recording that crosses a sector boundary needs more. The old code
 * handled the excess by DROPPING it in silence, so a demo carrying four sectors shipped
 * three of them and still verified — a wrong world reported as a correct one, which is
 * the shape this project refuses over and over. Growth removes the failure mode instead
 * of reporting it more politely. */
static int overlay_reserve(u2_demo *d)
{
    if (d->n_overlay < d->cap_overlay) return 0;
    const int next = d->cap_overlay ? d->cap_overlay * 2 : 8;
    struct demo_overlay *p = realloc(d->overlay, sizeof *p * (size_t)next);
    if (!p) return -1;
    d->overlay = p;
    d->cap_overlay = next;
    return 0;
}

static int world_reserve(u2_demo *d)
{
    if (d->n_world < d->cap_world) return 0;
    const int next = d->cap_world ? d->cap_world * 2 : 8;
    struct demo_world *p = realloc(d->world, sizeof *p * (size_t)next);
    if (!p) return -1;
    d->world = p;
    d->cap_world = next;
    return 0;
}

/* ---- small helpers ------------------------------------------------------- */

static void hex64(const uint8_t in[32], char out[65])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2]     = digits[in[i] >> 4];
        out[i * 2 + 1] = digits[in[i] & 0x0F];
    }
    out[64] = '\0';
}

static int hex_ok(const char *s)
{
    if (!s || strlen(s) != 64) return 0;
    for (int i = 0; i < 64; i++) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

/* Pull `key=` out of a line. Returns NULL when absent; `val` receives the value up
 * to the next space.
 *
 * The word-boundary test is load-bearing, not tidiness: `sha=` must not be found
 * inside `save-sha=`, and a naive strstr would read the save digest as the record
 * digest and report a clean file as corrupt. */
static const char *field(const char *line, const char *key, char *val, size_t valsz)
{
    const size_t klen = strlen(key);
    const char *p = line;

    while ((p = strstr(p, key)) != NULL) {
        if ((p == line || p[-1] == ' ') && p[klen] == '=') {
            p += klen + 1;
            size_t i = 0;
            while (p[i] && p[i] != ' ' && p[i] != '\n' && i + 1 < valsz) {
                val[i] = p[i];
                i++;
            }
            val[i] = '\0';
            return val;
        }
        p += klen;
    }
    return NULL;
}

void u2_demo_digest(const u2_ctx *ctx, char out[65])
{
    unsigned char span[U2_DEMO_SPAN];
    char head[80];
    uint8_t d[32];
    sha256_ctx c;

    /* The whole mirror, with the clock-derived bytes blanked rather than skipped:
     * the exclusion is then six zeroed positions in one fixed-length buffer
     * instead of a variable-length one whose length depends on which offsets
     * happened to be excluded. D3's "the snapshot carries them and the comparison
     * excludes them", made literal. */
    memcpy(span, &ctx->state, sizeof span);
    for (int i = 0; i < U2_DEMO_EXCLUDE_COUNT; i++)
        span[U2_DEMO_EXCLUDE[i]] = 0;

    /* State only, not pixels, and that is the plan's scope rather than a shortcut:
     * D6 names the frozen CP2 digest, whose screen contribution is the cursor and a
     * write counter, not the grid. The grid cannot be in here because the checkpoint
     * cannot restore it — a demo carries DS:0000..064F and the text grid lives
     * outside the DS mirror — and a digest of something playback cannot reproduce is
     * a guaranteed false positive, which is worse than no check.
     *
     * What that costs, stated rather than hidden: hazard 2's missing restamp leaves
     * every DS byte agreeing while the frame shows no creatures. The creature table
     * IS in here, so the state that a restamp is derived from is verified; the stamp
     * itself is not, and only a frame comparison can see it. W7's digest check is
     * therefore a state proof and not a picture proof, and must not be cited as one. */
    sha256_init(&c);
    const int n = snprintf(head, sizeof head, "udemo-digest %d rolls=%u\n",
                           U2_DEMO_DIGEST_VERSION, ctx->rng_rolls);
    if (n > 0) sha256_update(&c, head, (size_t)n);
    sha256_update(&c, span, sizeof span);
    sha256_final(&c, d);
    hex64(d, out);
}

const char *u2_demo_mode_name(u2_demo_mode m)
{
    switch (m) {
    case U2_DEMO_MODE_TITLE:     return "title";
    case U2_DEMO_MODE_CREATION:  return "creation";
    case U2_DEMO_MODE_OVERWORLD: return "overworld";
    case U2_DEMO_MODE_SITE:      return "site";
    case U2_DEMO_MODE_DUNGEON:   return "dungeon";
    case U2_DEMO_MODE_SPACE:     return "space";
    }
    return "overworld";
}

static u2_demo_mode mode_from_name(const char *s)
{
    if (!strcmp(s, "creation"))  return U2_DEMO_MODE_CREATION;
    if (!strcmp(s, "site"))      return U2_DEMO_MODE_SITE;
    if (!strcmp(s, "dungeon"))   return U2_DEMO_MODE_DUNGEON;
    if (!strcmp(s, "space"))     return U2_DEMO_MODE_SPACE;
    if (!strcmp(s, "title"))     return U2_DEMO_MODE_TITLE;
    return U2_DEMO_MODE_OVERWORLD;
}

int u2_demo_mode_needs_world(u2_demo_mode m)
{
    switch (m) {
    case U2_DEMO_MODE_OVERWORLD:
    case U2_DEMO_MODE_SITE:
    case U2_DEMO_MODE_DUNGEON:
    case U2_DEMO_MODE_SPACE:
        return 1;
    default:
        return 0;
    }
}

/* ---- the recorder -------------------------------------------------------- */

static void emit(u2_demo *d, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* One line to disk and into the framing digest, from the same bytes. Two writers
 * of the same line is how an epilogue digest stops covering what it claims to. */
static void emit(u2_demo *d, const char *fmt, ...)
{
    char line[U2_DEMO_LINEBUFSZ];
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    /* vsnprintf returns the length it WOULD have written. Passing that straight to fwrite
     * reads off the end of `line` the moment a line outgrows the buffer, and the extra
     * bytes also went into the framing digest, so the file would carry a digest over stack
     * junk. Clamp, and say so: a truncated line is still a corrupt file, but it is a
     * corrupt file that announces itself instead of one that verifies. */
    size_t w = (size_t)n;
    if (w >= sizeof line) {
        fprintf(stderr, "u2: demo line exceeded %zu bytes and was truncated;"
                        " the recording is corrupt\n", sizeof line);
        w = sizeof line - 1;
    }
    fwrite(line, 1, w, d->file);
    sha256_update(&d->framing, line, w);
}

static void emit_hex_payload(u2_demo *d, const uint8_t *bytes, size_t n)
{
    /* Two leading spaces, because the format says a payload line is a payload line
     * (§ 4) and the parser dispatches on exactly that. Writing bare hex here produced
     * a file whose checkpoint the reader could not find. */
    char line[132];
    size_t i = 0;

    while (i < n) {
        size_t j = 2;
        line[0] = ' ';
        line[1] = ' ';
        for (size_t k = 0; k < 32 && i + k < n; k++)
            j += (size_t)sprintf(line + j, "%02x", bytes[i + k]);
        line[j++] = '\n';
        fwrite(line, 1, j, d->file);
        sha256_update(&d->framing, line, j);
        i += 32;
    }
}

static void keys_to_text(const u2_demo_rec *r, char *out, size_t outsz)
{
    size_t n = 0;

    *out = '\0';
    for (int i = 0; i < r->n_keys; i++) {
        const int w = snprintf(out + n, outsz - n, "%s%02x:%02x",
                               i ? "," : "", r->keys[i].ch, r->keys[i].scan);
        if (w <= 0 || (size_t)w >= outsz - n) break;
        n += (size_t)w;
    }
}

static void write_header(u2_ctx *ctx, u2_demo *d)
{
    char ver[32];
    char sha[65];

    u2_engine_version(ver, (int)sizeof ver);
    emit(d, "%s %d\n", U2_DEMO_MAGIC, U2_DEMO_VERSION);
    emit(d, "engine %s\n", ver);
    emit(d, "digest-version %d\n", U2_DEMO_DIGEST_VERSION);
    emit(d, "content %s\n", ctx->content_source ? ctx->content_source : "none");
    emit(d, "mode %s\n", u2_demo_mode_name(d->mode));
    emit(d, "sector %u%u\n", (unsigned)ctx->state.sector_first,
         (unsigned)ctx->state.sector_second);
    emit(d, "span %d\n", U2_DEMO_SPAN);
    emit(d, "exclude");
    for (int i = 0; i < U2_DEMO_EXCLUDE_COUNT; i++)
        emit(d, " %x", U2_DEMO_EXCLUDE[i]);
    emit(d, "\n");

    /* The record region of the checkpoint: the same 256 bytes the original's own
     * save writes (DS:36..0x135, core/save.h). Checked against the checkpoint at
     * load, which is what makes a corrupted checkpoint read as a broken file and
     * not as a demo that diverges at record 1 — hazard 4's two digests, two jobs. */
    sha256_hex((const unsigned char *)&ctx->state + U2_PLAYER_RECORD_DS,
               U2_DEMO_RECORD_BYTES, sha);
    emit(d, "save-sha %s\n", sha);

    for (int i = 0; i < d->n_overlay; i++)
        emit(d, "%s %s %s\n",
             d->overlay[i].from_slot ? "overlay-slot" : "overlay",
             d->overlay[i].name, d->overlay[i].sha);

    /* The § 13.3 world lines. Only the map carries a delta: § 13.2's proven property is
     * that the checkpoint's creature table drives the stamp, so creature state needs no
     * delta at all and monx is base-only. */
    for (int i = 0; i < d->n_world; i++)
        emit(d, "@world %s base=%s delta=%s\n",
             d->world[i].name, d->world[i].base, d->world[i].delta);
    d->n_world_written = d->n_world;

    emit(d, "art %s\n", ctx->art_mode == U2_ART_EGA ? "ega" : "cga");
    emit(d, "theme %s\n", (ctx->ega_theme && *ctx->ega_theme) ? ctx->ega_theme : "default");
    emit(d, "enhanced %d\n", ctx->enhanced_layout ? 1 : 0);
    emit(d, "\n");
}

static void write_checkpoint(u2_ctx *ctx, u2_demo *d)
{
    char sha[65];

    emit(d, "@checkpoint keys=%d rolls=%u sector=%u%u\n",
         ctx->keys_consumed, ctx->rng_rolls,
         (unsigned)ctx->state.sector_first, (unsigned)ctx->state.sector_second);
    emit_hex_payload(d, (const uint8_t *)&ctx->state, U2_DEMO_SPAN);
    sha256_hex((const unsigned char *)&ctx->state, U2_DEMO_SPAN, sha);
    emit(d, "@checkpoint-sha %s\n", sha);
}

static void write_record(u2_ctx *ctx, u2_demo *d, const u2_demo_rec *r)
{
    char keys[U2_DEMO_MAX_KEYS_PER_REC * 6 + 1];

    keys_to_text(r, keys, sizeof keys);
    emit(d, "@rec %d kind=%s keys=%s rolls=%u sha=%s\n",
         d->n_written + 1, r->kind ? "auto" : "key", keys, r->rolls, r->sha);

    if (ctx->demo_snapshots) {
        emit(d, "@snap %d\n", d->n_written + 1);
        emit_hex_payload(d, (const uint8_t *)&ctx->state, U2_DEMO_SPAN);
    }
    d->n_written++;
    fflush(d->file);      /* D8: a hard kill loses at most the open record */
}

/* The default filename: basic ISO 8601, so lexical order is chronological order
 * and the name is legal on a Windows filesystem (the engine ships `make sdl-win`,
 * and the extended form's colons are not). Host time, never ctx->clock, which is
 * virtual under a scripted run and would name every run the same file (hazard 7).
 */
static void default_demo_path(u2_ctx *ctx, char *out, size_t outsz)
{
    char dir[430];
    char stamp[32];
    time_t now = time(NULL);
    struct tm lt;

    if (ctx->demo_dir && *ctx->demo_dir)
        snprintf(dir, sizeof dir, "%.400s", ctx->demo_dir);
    else
        snprintf(dir, sizeof dir, "%s", "demos");

    if (ctx->demo_name && *ctx->demo_name) {
        snprintf(out, outsz, "%.429s/%.120s", dir, ctx->demo_name);
        if (!strstr(out, ".demo")) {
            const size_t n = strlen(out);
            if (n + 6 < outsz) snprintf(out + n, outsz - n, ".demo");
        }
        return;
    }

    localtime_r(&now, &lt);
    strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%S", &lt);
    snprintf(out, outsz, "%.429s/%.16s.demo", dir, stamp);

    /* Never overwrite what someone was about to share. A collision is unlikely at
     * one-second resolution on a single-instance engine and unrecoverable when it
     * happens, so the cheap check is worth it. */
    for (int n = 2; n < 100; n++) {
        FILE *probe = fopen(out, "r");
        if (!probe) break;
        fclose(probe);
        snprintf(out, outsz, "%.429s/%.16s-%d.demo", dir, stamp, n);
    }
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    return mkdir(path, 0755);
}

void u2_demo_begin_recording(u2_ctx *ctx, const char *path)
{
    u2_demo *d;

    if (ctx->demo) {
        if (u2_demo_state_of(ctx) == U2_DEMO_PLAYING)
            fprintf(stderr, "u2: refusing to record a playback. A demo of a demo "
                            "would carry two checkpoints and one honest explanation.\n");
        return;
    }
    d = calloc(1, sizeof *d);
    if (!d) return;
    d->state = U2_DEMO_IDLE;
    d->diverged = -1;
    ctx->demo = d;
    d->pending_start = 1;

    if (path && *path)
        snprintf(d->path, sizeof d->path, "%.599s", path);
    else
        default_demo_path(ctx, d->path, sizeof d->path);
    /* Announced at the press, not at the quiescent point, because the whole point of
     * the message is that the player knows to stop it. AGENTS.md § 3: an action the
     * player takes must produce a message, and a recording that started silently is
     * a recording nobody remembers to end. */
    fprintf(stderr, "u2: RECORDING - Ctrl-R TO STOP\n");
}

/* ---- the seam tap -------------------------------------------------------- */

static u2_demo *of(const u2_ctx *ctx) { return ctx->demo; }

int u2_demo_key(u2_ctx *ctx, u2_key *k)
{
    u2_demo *d = of(ctx);

    if (k->ch == U2_KEY_DEMO_TOGGLE) {
        const int at_boundary = ctx->demo_at_quiescent;
        const u2_demo_mode boundary_mode = (u2_demo_mode)ctx->demo_quiescent_mode;

        if (!d) {
            /* The first Ctrl-R, with nothing armed. This is the only way recording
             * starts: there is no --record flag, because the owner's spec is a key
             * press and a second way to do it is a second way to get it wrong. */
            u2_demo_begin_recording(ctx, NULL);
            d = of(ctx);
            if (!d) return 1;
            /* Honour it here and stop. Falling through would land in the "second press
             * cancels" arm below, because a freshly armed recorder has a pending start
             * and has not started — which is exactly the shape that arm looks for, and
             * it made the first Ctrl-R arm and cancel itself in the same breath. */
            d->toggles = 1;
            if (at_boundary)
                u2_demo_quiescent(ctx, boundary_mode, NULL);
            return 1;
        }
        d->toggles++;
        if (d->state == U2_DEMO_PLAYING) {
            /* Ctrl-R in playback stops it and does NOT start a recording, so a
             * player cannot accidentally record a playback. */
            fprintf(stderr, "u2: playback stopped at record %d of %d\n",
                    d->cur_rec, d->n_recs);
            d->cur_rec = d->n_recs;
            d->cur_key = 0;
            return 1;
        }
        if (d->state == U2_DEMO_RECORDING) {
            /* Idempotent under a double tap: a second Ctrl-R before the quiescent
             * point is the same request, not a stop-then-start. */
            if (!d->pending_stop) d->pending_stop = 1;
        } else if (!d->pending_start) {
            d->pending_start = 1;
        } else if (!d->started && d->toggles >= 2) {
            /* A second press before anything started cancels. Without this the
             * "Ctrl-R again cancels" promise on the deferred message was prose the
             * code did not keep, and a player who changed their mind had no way out
             * except reaching the world and stopping a recording they never wanted. */
            d->pending_start = 0;
            fprintf(stderr, "u2: recording cancelled\n");
        }
        /* If the caller is already standing at a boundary, honour the toggle now
         * rather than at the next one. Waiting costs the next key: the seam fetches
         * another key inside the same wait to replace the swallowed one, and that key
         * would be delivered before any quiescent call had a chance to run. */
        if (at_boundary)
            u2_demo_quiescent(ctx, boundary_mode, NULL);   /* falls back to ctx->demo_world */
        return 1;
    }

    if (!d) return 0;
    ctx->demo_at_quiescent = 0;   /* a key is in flight until the caller says otherwise */

    if (d->state == U2_DEMO_RECORDING && d->started) {
        u2_demo_rec r;
        memset(&r, 0, sizeof r);
        r.kind = 0;
        r.keys[0] = *k;
        r.n_keys = 1;
        r.rolls = ctx->rng_rolls;
        u2_demo_digest(ctx, r.sha);
        write_record(ctx, d, &r);
    }
    /* Playback needs nothing here: the verification is driven from
     * u2_demo_next_key, which is the only place that knows which record the key
     * about to be delivered came from. */
    return 0;
}

void u2_demo_quiescent(u2_ctx *ctx, u2_demo_mode mode, const u2_world *world)
{
    u2_demo *d = of(ctx);

    /* The boundary flags are set before the null test on purpose: the first Ctrl-R
     * arrives with no demo struct yet, and the recorder it creates has to know that
     * the press was already standing at a boundary a moment earlier. */
    ctx->demo_at_quiescent = 1;
    ctx->demo_quiescent_mode = (int)mode;
    if (world)
        ctx->demo_world = world;
    else
        world = (const u2_world *)ctx->demo_world;

    if (!d) return;

    /* A staged playback mounts here, once, at the first quiescent point of the flow
     * it names. See the header: this is the instant the recorder's checkpoint was
     * taken, so it is the only instant the two directions can be compared at. */
    if (d->state == U2_DEMO_PLAYING && !d->applied) {
        d->applied = 1;
        if (u2_state_load_ds_buf(&ctx->state, d->checkpoint, U2_DEMO_SPAN) != 0) {
            d->diverged = 0;
            d->cur_rec = d->n_recs;
            return;
        }
        /* The roll counter is NOT part of the DS mirror — ctx->rng_rolls is the engine's
         * own tally, and u2_rng_load resets it precisely because it is not original state.
         * But u2_demo_digest puts it in the head string, so a playback that restores 1616
         * bytes and leaves the tally at 0 hashes "rolls=0" over a recording made at
         * "rolls=14" and reports a divergence on every single record while every byte
         * agrees. Restoring it is the whole fix; the RNG itself needs no help, because its
         * shift register lives at DS:0x23F and the mirror restore already carried it.
         *
         * Measured, not inferred: hashing the recorded checkpoint span with rolls=0
         * reproduces the engine's reported live digest exactly, and with rolls=14 it
         * reproduces the recorded record-1 digest exactly. Nothing else differs. */
        ctx->rng_rolls = d->ckpt_rolls;
        if (ctx->demo_speed_pin) {
            /* D5: "not compared" is not "not used". DS:74 feeds the delay table via
             * asm/0711, so a playback that recalibrated it would diverge for a reason
             * that looks like a deep bug. The engine performs no startup calibration
             * today — speed.c is a model with no callers — so the pin's job here is to
             * name what is in force and to be the one place a future calibration has
             * to stand down at. */
            fprintf(stderr, "u2: speed pinned from the demo: DS:74=%04X DS:550=%04X\n",
                    (unsigned)ctx->state.speed, (unsigned)ctx->state.tick_low);
        }
        if (u2_demo_mode_needs_world(d->mode))
            ctx->demo_restamp_pending = 1;
    }

    if (d->state != U2_DEMO_IDLE) return;

    if (d->pending_start && !d->started) {
        char dir[430];
        const char *slash = strrchr(d->path, '/');

        /* pending_start is cleared only on the paths that actually start or abandon
         * the request. Clearing it first, which is what the first version did, made the
         * title deferral a lie: the note said it would start later and the flag was
         * already gone, so nothing ever started. */
        if (mode == U2_DEMO_MODE_TITLE) {
            /* Deferred, not refused. The way into play goes through title-flow waits —
             * the menu, the post-load "press any key" — so a player who hits Ctrl-R on
             * the way in is standing at a title boundary while genuinely intending to
             * record a session. Refusing there made the real flow unusable: a scripted
             * `P` then Ctrl-R never produced a file at all. The toggle stays pending and
             * takes effect at the first boundary that playback can resume into, which is
             * what D12's "next quiescent point" always meant. */
            if (!d->told_title) {
                d->told_title = 1;
                fprintf(stderr, "u2: recording will start when you reach the world. "
                                "Ctrl-R again cancels.\n");
            }
            return;
        }
        if (mode == U2_DEMO_MODE_CREATION) {
            /* Refused, and the reason is a measurement rather than a guess: the form's
             * cursor — which field is highlighted, which dropdown is open — lives in a
             * local struct and not in the DS mirror, so a checkpoint taken mid-form
             * names a state playback cannot resume and the digests diverge a few keys
             * in. That is D12's quiescence rule with a wider reach than
             * ow->pending_cmd: the engine keeps scratch outside the mirror in more
             * places than the overworld's half-typed command. Fixing it means moving
             * the form's state into DS or into a second checkpoint block, which is a
             * change to the save model and not a detail of the demo. It is written up
             * in docs/demo-record-playback.md rather than left as a silent gap. */
            fprintf(stderr, "u2: not recording: character creation keeps its place in "
                            "engine state the checkpoint cannot carry, so playback "
                            "could not resume here. Start the recording in the world.\n");
            d->pending_start = 0;
            return;
        }
        d->pending_start = 0;
        if (slash) {
            size_t n = (size_t)(slash - d->path);
            if (n >= sizeof dir) n = sizeof dir - 1;
            memcpy(dir, d->path, n);
            dir[n] = '\0';
            if (n > 0 && ensure_dir(dir) != 0) {
                fprintf(stderr, "u2: cannot create demo directory '%s'\n", dir);
                ctx->demo = NULL;
                free(d);
                return;
            }
        }
        d->file = fopen(d->path, "w");
        if (!d->file) {
            fprintf(stderr, "u2: cannot open demo file '%s'\n", d->path);
            ctx->demo = NULL;
            free(d);
            return;
        }
        d->mode = mode;
        d->state = U2_DEMO_RECORDING;
        d->stopped_by_toggle = 0;
        /* The order is the point: note_world captures only while a recording is armed
         * and not yet written, so the sector digests have to be taken before `started`
         * goes up or the header is written with no overlay lines at all — which is
         * what the first version did, and it looked like a working demo. */
        if (world)
            u2_demo_note_world(ctx, world);
        d->started = 1;
        sha256_init(&d->framing);
        write_header(ctx, d);
        write_checkpoint(ctx, d);
        fflush(d->file);
        /* stderr, never the game screen: the per-record digest covers the text grid,
         * so a line only the recorder printed would make the first record mismatch.
         * This is the same channel and the same reason as the Ctrl-S notice. */
        fprintf(stderr, "u2: recording to %s\n", d->path);
        return;
    }

    if (d->pending_stop && d->state == U2_DEMO_RECORDING) {
        d->pending_stop = 0;
        d->stopped_by_toggle = 1;
        u2_demo_finish(ctx);
    }
}

int u2_demo_mark_auto_pass(u2_ctx *ctx)
{
    u2_demo *d = of(ctx);
    u2_demo_rec r;

    if (!d || d->state != U2_DEMO_RECORDING || !d->started) return 0;
    memset(&r, 0, sizeof r);
    r.kind = 1;
    r.rolls = ctx->rng_rolls;
    u2_demo_digest(ctx, r.sha);
    write_record(ctx, d, &r);
    return 1;
}

/* ---- the parser ---------------------------------------------------------- */

static int rec_push(u2_demo *d, const u2_demo_rec *r)
{
    if (d->n_recs >= d->cap_recs) {
        const int cap = d->cap_recs ? d->cap_recs * 2 : 256;
        u2_demo_rec *grown = realloc(d->recs, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        d->recs = grown;
        d->cap_recs = cap;
    }
    d->recs[d->n_recs++] = *r;
    return 0;
}

static int parse_keys_list(const char *s, u2_demo_rec *r)
{
    const char *p = s;

    if (!s || !*s) return 0;
    while (*p && r->n_keys < U2_DEMO_MAX_KEYS_PER_REC) {
        unsigned ch = 0, sc = 0;
        if (sscanf(p, "%2x:%2x", &ch, &sc) != 2) return -1;
        r->keys[r->n_keys].ch = (uint8_t)ch;
        r->keys[r->n_keys].scan = (uint8_t)sc;
        r->n_keys++;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
    return 0;
}

static int parse_hex_payload_line(const char *s, uint8_t *dst, size_t cap, size_t *n)
{
    size_t i = 0;

    while (s[i] && s[i] != '\n' && s[i] != ' ' && s[i] != '#') {
        unsigned v;
        char pair[3];
        pair[0] = s[i];
        pair[1] = s[i + 1];
        pair[2] = '\0';
        if (!pair[1] || sscanf(pair, "%2x", &v) != 1) return -1;
        if (*n >= cap) return -1;
        dst[(*n)++] = (uint8_t)v;
        i += 2;
    }
    return 0;
}

static int refuse_playback(u2_demo *d, const char *why)
{
    fprintf(stderr, "u2: %s\n", why);
    free(d->recs);
    free(d->overlay);
    free(d->world);
    free(d);
    return -1;
}

/* Split a header line into `<key> <value>`. The format is space-separated (the
 * § 4 example is the format; the prose's "key=value" describes the @rec lines,
 * which do use `=` and are parsed by field() above). Returns 0 when the line has
 * no space, and strips the trailing newline and any trailing comment. */
static int split_header(const char *line, char *key, size_t keysz, char *val, size_t valsz)
{
    const char *sp = strchr(line, ' ');
    if (!sp) return -1;

    size_t n = (size_t)(sp - line);
    if (n == 0 || n >= keysz) return -1;
    memcpy(key, line, n);
    key[n] = '\0';

    const char *v = sp + 1;
    while (*v == ' ') v++;
    n = strlen(v);
    while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r')) n--;
    const char *hash = memchr(v, '#', n);
    if (hash) n = (size_t)(hash - v);
    while (n > 0 && v[n - 1] == ' ') n--;
    if (n >= valsz) n = valsz - 1;
    memcpy(val, v, n);
    val[n] = '\0';
    return 0;
}

int u2_demo_begin_playback(u2_ctx *ctx, const char *path)
{
    u2_demo *d = calloc(1, sizeof *d);
    FILE *f;
    char line[4096];
    /* Must hold a whole `@world` value: name + ` base=` + 64 hex + ` delta=` + a full
     * delta. It was 128, which silently cut a real six-cell delta mid-token at 127
     * characters — the reader then applied three cells, choked on the fragment "35,23",
     * and refused a perfectly good demo. split_header truncates rather than failing, so
     * the only defence is a buffer that cannot overflow and a completeness check below. */
    char val[U2_DEMO_DELTA_MAX + 128];
    size_t ck_n = 0;
    int in_checkpoint = 0;
    sha256_ctx framing;
    uint8_t raw[32];
    char frame_sha[65];

    if (!d) return -1;
    d->diverged = -1;
    d->state = U2_DEMO_PLAYING;
    snprintf(d->path, sizeof d->path, "%.599s", path ? path : "");
    sha256_init(&framing);

    f = fopen(path, "r");
    if (!f) {
        free(d);
        fprintf(stderr, "u2: cannot open demo '%s'\n", path ? path : "(null)");
        return -1;
    }

    while (fgets(line, (int)sizeof line, f)) {
        const int is_end = !strncmp(line, "@end", 4);

        /* Everything above @end, byte for byte, in the order the writer hashed it,
         * and hashed BEFORE the dispatch below, because every branch of the dispatch
         * `continue`s. Without this the framing digest is decoration and an edited
         * record digest reads as an engine divergence — hazard 4's whole point, and
         * the reason `@checkpoint-sha` exists separately for the payload. */
        if (!is_end) sha256_update(&framing, line, strlen(line));

        if (line[0] == '#') continue;

        if (line[0] == ' ' && line[1] == ' ') {
            /* A payload line: the checkpoint's, or a @snap's, which the reader
             * ignores by design (D7 — snapshots are a debugging lever, not data). */
            if (in_checkpoint &&
                parse_hex_payload_line(line + 2, d->checkpoint, sizeof d->checkpoint,
                                       &ck_n) != 0) {
                fclose(f);
                return refuse_playback(d, "malformed checkpoint payload");
            }
            continue;
        }

        if (!strncmp(line, U2_DEMO_MAGIC " ", strlen(U2_DEMO_MAGIC) + 1)) {
            d->format_version = atoi(line + strlen(U2_DEMO_MAGIC) + 1);
            in_checkpoint = 0;
            continue;
        }
        if (!strncmp(line, "@checkpoint-sha ", 16)) {
            if (sscanf(line + 16, "%127s", val) == 1)
                snprintf(d->checkpoint_sha, sizeof d->checkpoint_sha, "%.64s", val);
            in_checkpoint = 0;
            if (ck_n != (size_t)U2_DEMO_SPAN) {
                fclose(f);
                fprintf(stderr, "u2: demo '%s': checkpoint is %zu bytes, the format "
                                "names %d\n", path, ck_n, U2_DEMO_SPAN);
                free(d->recs);
                free(d);
                return -1;
            }
            d->has_checkpoint = 1;
            continue;
        }
        if (!strncmp(line, "@checkpoint ", 12)) {
            in_checkpoint = 1;
            if (field(line, "sector", val, sizeof val) && strlen(val) == 2) {
                d->sector_hi = val[0] - '0';
                d->sector_lo = val[1] - '0';
            }
            if (field(line, "rolls", val, sizeof val))
                d->ckpt_rolls = (uint32_t)strtoul(val, NULL, 10);
            continue;
        }
        if (!strncmp(line, "@snap ", 6)) { in_checkpoint = 0; continue; }
        if (!strncmp(line, "@rec ", 5)) {
            u2_demo_rec r;
            memset(&r, 0, sizeof r);
            in_checkpoint = 0;
            r.kind = (field(line, "kind", val, sizeof val) && !strcmp(val, "auto")) ? 1 : 0;
            if (field(line, "rolls", val, sizeof val))
                r.rolls = (uint32_t)strtoul(val, NULL, 10);
            if (field(line, "keys", val, sizeof val) && parse_keys_list(val, &r) != 0) {
                fclose(f);
                fprintf(stderr, "u2: demo '%s': malformed keys list '%s'\n", path, val);
                free(d->recs);
                free(d);
                return -1;
            }
            if (!field(line, "sha", val, sizeof val) || !hex_ok(val)) {
                fclose(f);
                fprintf(stderr, "u2: demo '%s': a record carries no digest\n", path);
                free(d->recs);
                free(d);
                return -1;
            }
            snprintf(r.sha, sizeof r.sha, "%.64s", val);
            if (rec_push(d, &r) != 0) { fclose(f); goto oom; }
            continue;
        }
        if (!strncmp(line, "@end", 4)) {
            d->complete = 1;
            if (field(line, "state", val, sizeof val) && hex_ok(val))
                snprintf(d->end_state_sha, sizeof d->end_state_sha, "%.64s", val);
            if (field(line, "stop", val, sizeof val))
                d->stop_at_exit = strcmp(val, "toggle") != 0;
            if (field(line, "sha", val, sizeof val) && hex_ok(val))
                snprintf(d->end_frame_sha, sizeof d->end_frame_sha, "%.64s", val);
            continue;               /* the epilogue is not inside its own digest */
        }

        /* header: `<key> <value>`. Dispatch on the key itself rather than searching
         * the line, because a substring search found `sha` inside `save-sha` and
         * `art` inside any word that happened to carry it — the same class of defect
         * the @rec field scanner had to be written around. */
        {
            char key[24];
            if (split_header(line, key, sizeof key, val, sizeof val) != 0) continue;
            if (!strcmp(key, "digest-version")) {
                d->digest_version = atoi(val);
            } else if (!strcmp(key, "engine")) {
                snprintf(d->engine, sizeof d->engine, "%.63s", val);
            } else if (!strcmp(key, "content")) {
                snprintf(d->content, sizeof d->content, "%.127s", val);
            } else if (!strcmp(key, "mode")) {
                d->mode = mode_from_name(val);
            } else if (!strcmp(key, "sector") && strlen(val) == 2) {
                d->sector_hi = val[0] - '0';
                d->sector_lo = val[1] - '0';
            } else if (!strcmp(key, "span")) {
                d->span = atoi(val);
            } else if (!strcmp(key, "exclude")) {
                const char *p = line + strlen("exclude") + 1;
                d->n_excl = 0;
                for (;;) {
                    unsigned v;
                    while (*p == ' ') p++;
                    if (!*p || *p == '\n' || *p == '#' ||
                            d->n_excl >= U2_DEMO_EXCLUDE_COUNT) break;
                    if (sscanf(p, "%2x", &v) != 1) break;
                    d->excl[d->n_excl++] = (int)v;
                    p += 2;
                }
            } else if (!strcmp(key, "save-sha") && hex_ok(val)) {
                snprintf(d->save_sha, sizeof d->save_sha, "%.64s", val);
            } else if (!strcmp(key, "overlay") || !strcmp(key, "overlay-slot")) {
                char nm[24];
                char sh[80];
                if (overlay_reserve(d) != 0)
                    return refuse_playback(d, "out of memory parsing the demo");
                if (sscanf(val, "%23s %79s", nm, sh) == 2 && hex_ok(sh)) {
                    snprintf(d->overlay[d->n_overlay].name, 24, "%.23s", nm);
                    snprintf(d->overlay[d->n_overlay].sha, 65, "%.64s", sh);
                    d->overlay[d->n_overlay].from_slot = !strcmp(key, "overlay-slot");
                    d->n_overlay++;
                }
            } else if (!strcmp(key, "@world")) {
                /* `@world mapx20 base=<sha64> delta=<cells|none|too-big>`.
                 * Parsed with strstr rather than one wide sscanf so an over-long delta
                 * cannot walk off the buffer, and so a malformed line is dropped rather
                 * than half-accepted: a half-parsed delta would apply partially and then
                 * verify, which is the worst possible outcome. */
                const char *bp = strstr(val, " base=");
                const char *dp = strstr(val, " delta=");
                char sh[80];
                char nm[24];
                if (world_reserve(d) != 0)
                    return refuse_playback(d, "out of memory parsing the demo");
                /* The name is the FIRST TOKEN of val, not 23 characters of it. A fixed
                 * slice stored [mapx70 base=0428859797292] as the name, so no sector ever
                 * matched its own line and every delta was silently treated as absent.
                 * That failure is invisible by construction, because having no delta for a
                 * sector is the normal case for a clean install. */
                if (bp && dp && sscanf(val, "%23s", nm) == 1
                        && sscanf(bp + 6, "%79s", sh) == 1 && hex_ok(sh)) {
                    snprintf(d->world[d->n_world].name, 24, "%.23s", nm);
                    snprintf(d->world[d->n_world].base, 65, "%.64s", sh);
                    snprintf(d->world[d->n_world].delta, U2_DEMO_DELTA_MAX,
                             "%.2047s", dp + 7);
                    /* Completeness, not tidiness. Every cell the writer emits ends with
                     * ';', so a value that stops anywhere else was cut short somewhere —
                     * by a buffer, by an edit, or by a truncation nobody noticed. Applying
                     * the intact prefix of a delta is the worst available outcome: a
                     * partially rebuilt sector, and the per-cell byte check is the only
                     * thing that would ever have caught it. Refuse the file instead. */
                    {
                        const char *dv = d->world[d->n_world].delta;
                        const size_t dl = strlen(dv);
                        if (dl != 0 && strcmp(dv, "none") && strcmp(dv, "too-big")
                                && dv[dl - 1] != ';') {
                            return refuse_playback(d,
                                "a @world delta stops mid-cell: this file is truncated or"
                                " was edited after it was written, so no sector in it can"
                                " be rebuilt safely");
                        }
                    }
                    d->n_world++;
                }
            } else if (!strcmp(key, "art")) {
                snprintf(d->art, sizeof d->art, "%.15s", val);
            } else if (!strcmp(key, "theme")) {
                snprintf(d->theme, sizeof d->theme, "%.39s", val);
            } else if (!strcmp(key, "enhanced")) {
                d->enhanced = atoi(val);
            }
        }
    }
    fclose(f);

    /* --- refusals, in the order a reader would want them named --- */
    if (d->format_version < 1 || d->format_version > U2_DEMO_VERSION) {
        /* A range, not an equality. § 13.5 rules that cross-version playback is a
         * supported use case and a v2 reader must still play a v1 file — v1 files simply
         * carry no @world lines, which means "nothing to apply", the correct v1
         * semantics. Refusing on inequality here would have made the bump destructive. */
        char msg[160];
        snprintf(msg, sizeof msg, "demo '%s': format version %d, this engine reads 1..%d",
                 path, d->format_version, U2_DEMO_VERSION);
        return refuse_playback(d, msg);
    }
    if (d->mode == U2_DEMO_MODE_TITLE || d->mode == U2_DEMO_MODE_CREATION) {
        char msg[200];
        snprintf(msg, sizeof msg, "demo '%s': start mode '%s' cannot be resumed. "
                                  "Playback enters the recorded mode's own flow, and "
                                  "that mode's position is held in engine scratch the "
                                  "checkpoint does not carry.",
                 path, u2_demo_mode_name(d->mode));
        return refuse_playback(d, msg);
    }
    if (!d->has_checkpoint) {
        char msg[160];
        snprintf(msg, sizeof msg,
                 "demo '%s': no checkpoint, so there is no starting state", path);
        return refuse_playback(d, msg);
    }
    if (!d->complete && !ctx->demo_salvage) {
        fprintf(stderr, "u2: demo '%s' has no @end: it is an interrupted recording of "
                        "%d records. Play it with --salvage if that is what you want.\n",
                path, d->n_recs);
        free(d->recs);
        free(d);
        return -1;
    }
    if (!d->complete && ctx->demo_salvage) {
        fprintf(stderr, "u2: salvaging '%s': %d records present, no epilogue. "
                        "Whatever was in flight when it stopped is gone.\n",
                path, d->n_recs);
    }

    if (d->complete && *d->end_frame_sha) {
        sha256_final(&framing, raw);
        hex64(raw, frame_sha);
        if (strcmp(frame_sha, d->end_frame_sha) != 0) {
            fprintf(stderr, "u2: demo '%s': the file does not match its own @end digest. "
                            "Something was edited or truncated after it was written, so "
                            "nothing in it can be trusted.\n", path);
            free(d->recs);
            free(d);
            return -1;
        }
    }

    /* The checkpoint's own integrity, before anything else (hazard 4). A corrupted
     * checkpoint must read as a broken file, not as a demo that diverges at record 1. */
    {
        char sha[65], rec_sha[65];
        sha256_hex(d->checkpoint, U2_DEMO_SPAN, sha);
        if (*d->checkpoint_sha && strcmp(sha, d->checkpoint_sha) != 0) {
            char msg[200];
            snprintf(msg, sizeof msg, "demo '%s': the checkpoint does not match its own "
                                      "@checkpoint-sha; the file is corrupt", path);
            return refuse_playback(d, msg);
        }
        sha256_hex(d->checkpoint + U2_PLAYER_RECORD_DS, U2_DEMO_RECORD_BYTES, rec_sha);
        if (*d->save_sha && strcmp(rec_sha, d->save_sha) != 0) {
            char msg[200];
            snprintf(msg, sizeof msg, "demo '%s': the checkpoint's player record does not "
                                      "match save-sha; the file is corrupt", path);
            return refuse_playback(d, msg);
        }
    }

    if (d->digest_version != U2_DEMO_DIGEST_VERSION) {
        /* D13: not a refusal. The stimulus is a checkpoint plus keys and neither
         * depends on the engine version, which is exactly what makes an old demo
         * useful for confirming a bug was fixed. What is impossible is VERIFYING it:
         * the digest scope is frozen per version, so comparing across versions is
         * comparing two different functions. */
        fprintf(stderr, "u2: demo '%s': digest scope %d, this engine is scope %d — "
                        "PLAYBACK UNVERIFIED. It will play; nothing is being checked.\n",
                path, d->digest_version, U2_DEMO_DIGEST_VERSION);
    }

    /* Art is a note, never a refusal (§ 4's correction), and the note is deferred to
     * u2_demo_finish because `content_source` is not known until the title flow has
     * run — it comes from the generated header and is stamped there, not here. */

    ctx->demo = d;
    return 0;

oom:
    return refuse_playback(d, "out of memory parsing the demo");
}

/* ---- playback ------------------------------------------------------------ */

static void verify_record(u2_ctx *ctx, u2_demo *d, int idx)
{
    char now[65];

    if (idx < 0 || idx >= d->n_recs) return;
    if (d->digest_version != U2_DEMO_DIGEST_VERSION) return;   /* D13: unverified */
    if (d->diverged >= 0 && !ctx->demo_play_continue) return;

    /* The pairing is record N against the state at the instant key N is taken, not
     * the instant after it. The plan's D12 words it the other way round ("before
     * delivering key N+1, checks the live digest against N's recorded digest"), and
     * that reading cannot work: a record holds the state BEFORE its own key, so at
     * the tap for N+1 the live state is the state AFTER N and the two differ by
     * exactly one turn. Verified against its own record, the comparison is the same
     * instant on both sides, and the divergence lands on the key that caused it
     * before that key has been applied — which is finer than the plan asked for.
     *
     * What is still unverified by any record is the state the LAST record produced,
     * and that is what @end's `state=` digest covers (D12's first consequence). */
    u2_demo_digest(ctx, now);
    if (strcmp(now, d->recs[idx].sha) == 0) return;

    d->diverged = idx;
    fprintf(stderr, "u2: DEMO DIVERGED at record %d (kind=%s, %d key%s, rolls=%u): "
                    "recorded %s, live %s\n",
            idx + 1, d->recs[idx].kind ? "auto" : "key", d->recs[idx].n_keys,
            d->recs[idx].n_keys == 1 ? "" : "s", d->recs[idx].rolls,
            d->recs[idx].sha, now);
    if (!ctx->demo_play_continue) {
        /* The plan asks for the field diff from tools/replay_diff.py here. That
         * differ is listed as partial in § 5 and its alignment work is a separate
         * item, so the engine names the tool rather than growing a second differ in
         * C — and `engine/tools/demo.py diff` is the one that reads a demo. */
        fprintf(stderr, "u2: re-run with --playback-continue to see every divergence, or\n"
                        "u2:   engine/tools/demo.py diff <demo> --against <run>\n");
    }
}

int u2_demo_auto_pass_due(u2_ctx *ctx)
{
    u2_demo *d = of(ctx);

    if (!d || d->state != U2_DEMO_PLAYING) return 0;
    if (d->cur_rec >= d->n_recs) return 0;
    if (d->recs[d->cur_rec].kind != 1) return 0;

    verify_record(ctx, d, d->cur_rec);
    d->verified_through = d->cur_rec;
    d->cur_rec++;
    return 1;
}

int u2_demo_next_key(u2_ctx *ctx, u2_key *out)
{
    u2_demo *d = of(ctx);
    u2_demo_rec *r;

    if (!d || d->state != U2_DEMO_PLAYING) return -1;
    if (d->cur_rec >= d->n_recs) {
        /* The closing digest (D12's first consequence), and it is compared HERE — at
         * the poll after the last record, which is after the frame has been drawn —
         * because that is where the recorder was standing when it wrote @end: the
         * stop toggle arrives at a poll, one draw after the last recorded key. An
         * earlier version compared it at the next quiescent point, which is BEFORE
         * the draw, and the two differed by exactly one frame's worth of cursor
         * movement. That is the whole lesson: the two directions have to be compared
         * at the same point in the loop, not merely at the same loop iteration. */
        if (d->applied && !d->closing_checked && !d->stop_at_exit) {
            char now[65];
            d->closing_checked = 1;
            if (*d->end_state_sha && d->digest_version == U2_DEMO_DIGEST_VERSION) {
                u2_demo_digest(ctx, now);
                if (strcmp(now, d->end_state_sha) != 0) {
                    d->diverged = d->n_recs;
                    fprintf(stderr, "u2: DEMO DIVERGED after the last record: "
                                    "recorded %s, live %s\n", d->end_state_sha, now);
                }
            }
        }
        return -1;
    }
    if (d->diverged >= 0 && !ctx->demo_play_continue) return -1;

    r = &d->recs[d->cur_rec];
    if (r->kind == 1) return 0;            /* the loop owes a PASS, not a key */
    if (d->cur_key == 0) {
        verify_record(ctx, d, d->cur_rec);
        d->verified_through = d->cur_rec;
    }
    *out = r->keys[d->cur_key++];
    if (d->cur_key >= r->n_keys) { d->cur_rec++; d->cur_key = 0; }
    return 1;
}

int u2_demo_preload_sector(const u2_ctx *ctx, int *sector_hi, int *sector_lo)
{
    const u2_demo *d = of(ctx);

    if (!d || d->state != U2_DEMO_PLAYING) return 0;
    if (!u2_demo_mode_needs_world(d->mode)) return 0;
    if (sector_hi) *sector_hi = d->sector_hi;
    if (sector_lo) *sector_lo = d->sector_lo;
    return 1;
}

u2_demo_mode u2_demo_playback_mode(const u2_ctx *ctx)
{
    const u2_demo *d = of(ctx);
    return d ? d->mode : U2_DEMO_MODE_OVERWORLD;
}

/* The filename a sector's bytes came from, spelled the way the loader found it, so
 * the name in the header is the name a player can go and look at. */
static void sector_name(char *out, size_t outsz, const char *prefix,
                        unsigned hi, unsigned lo)
{
    snprintf(out, outsz, "%.8s%u%u", prefix ? prefix : "mapx", hi & 0x0Fu, lo & 0x0Fu);
}

/* The delta between two RAW sector images, in the canonical spelling § 13.3 requires.
 *
 * Canonical order is not cosmetics. D7 chose text so demos would be diffable, and an
 * unordered delta makes two recordings of one session differ in the file while being
 * identical in fact — which turns every later comparison into a judgement call.
 *
 * Returns the cell count, or -1 if the delta does not fit. Both sides must be file
 * bytes: diffing against `map_base` would compare a stamped, spawn-cleared pipeline
 * against a file, and would tie every demo to whichever restore mechanism the engine
 * happens to implement. That is the § 13.11 correction that killed the map_base digest.
 */
static int world_delta(const uint8_t *base, const uint8_t *now, size_t n,
                       char *out, size_t outsz)
{
    size_t used = 0;
    int cells = 0;

    if (!out || !outsz) return -1;
    out[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (base[i] == now[i]) continue;
        /* "63,63:ff->fe;" is 13 characters worst case, and gcc's truncation check only
         * believes the arithmetic when the bound is written out. */
        if (used + 14 >= outsz) { out[0] = '\0'; return -1; }
        used += (size_t)snprintf(out + used, outsz - used, "%u,%u:%02x->%02x;",
                                 (unsigned)(i % U2_SECTOR_EDGE),
                                 (unsigned)(i / U2_SECTOR_EDGE),
                                 base[i], now[i]);
        cells++;
    }
    return cells;
}

void u2_demo_note_world(u2_ctx *ctx, const u2_world *w)
{
    u2_demo *d = of(ctx);
    char sha[65], name[24];

    if (!d || !w) return;

    struct src {
        const char *prefix; unsigned hi, lo; const uint8_t *bytes; size_t n;
    } f[3];

    f[0].prefix = w->map_prefix;  f[0].hi = w->map_id.hi;  f[0].lo = w->map_id.lo;
    f[0].bytes  = w->map_base;    f[0].n  = sizeof w->map_base;
    f[1].prefix = w->mon_prefix;  f[1].hi = w->mon_id.hi;  f[1].lo = w->mon_id.lo;
    f[1].bytes  = w->mon;         f[1].n  = sizeof w->mon;
    f[2].prefix = w->tlkx_prefix; f[2].hi = w->tlkx_id.hi; f[2].lo = w->tlkx_id.lo;
    f[2].bytes  = w->tlkx;        f[2].n  = sizeof w->tlkx;

    for (int i = 0; i < 3; i++) {
        /* Only a file that was actually read. The guard is not tidiness: an absent
         * tlkx leaves `tlkx_prefix` NULL and `tlkx_id` at 00, and the fallback spelling
         * in sector_name() then produced "mapx00" for the dialogue buffer — so a
         * sector with no tlkx reported that its own map file had changed. A name that
         * is a guess must never be checked against a digest. */
        if (!f[i].prefix || !f[i].bytes || f[i].n == 0) continue;
        if (i == 0 && !w->map_loaded) continue;
        if (i == 1 && !w->mon_loaded) continue;
        if (i == 2 && !w->tlkx_loaded) continue;

        sector_name(name, sizeof name, f[i].prefix, f[i].hi, f[i].lo);
        sha256_hex(f[i].bytes, f[i].n, sha);

        if (d->state == U2_DEMO_PLAYING) {
            /* Once per FILE, not once per run. `mon` is the LIVE creature table and its
             * hp bytes move in play, so re-checking it every frame would report that the
             * content changed the first time anything was wounded — hence a latch. But
             * the latch used to be global, so the one run-wide check bought whichever
             * sector loaded first and every later sector went unchecked in silence. The
             * flag now sits on the entry: each file is compared exactly once, and no
             * sector is skipped because an earlier one got there first. */
            for (int k = 0; k < d->n_overlay; k++) {
                if (strcmp(d->overlay[k].name, name) != 0) continue;
                if (d->overlay[k].checked) continue;
                d->overlay[k].checked = 1;
                if (d->overlay[k].from_slot) {
                    if (!d->slot_note) {
                        d->slot_note = 1;
                        fprintf(stderr, "u2: this recording started from its own save's "
                                        "overlay of %s, which a viewer cannot be expected "
                                        "to have. Not a refusal; if the digests diverge, "
                                        "that is why.\n", name);
                    }
                    continue;
                }
                if (strcmp(d->overlay[k].sha, sha) != 0 && !d->overlay_failed) {
                    if (ctx->demo_ignore_content) {
                        fprintf(stderr, "u2: %s differs from the bytes this recording "
                                        "carried, and --playback-ignore-content was "
                                        "given. Playing anyway: if a digest diverges, "
                                        "that may be the content and not the engine.\n",
                                name);
                        continue;
                    }
                    d->overlay_failed = 1;
                    fprintf(stderr, "u2: this demo needs %s to hold the bytes it recorded; "
                                    "yours differ. Point --data-dir at the content the "
                                    "recording used, or remove the slot's overlay of it.\n",
                            name);
                }
            }
        } else if (d->state == U2_DEMO_RECORDING && !d->started) {
            /* Captured once, for the checkpoint. A sector the recording later mutates
             * is covered by the checkpoint itself, which carries the whole DS mirror;
             * this line is about the bytes the world was BUILT from.
             *
             * Which LAYER it was built from decides whether the line can be a refusal,
             * and getting that wrong made a real recording on a real install refuse to
             * play with "this demo needs mapx20 to hold the bytes it recorded; yours
             * differ" — while both sides were telling the truth. The recording read
             * mapx20 out of its own save slot, because play writes sector overlays
             * there; playback deliberately cannot see that slot, because § 7.4 sends
             * its writes to scratch so a viewer's saves survive. Two honest runs,
             * different layers, a false refusal.
             *
             * So: a content-layer digest is a refusal, because that is the question
             * the check was built to answer — "does the viewer have the same release
             * content". A slot-layer digest is a note, because no viewer can be
             * expected to have the recorder's save, and refusing on it would make
             * every demo recorded after the first sector write-back unplayable. */
            const int from_slot =
                (i == 0 && w->map_root && strcmp(w->map_root, U2_WORLD_SLOT_LAYER) == 0) ||
                (i == 1 && w->mon_root && strcmp(w->mon_root, U2_WORLD_SLOT_LAYER) == 0);
            /* tlkx is never in the test above on purpose: dialogue is read-only, the
             * original never writes it home, so a slot can never hold one and its
             * digest is always a content digest. */
            int seen = 0;
            for (int k = 0; k < d->n_overlay; k++)
                if (strcmp(d->overlay[k].name, name) == 0) { seen = 1; break; }
            if (!seen && overlay_reserve(d) == 0) {
                snprintf(d->overlay[d->n_overlay].name, 24, "%.23s", name);
                snprintf(d->overlay[d->n_overlay].sha, 65, "%.64s", sha);
                d->overlay[d->n_overlay].from_slot = from_slot;
                d->n_overlay++;
            }
        }
    }

    /* The § 13.3 world lines, recording side.
     *
     * Both sides of the delta are RAW FILE BYTES read off disk, not the world's copy.
     * By now w->map has been stamped and w->map_base spawn-cleared, and a diff against
     * either would compare two pipelines rather than two worlds — and would tie every
     * demo to whichever restore mechanism the engine happens to implement. The base is
     * always the content layer, because that is the one thing a viewer is expected to
     * already have, which is what makes the § 13.1 refusal satisfiable again.
     *
     * Only the map gets a delta. § 13.2's proven property is that the checkpoint's
     * creature table drives the stamp, so creature state needs no delta and monx is
     * base-only. That is the whole reason the delta stays a handful of bytes and the
     * demo stays a text file.
     */
    if (d->state != U2_DEMO_PLAYING && w->map_loaded) {
        static uint8_t content[U2_SECTOR_TILES], over[U2_SECTOR_TILES];
        char nm[24] = "";
        char delta[U2_DEMO_DELTA_MAX];
        const char *dtxt = "none";

        if (u2_world_read_content(ctx, 1, w->map_id, content, sizeof content,
                                  nm, sizeof nm) == U2_WORLD_OK) {
            /* One line per sector file, not one per visit. note_world now runs at every
             * sector load, so a recording that steps out and back in would otherwise
             * repeat a sector and grow the header with the walk. */
            int seen = 0;
            for (int k = 0; k < d->n_world; k++)
                if (strcmp(d->world[k].name, nm) == 0) { seen = 1; break; }
            if (!seen) {
                if (w->map_root && strcmp(w->map_root, U2_WORLD_SLOT_LAYER) == 0 &&
                    u2_world_read_slot(ctx, 1, w->map_id, over, sizeof over,
                                       NULL, 0) == U2_WORLD_OK) {
                    const int cells = world_delta(content, over, U2_SECTOR_TILES,
                                                  delta, sizeof delta);
                    if (cells > 0)      dtxt = delta;
                    /* reported, never truncated */
                    else if (cells < 0) dtxt = "too-big";
                }
                if (world_reserve(d) == 0) {
                    snprintf(d->world[d->n_world].name, 24, "%.23s", nm);
                    sha256_hex(content, U2_SECTOR_TILES, d->world[d->n_world].base);
                    snprintf(d->world[d->n_world].delta, U2_DEMO_DELTA_MAX,
                             "%.2047s", dtxt);
                    d->n_world++;
                }
            }
        }
    }
}

/* ---- applying a recorded world delta ------------------------------------- */

/* Parse one `x,y:oo->nn` cell and write it into buf. Returns 0 on success, -1 when the
 * text is malformed or the byte it should replace is not what the delta claims. A delta
 * that does not match the bytes under it is not a thing to apply "as best we can": a
 * half-applied sector verifies against nothing and looks exactly like a correct one. */
static int delta_cell(const char *tok, uint8_t *buf, size_t n)
{
    unsigned x, y, o, v;
    if (sscanf(tok, "%u,%u:%x->%x", &x, &y, &o, &v) != 4) return -1;
    if (x >= U2_SECTOR_EDGE || y >= U2_SECTOR_EDGE) return -1;
    const size_t i = (size_t)y * U2_SECTOR_EDGE + x;
    if (i >= n || buf[i] != (uint8_t)o) return -1;
    buf[i] = (uint8_t)v;
    return 0;
}

int u2_demo_apply_world(u2_ctx *ctx, u2_world *w, u2_sector_id id)
{
    u2_demo *d = of(ctx);
    char name[24], fname[24], sha[65];

    if (!d || !w || d->state != U2_DEMO_PLAYING || d->n_world == 0) return 0;

    sector_name(name, sizeof name, "mapx", id.hi, id.lo);
    int idx = -1;
    for (int i = 0; i < d->n_world; i++)
        if (strcmp(d->world[i].name, name) == 0) { idx = i; break; }
    if (idx < 0) return 0;

    const char *dt = d->world[idx].delta;
    const int nothing = (!dt[0] || strcmp(dt, "none") == 0);

    /* Re-read the CONTENT layer rather than patching whatever the loader picked. A
     * viewer holding their own overlay for this sector would otherwise get a third
     * world that matches neither the recording nor their save — and would be told it
     * verified. § 7.4: playback does not read the player's slot. */
    uint8_t buf[U2_SECTOR_TILES];
    if (u2_world_read_content(ctx, 1, id, buf, sizeof buf, fname, sizeof fname)
            != U2_WORLD_OK) {
        fprintf(stderr, "u2: %s: the demo describes this sector but the content file"
                        " cannot be read, so it cannot be checked.\n", name);
        d->overlay_failed = 1;
        return -1;
    }

    /* The base is verified even when there is nothing to patch. `delta=none` is a claim
     * that the recorder's sector WAS the shipped content, so a viewer holding different
     * bytes there has a different world; skipping the check because no cells need
     * rewriting lets a wrong sector through in silence, which is the one thing § 13.1
     * exists to prevent. This is what a later, unchanged sector used to do: it carried a
     * base nobody read, and a mutated mapx71 played as if it had been checked. */
    sha256_hex(buf, sizeof buf, sha);
    if (strcmp(sha, d->world[idx].base) != 0) {
        if (!ctx->demo_ignore_content) {
            fprintf(stderr, "u2: %s: your copy of this sector is not the one the"
                            " recording was made against.\n"
                            "    recorded base %s\n    your copy    %s\n"
                            "    The demo can only rebuild the sector it recorded, so it"
                            " refuses.\n"
                            "    --playback-ignore-content plays anyway on your own"
                            " bytes.\n", name, d->world[idx].base, sha);
            d->overlay_failed = 1;
            return -1;
        }
        /* Announced, not swallowed. The flag says the player accepts a different world;
         * it does not say they want the recorded change dropped without being told, and
         * a sector that silently plays as someone else's ground is the exact silence this
         * project has been told twice to stop producing. */
        fprintf(stderr, "u2: %s: your copy is not the recording's base and"
                        " --playback-ignore-content was given, so the recorded delta is"
                        " NOT applied. This sector plays as your own bytes.\n", name);
        return 0;
    }

    if (nothing) {
        /* Only speak if it actually displaces something: a viewer with no overlay of
         * their own is already holding exactly these bytes. */
        if (memcmp(w->map, buf, sizeof buf) != 0) {
            fprintf(stderr, "u2: %s: your save holds an overlay of this sector and the"
                            " recording carries none, so the recording's bytes are used.\n",
                    name);
            memcpy(w->map, buf, sizeof buf);
            memcpy(w->map_base, buf, sizeof buf);
        }
        return 0;
    }

    if (!strcmp(dt, "too-big")) {
        /* The writer refused to inline a change this large, so the demo carries no way
         * to rebuild the sector. Announcing that beats the same lie as applying nothing
         * quietly, which is a sector that verifies and is wrong. */
        fprintf(stderr, "u2: %s: the recorded change is too large for a demo to carry,"
                        " so this sector plays as your own copy.\n", name);
        return 0;
    }

    /* The cells are `;`-separated and canonically ordered, but the reader must not
     * depend on the order: an unordered writer would otherwise apply silently. */
    /* The writer terminates every cell with ';', so an empty token is a separator and
     * not a cell. A parser that handed one to delta_cell would refuse every real delta
     * it read, which is the shape this loop had before anyone ran it.
     * `n_cells` and not `applied`: d->applied already means the checkpoint is mounted. */
    int n_cells = 0;
    for (const char *p = dt; *p; ) {
        const char *q = strchr(p, ';');
        const size_t len = q ? (size_t)(q - p) : strlen(p);
        if (len > 0) {
            char tok[32];
            if (len >= sizeof tok) { d->overlay_failed = 1; return -1; }
            memcpy(tok, p, len);
            tok[len] = '\0';
            if (delta_cell(tok, buf, sizeof buf) != 0) {
                fprintf(stderr, "u2: %s: the recorded delta does not describe these bytes"
                                " (cell '%s'). Refusing rather than applying part of it.\n",
                        name, tok);
                d->overlay_failed = 1;
                return -1;
            }
            n_cells++;
        }
        if (!q) break;
        p = q + 1;
    }

    /* Both buffers, because the loader fills map_base as a copy of map and the spawn
     * clear edits map_base afterwards. Patching one alone would leave them disagreeing
     * by the whole delta. */
    memcpy(w->map, buf, sizeof buf);
    memcpy(w->map_base, buf, sizeof buf);
    /* Digested from w->map AFTER the copy, deliberately not from `buf`. Hashing the
     * staging buffer made this leg pass with both memcpys commented out, because the
     * message then described bytes that never reached the world. The observable has to
     * be taken from the thing the claim is about or it proves nothing, which is the
     * second vacuous assertion this leg has caught in itself.
     * asm/2233 restores terrain without announcing it; a demo is not the original, and a
     * player has no other way to know where the ground came from. */
    sha256_hex(w->map, sizeof buf, sha);
    fprintf(stderr, "u2: %s: rebuilt from the demo's recorded delta"
                    " (%d cells, sector now %.8s).\n", name, n_cells, sha);
    return 0;
}

int u2_demo_overlay_failed(const u2_ctx *ctx)
{
    const u2_demo *d = of(ctx);
    return d && d->overlay_failed;
}

/* ---- finish -------------------------------------------------------------- */

int u2_demo_finish(u2_ctx *ctx)
{
    u2_demo *d = of(ctx);
    int rc = 0;

    if (!d) return 0;

    if (d->state == U2_DEMO_RECORDING && d->file && !d->finished) {
        char sha[65], frame[65];
        uint8_t raw[32];

        u2_demo_digest(ctx, sha);

        /* Sectors discovered after the header was written — every one the walk visited
         * past the starting sector. They belong to the recording as much as the first
         * does: a demo that carried only its starting sector silently rebuilds a world
         * the recorder never stood in, and the run diverges somewhere that looks exactly
         * like an engine bug. Emitted here rather than in the header because the header
         * is written at the checkpoint, which is the start of the session. */
        for (int i = d->n_world_written; i < d->n_world; i++)
            emit(d, "@world %s base=%s delta=%s\n",
                 d->world[i].name, d->world[i].base, d->world[i].delta);

        /* Copied AFTER the lines above and BEFORE `@end`, in that order and on purpose.
         * The framing digest covers everything above `@end`, and the reader hashes those
         * bytes in file order, so the epilogue `@world` lines have to be inside it or
         * every multi-sector demo would read back as edited. `@end` itself stays outside
         * its own digest, as before. */
        sha256_ctx c = d->framing;
        sha256_final(&c, raw);
        hex64(raw, frame);
        /* Two digests, two jobs (hazard 4): `state=` is the closing state digest
         * D12 needs because the last record has no successor to trigger its check,
         * and `sha=` is the framing digest that makes a truncated or edited file
         * detectable rather than merely shorter. */
        emit(d, "@end records %d state=%s sha=%s stop=%s\n", d->n_written, sha, frame,
             d->stopped_by_toggle ? "toggle" : "exit");
        fflush(d->file);
        fclose(d->file);
        d->file = NULL;
        d->finished = 1;
        fprintf(stderr, "u2: RECORDED %d RECORDS -> %s\n", d->n_written, d->path);
    } else if (d->state == U2_DEMO_PLAYING && !d->reported) {
        d->reported = 1;
        if (*d->content && ctx->content_source && strcmp(d->content, ctx->content_source) != 0)
            fprintf(stderr, "u2: this demo was recorded with content '%s' and this run has "
                            "'%s'. Art does not reach game state, so it played.\n",
                    d->content, ctx->content_source);
        if (d->diverged < 0 && !d->closing_checked && d->stop_at_exit && d->applied &&
                *d->end_state_sha && d->digest_version == U2_DEMO_DIGEST_VERSION) {
            /* The recorder's epilogue was written by u2_demo_finish at process exit,
             * so exit is the matching instant here. Both runs ended the same way, from
             * the same last record, through the same code. */
            char now[65];
            d->closing_checked = 1;
            u2_demo_digest(ctx, now);
            if (strcmp(now, d->end_state_sha) != 0) {
                d->diverged = d->n_recs;
                fprintf(stderr, "u2: DEMO DIVERGED at the end of the run: "
                                "recorded %s, live %s\n", d->end_state_sha, now);
            }
        }
        if (d->diverged < 0 && !d->closing_checked) {
            /* Every record verified and then the flow ended somewhere the recorder did
             * not stop. Saying so is the honest half of D12: the last record's own
             * effect is the one thing no record can vouch for. */
            d->diverged = d->n_recs;
            fprintf(stderr, "u2: DEMO INCOMPLETE: the run ended before the recording's "
                            "stop point, so the closing state was never reached "
                            "(%d of %d records delivered).\n", d->cur_rec, d->n_recs);
        }
        if (d->diverged >= 0) {
            rc = U2_DEMO_EXIT_DIVERGED;
        } else if (d->digest_version != U2_DEMO_DIGEST_VERSION) {
            fprintf(stderr, "u2: DEMO PLAYED %d/%d RECORDS UNVERIFIED (digest scope %d)\n",
                    d->cur_rec, d->n_recs, d->digest_version);
            rc = U2_DEMO_EXIT_UNVERIFIED;
        } else {
            fprintf(stderr, "u2: DEMO PLAYED %d/%d RECORDS\n", d->cur_rec, d->n_recs);
        }
    }

    if (d->file) { fclose(d->file); d->file = NULL; }
    free(d->recs);
    free(d);
    ctx->demo = NULL;
    return rc;
}

/* ---- observation --------------------------------------------------------- */

u2_demo_state u2_demo_state_of(const u2_ctx *ctx)
{
    const u2_demo *d = of(ctx);
    return d ? d->state : U2_DEMO_IDLE;
}

int u2_demo_record_count(const u2_ctx *ctx)
{
    const u2_demo *d = of(ctx);
    if (!d) return 0;
    return d->state == U2_DEMO_RECORDING ? d->n_written : d->n_recs;
}

int u2_demo_divergence(const u2_ctx *ctx)
{
    const u2_demo *d = of(ctx);
    return d ? d->diverged : -1;
}

int u2_demo_unverifiable(const u2_ctx *ctx)
{
    const u2_demo *d = of(ctx);
    return d && d->digest_version != U2_DEMO_DIGEST_VERSION;
}
