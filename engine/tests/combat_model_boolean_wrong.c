/*
 * combat_model_boolean_wrong.c — NEGATIVE control for tests/combat_check.c.
 *
 * This file is DELIBERATELY WRONG and must never be used as a reference. It
 * exists so tests/run_cp5.sh can prove that combat_check.c actually catches the
 * mistake the pre-existing documentation made. If the spec ever stops failing
 * against this model, the spec has gone soft and CP5 is unprotected.
 *
 * The three planted defects, each a real historical error rather than a
 * strawman:
 *
 *   1. BOOLEAN IMMUNITY. "Has boots" is treated as immunity, so the effect never
 *      lands while the item is held. This is exactly what the old docs said before
 *      findings § 12 measured the ~75% roll.
 *   2. FIXED COUNTER. The landed counter is a constant instead of rand & 0xF, so
 *      the counter never varies.
 *   3. THEFT FLAGS INSTEAD OF DELETING. The stolen item is marked but its count
 *      stays non-zero, so a live-slot gate keeps honouring an item the player no
 *      longer has.
 *
 * Expected result: a large number of failures across C1, C2 and C6. Zero
 * failures here means the spec is broken.
 */
#include <stdint.h>
#include <string.h>

#include "core/ctx.h"
#include "core/rng.h"
#include "core/state.h"
/* The contract header, so this negative control differs from the positive one only in
 * the semantics the spec must reject, never in the signatures it declares. */
#include "game/combat.h"

#define C_LEGS   0x26B
#define C_ARMS   0x26C
#define C_SLEEP  0x26D
#define I_BOOTS  0xD9
#define I_CLOAK  0xDA
#define I_IDOL   0xE4
#define ITEM_LO  0xD6
#define ITEM_HI  0xE5
#define ITEM_N   (ITEM_HI - ITEM_LO + 1)

#define WRONG_FIXED_COUNTER 8   /* defect 2: a constant, not a roll */

static uint8_t counters[3];
static uint8_t items[ITEM_N];
static uint8_t stolen_flag[ITEM_N];   /* defect 3: mark, do not remove */

static int cidx(uint16_t ds)
{
    switch (ds) {
    case C_LEGS:  return 0;
    case C_ARMS:  return 1;
    case C_SLEEP: return 2;
    default:      return -1;
    }
}

static uint16_t gate_slot(uint16_t counter_ds)
{
    switch (counter_ds) {
    case C_LEGS:  return I_BOOTS;
    case C_ARMS:  return I_CLOAK;
    case C_SLEEP: return I_IDOL;
    default:      return 0xFFFF;
    }
}

uint8_t u2_combat_counter(const u2_ctx *ctx, uint16_t counter_ds)
{
    (void)ctx;
    int i = cidx(counter_ds);
    return i < 0 ? 0 : counters[i];
}

void u2_combat_set_counter(u2_ctx *ctx, uint16_t counter_ds, uint8_t value)
{
    (void)ctx;
    int i = cidx(counter_ds);
    if (i >= 0) counters[i] = value;
}

uint8_t u2_combat_item(const u2_ctx *ctx, uint16_t item_ds)
{
    (void)ctx;
    if (item_ds < ITEM_LO || item_ds > ITEM_HI) return 0;
    return items[item_ds - ITEM_LO];   /* still non-zero after a theft: defect 3 */
}

void u2_combat_set_item(u2_ctx *ctx, uint16_t item_ds, uint8_t count)
{
    (void)ctx;
    if (item_ds >= ITEM_LO && item_ds <= ITEM_HI) items[item_ds - ITEM_LO] = count;
}

int u2_combat_special(u2_ctx *ctx, uint16_t counter_ds)
{
    uint16_t slot = gate_slot(counter_ds);
    /* Defect 1: item present means immune. No roll is taken at all. */
    if (slot != 0xFFFF && u2_combat_item(ctx, slot) != 0)
        return 0;
    /* Defect 2: a fixed value where the asm rolls rand & 0xF. */
    u2_combat_set_counter(ctx, counter_ds, WRONG_FIXED_COUNTER);
    return 1;
}

uint16_t u2_combat_thief_steal(u2_ctx *ctx)
{
    int nonempty = 0;
    for (int i = 0; i < ITEM_N; i++) if (items[i]) nonempty++;
    if (nonempty == 0) return 0;
    int pick = (int)(u2_rand_byte(ctx) % (uint8_t)nonempty);
    for (int i = 0; i < ITEM_N; i++) {
        if (!items[i]) continue;
        if (pick-- == 0) {
            stolen_flag[i] = 1;   /* defect 3: flagged, count left intact */
            return (uint16_t)(ITEM_LO + i);
        }
    }
    return 0;
}

/* defect 4 — assumes every combat event has an item save, so magic missile
 * rolls and can be skipped. asm/0F12 has no save path at all. */
int u2_combat_magic_missile(u2_ctx *ctx)
{
    if (u2_combat_item(ctx, I_BOOTS) != 0) {
        (void)u2_rand_byte(ctx);   /* a roll that must never happen */
        return 0;
    }
    return 1;
}

/* defect 5 — treats the -1000 as a plain binary subtract of 1 and never takes
 * the death branch. asm/24B5 does `stc; sbb al,0x10; das` on the high BCD byte
 * and branches to the death handler on borrow. */
int u2_combat_force_field(u2_ctx *ctx)
{
    if (u2_combat_item(ctx, ITEM_LO) != 0) return 0;
    ctx->state.hp = (uint8_t)(ctx->state.hp - 1);
    return 1;
}
