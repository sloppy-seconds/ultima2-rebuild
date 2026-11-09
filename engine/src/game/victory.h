#ifndef U2_GAME_VICTORY_H
#define U2_GAME_VICTORY_H
/*
 * victory.h — CP8: the endgame, and the condition that fires it.
 *
 * What is evidence and what is not
 * --------------------------------
 * The gate and the sequence were decoded statically on 2026-11-09 from asm/, and NOTHING
 * HERE HAS EVER EXECUTED IN A CAPTURE. That is stated first because the project's own rule
 * is that a plausible implementation reads as finished, and this one does not: the strings
 * it prints are discovered-but-not-executed in all 210 harvests. See docs/needs-gameplay.md
 * G75 for the capture that closes that gap, and docs/goal-spec.md § 1a for what the decode
 * changed about the win condition.
 *
 * The gate is three instructions in asm/2CB5_combat_kill_minax_gate:
 *
 *     mov 0x197(%di),%al     ; mon_type[slot]
 *     cmp $0x40,%al          ; Minax?
 *     jne L_002ed3           ; no -> the ordinary kill-drop dispatch at asm/2CD3
 *                            ; yes -> fall through into asm/2CCD_victory_call
 *
 * There is no weapon test, no damage-source test, no HP threshold and no era test anywhere on
 * this path. That is the single most important fact in this file, because it means the
 * quick-sword requirement the walkthroughs state is NOT a property of the win condition.
 */
#include "core/ctx.h"

/* asm/2CB5's compare operand. 0x40 is Minax: the art index is type>>2 == 0x10, which is the
 * tile asm/0F8E's own note identifies, and asm/1040 tests the same byte. */
#define U2_MON_TYPE_MINAX 0x40

/* asm/2CCD_victory_call -> asm/72A0 head. Prints the ending and applies the state writes the
 * original makes after them. Sets ctx->victory so a caller, a gate or the play API can see
 * that the game ended; the original does not return from this path at all, it enters a
 * looping animation, so a caller should treat the run as over. */
void u2_victory(u2_ctx *ctx);

#endif
