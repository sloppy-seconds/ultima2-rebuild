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

int u2_combat_attack(u2_ctx *ctx, int slot, u2_attack *out);

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
int u2_combat_cannon(u2_ctx *ctx, int cx, int cy, u2_attack *out);


/* asm/2CE5..2D8B: which item a kill grants, keyed on the TYPE of the creature that died.
 * Split out and public so the distribution is testable without driving a whole fight; the
 * three arms are the only sources in the binary for most of the item array. */
void u2_combat_kill_drop(u2_ctx *ctx, uint8_t dead_type);

#endif /* U2_GAME_COMBAT_H */
