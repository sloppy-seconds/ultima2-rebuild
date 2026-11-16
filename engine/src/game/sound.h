#ifndef U2_SOUND_H
#define U2_SOUND_H

#include "core/ctx.h"

/* The two PC-speaker sounds the original makes on the outdoor map, and nothing
 * else. Both are measured, not inferred: divisor from the register dump at
 * asm/5CD2_speaker_freq while the sound played, duration from the span the
 * speaker gate stayed up. See captures/spice86/step-sound-20261117/ for the
 * probes, their controls and the numbers.
 *
 * What is deliberately NOT here: asm/5DA9's tone player (the wound and HIT tones,
 * reached through asm/5EAC / asm/5ED7 / asm/5EF2), whose frequency arithmetic is
 * still inferred and whose durations have no bytes; and asm/5E83's descending
 * sweep, which spells -FAILED! for a spell that did not land. Those stay unwired
 * rather than guessed, and asm/5DA9's own header says why. */

/* asm/5EC7 -> asm/5D09 sequence 0: one record at CS:5D5D, {count 0x05, divisor
 * 0x4000}, which is a 73 Hz thud. The original plays it TWICE per step attempt —
 * once when the step is taken and once about 120 ms later, at the far end of the
 * step's redraw — and it plays it whether the step is accepted or refused,
 * because the arm that sounds it (asm/2575 / asm/2590, both arms of the mode
 * dispatch at asm/253B) runs ahead of the terrain verdict, same as the food
 * charge beside it. */
void u2_sound_step(u2_ctx *ctx);

/* asm/5C95: divisor 0x500 (932 Hz) held for cx=0x4000 delay units, which measures
 * about 175 ms — an order of magnitude longer than the step thud, and the sound
 * of the game refusing you. asm/0A30 is the shared tail that plays it, and it is
 * jumped to from the invalid-move path, the no-door path, the offer-what path,
 * the can't-afford path, the vehicle refusals and the arms-refused attack path.
 * The engine currently sounds it only where the refusal has been measured. */
void u2_sound_refusal(u2_ctx *ctx);

#endif /* U2_SOUND_H */
