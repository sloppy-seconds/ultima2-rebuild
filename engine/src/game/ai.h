/*
 * ai.h — the creature turn: pursuit, movement, and the special-attack gate.
 *
 * CP5 W6/W6b. Two passes over the 32 creature slots, both of them proven in the
 * bytes and both written up in docs/monster-spawn.md § 13:
 *
 *   asm/0A6D..0AB5  per slot: skip empty, compute the pursuit vector
 *                   (asm/4F36), and if |dx| < 3 and |dy| < 3 roll 1/8 for one of
 *                   the four specials (asm/0AAA gate, asm/0AD3 dispatch).
 *   asm/2122..2260  per slot: regenerate below 16 HP, then try to step along the
 *                   pursuit vector — diagonal first, then y-only, then x-only —
 *                   where each candidate must pass a per-species tile test.
 *
 * There is no "attack the player on contact" in the original. A creature whose
 * candidate cell is the player's cell does not move, and a thief in that
 * position attempts a steal (asm/0B4D -> asm/0FCD). Everything else about being
 * next to a monster is the player's problem, not the monster's.
 */
#ifndef U2_GAME_AI_H
#define U2_GAME_AI_H

#include "core/ctx.h"
#include "core/world.h"

typedef struct u2_ai_report {
    int examined;   /* non-empty slots the pass looked at                       */
    int moved;      /* slots that changed cell                                 */
    int gates;      /* times the asm/0AAA proximity test passed and the roll ran */
    int landed;     /* specials that were not resisted                          */
    int stolen;     /* successful thefts (asm/0FCD)                             */
    int retreated;  /* slots whose pursuit vector was negated (asm/0B0F)        */
    int contacts;   /* slots that would have stepped onto the player (asm/0B4D)  */
    /* Minax's ranged strike (asm/1040 -> asm/0F8E): how many times she fired the
     * < 4 proximity attack this turn. She is role 0 in a castle, so this is the
     * only thing the gated sweep does for her; see the gated_sweep branch in ai.c. */
    int minax_strikes;
    /* The melee damage pool at DS:22. Each contacting creature that lands a blow
     * adds `mon_hp >> 2 + 1` (asm/0B79) and the caller applies the pool with
     * asm/0BDB's randomised mask. Zero means nothing landed, and asm/0BD1 is what
     * turns a zero pool into no damage at all rather than a damage of 1. */
    unsigned damage_pool;
} u2_ai_report;

/*
 * One creature turn. Call it once per elapsed turn, after whatever the player
 * did. `px`/`py` are the player's cell in sector coordinates.
 *
 * Prints the effect messages the original prints from inside these routines
 * ("LEGS PARALIZED!", "SAVED BY MAGICAL BOOTS!", "A THIEF STOLE SOMETHING!" and
 * the rest) into the message window, because in the original the print lives
 * inside the effect routine and not in a caller.
 *
 * Returns the report above. The caller is responsible for u2_monsters_store()
 * when rep.moved is non-zero.
 */
u2_ai_report u2_ai_creature_turn(u2_ctx *ctx, u2_world *w, int px, int py);

/*
 * The alarm — asm/113A, reached from asm/2BE9 on a landed hit and from
 * asm/3F5D_steal_attempt.
 *
 *     113a  if [DS:4A] == 0 -> ret            ; the world map has no guards
 *     1142  di := 7 ; al := 1
 *     114a  mon_role[di] := al ; dec di ; jns 114a
 *
 * So violence inside a site sets role 1 on slots 0..7, and role 1 is the pass that
 * casts and chases while role 0 does nothing at all. That is the whole of the
 * sector-wide guard response measured in docs/spice86-live-findings.md § 16, and
 * it explains why a missed swing flips nothing: asm/2BE9 sits on the hit path,
 * before the "--HIT!!!" printer, and the miss path never reaches it.
 *
 * One arm is deliberately not reproduced. The loop falls through into asm/1151,
 * which writes the same 1 into mon_role[DS:20] — and DS:20 is the damage byte on
 * the attack path and a poll counter elsewhere, so what slot it names here is not
 * established. Writing a slot on an unproven index is worse than not writing it.
 */
void u2_ai_alarm(u2_ctx *ctx);

#endif /* U2_GAME_AI_H */
