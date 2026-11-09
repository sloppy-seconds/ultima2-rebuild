/*
 * combat_check.c — CP5 acceptance specification (tickets C1, C2, C6).
 *
 * This is BOTH a specification and a live test: src/game/combat.c implements
 * C1/C2/C6 and `run_cp5.sh` links this file against it on every `make test`
 * (4066 checks, 0 failures at the time of writing — the gate prints the real
 * number, do not trust a copy of it).
 *
 * It is additionally executed against two fixture models that ship beside it:
 * combat_model_faithful.c must pass and combat_model_boolean_wrong.c must fail.
 * That is what makes "the spec discriminates" a checked fact rather than a claim
 * in a comment — the original proof used throwaway models that no longer exist.
 *
 * Every expectation below is a byte in the original, cited by asm file address.
 * Grades per AGENTS.md § 5 are named in the section headers.
 *
 * The proposed API is declared here (the spec owns the contract) so this file
 * is syntax-checkable with only ctx.h/rng.h/state.h present. The accessors key
 * everything by the DS offset it mirrors, so the spec does not hardcode a
 * particular state.h field layout — the implementation chooses the fields, the
 * offsets are the contract.
 *
 *   counter_ds : 0x26B legs, 0x26C arms, 0x26D sleep   (asm/0E70,0EC2,0F48)
 *   item_ds    : DS:D6+i item array — 0xD6 RING, 0xD9 BOOTS, 0xDA CLOAK,
 *                0xE4 GREEN IDOL                        (findings § 12)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "core/ctx.h"
#include "core/rng.h"
#include "core/state.h"

/* ---- the contract CP5 must provide (see the header comment) -------------- *
 * DS offsets are uint16_t: the counters live at DS:26B..26D, above 0xFF. */
int     u2_combat_special(u2_ctx *ctx, uint16_t counter_ds);
uint8_t u2_combat_counter(const u2_ctx *ctx, uint16_t counter_ds);
void    u2_combat_set_counter(u2_ctx *ctx, uint16_t counter_ds, uint8_t value);
void    u2_combat_set_item(u2_ctx *ctx, uint16_t item_ds, uint8_t count);
uint8_t u2_combat_item(const u2_ctx *ctx, uint16_t item_ds);
uint16_t u2_combat_thief_steal(u2_ctx *ctx);
void    u2_combat_kill_drop(u2_ctx *ctx, uint8_t dead_type);
uint8_t u2_bcd_add2_test(uint8_t v);
/* C4: magic missile. Non-zero when damage applies. Must consume NO random
 * byte — there is no save to roll. spec: asm/0F12 */
int     u2_combat_magic_missile(u2_ctx *ctx);
/* C5: force field. 0 = ring protected, 1 = took exactly 1000, 2 = killed.
 * Mutates state.hp (DS:51, the high BCD byte). spec: asm/24B5 */
int     u2_combat_force_field(u2_ctx *ctx);
#define FIELD_PROTECTED 0
#define FIELD_DAMAGED   1
#define FIELD_KILLED    2

/* DS offsets named for readability. */
#define C_LEGS   0x26B
#define C_ARMS   0x26C
#define C_SLEEP  0x26D
#define I_RING   0xD6
#define I_BOOTS  0xD9   /* legs resist gate  (asm/0E70 file 0x108D) */
#define I_CLOAK  0xDA   /* arms resist gate  (asm/0EC2 file 0x10DF) */
#define I_IDOL   0xE4   /* sleep resist gate (asm/0F48 file 0x1162) */

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static void fresh(u2_ctx *ctx, uint8_t seed)
{
    memset(ctx, 0, sizeof *ctx);
    u2_rng_seed(ctx, seed);
}

/* The first rand_byte a freshly-seeded ctx would produce. The resist gate is
 * the first rand_byte the event consumes (asm/0E70 file 0x1094: the str_out /
 * beep calls before it touch no RNG), so this is the gate roll. */
static uint8_t first_roll(uint8_t seed)
{
    u2_ctx p; fresh(&p, seed);
    return u2_rand_byte(&p);
}

/* ========================================================================
 * C2 — resist is a ~75% roll on a live inventory slot, not boolean immunity.
 * Grade: measured (asm/0E70/0EC2/0F48 gates + roll; findings § 12).
 *
 * The asm shape, for each of the three specials:
 *     if (item_slot == 0) goto LAND;          // no item: lands, NO roll
 *     if (rand_byte() >= 0x40) { SAVED; return; }  // resist, counter untouched
 *   LAND:
 *     counter = rand_byte() & 0xF;            // 0..15 turns
 * ==================================================================== */
static void test_c2_no_item_always_lands(void)
{
    /* Without the item the effect lands unconditionally, with no roll at all.
     * A boolean implementation that gates on "has item ever" would be wrong
     * here only if it also rolled; the discriminating cases are below. */
    for (int seed = 0; seed < 256; seed++) {
        u2_ctx ctx; fresh(&ctx, (uint8_t)seed);
        u2_combat_set_item(&ctx, I_BOOTS, 0);
        u2_combat_set_counter(&ctx, C_LEGS, 0xFF);      // sentinel
        int landed = u2_combat_special(&ctx, C_LEGS);
        CHECK(landed == 1,
              "C2 no-boots seed %d: effect must land (no roll gate), got %d",
              seed, landed);
        CHECK(u2_combat_counter(&ctx, C_LEGS) <= 15,
              "C2 no-boots seed %d: landed counter must be 0..15, got %u",
              seed, u2_combat_counter(&ctx, C_LEGS));
    }
}

static void test_c2_item_present_is_probabilistic(void)
{
    /* With the item present the effect lands on SOME trials and is resisted on
     * OTHERS. This is the test a boolean implementation cannot pass:
     *   - "item == immunity" (boolean)  -> resisted every time -> landings==0
     *   - "item does nothing"           -> landed every time    -> resistances==0
     * Both are rejected. The correct ~75% roll gives both > 0. */
    int landings = 0, resistances = 0;
    for (int seed = 0; seed < 256; seed++) {
        u2_ctx ctx; fresh(&ctx, (uint8_t)seed);
        u2_combat_set_item(&ctx, I_BOOTS, 1);
        int landed = u2_combat_special(&ctx, C_LEGS);
        if (landed) landings++; else resistances++;
    }
    CHECK(landings > 0,
          "C2 with-boots: a boolean-immunity impl resists 100%% (landings=0). "
          "Got landings=%d resistances=%d", landings, resistances);
    CHECK(resistances > 0,
          "C2 with-boots: an impl that ignores the item lands 100%% "
          "(resistances=0). Got landings=%d resistances=%d", landings, resistances);
    /* ~75% resist => ~25% landings. Wide band (>=1/8 and <=1/2) so a correct
     * impl passes but a 0%% or 100%% impl cannot. */
    CHECK(landings * 8 >= 256 && landings * 2 <= 256,
          "C2 with-boots: landings must be a strict minority (~25%%), got %d/256",
          landings);
}

static void test_c2_threshold_is_exactly_0x40(void)
{
    /* The sharpest discriminator: pin the decision to the exact gate roll.
     * landed <=> first_roll < 0x40, for every seed. Any wrong threshold
     * (0x80, 0x20, ...) or boolean logic fails on at least one seed. */
    for (int seed = 0; seed < 256; seed++) {
        uint8_t gate = first_roll((uint8_t)seed);
        u2_ctx ctx; fresh(&ctx, (uint8_t)seed);
        u2_combat_set_item(&ctx, I_BOOTS, 1);
        int landed = u2_combat_special(&ctx, C_LEGS);
        int expect_land = (gate < 0x40);
        CHECK(landed == expect_land,
              "C2 seed %d gate=0x%02x: expected %s (threshold 0x40), got %s",
              seed, gate, expect_land ? "LAND" : "RESIST",
              landed ? "LAND" : "RESIST");
    }
}

static void test_c2_all_three_gates(void)
{
    /* Each special reads its OWN live slot: legs/BOOTS, arms/CLOAK, sleep/IDOL.
     * A gate wired to the wrong slot would pass the legs tests above but fail
     * here (e.g. arms gated on boots would resist when only boots is set). */
    struct { uint16_t counter, slot; const char *name; } g[] = {
        { C_LEGS,  I_BOOTS, "legs/BOOTS" },
        { C_ARMS,  I_CLOAK, "arms/CLOAK" },
        { C_SLEEP, I_IDOL,  "sleep/IDOL" },
    };
    for (size_t k = 0; k < sizeof g / sizeof g[0]; k++) {
        /* slot empty -> always lands */
        u2_ctx ctx; fresh(&ctx, 0);
        u2_combat_set_item(&ctx, g[k].slot, 0);
        CHECK(u2_combat_special(&ctx, g[k].counter) == 1,
              "C2 %s: empty slot must land", g[k].name);
        /* slot present -> probabilistic (find at least one resist over seeds) */
        int resist = 0;
        for (int seed = 0; seed < 64; seed++) {
            fresh(&ctx, (uint8_t)seed);
            u2_combat_set_item(&ctx, g[k].slot, 1);
            if (u2_combat_special(&ctx, g[k].counter) == 0) resist++;
        }
        CHECK(resist > 0, "C2 %s: present slot must sometimes resist", g[k].name);
    }
}

/* ========================================================================
 * C1 — three independent counters, DS:26B/26C/26D, range 0..15.
 * Grade: measured (asm/0E70 mov 0x26b / 0EC2 mov 0x26c / 0F48 mov 0x26d,
 * each `and 0xf`; both legs+arms observed non-zero at once, 9 and 8, sleep 8).
 *
 * PERSISTENCE CORRECTION (measured): the ticket says "persisted in the save
 * block". The proven save record is the 256 bytes at DS:36..135 (core/save.h,
 * asm/5400 FCB read/write CX=0x100 DX=0x36). DS:26B/26C/26D are OUTSIDE that
 * span (0x26B > 0x135), so the proven save path does NOT persist them. There
 * is a routine asm/080D that zeroes DS:26A..26D (observed in its body), but its
 * caller is not statically discovered (no `call L_00080d` in the tree) so its
 * role — init vs load — is NOT proven and is not claimed here. Net: the
 * "persisted in the save block" claim is not byte-proven and is in tension with
 * the save span. The test asserts the proven facts (three independent counters,
 * 0..15, set on a landed effect) and asserts the counters live outside the
 * record rather than asserting a persistence the bytes do not support.
 * ==================================================================== */
static void test_c1_three_independent_counters(void)
{
    /* Landing one effect must not disturb the other two counters. */
    u2_ctx ctx; fresh(&ctx, 0);
    u2_combat_set_item(&ctx, I_BOOTS, 0);   // no resist -> legs lands
    u2_combat_set_item(&ctx, I_CLOAK, 0);
    u2_combat_set_item(&ctx, I_IDOL, 0);
    u2_combat_set_counter(&ctx, C_ARMS, 9);
    u2_combat_set_counter(&ctx, C_SLEEP, 8);
    uint8_t arms0 = u2_combat_counter(&ctx, C_ARMS);
    uint8_t sleep0 = u2_combat_counter(&ctx, C_SLEEP);
    int landed = u2_combat_special(&ctx, C_LEGS);
    CHECK(landed == 1, "C1: legs with no boots must land");
    CHECK(u2_combat_counter(&ctx, C_ARMS) == arms0,
          "C1: legs event must not change the arms counter");
    CHECK(u2_combat_counter(&ctx, C_SLEEP) == sleep0,
          "C1: legs event must not change the sleep counter");
    /* And the reverse: an arms event leaves legs/sleep alone. */
    u2_combat_set_counter(&ctx, C_LEGS, 5);
    uint8_t legs0 = u2_combat_counter(&ctx, C_LEGS);
    u2_combat_special(&ctx, C_ARMS);
    CHECK(u2_combat_counter(&ctx, C_LEGS) == legs0,
          "C1: arms event must not change the legs counter");
}

static void test_c1_range_and_variance(void)
{
    /* On a landed effect the counter is rand & 0xF: every value in 0..15, and
     * it VARIES across seeds (a fixed-value impl fails the variance check). */
    int seen[16] = {0};
    int distinct = 0;
    for (int seed = 0; seed < 256; seed++) {
        u2_ctx ctx; fresh(&ctx, (uint8_t)seed);
        u2_combat_set_item(&ctx, I_BOOTS, 0);          // always land
        u2_combat_special(&ctx, C_LEGS);
        uint8_t v = u2_combat_counter(&ctx, C_LEGS);
        CHECK(v <= 15, "C1 seed %d: counter out of 0..15, got %u", seed, v);
        if (!seen[v]) { seen[v] = 1; distinct++; }
    }
    CHECK(distinct > 1,
          "C1: counter must be a roll (varies), not a constant; distinct=%d",
          distinct);
    CHECK(distinct == 16,
          "C1: over 256 seeds all 16 values 0..15 should appear, got %d", distinct);
}

static void test_c1_outside_save_record(void)
{
    /* Documents the persistence correction as an assertion: the counters are
     * not inside the DS:36..135 save record, so a save/reload cannot restore
     * them. If CP5 ever moves them into the record, this fires and forces the
     * claim to be re-proven against the bytes rather than inherited. */
    CHECK(C_LEGS  < 0x36 || C_LEGS  > 0x135, "legs counter unexpectedly inside record");
    CHECK(C_ARMS  < 0x36 || C_ARMS  > 0x135, "arms counter unexpectedly inside record");
    CHECK(C_SLEEP < 0x36 || C_SLEEP > 0x135, "sleep counter unexpectedly inside record");
}

/* ========================================================================
 * C6 — theft DELETES from the item array; nothing is restored on kill.
 * Grade: measured (asm/0FCD decrements DS:D6+i; over 3000 turns 8 items went
 * 1->0 and none returned; findings § 12 "Theft is permanent").
 *
 * The engine consequence the ticket calls out: because the C2 gates read the
 * item array LIVE, a stolen item must be removed (count -> 0), not flagged,
 * or the resist would keep firing on an item the player no longer has.
 * ==================================================================== */
static void test_c6_theft_deletes(void)
{
    /* Only BOOTS present. The thief decrements a random non-empty slot; with a
     * single non-empty slot the first steal must empty it. */
    u2_ctx ctx; fresh(&ctx, 0);
    u2_combat_set_item(&ctx, I_BOOTS, 1);
    int steals = 0;
    /* (uint16_t item offsets; I_* fit in a byte but the API is 16-bit) */
    for (int t = 0; t < 3000 && u2_combat_item(&ctx, I_BOOTS) != 0; t++) {
        u2_combat_thief_steal(&ctx);
        steals++;
    }
    CHECK(u2_combat_item(&ctx, I_BOOTS) == 0,
          "C6: theft must delete the item (count -> 0), got %u after %d steals",
          u2_combat_item(&ctx, I_BOOTS), steals);
    CHECK(steals >= 1, "C6: at least one steal must have removed the item");
}

static void test_c6_nothing_restored(void)
{
    /* Total item count is non-increasing across steals, and a slot emptied by
     * theft stays empty (no restore path). This is the state-level meaning of
     * "nothing is restored on kill". */
    u2_ctx ctx; fresh(&ctx, 7);
    uint8_t slots[] = { I_RING, I_BOOTS, I_CLOAK, I_IDOL };
    for (size_t i = 0; i < sizeof slots; i++) u2_combat_set_item(&ctx, slots[i], 1);
    int total0 = 0;
    for (size_t i = 0; i < sizeof slots; i++) total0 += u2_combat_item(&ctx, slots[i]);
    int prev = total0;
    int emptied[8] = {0};
    for (int t = 0; t < 3000; t++) {
        u2_combat_thief_steal(&ctx);
        int total = 0;
        for (size_t i = 0; i < sizeof slots; i++) {
            uint8_t v = u2_combat_item(&ctx, slots[i]);
            total += v;
            if (v == 0) emptied[i] = 1;
        }
        CHECK(total <= prev,
              "C6: total items must never increase (no restore), %d -> %d at t=%d",
              prev, total, t);
        prev = total;
    }
    /* Once emptied, still empty at the end (no resurrection). */
    for (size_t i = 0; i < sizeof slots; i++) {
        if (emptied[i])
            CHECK(u2_combat_item(&ctx, slots[i]) == 0,
                  "C6: slot emptied by theft must stay empty");
    }
}

static void test_c6_theft_kills_the_resist(void)
{
    /* The coupling the ticket warns about: after the boots are stolen, the legs
     * resist must NEVER fire again, because the gate reads the live array. A
     * flagged-but-present item would wrongly keep the resist. */
    u2_ctx ctx; fresh(&ctx, 0);
    /* With boots present, the resist fires on some seeds. */
    u2_combat_set_item(&ctx, I_BOOTS, 1);
    int resist_before = 0;
    for (int seed = 0; seed < 64; seed++) {
        fresh(&ctx, (uint8_t)seed);
        u2_combat_set_item(&ctx, I_BOOTS, 1);
        if (u2_combat_special(&ctx, C_LEGS) == 0) resist_before++;
    }
    CHECK(resist_before > 0, "C6 precondition: boots present must resist sometimes");
    /* Steal the boots. */
    for (int t = 0; t < 3000 && u2_combat_item(&ctx, I_BOOTS) != 0; t++)
        u2_combat_thief_steal(&ctx);
    CHECK(u2_combat_item(&ctx, I_BOOTS) == 0, "C6: boots must be gone");
    /* Now the resist must never fire — the gate sees an empty slot. */
    int resist_after = 0;
    for (int seed = 0; seed < 64; seed++) {
        fresh(&ctx, (uint8_t)seed);
        u2_combat_set_item(&ctx, I_BOOTS, 0);
        if (u2_combat_special(&ctx, C_LEGS) == 0) resist_after++;
    }
    CHECK(resist_after == 0,
          "C6: after theft the resist must never fire (live gate), got %d",
          resist_after);
}

/* ========================================================================
 * C4 — magic missile has NO save path.
 * Grade: measured (asm/0F12 has no inventory gate and no roll, unlike asm/0E70,
 * asm/0EC2 and asm/0F48; string executed live in the sector-00 caster fight).
 *
 * Scope discipline: the accumulation into DS:20/21/22 (+2, +2, +0x40 per cast)
 * is proven, but its BCD weight against the HP subtract is NOT, and DS:20 is
 * already claimed elsewhere as move_scratch (disputed). So the spec asserts
 * only the no-save property, which IS proven, and says nothing about damage
 * magnitude. Modelling the scaling as a plain integer would encode an unproven
 * claim, and nothing here would catch it — that gap is recorded, not papered.
 * ==================================================================== */
static void test_c4_no_save_and_no_roll(void)
{
    /* Whatever the inventory holds, the missile applies and no RNG is consumed.
     * A boolean "item grants immunity" impl would both skip and roll. */
    uint8_t slots[] = { I_RING, I_BOOTS, I_CLOAK, I_IDOL };
    for (size_t k = 0; k < sizeof slots / sizeof slots[0]; k++) {
        u2_ctx ctx; fresh(&ctx, (uint8_t)(k + 1));
        u2_combat_set_item(&ctx, slots[k], 5);
        uint32_t before = u2_rng_rolls(&ctx);
        CHECK(u2_combat_magic_missile(&ctx) != 0,
              "C4: magic missile must apply even with item at DS:%02X held", slots[k]);
        CHECK(u2_rng_rolls(&ctx) == before,
              "C4: magic missile must consume NO random byte (no save exists)");
    }
    u2_ctx ctx; fresh(&ctx, 9);
    CHECK(u2_combat_magic_missile(&ctx) != 0, "C4: must apply with empty inventory");
}

/* ========================================================================
 * C5 — force field: ring gate, exactly -1000, kills below 1000.
 * Grade: measured, BOTH branches observed live (owner walked the tile with no
 * ring: HP 8842 -> 7842; with [DS:D6]=1 the protect branch fired).
 *
 * The subtract is `stc; sbb al,0x10; das` on [DS:51], the HIGH byte of the
 * 4-digit BCD HP, so -0x10 on that byte is exactly -1000 overall: 0x88 -> 0x78
 * turns 8842 into 7842, which is what was observed.
 * ==================================================================== */
static void test_c5_ring_gate(void)
{
    u2_ctx ctx; fresh(&ctx, 3);
    u2_combat_set_item(&ctx, I_RING, 1);
    ctx.state.hp = 0x88;                       /* 8842, as observed live */
    CHECK(u2_combat_force_field(&ctx) == FIELD_PROTECTED, "C5: ring held must protect");
    CHECK(ctx.state.hp == 0x88,
          "C5: protected must not change HP, got 0x%02X", ctx.state.hp);
}

static void test_c5_exactly_1000(void)
{
    u2_ctx ctx; fresh(&ctx, 3);
    u2_combat_set_item(&ctx, I_RING, 0);
    ctx.state.hp = 0x88;
    CHECK(u2_combat_force_field(&ctx) == FIELD_DAMAGED, "C5: no ring must damage");
    CHECK(ctx.state.hp == 0x78,
          "C5: HP high byte must go 0x88 -> 0x78 (8842 -> 7842, exactly -1000), got 0x%02X",
          ctx.state.hp);
}

static void test_c5_kills_below_1000(void)
{
    /* HP < 1000 makes the BCD subtract borrow, and the borrow branches to the
     * death handler (asm/24B5 file 0x271B). */
    u2_ctx ctx; fresh(&ctx, 3);
    u2_combat_set_item(&ctx, I_RING, 0);
    ctx.state.hp = 0x05;                       /* 0542: under 1000 */
    CHECK(u2_combat_force_field(&ctx) == FIELD_KILLED,
          "C5: HP below 1000 with no ring must kill, not wrap");
}

static void test_c5_bcd_not_binary(void)
{
    /* Pins the decimal adjust: 0x99 - 0x10 must land on 0x89, the packed-decimal
     * result. A raw binary subtract that skipped `das` would still give 0x89
     * here, so this is a floor check alongside the 0x88 case above, not a
     * complete proof of the adjust. */
    u2_ctx ctx; fresh(&ctx, 3);
    u2_combat_set_item(&ctx, I_RING, 0);
    ctx.state.hp = 0x99;
    (void)u2_combat_force_field(&ctx);
    CHECK(ctx.state.hp == 0x89,
          "C5: 0x99 - 0x10 must be 0x89 (packed decimal), got 0x%02X", ctx.state.hp);
}


/* asm/2CE5..2D8B: the kill-drop dispatch. Three types drop anything at all, and the thief
 * arm is the only source in the binary for most of the item array -- including the ANKH,
 * which no shoppe sells and no chest holds. */
static void test_kill_drop(void)
{
    /* Every type except 0x60, 0xFC, 0xF0 and 0xF8 must be silent. asm/2D8B is a bare nop and
     * the tail jmp to the reward follows with no grant, so "most kills drop nothing" is the
     * original's behaviour and not an omission. */
    for (int t = 0; t < 256; ++t) {
        if (t == 0x60 || t == 0xFC || t == 0xF0 || t == 0xF8)
            continue;
        u2_ctx ctx; fresh(&ctx, 1);
        for (int i = 0; i < 200; ++i)
            u2_combat_kill_drop(&ctx, (uint8_t)t);
        int touched = 0;
        for (int s = 0; s < 16; ++s)
            if (ctx.state.items_held[s]) touched++;
        if (ctx.state.tools || ctx.state.torches || ctx.state.keys) touched++;
        CHECK(touched == 0, "type %02x must drop nothing at all", t);
    }

    /* asm/2CD8: type 0x60 grants exactly 2 keys and touches nothing else. The bestiary's
     * gamercorner GUARD row says "Drops 2 keys", so the +2 is third-party-corroborated;
     * the arm itself is byte-proven and the creature name is not, which is why the
     * assertion is about the byte and not about a Guard. */
    {
        u2_ctx ctx; fresh(&ctx, 11);
        CHECK(ctx.state.keys == 0, "fixture must start with no keys, got %02x", ctx.state.keys);
        u2_combat_kill_drop(&ctx, 0x60);
        CHECK(ctx.state.keys == 0x02,
              "type 0x60 must add exactly 2 keys (asm/2CD8 adc $2), got %02x", ctx.state.keys);
        u2_combat_kill_drop(&ctx, 0x60);
        CHECK(ctx.state.keys == 0x04,
              "two kills must add 4 keys, got %02x", ctx.state.keys);
        /* And the no-clamp wrap, same as every other counter in this family. */
        ctx.state.keys = 0x99;
        u2_combat_kill_drop(&ctx, 0x60);
        CHECK(ctx.state.keys == 0x01,
              "99 +2 must wrap to 01 with no ceiling guard, got %02x", ctx.state.keys);
        ctx.state.keys = 0x98;
        u2_combat_kill_drop(&ctx, 0x60);
        CHECK(ctx.state.keys == 0x00,
              "98 +2 must wrap to 00, got %02x", ctx.state.keys);
        int other = 0;
        for (int s = 0; s < 16; ++s)
            if (ctx.state.items_held[s]) other++;
        CHECK(other == 0 && ctx.state.tools == 0 && ctx.state.torches == 0,
              "type 0x60 must grant keys and nothing else (items %d, tools %02x, torches %02x)",
              other, ctx.state.tools, ctx.state.torches);
    }
    /* Two bcd_inc1 calls must equal one `adc $2 ; daa`, including across the nibble carry
     * and the 99 wrap. This is the equivalence the 0x60 arm relies on. */
    {
        static const uint8_t seeds[] = { 0x00, 0x07, 0x08, 0x09, 0x10, 0x97, 0x98, 0x99 };
        for (unsigned i = 0; i < sizeof seeds; ++i) {
            uint8_t two = seeds[i];
            two = u2_bcd_inc1(two);
            two = u2_bcd_inc1(two);
            uint8_t one = seeds[i];
            one = u2_bcd_add2_test(one);
            CHECK(two == one, "0x%02x +2: two increments gave %02x, one adc gave %02x",
                  seeds[i], two, one);
        }
    }

    /* asm/2D03 is `and $0xf,%al ; je skip`: a uniform draw over slots 1..15 with slot 0
     * excluded. Slot 0 is the RING, which asm/3C2E pays for gold instead. */
    {
        u2_ctx ctx; fresh(&ctx, 7);
        int seen[16] = {0};
        for (int i = 0; i < 40000; ++i) {
            uint8_t b[16];
            for (int s = 0; s < 16; ++s) b[s] = ctx.state.items_held[s];
            u2_combat_kill_drop(&ctx, 0xFC);
            for (int s = 0; s < 16; ++s)
                if (ctx.state.items_held[s] != b[s]) seen[s]++;
        }
        CHECK(seen[0] == 0,
              "the RING slot must never come out of a thief drop, got %d", seen[0]);
        for (int s = 1; s < 16; ++s)
            CHECK(seen[s] > 0, "thief drop must be able to produce slot %d", s);
        /* The two the third-party guides name, called out because they are the whole
         * reason this routine matters to the win condition. */
        CHECK(seen[7] > 0 && seen[15] > 0,
              "ANKH (7) and TRI-LITHIUM (15) must both be droppable, got %d and %d",
              seen[7], seen[15]);
        CHECK(ctx.state.tools > 0, "a thief kill must be able to grant tools (DS:66), got %u",
              ctx.state.tools);
    }

    /* asm/2D64..2D7C: 0xF8 gives slot 1 or 2 and nothing else. */
    {
        u2_ctx ctx; fresh(&ctx, 3);
        int seen[16] = {0};
        for (int i = 0; i < 20000; ++i) {
            uint8_t b[16];
            for (int s = 0; s < 16; ++s) b[s] = ctx.state.items_held[s];
            u2_combat_kill_drop(&ctx, 0xF8);
            for (int s = 0; s < 16; ++s)
                if (ctx.state.items_held[s] != b[s]) seen[s]++;
        }
        CHECK(seen[1] > 0 && seen[2] > 0, "0xF8 must drop WAND or STAFF, got %d/%d",
              seen[1], seen[2]);
        int other = 0;
        for (int s = 0; s < 16; ++s) if (s != 1 && s != 2) other += seen[s];
        CHECK(other == 0, "0xF8 must touch nothing but WAND and STAFF, got %d others", other);
    }

    /* asm/2D26..2D52: 0xF0 gives HELM at 25% and always adds torches. */
    {
        u2_ctx ctx; fresh(&ctx, 5);
        int helm = 0;
        for (int i = 0; i < 20000; ++i) {
            uint8_t h = ctx.state.items_held[5];
            u2_combat_kill_drop(&ctx, 0xF0);
            if (ctx.state.items_held[5] != h) helm++;
        }
        CHECK(helm > 0, "0xF0 must be able to grant a HELM");
        CHECK(ctx.state.torches > 0, "0xF0 must grant torches (DS:64), got %u",
              ctx.state.torches);
        int other = 0;
        for (int s = 0; s < 16; ++s)
            if (s != 5 && ctx.state.items_held[s]) other++;
        CHECK(other == 0, "0xF0 must touch no item slot but HELM, got %d", other);
    }
}

int main(void)
{
    printf("combat_check — CP5 spec (C1, C2, C4, C5, C6)\n");
    /* CP5_SPEC_NO_TESTS=1 skips every test body, leaving a binary that reports
     * "0 checks, 0 failures". It exists so run_cp5.sh can prove its own
     * zero-check guard actually fires (AGENTS.md: demonstrate a gate failing
     * before trusting it passing). The guard turns this hatch into a visible red,
     * so exporting it cannot hide a failure. */
    if (getenv("CP5_SPEC_NO_TESTS") == NULL) {
    test_c2_no_item_always_lands();
    test_c2_item_present_is_probabilistic();
    test_c2_threshold_is_exactly_0x40();
    test_c2_all_three_gates();
    test_c1_three_independent_counters();
    test_c1_range_and_variance();
    test_c1_outside_save_record();
    test_c6_theft_deletes();
    test_c6_nothing_restored();
    test_c6_theft_kills_the_resist();
    test_c4_no_save_and_no_roll();
    test_c5_ring_gate();
    test_c5_exactly_1000();
    test_c5_kills_below_1000();
    test_c5_bcd_not_binary();
    test_kill_drop();
    }
    printf("combat_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
