/*
 * tests/chest_check.c — asm/363B_cmd_G's loot decision, made deterministic.
 *
 * The headless CP7 chest case (run_cp7.sh § 5) proves the chest is found on a 0x40
 * cell and the " CHEST! IT CONTAINS " line prints. It cannot prove WHICH loot comes
 * out, because the loot is a roll and the headless run cannot pin the register. This
 * unit gate pins it: the register is a feedback shift register (core/rng.c), so a
 * seeded state replays byte for byte, and the first rand_byte inside u2_dungeon_get
 * (for a non-deepest floor) is exactly the gold-versus-item decision roll.
 *
 * What the bytes say (asm/363B_cmd_G.asm L_00396a..L_0038d8):
 *   - TRI-LITHIUM is gated on [DS:25] == 0xf (the deepest floor) and is the ONLY
 *     way to obtain it. It is NOT a random outcome on a shallow floor. This is the
 *     bug this gate exists to hold: an earlier engine gave TRI-LITHIUM on any roll
 *     >= 0x40, i.e. 75% of chests on every floor.
 *   - otherwise one roll: >= 0x40 -> GOLD (75%), < 0x40 -> an item (25%).
 *   - the item is a WEAPON on an even floor, ARMOUR on an odd one (rcr of the floor
 *     byte, jae = even). A second roll picks the slot; slot 0 is " EMPTY!".
 *   - GOLD amount = ((rand & 0x1f) + 2*floor) & 0x77, added to the odometer with
 *     the same adc/daa pair asm/2DC1 uses (u2_bcd_adc2).
 *
 * Build and run by tests/run_cp7.sh.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/state.h"
#include "core/rng.h"
#include "core/world.h"
#include "game/dungeon.h"
#include "game/overworld.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* Seed the register from a counter so the runs sweep the decision space. */
static void make_seed(uint8_t out[6], unsigned n)
{
    for (int i = 0; i < 6; i++)
        out[i] = (uint8_t)(n * 31u + i * 7u + 1u);
}

/* The first `n` rand_bytes a fresh GET would draw from this seed. */
static uint8_t peek(const uint8_t seed[6], int n)
{
    u2_ctx c;
    memset(&c, 0, sizeof c);
    u2_rng_load(&c, seed);
    uint8_t v = 0;
    for (int i = 0; i < n; i++)
        v = u2_rand_byte(&c);
    return v;
}

/* Put the player on a chest on `floor`, seed the register, and run GET. */
static void run_get(u2_ctx *ctx, u2_overworld *ow, uint8_t floor, const uint8_t seed[6])
{
    memset(ctx, 0, sizeof *ctx);
    memset(ow, 0, sizeof *ow);
    ow->x = 5; ow->y = 5;
    ctx->state.face_dx = 1;
    ctx->state.face_dy = 0;
    ctx->state.site_floor = floor;
    ctx->fidelity = U2_FIDELITY_ORIGINAL;
    u2_world_dungeon_put(&ow->world, floor, 5, 5, U2_DG_CHEST);
    u2_rng_load(ctx, seed);
    u2_dungeon_get(ctx, ow);
}

int main(void)
{
    /* The shallow floors this sweep exercises: even and odd, and one below 0xf. */
    static const uint8_t shallow[] = { 0, 1, 2, 3, 14 };

    /* --- the bug this gate exists to hold: TRI-LITHIUM never drops on a shallow
     * floor, for any roll. items_held[15] (DS:e5) is the TRI-LITHIUM slot. --- */
    {
        int lithium_on_shallow = 0;
        for (unsigned n = 0; n < 400; n++) {
            uint8_t seed[6]; make_seed(seed, n);
            for (size_t f = 0; f < sizeof shallow; f++) {
                u2_ctx ctx; u2_overworld ow;
                run_get(&ctx, &ow, shallow[f], seed);
                if (ctx.state.items_held[15] != 0)
                    lithium_on_shallow++;
            }
        }
        CHECK(lithium_on_shallow == 0,
              "TRI-LITHIUM (items_held[15]) appeared on a shallow floor %d times "
              "(it is gated on floor==0xf only)", lithium_on_shallow);
    }

    /* --- on the deepest floor (0xf) it ALWAYS drops, whatever the roll. --- */
    {
        int lithium_missed = 0;
        for (unsigned n = 0; n < 400; n++) {
            uint8_t seed[6]; make_seed(seed, n);
            u2_ctx ctx; u2_overworld ow;
            run_get(&ctx, &ow, 0xf, seed);
            if (ctx.state.items_held[15] != 1)
                lithium_missed++;
        }
        CHECK(lithium_missed == 0,
              "TRI-LITHIUM failed to drop on floor 0xf %d times (should be every chest)",
              lithium_missed);
    }

    /* --- the roll split: the decision roll decides gold vs item, and the floor
     * parity decides weapon vs armour. Gold amount matches the BCD formula. --- */
    {
        int wrong_branch = 0, wrong_gold = 0, wrong_parity = 0, both_changed = 0;
        for (unsigned n = 0; n < 400; n++) {
            uint8_t seed[6]; make_seed(seed, n);
            for (size_t fi = 0; fi < sizeof shallow; fi++) {
                const uint8_t floor = shallow[fi];
                u2_ctx ctx; u2_overworld ow;
                run_get(&ctx, &ow, floor, seed);

                const uint8_t decision = peek(seed, 1);
                int gold_changed = (ctx.state.gold_lo != 0 || ctx.state.gold_hi != 0);
                int weapon_changed = 0, armour_changed = 0;
                for (int i = 0; i < 10; i++)
                    if (ctx.state.weapons_owned[i]) weapon_changed = 1;
                for (int i = 0; i < 6; i++)
                    if (ctx.state.armour_owned[i]) armour_changed = 1;

                /* Exactly one bucket moved (or none, for an EMPTY item roll). */
                int moved = gold_changed + weapon_changed + armour_changed;
                if (moved > 1) both_changed++;

                if (decision >= 0x40) {
                    /* amount = ((second & 0x1f) + 2*floor) & 0x77, from a zero odometer.
                       It can legitimately be 0 (e.g. floor 0 with second&0x1f==0), in
                       which case the odometer is unchanged even though the gold branch
                       ran — so the branch is judged by the amount, not by movement. */
                    const uint8_t second = peek(seed, 2);
                    const uint8_t amount =
                        (uint8_t)(((second & 0x1fu) + 2u * floor) & 0x77u);
                    const int amount_int = (amount >> 4) * 10 + (amount & 0x0f);
                    if ((amount != 0) != gold_changed) wrong_branch++;
                    if (weapon_changed || armour_changed) wrong_branch++;
                    if (u2_bcd16(ctx.state.gold_lo, ctx.state.gold_hi) != amount_int)
                        wrong_gold++;
                } else {
                    /* item branch: gold untouched; weapon on even floor, armour on odd. */
                    if (gold_changed) wrong_branch++;
                    if (floor & 1u) {
                        if (weapon_changed) wrong_parity++;   /* odd floor -> armour */
                    } else {
                        if (armour_changed) wrong_parity++;   /* even floor -> weapon */
                    }
                }
            }
        }
        CHECK(both_changed == 0,
              "%d chests moved more than one loot bucket (must be exactly one)", both_changed);
        CHECK(wrong_branch == 0,
              "%d chests took the wrong gold/item branch for their decision roll", wrong_branch);
        CHECK(wrong_gold == 0,
              "%d gold chests credited the wrong amount (formula ((r&0x1f)+2*floor)&0x77)",
              wrong_gold);
        CHECK(wrong_parity == 0,
              "%d item chests gave the wrong type for the floor parity (even=weapon,odd=armour)",
              wrong_parity);
    }

    /* --- control: the increment is tied to the chest, not unconditional. A GET
     * on a plain floor cell (not 0x40) must change nothing. --- */
    {
        int leaked = 0;
        for (unsigned n = 0; n < 50; n++) {
            uint8_t seed[6]; make_seed(seed, n);
            u2_ctx ctx; u2_overworld ow;
            memset(&ctx, 0, sizeof ctx);
            memset(&ow, 0, sizeof ow);
            ow.x = 5; ow.y = 5;
            ctx.state.face_dx = 1;
            ctx.state.site_floor = 0xf;      /* deepest, so a chest WOULD give lithium */
            ctx.fidelity = U2_FIDELITY_ORIGINAL;
            /* No chest placed: the cell under the player is plain floor. */
            u2_rng_load(&ctx, seed);
            u2_dungeon_get(&ctx, &ow);
            if (ctx.state.items_held[15] || ctx.state.gold_lo || ctx.state.gold_hi)
                leaked++;
        }
        CHECK(leaked == 0,
              "%d GETs on a non-chest cell still granted loot (the gate is the 0x40 cell)",
              leaked);
    }

    printf("chest_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
