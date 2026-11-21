/*
 * combat_model_faithful.c — POSITIVE control for tests/combat_check.c.
 *
 * A reference implementation of the CP5 contract written from the asm, NOT from
 * any engine code. Its only job is to prove the spec is satisfiable: if this
 * model fails the spec, the spec is wrong, not the future implementation.
 *
 * Why a fixture and not just a comment: the spec's whole value is that it
 * discriminates a probabilistic resist from a boolean one, and that claim was
 * originally proven with throwaway models in /var/tmp that no longer exist and
 * nobody could re-run. AGENTS.md § 5 — a control nobody can re-run is weak
 * evidence. So the controls ship.
 *
 * Semantics implemented (grades per AGENTS.md § 5):
 *   asm/0E70, asm/0EC2, asm/0F48 (measured, findings § 12):
 *       if (own item slot == 0) goto LAND;          // lands, NO roll
 *       if (rand_byte() >= 0x40) { SAVED; return; } // resist, counter untouched
 *     LAND:
 *       counter = rand_byte() & 0xF;                // 0..15 turns
 *   asm/0FCD (measured): theft decrements a non-empty item slot; nothing is
 *   restored when the thief dies.
 *
 * The counters are at DS:26B/26C/26D, above the 0xFF span that u2_state mirrors,
 * so this fixture keeps them in its own store and reaches them only through the
 * contract's accessors. That is the spec's intent: the offsets are the contract,
 * the storage is the implementation's choice.
 *
 * Fixture limitation: state is file-static, not per-u2_ctx. Every test in
 * combat_check.c sets the fields it reads before reading them, so this is
 * sufficient; a real combat.c should keep this state in u2_ctx instead.
 */
#include <stdint.h>
#include <string.h>

#include "core/ctx.h"
#include "core/rng.h"
#include "core/state.h"
/* The contract header, so these definitions are checked against the prototypes the
 * engine actually declares. Without it the model could drift from combat.h and still
 * compile, which is the one thing a positive control must not do. */
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

static uint8_t counters[3];
static uint8_t items[ITEM_N];

static int cidx(uint16_t ds)
{
    switch (ds) {
    case C_LEGS:  return 0;
    case C_ARMS:  return 1;
    case C_SLEEP: return 2;
    default:      return -1;
    }
}

/* Which live item slot gates which special (asm/0E70 file 0x108D, asm/0EC2
 * file 0x10DF, asm/0F48 file 0x1162). */
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
    return items[item_ds - ITEM_LO];
}

void u2_combat_set_item(u2_ctx *ctx, uint16_t item_ds, uint8_t count)
{
    (void)ctx;
    if (item_ds >= ITEM_LO && item_ds <= ITEM_HI) items[item_ds - ITEM_LO] = count;
}

int u2_combat_special(u2_ctx *ctx, uint16_t counter_ds)
{
    uint16_t slot = gate_slot(counter_ds);
    if (slot != 0xFFFF && u2_combat_item(ctx, slot) != 0) {
        /* Item present: the resist is a roll, not immunity. >= 0x40 resists and
         * leaves the counter untouched. */
        if (u2_rand_byte(ctx) >= 0x40) return 0;
    }
    /* No item, or the roll failed: the effect lands and the counter is a roll. */
    u2_combat_set_counter(ctx, counter_ds, (uint8_t)(u2_rand_byte(ctx) & 0xF));
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
            items[i]--;          /* deletion, not a flag */
            return (uint16_t)(ITEM_LO + i);
        }
    }
    return 0;
}

/* C4 — asm/0F12 has no inventory gate and no roll, so the missile always
 * applies and consumes no random byte. Damage magnitude is deliberately not
 * modelled: the DS:20/21/22 accumulation's BCD weight is unproven. */
int u2_combat_magic_missile(u2_ctx *ctx)
{
    (void)ctx;
    return 1;
}

/* C5 — asm/24B5. Gate is the RING slot (DS:D6 == ITEM_LO). The damage is
 * `stc; sbb al,0x10; das` on the HIGH BCD byte of HP, i.e. exactly -1000. */
int u2_combat_force_field(u2_ctx *ctx)
{
    if (u2_combat_item(ctx, ITEM_LO) != 0) return 0;      /* FIELD_PROTECTED */
    uint8_t hp = ctx->state.hp;
    if ((hp >> 4) == 0) { ctx->state.hp = 0; return 2; }  /* FIELD_KILLED */
    ctx->state.hp = (uint8_t)(hp - 0x10);                 /* packed decimal: the
                                                           * low nibble is carried
                                                           * through untouched, so
                                                           * subtracting 0x10 from
                                                           * the high nibble IS the
                                                           * das-adjusted result. */
    return 1;                                             /* FIELD_DAMAGED */
}
