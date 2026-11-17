#ifndef U2_GAME_COMBAT_H
#define U2_GAME_COMBAT_H
/*
 * CP5 — combat specials and theft. Contract owned by tests/combat_check.c;
 * these prototypes exist so combat.c compiles under -Wmissing-prototypes.
 *
 * Everything is keyed by the DS offset it mirrors rather than by a field name,
 * so the offsets are the contract and the storage stays the implementation's
 * choice (see the header comment of tests/combat_check.c).
 */
#include <stdint.h>
#include "core/ctx.h"

struct u2_world;   /* forward: the world is by pointer only here; core/world.h owns the body */

/* Run one special-effect event against `counter_ds` (0x26B legs, 0x26C arms,
 * 0x26D sleep). Returns 1 if the effect landed and the counter was set, 0 if it
 * was resisted. spec: asm/0E70, asm/0EC2, asm/0F48 */
int u2_combat_special(u2_ctx *ctx, uint16_t counter_ds);

uint8_t u2_combat_counter(const u2_ctx *ctx, uint16_t counter_ds);
void    u2_combat_set_counter(u2_ctx *ctx, uint16_t counter_ds, uint8_t value);

/* Live inventory accessors over state.items_held (DS:D6..DS:E5). */
uint8_t u2_combat_item(const u2_ctx *ctx, uint16_t item_ds);
void    u2_combat_set_item(u2_ctx *ctx, uint16_t item_ds, uint8_t count);

/* One thief steal attempt. Returns the DS offset of the slot emptied, or 0 when
 * nothing was taken. spec: asm/0FCD */
uint16_t u2_combat_thief_steal(u2_ctx *ctx);

/* C4: magic missile. Non-zero when the damage applies. Consumes NO random byte,
 * because no save exists. spec: asm/0F12 */
int u2_combat_magic_missile(u2_ctx *ctx);

/* C5: force field. 0 = ring protected, 1 = took exactly 1000, 2 = killed.
 * Mutates state.hp (DS:51, the high BCD byte). spec: asm/24B5 */
int u2_combat_force_field(u2_ctx *ctx);

/*
 * W3: the player's attack on a creature slot.
 *   0 = MISS, 1 = hit and it survived, 2 = killed.
 * Mutates the creature arrays (mon_hp always; mon_type and mon_kind on a kill,
 * which is what the original clears — NOT the x/y pair) and, on a kill, the gold
 * and exp BCD fields. spec: asm/2E3D arithmetic via the asm/2B8F cmd_A chain.
 *
 * The arms-paralysis gate (asm/2BA5_attack_arms_gate) and the site-kind gate
 * (asm/2B9B) are NOT here: they refuse the attack before any roll is taken and
 * print their own message, so they belong to the command layer.
 */
typedef struct u2_attack {
    uint8_t damage;   /* proven formula; 0 when the swing missed */
    uint8_t gold;     /* BCD amount added to gold on a kill, else 0 */
    uint8_t exp;      /* BCD amount added to exp on a kill, else 0 */
} u2_attack;

/* Every one of these takes the world, and takes it because asm/2CB5 does: the kill path
 * points the cell pointer at the dead creature's cell (asm/2CA4 -> asm/50B3, which only
 * computes an address) and then writes DS:1B7[slot] — the terrain byte saved at spawn —
 * through it. The original therefore CLEARS the corpse's cell on the kill and leaves no
 * ghost on the map. A kill with w == NULL skips that write, which is what the arithmetic
 * tests pass; it is not a valid game call. */
int u2_combat_attack(u2_ctx *ctx, struct u2_world *w, int slot, u2_attack *out);

/*
 * The frigate's cannon. spec: asm/35ED..asm/3611, damage via asm/2C19.
 *
 * cx/cy is the cell asm/23E6 would probe — the player's position plus the aim delta, ONE cell,
 * which is the point the owner's recollection and the bytes disagree on; see the long note at
 * the definition and G49 in docs/needs-gameplay.md.
 *
 * Returns the same three codes as u2_combat_attack: 0 = no creature there (asm/2E09 prints
 * "--MISS"), 1 = hit and it survived, 2 = killed. out->damage is the rolled byte, rand|0x20,
 * which is 32..63 and is NOT the melee formula.
 */
int u2_combat_cannon(u2_ctx *ctx, struct u2_world *w, int cx, int cy, u2_attack *out);


/* asm/3139 slot 7: MAGIC MISSILE applies a caller-computed amount to one slot.
 * Same three return codes as u2_combat_attack. The amount is computed by the spell, not here:
 * DS:20 = 2*DS:56 + 0x1e + CF and the `cmc ; sbb` idiom then subtracts DS:20 + 1, which is
 * arithmetic with a shape worth reading in cmd_C before trusting it. */
int u2_combat_cast_damage(u2_ctx *ctx, struct u2_world *w, int slot, unsigned dmg, u2_attack *out);

/* asm/3139 L_0034b2: the MAGIC MISSILE amount as a pure function of [DS:56], the HIGH
 * packed-decimal byte of total experience. Doubling a BCD byte with no `daa` is a sawtooth,
 * not a scale: it peaks at 255 for 0x70 and collapses to 0 for 0x71. Exposed so a gate can pin
 * the curve -- a rewrite that "fixes" this produces a different spell and nothing else in the
 * engine would notice. combat.c carries the instruction-by-instruction transcription. */
unsigned u2_cast_missile_amount(uint8_t exp_hi);

/* asm/3139 slots 6 and 9: PRAYER and KILL reach asm/30D1 with no damage step, so the creature
 * dies at any HP and the reward block runs as it does for melee. Always returns 2. */
int u2_combat_cast_kill(u2_ctx *ctx, struct u2_world *w, int slot, u2_attack *out);


/* asm/2CE5..2D8B: which item a kill grants, keyed on the TYPE of the creature that died.
 * Split out and public so the distribution is testable without driving a whole fight; the
 * three arms are the only sources in the binary for most of the item array. */
void u2_combat_kill_drop(u2_ctx *ctx, uint8_t dead_type);


/* Test surface for the packed-decimal equivalence the type-0x60 arm depends on: two
 * single-step increments must equal one `adc $2,%al ; daa`. */
uint8_t u2_bcd_add2_test(uint8_t v);

#endif /* U2_GAME_COMBAT_H */
