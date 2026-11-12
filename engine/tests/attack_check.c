/*
 * tests/attack_check.c — CP5 W3: the player's attack, against the real combat.c.
 *
 * This is NOT part of combat_check.c on purpose. That file is a specification that
 * runs against two fixture models, and it exists to pin a contract before the
 * code lands. The attack is implemented, so it gets tested directly; adding it to
 * the spec would mean writing two more fixture models of a thing that already
 * exists, which is how you end up with three copies of a rule drifting apart.
 *
 * Every expectation cites the byte it came from. The three that are easy to get
 * wrong, and are therefore pinned hardest here:
 *   - the hit operand is the AGILITY BYTE, not strength (asm/2BDA, asm/2E3D 0x307E);
 *   - damage is computed on packed-decimal bytes, so strength 40 is 64 (0x309F);
 *   - the gold/exp field names in state.h are inverted relative to significance.
 *
 * Build and run by tests/run_cp5.sh.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/rng.h"
#include "game/combat.h"
#include "game/monsters.h"

static int fails, checks;

/* CP8: the kill gate hands off to game/victory.c, which prints through the platform seam and
 * so cannot be linked into this minimal harness. It is stubbed here instead -- and the stub IS
 * the test. What this harness can prove is WHICH kills hand off, and that is the new logic;
 * the sequence itself is the engine's business. */
static int victory_calls;
void u2_victory(u2_ctx *ctx) { (void)ctx; victory_calls++; }

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* The first rand_byte a freshly-seeded ctx produces, which is the roll the attack
 * consumes first (asm/2E3D calls rand before anything else on this path). */
static uint8_t first_roll(uint8_t seed)
{
    u2_ctx p;
    memset(&p, 0, sizeof p);
    u2_rng_seed(&p, seed);
    return u2_rand_byte(&p);
}

static void arm(u2_ctx *ctx, uint8_t seed, uint8_t agi, uint8_t str, uint8_t weapon)
{
    memset(ctx, 0, sizeof *ctx);
    u2_rng_seed(ctx, seed);
    ctx->state.stat_agi = agi;
    ctx->state.stat_str = str;
    ctx->state.readied_weapon = weapon;
    ctx->state.mon_x[4] = 10;      /* a creature standing somewhere */
    ctx->state.mon_y[4] = 12;
    ctx->state.mon_type[4] = 0xF0;
    ctx->state.mon_kind[4] = 0x70;
    ctx->state.mon_hp[4] = 200;
}

int main(void)
{
    u2_ctx c;
    u2_attack a;

    /* --- 1. the hit operand is agility, not strength --------------------- */
    /* agi 0 can never hit; strength is set high so a strength-based reading
     * would hit, which makes this pair a discriminator rather than a tautology. */
    int zero_agi_hits = 0, high_agi_misses = 0;
    for (int s = 0; s < 256; s++) {
        arm(&c, (uint8_t)s, 0x00, 0x40, 9);
        if (u2_combat_attack(&c, 4, &a) != 0) zero_agi_hits++;

        arm(&c, (uint8_t)s, 0xFF, 0x00, 9);
        if (u2_combat_attack(&c, 4, &a) == 0) high_agi_misses++;
    }
    CHECK(zero_agi_hits == 0,
          "agi 0 never hits regardless of strength (%d unexpected hits)", zero_agi_hits);
    CHECK(high_agi_misses == 0,
          "agi 0xFF always hits regardless of strength (%d unexpected misses)", high_agi_misses);

    /* The rate must track the agility BYTE, i.e. chance = agi/128. agi 0x40 (64)
     * is 50%; a strength-driven or fixed implementation lands elsewhere. */
    int hits = 0;
    for (int s = 0; s < 256; s++) {
        arm(&c, (uint8_t)s, 0x40, 0x40, 9);
        if (u2_combat_attack(&c, 4, &a) != 0) hits++;
    }
    CHECK(hits > 110 && hits < 146,
          "agi byte 0x40 hits ~50%% of 256 seeds (got %d)", hits);

    /* Cross-check against the roll the engine actually consumed, so the assertion
     * above cannot be satisfied by a lucky seed distribution. */
    int agree = 1;
    for (int s = 0; s < 256; s++) {
        arm(&c, (uint8_t)s, 0x20, 0x40, 9);
        int got = u2_combat_attack(&c, 4, &a);
        int want = ((first_roll((uint8_t)s) >> 1) < 0x20) ? 1 : 0;
        if ((got != 0) != want) agree = 0;
    }
    CHECK(agree, "every hit/miss matches (rand >> 1) < agi byte, per seed");

    /* --- 2. damage is computed on the packed-decimal BYTES --------------- */
    /* str 0x40 (= 64 decimal) and weapon 9: (8*9 + 64) >> 2 = 34. This is the
     * live measurement (Fighter 50 -> 16, capture cp5-dmg), not a guess. */
    arm(&c, 0, 0xFF, 0x40, 9);
    CHECK(u2_combat_attack(&c, 4, &a) == 1 && a.damage == 34,
          "str byte 0x40, weapon 9 -> 34 damage (got %u)", a.damage);

    /* The engine must read the byte VERBATIM, with no decimal conversion of its
     * own — converting is the save layer's job (tools/u2_edit.py writes these
     * fields with kind 'bcd'). So byte 0x28 gives 28 and byte 0x40 gives 34; an
     * implementation that decoded the BCD first would produce 28 for both. */
    arm(&c, 0, 0xFF, 0x28, 9);
    u2_combat_attack(&c, 4, &a);
    CHECK(a.damage == 28,
          "strength byte 0x28 -> 28, i.e. the byte is used verbatim (got %u)", a.damage);

    /* HANDS (weapon 0) still does something: (0 + 64) >> 2 = 16. */
    arm(&c, 0, 0xFF, 0x40, 0);
    u2_combat_attack(&c, 4, &a);
    CHECK(a.damage == 16, "unarmed with str byte 0x40 -> 16 (got %u)", a.damage);

    /* --- 3. a kill clears hp/type/kind and NOT the position -------------- */
    arm(&c, 0, 0xFF, 0x40, 9);
    c.state.mon_hp[4] = 20;                    /* 34 > 20, so this kills */
    int r = u2_combat_attack(&c, 4, &a);
    CHECK(r == 2, "damage above hp is a kill (got %d)", r);
    CHECK(c.state.mon_hp[4] == 0 && c.state.mon_type[4] == 0 && c.state.mon_kind[4] == 0,
          "kill clears hp, type and kind");
    CHECK(c.state.mon_x[4] == 10 && c.state.mon_y[4] == 12,
          "kill leaves x and y alone — asm/2E3D clears three fields, not five");
    CHECK(a.gold >= 1 && (a.gold & 0x17u) == a.gold && (a.gold & 1u) == 1u,
          "gold is (rand & 0x17) | 1 (got %u)", a.gold);
    CHECK(a.exp <= 7u, "exp is rand & 0x07 (got %u)", a.exp);

    /* --- 4. the inverted branch sense: hp == damage SURVIVES at zero ------ */
    arm(&c, 0, 0xFF, 0x40, 9);
    c.state.mon_hp[4] = 34;                    /* equal, not less */
    r = u2_combat_attack(&c, 4, &a);
    CHECK(r == 1 && c.state.mon_hp[4] == 0,
          "hp == damage stores 0 and leaves a live creature (asm/2E3D 0x30C8 jae), got r=%d hp=%u",
          r, c.state.mon_hp[4]);

    /* --- 5. the BCD reward adds, including the wrap ---------------------- */
    /* Field order is (hi = DS:58 = gold_lo, lo = DS:59 = gold_hi) per asm/2DC1:
     * the original adds into DS:59 first and carries into DS:58, so the state.h
     * names read backwards. 9999 + 15 -> 14 is the B1 ticket's measured value. */
    int found15 = 0;
    for (int s = 0; s < 256 && !found15; s++) {
        arm(&c, (uint8_t)s, 0xFF, 0x40, 9);
        c.state.mon_hp[4] = 1;
        c.state.gold_lo = 0x99;
        c.state.gold_hi = 0x99;
        u2_combat_attack(&c, 4, &a);
        if (a.gold == 0x15) {
            found15 = 1;
            CHECK(c.state.gold_lo == 0x00 && c.state.gold_hi == 0x14,
                  "gold 9999 + 15 wraps to 0014 (B1's measured value), got %02x %02x",
                  c.state.gold_lo, c.state.gold_hi);
        }
    }
    /* Whether or not a seed happens to roll 0x15, the pair must stay valid BCD. */
    int bcd_ok = 1;
    for (int s = 0; s < 256; s++) {
        arm(&c, (uint8_t)s, 0xFF, 0x40, 9);
        c.state.mon_hp[4] = 1;
        c.state.gold_lo = 0x99;
        c.state.gold_hi = 0x99;
        u2_combat_attack(&c, 4, &a);
        if (((c.state.gold_lo & 0x0Fu) > 9u) || ((c.state.gold_lo >> 4) > 9u) ||
            ((c.state.gold_hi & 0x0Fu) > 9u) || ((c.state.gold_hi >> 4) > 9u))
            bcd_ok = 0;
    }
    CHECK(bcd_ok, "gold stayed valid packed decimal across every seed");

    /* The exact live observation: exp 9999 + 2 displayed 0001, i.e. the pair
     * wrapped and the carry out was discarded. */
    arm(&c, 0, 0xFF, 0x40, 9);
    c.state.mon_hp[4] = 1;
    c.state.exp_lo = 0x99;       /* DS:56 high */
    c.state.exp_hi = 0x99;       /* DS:57 low */
    /* Drive the add with a known exp by searching for a seed that rolls exp == 2. */
    int found2 = 0;
    for (int s = 0; s < 256 && !found2; s++) {
        arm(&c, (uint8_t)s, 0xFF, 0x40, 9);
        c.state.mon_hp[4] = 1;
        c.state.exp_lo = 0x99;
        c.state.exp_hi = 0x99;
        u2_combat_attack(&c, 4, &a);
        if (a.exp == 2) {
            found2 = 1;
            CHECK(c.state.exp_lo == 0x00 && c.state.exp_hi == 0x01,
                  "exp 9999 + 2 wraps to 0001 as seen live (got %02x %02x)",
                  c.state.exp_lo, c.state.exp_hi);
        }
    }
    CHECK(found2, "some seed rolls exp +2 so the wrap case is actually exercised");

    /* --- 6. control: the result must vary, or a fixed implementation passes */
    int distinct = 0;
    uint8_t seen[256] = {0};
    for (int s = 0; s < 256; s++) {
        arm(&c, (uint8_t)s, 0x40, 0x40, 9);
        int got = u2_combat_attack(&c, 4, &a);
        if (!seen[got]) { seen[got] = 1; distinct++; }
    }
    CHECK(distinct > 1,
          "attack outcomes vary across seeds — a fixed-hit implementation fails here");

    /* --- the frigate's cannon (asm/35ED..asm/3611) ---------------------------
     * The damage byte is `rand | 0x20`: bit 5 forced on a full-range random byte. That is NOT
     * "32..63", which is what asm/2C19's header has been saying and what the first draft of this
     * check asserted. `or` sets a bit, it does not mask, so the reachable set is every byte with
     * bit 5 SET — 32..63, 96..127, 160..191 and 224..255. The floor is 32 and the ceiling is 255.
     * Swept across all 256 seeds because the bound is the finding and a sample could not
     * distinguish "32..63" from "32..63 plus a rare outlier".
     *
     * The owner's recollection was "typically between 32 and 70". The 32 is exactly the formula's
     * floor. 70 (0x46) is not reachable at all — bit 5 is clear in it — so the upper end of that
     * memory does not match the bytes either way, and neither number is being quietly adopted.
     * What the bytes say is recorded here, and the disagreement is named in G49. */
    int c_lo = 999, c_hi = 0, c_hits = 0, c_over63 = 0;
    for (int s = 0; s < 256; s++) {
        arm(&c, (uint8_t)s, 0x40, 0x40, 3);
        u2_attack g;
        int cr = u2_combat_cannon(&c, 10, 12, &g);
        if (cr == 0) continue;
        c_hits++;
        if (g.damage < c_lo) c_lo = g.damage;
        if (g.damage > c_hi) c_hi = g.damage;
        if (g.damage > 63) c_over63++;
        CHECK((g.damage & 0x20u) != 0,
              "cannon damage %u does not have bit 5 set, seed %d", g.damage, s);
    }
    CHECK(c_hits > 0, "cannon reaches a creature standing on the probed cell");
    CHECK(c_lo == 32, "the cannon's floor is 32 (got %d)", c_lo);
    CHECK(c_over63 > 0,
          "values above 63 occur — this is the assertion that refutes asm/2C19's old '32..63'");

    /* No creature at the cell -> asm/2E09's --MISS, and nothing is hurt. */
    arm(&c, 7, 0x40, 0x40, 3);
    CHECK(u2_combat_cannon(&c, 11, 12, &a) == 0 && c.state.mon_hp[4] == 200,
          "cannon at an empty cell misses and hurts nothing");

    /* A cannon kill pays, and this is the assertion that used to claim the opposite.
     * The old comment read "asm/2C19's death branch lands on asm/2CA4, which stamps the
     * emptied cell, and the gold/exp rolls live on the melee path instead", and it was a
     * correct reading of eleven bytes and a wrong reading of the game: asm/2CA4 has no `ret`,
     * and the byte after it is asm/2CB5_combat_kill_minax_gate, on through the drop dispatch
     * to asm/2D8C_combat_kill_reward. Observed 2026-11-11 in
     * captures/spice86/cannon-kill-live: `KILLED--GOLD+13--EXP.+03` from a one-cell shot.
     * The visible line and the slot clear are gated end-to-end by engine/tests/cannon_kill.py;
     * what this unit can pin is that the reward struct is populated on the cannon route too. */
    arm(&c, 9, 0x40, 0x40, 3);
    c.state.mon_hp[4] = 0x30;                 /* 30 BCD, below the minimum roll of 32 */
    /* Zeroed rather than sampled, so the add is checkable exactly: with the field-name
     * inversion of state.h (gold_lo = DS:58 is the HIGH byte, gold_hi = DS:59 the LOW),
     * a small reward from zero lands entirely in the _hi fields. */
    c.state.gold_lo = c.state.gold_hi = 0;
    c.state.exp_lo  = c.state.exp_hi  = 0;
    int rr = u2_combat_cannon(&c, 10, 12, &a);
    CHECK(rr == 2, "a cannon blow above the creature's hp kills it (got %d)", rr);
    CHECK(c.state.mon_type[4] == 0 && c.state.mon_kind[4] == 0,
          "the kill clears type and kind");
    CHECK(a.gold >= 1 && (a.gold & 0x17u) == a.gold && (a.gold & 1u) == 1u,
          "a cannon kill rolls gold like a melee kill: (rand & 0x17) | 1 (got %u)", a.gold);
    CHECK((a.exp & 0x08u) == 0,
          "a cannon kill rolls exp like a melee kill: rand & 7 (got %u)", a.exp);
    CHECK(c.state.gold_hi == a.gold && c.state.gold_lo == 0
          && c.state.exp_hi == a.exp && c.state.exp_lo == 0,
          "the reward reached the totals, in the inverted field order asm/2DC1 uses "
          "(gold %02x%02x exp %02x%02x for +%u/+%u)",
          c.state.gold_lo, c.state.gold_hi, c.state.exp_lo, c.state.exp_hi,
          (unsigned)a.gold, (unsigned)a.exp);

    /* --- CP8: the victory gate, asm/2CB5_combat_kill_minax_gate ------------------------
       `mov 0x197(%di),%al ; cmp $0x40,%al ; jne <kill-drop>`. The kill path branches on the
       dying creature's TYPE and on nothing else -- no weapon test, no damage-source test, no
       HP threshold. That is why W2 in docs/goal-spec.md is no longer a win precondition. */
    arm(&c, 0x5A, 0x40, 0x40, 1);
    c.state.mon_type[4] = 0x40;                    /* Minax */
    c.state.mon_hp[4]   = 0x01;                    /* one blow from dead */
    victory_calls = 0;
    r = u2_combat_attack(&c, 4, &a);
    CHECK(r == 2, "the Minax blow is lethal (got %d; 2 = killed)", r);
    CHECK(victory_calls == 1,
          "killing a type-0x40 creature hands off to the victory sequence (asm/2CB5)");
    CHECK(c.state.mon_type[4] == 0, "and the kill still clears the slot on the way out");

    /* CONTROL: the same kill with any other type must NOT end the game. Without this the
     * first check would pass even if the gate fired on every kill. */
    arm(&c, 0x5A, 0x40, 0x40, 1);
    c.state.mon_type[4] = 0xF0;                    /* a Fighter: the ordinary drop path */
    c.state.mon_hp[4]   = 0x01;
    victory_calls = 0;
    r = u2_combat_attack(&c, 4, &a);
    CHECK(r == 2, "the control blow is lethal (got %d)", r);
    CHECK(victory_calls == 0, "CONTROL: a non-Minak kill does not reach the ending");

    /* CONTROL 2: a Minax that survives the blow must not end it either, or the gate would be
     * reading the slot rather than the kill. */
    arm(&c, 0x5A, 0x40, 0x40, 1);
    c.state.mon_type[4] = 0x40;
    c.state.mon_hp[4]   = 0x99;                    /* far above any single blow */
    victory_calls = 0;
    u2_combat_attack(&c, 4, &a);
    CHECK(victory_calls == 0, "CONTROL: a Minax still standing does not end the game");

    /* --- 7. slot 0 is invulnerable: asm/23E6 never scans it (G78) -------------------
     * The original's invulnerability is not a flag, a locked HP byte or a monster-defense
     * term. asm/23E6 find_creature_at_cursor walks slots 31..1 with `mov $0x1f,%bl; ... dec
     * %di; jne` and falls out at DI==0 WITHOUT examining slot 0, so the ATTACK chain
     * (asm/2BC4 -> asm/2BC7 `cmp $0,%bl; jne`) prints MISS for a slot-0 creature while it
     * still draws, blocks the cell and attacks. u2_monsters_at_attackable reproduces that
     * scan; u2_monsters_at (step-blocking) still includes slot 0, because the creature DOES
     * block movement. Live proof (Port Boniface, sector 31): the unkillable guard was slot 0
     * at (31,47); the three guards killed in the same session were slots 1,3,4, all
     * byte-identical (type 0x60 kind 0x70 role 0x01). The discriminator is the slot index. */
    {
        u2_ctx z;
        memset(&z, 0, sizeof z);
        /* Two byte-identical guards, one in slot 0 (invulnerable) and one in slot 1. */
        z.state.mon_type[0] = 0x60; z.state.mon_x[0] = 31; z.state.mon_y[0] = 47; z.state.mon_hp[0] = 255;
        z.state.mon_type[1] = 0x60; z.state.mon_x[1] = 31; z.state.mon_y[1] = 48; z.state.mon_hp[1] = 255;
        CHECK(u2_monsters_at_attackable(&z, 31, 47) == -1,
              "slot 0 is NOT attackable (asm/23E6 never examines it) — the invulnerable guard");
        CHECK(u2_monsters_at_attackable(&z, 31, 48) == 1,
              "slot 1 IS attackable — the killable guard, byte-identical to slot 0");
        /* CONTROL: the step-blocking scan still returns slot 0 (you cannot walk over it). */
        CHECK(u2_monsters_at(&z, 31, 47) == 0,
              "CONTROL: u2_monsters_at still returns slot 0 for step-blocking (it blocks movement)");
        /* CONTROL 2: if the scan were changed to start at 0 (the mutation this gate exists to
         * catch), slot 0 would become attackable. Assert it never returns slot 0. */
        CHECK(u2_monsters_at_attackable(&z, 31, 47) != 0,
              "the attack scan must never return slot 0 — gutting the skip fails here");
    }

    printf("attack_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
