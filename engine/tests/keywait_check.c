/*
 * keywait_check.c — the seam_key_wait contract, driven by a fake input backend.
 *
 * Why a fake backend and not a scripted run: the defect only appears on the third
 * answer a backend can give. The script backend reports a spent run as 0, the
 * terminal reports EOF as 0, and only SDL answers -1 — for a window close or a
 * SIGINT. So no headless scripted capture could ever have shown it, and the report
 * came from a human pressing Ctrl-C: "in the character creation screen, closing
 * the window or Ctrl-C is taken as a down button".
 *
 * The mechanism the checks below pin down: seam_key_poll has always collapsed its
 * backend's three states to 1/0, and seam_key_wait did not. Callers ask `if (!got)`
 * or `if (got == 0)`, so a -1 was read as "a key arrived" and the caller then read
 * out->ch, which the backend never wrote. Because u2_key sits at the same stack
 * offset on every iteration, that byte still held the LAST key pressed — Down, on
 * the creation form.
 */
#include <stdio.h>
#include <string.h>
#include "core/ctx.h"
#include "core/seam.h"
#include "platform/platform.h"

static int checks = 0;
#define OK(cond, msg) do { if (!(cond)) { printf("  NOT OK  %s\n", (msg)); return 1; } checks++; } while (0)

/* One backend, three scripted answers, in order. */
static const int ANSWERS[4] = { 1, -1, 0, -1 };
static int next_answer;
static uint8_t delivered;

/* This file deliberately does NOT use tests/u2_scenario.h. The fake backend below is the
 * subject, not the harness: it is the only backend in the tree that can answer all three
 * of the seam's answers, and the trap it pins is that nothing is written to *out on 0 and
 * -1. A shared fixture would put the thing under test inside the harness. */
static int fake_init(u2_ctx *ctx) { (void)ctx; return 0; }
static int fake_poll(u2_ctx *ctx, u2_key *out) { (void)ctx; (void)out; return 0; }
static int fake_wait(u2_ctx *ctx, u2_key *out)
{
    (void)ctx;
    const int a = ANSWERS[next_answer++];
    if (a == 1) {
        out->ch = delivered;
        out->scan = 0;
    }
    /* On -1 and 0 the backend writes NOTHING to *out, which is the whole trap: a
     * caller that treats -1 as a key reads whatever was on the stack before. */
    return a;
}
static void fake_shutdown(u2_ctx *ctx) { (void)ctx; }

static const u2_input fake_input = {
    .name = "fake",
    .init = fake_init,
    .poll = fake_poll,
    .wait = fake_wait,
    .shutdown = fake_shutdown,
};

int main(void)
{
    u2_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.input = &fake_input;

    /* 1. a real key is a real key, and it arrives folded to upper case. */
    delivered = 'y';
    u2_key k;
    memset(&k, 0, sizeof k);
    OK(seam_key_wait(&ctx, &k) == 1, "a key must be reported as 1");
    OK(k.ch == 'Y', "the wait path must carry the upper-case fold (asm/4F4D)");

    /* 2. THE REGRESSION: a quit request is not a key. */
    memset(&k, 0, sizeof k);
    k.ch = 0xF1;                       /* stand in for the stale stack byte */
    const int quit = seam_key_wait(&ctx, &k);
    OK(quit == 0, "a quit request (-1) must NOT be reported as a key");
    OK(k.ch == 0xF1, "a quit must not touch *out — the caller must not rely on it");
    OK(ctx.input_exhausted == 1, "a quit must mark the player gone, like a spent script");

    /* 3. a spent script still answers 0 and still marks the player gone. */
    memset(&k, 0, sizeof k);
    OK(seam_key_wait(&ctx, &k) == 0, "end of input must be 0");

    /* 4. and a quit after that is still not a key (no state stuck from last time). */
    memset(&k, 0, sizeof k);
    OK(seam_key_wait(&ctx, &k) == 0, "a second quit must also be 0");

    OK(next_answer == 4, "every scripted answer was consumed");
    printf("  ok  %d checks: wait answers 1 only for a key; -1 and 0 both mean the player left\n", checks);
    return 0;
}
