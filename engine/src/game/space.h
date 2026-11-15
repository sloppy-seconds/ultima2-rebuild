/* space.h — the world coordinate model: orbit, hyperwarp, dive.
 *
 * Everything here is derived from bytes and then confirmed against
 * captures/spice86/g39-rocket-flight, the first session ever to enter a rocket.
 * The grades are per-item, because they are not all the same.
 *
 * Design rule, the same one core/scale.c follows: this file decides what the
 * numbers look like, and no renderer or command handler gets to invent them. It
 * takes the random source as an argument rather than calling one, so a test can
 * make the off-course roll happen on demand instead of hoping.
 */
#ifndef U2_SPACE_H
#define U2_SPACE_H

#include <stdint.h>

/* The world byte, DS:6D. Observed moving 0x00 -> 0x0A (deep space, after an
 * off-course hyperwarp) and 0x00 -> 0x09 (orbiting X, on course), each with a
 * frame and a live DS read in the same second. asm/7030 is the only writer. */
#define U2_WORLD_EARTH 0x00
#define U2_WORLD_DEEP_SPACE 0x0A
#define U2_WORLD_COUNT 10 /* indices 0..9; 0x0A is a state, not a world */

/* asm/6999_space_status, tables at file offset 0x7212 of the original. Located by
 * pattern rather than arithmetic: exactly one 30-byte run in the whole binary has
 * table[0] == 6/6/6 and table[9] == 9/9/9, which are the two triples this capture
 * read off live screens. */
typedef struct u2_space_world {
    uint8_t xeno;
    uint8_t yako;
    uint8_t zabo;
    const char *name; /* as asm/7030's dispatch prints it, without the full stop */
} u2_space_world;

/* The ten worlds. Index is the world byte. */
extern const u2_space_world u2_space_worlds[U2_WORLD_COUNT];

/* Which world, if any, sits at these coordinates. Returns the world byte for a
 * match, or U2_WORLD_DEEP_SPACE for none. This is the whole of asm/7030's orbit
 * test: your position has to EQUAL a world's triple to orbit it, and there is no
 * proximity, no radius, no tolerance.
 *
 * Observed: 9/9/9 -> X, and 7/1/1 -> deep space. */
uint8_t u2_space_world_at(uint8_t xeno, uint8_t yako, uint8_t zabo);

/* asm/7030's orbit determination (entry L_007230) tests one coordinate triple
 * against a literal before it consults the world table: if XENO==YAKO==ZABO==4 it
 * prints "U HIT THE SUN!" and then falls through to the table match anyway (4/4/4
 * equals no world, so the fall-through lands you in deep space). The sun is called
 * out separately because it is a distinct message on the path, not a world. The
 * bytes AFTER the sun message are misdisassembled (the string tail runs into the
 * next instruction), so whether the sun is merely a message or also a halt is NOT
 * decoded; this returns the fact that the triple is the sun and nothing more. */
int u2_space_is_sun(const uint8_t dest[3]);

/* asm/6DFD_hyperwarp_flow at L_007086.
 *
 *   mov 0xe5,%al ; clc ; rcr $1,%al ; jae skip ; clc ; rcr $1,%al ; jae skip
 *
 * so off-course fires iff the low two bits are both set, and the value it tests is
 * the POST-decrement fuel count. Observed on both arms: fuel 8 -> 7 fired, fuel
 * 7 -> 6 did not.
 *
 * rng supplies three bytes; each coordinate is then rand & 7, so 0..7. Because
 * world 9 is the only world with a 9 in its triple and the roll cannot produce a
 * 9, an off-course jump can never land on X by accident. */
typedef enum {
    U2_HYPERWARP_NO_FUEL,  /* "NO FUEL!" and nothing else happens */
    U2_HYPERWARP_ON_COURSE,/* keep the typed destination */
    U2_HYPERWARP_OFF_COURSE/* "SHIP OFF COURSE!", coordinates replaced by the roll */
} u2_hyperwarp_result;

u2_hyperwarp_result u2_space_hyperwarp(uint8_t fuel_count, const uint8_t rng[3],
                                       uint8_t dest[3]);

/* The sector a dive delivers you into. asm/7030:
 *   world == 0: tens = 4, ones = 0   -> sector 40
 *   world != 0: tens = world, ones = 0
 * The ones digit is always forced to 0, so a rocket always arrives in a world's
 * WILDERNESS sector and never in a site. Observed: Earth -> 40, world 9 -> 90. */
int u2_space_dive_sector(uint8_t world);

/* asm/7030 answers "REQUEST DENIED!" on exactly one condition, `cmp $0xa` against
 * the world byte: you cannot land from deep space. */
int u2_space_dive_allowed(uint8_t world);

/* asm/6999_space_status at L_006c57: `mov 0x62,%al ; cmp $0x5,%al ; jae`. The
 * armour pool is SKIN CLOTH LEATHER CHAIN PLATE REFLECT POWER NONE, so the gate is
 * REFLECT (5) or POWER (6).
 *
 * OBSERVED 2026-11-03 with PLATE seeded, and the consequence is NOT a death. The
 * failing arm prints "YOU HAVE EXPLODED!", calls L_007489, and then executes
 * `jmp L_006c77` -- a self-jump. The game halts. Measured: HP, FOOD and GOLD all
 * stayed put and the passive food tick stopped, and DS:51..59 were NOT zeroed, which
 * is the exact opposite of the death tail's signature. So a caller must not route
 * this through u2_death: it is a HANG, the character's ledger is untouched, and the
 * engine should say so out loud rather than pretend the character died.
 *
 * Do not oversell the difference either. Neither this nor death writes the save file
 * (docs/save-format.md § 10), so neither can cost a character permanently; what each
 * costs is the session. Claiming more than that is how the wrong mechanic gets
 * re-asserted with better vocabulary.
 *
 * Also observed: this is not reached by launching from Earth orbit. A PLATE character
 * launched, got "HYPERWARP ENGAGED!" and kept ticking. The check lives in the status
 * path. */
int u2_space_armour_ok(uint8_t worn_armour);

/* asm/38B0's descent, and the strongest confirmation in this project: this rule was
 * written from bytes alone, and then predicted both live landings to the cell.
 * y += 2 every step, x += 1 whenever the new y is a multiple of 8.
 *
 * The start is (0,0). This comment used to say (0,6), which was inferred from the
 * landing-cell arithmetic and is one step wrong; captures/spice86/dive-sweep-rate
 * sampled DS:00/DS:01 across a live dive and the first distinct position is (0,0).
 *
 * Advances *y and *x in place, wrapping at 64 like the original's byte arithmetic. */
void u2_space_descent_step(uint8_t *x, uint8_t *y);

/* The pace of the two animated launches, in milliseconds per march step.
 *
 * Both are the SAME borrowed figure, and neither is measured on its own loop. One march
 * step is one frame, and in the original a frame costs one whole sector render
 * (asm/5AB1_map_render): 492,074 emulated cycles between steps, measured over 13
 * consecutive steps in captures/spice86/dive-sweep-rate/dive1.csv. Divided by the
 * nominal 4.77 MHz that is 103 ms and about 9.7 steps/s.
 *
 * The division is the weak half and it is a LOWER BOUND, not an estimate with error bars.
 * Spice86 counts CPU cycles and models no 8088 bus wait states, so real hardware spent
 * more wall-clock time per cycle than the nominal clock implies. Owner recollection
 * 2026-11-15 is that the original's landing looked slower than the engine's, which is
 * exactly what a lower bound looks like when you meet it. docs/needs-gameplay.md G86 is
 * the capture that would replace the arithmetic with a loop-level figure.
 *
 * There is no timeout behind either loop and no delay primitive anywhere in the binary.
 * The speed-delay slot both loops load is discarded by its callee
 * (asm/0711_speed_delay_lookup), and asm/52CA — which an earlier reading called
 * `timed_wait` — is the DOS FCB call wrapper. See core/timer.c.
 *
 * Pinned by tests/dive_pace_check.c, which recomputes the division and fails if the
 * constant and its derivation part company. */
#define U2_DIVE_STEP_MS 103
#define U2_PLANE_STEP_MS 103

/* The dive's verdict on the tile it stopped on. asm/38B0: only 0x08 grass lives,
 * anything else jumps to runtime 0x0E62, the death tail. Observed twice, both
 * lethal: 0x00 water and 0x0c forest. Landing on 0x08 is predicted safe and has
 * never been tested, so a test that asserts survival is asserting an inference. */
int u2_space_dive_survives(uint8_t tile);

#endif /* U2_SPACE_H */
