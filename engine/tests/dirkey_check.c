/*
 * dirkey_check.c — the four arrow direction codes, and the one a player can type.
 *
 * The arrow table at CS:0x4F09 is `48 0e 50 19 4d 05 4b 17`: it maps each arrow's
 * scancode to a CONTROL BYTE — Up->0x0E (Ctrl-N), Down->0x19 (Ctrl-Y), Right->0x05
 * (Ctrl-E), Left->0x17 (Ctrl-W). So the game's internal direction vocabulary is
 * control letters, and the arrows are just one of the two ways to produce them.
 *
 * key_poll then swallows three of the four (asm/4F58 `cmp $0x5,%al`, asm/4F5C
 * `cmp $0xe,%al`, asm/4F60 `cmp $0x17,%al`) and does NOT swallow 0x19. That asymmetry
 * is the whole finding: three directions are arrow-only, and south can be typed.
 *
 * Why the typed byte and the arrow are indistinguishable downstream, which is what
 * makes this a seam concern rather than a per-caller arm: asm/4FBE matches the table on
 * AH, asm/4FCC writes the mapped byte into AL, and asm/4FD1 then runs `mov $0xff,%ah`
 * — the "a key was consumed" flag, which destroys the scancode. Everything after that
 * sees AL = 0x19 and AH = 0xFF whichever way it arrived. So the mapping belongs in
 * seam_key_poll, and one arm there is correct for the overworld AND for the dungeon,
 * where asm/277B has Down mean RETREAT. An arm in each caller instead would have left
 * the dungeon wrong while looking finished.
 *
 * Driven by a fake backend for the same reason keywait_check.c is: this is a property of
 * the seam, and a scripted run cannot tell "the seam swallowed it" from "the game ignored
 * it" — which is exactly the confusion that made the first live attempt at this question
 * read a disconnected wire as a negative result.
 */
#include <stdio.h>
#include <string.h>
#include "core/ctx.h"
#include "core/seam.h"
#include "platform/platform.h"

static int checks = 0;
static int failures = 0;
#define OK(cond, msg) do { if (!(cond)) { printf("  NOT OK  %s\n", (msg)); failures++; } \
                           else checks++; } while (0)

static uint8_t delivered;
static int has_key = 1;

/* This file deliberately does NOT use tests/u2_scenario.h. The fake backend below is the
 * subject, not the harness: the seam's answer alphabet includes -1, and the trap this
 * pins is that the backend writes nothing to *out on 0 and -1. A shared fixture would
 * put the thing under test inside the harness. */
static int fake_init(u2_ctx *ctx) { (void)ctx; return 0; }
static int fake_poll(u2_ctx *ctx, u2_key *out)
{
    (void)ctx;
    if (!has_key) return 0;
    out->ch = delivered;
    out->scan = 0x1E;                 /* a real letter scancode; the seam may replace it */
    return 1;
}
static int fake_wait(u2_ctx *ctx, u2_key *out) { return fake_poll(ctx, out); }
static void fake_shutdown(u2_ctx *ctx) { (void)ctx; }

static const u2_input fake_input = {
    .name = "fake", .init = fake_init, .poll = fake_poll,
    .wait = fake_wait, .shutdown = fake_shutdown,
};

/* Drive one byte through the seam and report what game logic would see. */
static int feed(u2_ctx *ctx, uint8_t b, u2_key *out)
{
    delivered = b;
    has_key = 1;
    memset(out, 0, sizeof *out);
    return seam_key_poll(ctx, out);
}

int main(void)
{
    u2_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.input = &fake_input;
    u2_key k;

    /* --- the one direction code that survives being typed ------------------- */
    OK(feed(&ctx, 0x19, &k) == 1, "Ctrl-Y must reach game logic as a key");
    OK(k.ch == U2_KEY_DOWN, "Ctrl-Y (0x19) must arrive as the arrow's own code");
    OK(k.scan == 0x50, "and carry Down's scancode, since the arrow path sets it");

    /* --- the three that key_poll discards before anything can see them ------ */
    OK(feed(&ctx, 0x05, &k) == 0, "Ctrl-E (0x05) must be swallowed (asm/4F58)");
    OK(feed(&ctx, 0x0E, &k) == 0, "Ctrl-N (0x0E) must be swallowed (asm/4F5C)");
    OK(feed(&ctx, 0x17, &k) == 0, "Ctrl-W (0x17) must be swallowed (asm/4F60)");

    /* --- the swallow is on control bytes, never on the letters themselves ---
     * Without this pair the test would pass if someone swallowed 'E'/'N'/'W' too,
     * which would delete three command handlers. */
    OK(feed(&ctx, 'E', &k) == 1 && k.ch == 'E', "'E' must still reach the dispatcher");
    OK(feed(&ctx, 'n', &k) == 1 && k.ch == 'N', "'n' must still reach it, folded up");
    OK(feed(&ctx, 'w', &k) == 1 && k.ch == 'W', "'w' must still reach it, folded up");

    /* --- the arrow itself is untouched, and so is the fold ------------------ */
    delivered = U2_KEY_DOWN; has_key = 1;
    memset(&k, 0, sizeof k);
    OK(seam_key_poll(&ctx, &k) == 1 && k.ch == U2_KEY_DOWN,
       "the arrow code must pass through unchanged");
    OK(feed(&ctx, 'y', &k) == 1 && k.ch == 'Y',
       "a plain y folds to 'Y' and is NOT remapped — 0x79 is not 0x19");

    /* --- the asymmetry is the finding, so pin its shape --------------------- */
    /* Three swallowed, one mapped: if a future edit swallows 0x19 too, south becomes
     * arrow-only and this is the check that says so. */
    int swallowed = 0;
    const uint8_t codes[4] = { 0x05, 0x0E, 0x17, 0x19 };
    for (int i = 0; i < 4; i++)
        if (feed(&ctx, codes[i], &k) == 0) swallowed++;
    OK(swallowed == 3, "exactly three of the four direction codes are swallowed");

    if (failures) {
        printf("  FAIL  %d checks passed, %d failed\n", checks, failures);
        return 1;
    }
    printf("  ok  %d checks: three direction codes swallowed, south typable as 0x19\n",
           checks);
    return 0;
}
