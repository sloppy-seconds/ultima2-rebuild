/*
 * mode_check.c — which screen the state block says the player is on, and why the
 * tests are in this order.
 *
 * The original has no phase byte. `mode` has to be derived, and the derivation is the
 * finding rather than the plumbing: in orbit the sector, the position, DS:12, DS:13 and
 * DS:14 are byte-identical to the pad the rocket launched from
 * (docs/handoff-space-view.md § 3a, captures/spice86/w6-orbit-dive). The pad in that
 * capture was sector 41. So a derivation that looked at the settlement digit first — which
 * is what api.c used before this existed — reports a character in orbit as standing in a
 * village, and a bot driving the original would act on that.
 *
 * What separates orbit from the pad is that the two staging buffers, DS:272..371 and
 * DS:372..471, are uniformly 0xFF while in space and hold a staged sector map otherwise.
 * § 3a is right that control flow must not test that, and nothing here gates on it; this is
 * a report. But it is the only DS-observable discriminator there is, which is what makes it
 * worth pinning: the bot reads the original's memory and nothing else, so if this rule is
 * wrong the bot is silently wrong about where it is standing.
 *
 * The checks are driven against synthetic state blocks rather than a live run for the same
 * reason dirkey_check.c uses a fake backend: the property is "given these bytes, what do we
 * answer", and a scripted run cannot distinguish a wrong answer from an unreachable state.
 * The live side is covered separately — the same function answers a raw DS blob off the
 * original via `u2 --ds-json`, which is the point of it taking a state block and not a ctx.
 */
#include <stdio.h>
#include <string.h>

#include "core/state.h"

static int fails;
static int checks;

static void expect(const u2_state *s, const char *want, const char *why)
{
    const char *got = u2_state_mode(s);
    checks++;
    if (strcmp(got, want) == 0) {
        printf("  ok   %-58s -> %s\n", why, got);
        return;
    }
    fails++;
    printf("  FAIL %-58s -> %s, want %s\n", why, got, want);
}

/* A block with a name in it, so nothing answers "title" by accident. */
static void base(u2_state *s)
{
    memset(s, 0, sizeof *s);
    strcpy((char *)s->name, "MODECHK");
}

static void stage_all(u2_state *s, uint8_t v)
{
    memset(s->stage_a, v, sizeof s->stage_a);
    memset(s->stage_b, v, sizeof s->stage_b);
}

int main(void)
{
    u2_state s;

    puts("mode_check: the derivation, and the order it runs in");

    /* --- title ---------------------------------------------------------------- */
    base(&s);
    s.name[0] = 0;
    expect(&s, "title", "no name in the record");

    /* A nameless block with the staging pair set is still title: the name test runs
     * first, and it has to, or a fresh process would read as space. */
    base(&s);
    s.name[0] = 0;
    stage_all(&s, 0xFF);
    expect(&s, "title", "nameless even with the staging pair set");

    /* --- the ground states ----------------------------------------------------- */
    base(&s);
    s.sector_second = 0;
    expect(&s, "world", "wilderness, settlement digit 0");

    base(&s);
    s.sector_second = 1;
    expect(&s, "site", "settlement digit 1");

    base(&s);
    s.sector_second = 4;
    expect(&s, "dungeon", "settlement digit 4");

    base(&s);
    s.sector_second = 5;
    expect(&s, "dungeon", "settlement digit 5");

    /* --- space, and the ordering that is the whole point ----------------------- */
    base(&s);
    s.sector_second = 1;                 /* the pad in w6-orbit-dive was sector 41 */
    stage_all(&s, 0xFF);
    expect(&s, "space", "staging pair all-0xFF from a SITE pad (sector 41)");

    base(&s);
    s.sector_second = 0;
    stage_all(&s, 0xFF);
    expect(&s, "space", "staging pair all-0xFF from a wilderness pad");

    /* The dungeon digit outranks the staging pair, and this is the check that caught the
     * first version of the rule getting it backwards. captures/spice86/dungeon-entry
     * ev034..ev051 is a dungeon descent in sector 35 with both staging buffers uniformly
     * 0xFF -- a dungeon has no staged sector map either -- and calling those frames space
     * is exactly the plausible wrong answer this project exists to catch. */
    base(&s);
    s.sector_second = 5;
    stage_all(&s, 0xFF);
    expect(&s, "dungeon", "dungeon digit outranks the staging pair (sector 35, ev040)");

    base(&s);
    s.sector_second = 4;
    stage_all(&s, 0xFF);
    expect(&s, "dungeon", "dungeon digit 4 with the pair all-0xFF");

    /* --- the discriminator is ALL of 512 bytes, not most of them -------------- */
    base(&s);
    s.sector_second = 1;
    stage_all(&s, 0xFF);
    s.stage_a[0] = 0x00;
    expect(&s, "site", "one byte of stage_a not 0xFF is not space");

    base(&s);
    s.sector_second = 1;
    stage_all(&s, 0xFF);
    s.stage_b[sizeof s.stage_b - 1] = 0x2A;
    expect(&s, "site", "one byte at the far end of stage_b is not space");

    /* A staged map full of 0x00 is the ordinary case and must never read as space. */
    base(&s);
    s.sector_second = 1;
    stage_all(&s, 0x00);
    expect(&s, "site", "a staged map of zeros is not space");

    /* --- the dungeon rule is one rule, not two -------------------------------- */
    /* u2_site_is_dungeon() delegates here, so the branch the game logic takes and the mode
     * the API reports are the same predicate. Checked for all ten digits, because "4 or 5"
     * written down twice is exactly the kind of rule that quietly becomes "4" in one of
     * the copies. */
    for (int d = 0; d <= 9; d++) {
        u2_state m;
        base(&m);
        m.sector_second = (uint8_t)d;
        checks++;
        const char *want = u2_state_is_dungeon(&m) ? "dungeon" : (d ? "site" : "world");
        if (strcmp(u2_state_mode(&m), want) == 0) {
            printf("  ok   digit %d agrees with u2_state_is_dungeon\n", d);
        } else {
            fails++;
            printf("  FAIL digit %d: mode says %s, the predicate says %s\n",
                   d, u2_state_mode(&m), want);
        }
    }

    /* --- the block is the whole input ----------------------------------------- */
    /* Nothing here reads a ctx, and that is load-bearing rather than stylistic: the
     * original has no ctx, so a rule written against one could only ever describe the
     * engine, and the two targets would drift on where the player is. */
    checks++;
    if (strcmp(u2_state_mode(NULL), "title") == 0) {
        printf("  ok   a null block answers title rather than crashing\n");
    } else {
        fails++;
        printf("  FAIL a null block must answer title\n");
    }

    printf(fails ? "  FAIL %d of %d checks failed\n" : "  ok  %d checks: mode derived, order pinned\n",
           fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
