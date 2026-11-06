/*
 * demo_check.c — the record/verify loop, in one process and with no game loop.
 *
 * This is W1 and W7's spine in `docs/demo-record-playback.md`: a demo written by
 * the recorder must parse back field for field, and playback must verify every
 * record. The control that matters is the second one — a state that differs by one
 * byte must be reported as a divergence at the record that caused it and nowhere
 * else. A verification that cannot go red is worse than none, and this project has
 * shipped that defect three times (AGENTS.md § 3).
 *
 * It drives core/demo.c through its public surface only: begin_recording,
 * quiescent, key, next_key, finish. That is the same surface the seam and the
 * playback input backend use, so nothing here is a test-only path.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/ctx.h"
#include "core/demo.h"
#include "core/state.h"
#include "core/vram.h"

static int fails;
static int checks;

static void ok(const char *what)
{
    checks++;
    printf("  ok  %s\n", what);
}

static void bad(const char *what)
{
    fails++;
    printf("  NOT OK  %s\n", what);
}

static void expect(int cond, const char *what)
{
    if (cond) ok(what);
    else bad(what);
}

/* One scripted "turn": a state mutation and the key that was delivered into it.
 * The mutation stands in for whatever a command handler would have done to DS. */
typedef struct step {
    uint8_t key;
    uint8_t auto_pass;    /* this record is the idle auto-PASS, not a key */
    uint8_t poke_off_lo;  /* DS offset to bump after the key is delivered */
    uint8_t poke_val;
    uint8_t poke_hi;      /* nonzero to bump a second, high-half offset */
} step;

static void apply_step(u2_ctx *ctx, const step *s)
{
    unsigned char *base = (unsigned char *)&ctx->state;

    base[s->poke_off_lo] = s->poke_val;
    if (s->poke_hi)
        base[0x137 + s->poke_off_lo] = (uint8_t)(s->poke_val + 1);
    ctx->rng_rolls += 3;
}

static void boot(u2_ctx *ctx, unsigned seed)
{
    memset(ctx, 0, sizeof *ctx);
    u2_vram_init(&ctx->vram);
    u2_state_init(&ctx->state);
    ctx->state.sector_first = 2;
    ctx->state.sector_second = 0;
    ctx->state.hp = (uint8_t)(0x40 + seed);
    ctx->rng_rolls = 0;
}

static const step STEPS[] = {
    { 'N', 0, 0x23, 0x14, 1 },
    { 'S', 0, 0x23, 0x15, 1 },
    { 0,   1, 0x51, 0x03, 0 },     /* the idle auto-PASS: no key, still a record */
    { 'A', 0, 0x58, 0x22, 0 },
    { 'Z', 0, 0x58, 0x23, 1 },
};
#define N_STEPS ((int)(sizeof STEPS / sizeof STEPS[0]))

static void record_demo(u2_ctx *ctx, const char *path)
{
    u2_demo_begin_recording(ctx, path);
    /* The toggle itself is what a player presses; the test presses it through the
     * same call the seam makes, so the swallow is on the record too. */
    u2_key tog = { 0x12, 0 };
    if (!u2_demo_key(ctx, &tog))
        bad("Ctrl-R was swallowed at the seam");
    u2_demo_quiescent(ctx, U2_DEMO_MODE_OVERWORLD, NULL);

    for (int i = 0; i < N_STEPS; i++) {
        u2_key k = { STEPS[i].key, 0 };
        if (STEPS[i].auto_pass) {
            u2_demo_mark_auto_pass(ctx);
        } else {
            if (u2_demo_key(ctx, &k))
                bad("a normal key was swallowed at the seam");
        }
        apply_step(ctx, &STEPS[i]);
    }
    if (u2_demo_finish(ctx) != 0)
        bad("recording finished with an error");
}

/* Playback, optionally perturbing the state to prove the check bites. */
static int play_demo(u2_ctx *ctx, const char *path, int perturb)
{
    int seen = 0;

    if (u2_demo_begin_playback(ctx, path) != 0) {
        bad("a demo the recorder just wrote was refused");
        return -1;
    }
    u2_demo_quiescent(ctx, U2_DEMO_MODE_OVERWORLD, NULL);

    for (;;) {
        u2_key k;
        const int r = u2_demo_next_key(ctx, &k);
        if (r < 0) break;
        if (r == 0) {
            /* The loop owes a PASS. u2_demo_auto_pass_due consumes the record and
             * verifies it, which is the playback half of D4. */
            if (!u2_demo_auto_pass_due(ctx)) {
                bad("an auto record was due and nothing consumed it");
                break;
            }
            apply_step(ctx, &STEPS[2]);
            seen++;
            continue;
        }
        for (int i = 0; i < N_STEPS; i++) {
            if (!STEPS[i].auto_pass && STEPS[i].key == k.ch) {
                apply_step(ctx, &STEPS[i]);
                break;
            }
        }
        seen++;
        if (perturb && seen == 2)
            ((unsigned char *)&ctx->state)[0x51] ^= 0xFF;
    }
    return u2_demo_finish(ctx);
}

int main(void)
{
    char path[256];
    u2_ctx a, b;
    FILE *f;

    snprintf(path, sizeof path, "%s/u2-demo-check-%d.demo",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/var/tmp", (int)getpid());
    unlink(path);

    boot(&a, 0);
    record_demo(&a, path);

    f = fopen(path, "r");
    if (!f) { bad("the recorder wrote no file"); return 1; }
    fclose(f);

    /* --- the file says what it is --- */
    {
        char line[8192];
        int nrec = 0, nauto = 0, saw_end = 0, saw_ckpt = 0, span_ok = 0;
        f = fopen(path, "r");
        while (fgets(line, (int)sizeof line, f)) {
            if (!strncmp(line, "@rec ", 5)) nrec++;
            if (strstr(line, "kind=auto")) nauto++;
            if (!strncmp(line, "@end", 4)) saw_end = 1;
            if (!strncmp(line, "@checkpoint-sha ", 16)) saw_ckpt = 1;
            if (!strncmp(line, "span ", 5) && strstr(line, "1616")) span_ok = 1;
        }
        fclose(f);
        expect(nrec == N_STEPS, "one record per delivered input");
        expect(nauto == 1, "the idle auto-PASS is recorded as kind=auto");
        expect(saw_ckpt, "the checkpoint carries its own digest");
        expect(saw_end, "the epilogue was written");
        expect(span_ok, "the checkpoint span is the whole DS mirror (0x650)");
    }

    /* --- playback verifies --- */
    boot(&b, 0);
    expect(play_demo(&b, path, 0) == 0, "playback reproduces every record");

    /* --- the control: one byte out of place must go red, and name the record --- */
    boot(&b, 0);
    {
        const int rc = play_demo(&b, path, 1);
        expect(rc != 0, "a perturbed state makes playback fail");
    }

    /* --- the control: a truncated file is refused, and says so --- */
    {
        char trunc[300];
        snprintf(trunc, sizeof trunc, "%s.trunc", path);
        f = fopen(path, "r");
        FILE *g = fopen(trunc, "w");
        char line[8192];
        while (fgets(line, (int)sizeof line, f))
            if (strncmp(line, "@end", 4)) fputs(line, g);
        fclose(f); fclose(g);

        u2_ctx c;
        boot(&c, 0);
        expect(u2_demo_begin_playback(&c, trunc) != 0,
               "a file with no @end is refused by default");

        boot(&c, 0);
        c.demo_salvage = 1;
        expect(u2_demo_begin_playback(&c, trunc) == 0,
               "--salvage plays the records that are present");
        (void)u2_demo_finish(&c);
        unlink(trunc);
    }

    /* --- the control: an edited digest is caught as corruption, not as a divergence --- */
    {
        char bad_path[300];
        snprintf(bad_path, sizeof bad_path, "%s.bad", path);
        f = fopen(path, "r");
        FILE *g = fopen(bad_path, "w");
        char line[8192];
        int flipped = 0;
        while (fgets(line, (int)sizeof line, f)) {
            if (!flipped && !strncmp(line, "@rec ", 5)) {
                char *sha = strstr(line, "sha=");
                if (sha) { sha[4] = sha[4] == '0' ? '1' : '0'; flipped = 1; }
            }
            fputs(line, g);
        }
        fclose(f); fclose(g);
        expect(flipped, "the control found a digest to flip");

        u2_ctx c;
        boot(&c, 0);
        expect(u2_demo_begin_playback(&c, bad_path) != 0,
               "an edited record digest is refused as a corrupt file");
        unlink(bad_path);
    }

    /* --- the deferred start: a Ctrl-R on the way into play --- */
    {
        char defer[400], defer2[400];
        u2_ctx c;
        snprintf(defer, sizeof defer, "%.250s.defer.demo", path);
        snprintf(defer2, sizeof defer2, "%.250s.cancel.demo", path);
        unlink(defer);

        boot(&c, 1);
        c.demo_at_quiescent = 1;
        c.demo_quiescent_mode = U2_DEMO_MODE_TITLE;
        u2_demo_begin_recording(&c, defer);
        u2_key tog = { 0x12, 0 };
        u2_demo_key(&c, &tog);
        u2_demo_quiescent(&c, U2_DEMO_MODE_TITLE, NULL);
        {
            FILE *probe = fopen(defer, "r");
            expect(probe == NULL, "a Ctrl-R at the title defers and writes nothing yet");
            if (probe) fclose(probe);
        }
        /* The player reaches the world. The pending request has to land here, not be
         * silently lost — which is what the first version did, because it cleared the
         * pending flag before the deferral branch and then returned. */
        u2_demo_quiescent(&c, U2_DEMO_MODE_OVERWORLD, NULL);
        {
            FILE *probe = fopen(defer, "r");
            expect(probe != NULL, "the deferred recording starts once the world is reached");
            if (probe) fclose(probe);
        }
        u2_key k = { 'N', 0 };
        u2_demo_key(&c, &k);
        if (u2_demo_finish(&c) != 0) bad("the deferred recording did not finalise");
        unlink(defer);

        /* And the cancel the message promises is real. */
        boot(&c, 1);
        c.demo_at_quiescent = 1;
        c.demo_quiescent_mode = U2_DEMO_MODE_TITLE;
        u2_demo_begin_recording(&c, defer2);
        u2_demo_key(&c, &tog);
        u2_demo_key(&c, &tog);
        {
            FILE *probe = fopen(defer2, "r");
            expect(probe == NULL, "a second Ctrl-R before the world cancels the request");
            if (probe) fclose(probe);
        }
        unlink(defer2);
    }

    unlink(path);
    printf("  %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
