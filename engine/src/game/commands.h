#ifndef U2_COMMANDS_H
#define U2_COMMANDS_H

/*
 * Command dispatch -- the asm/0A1F shape, as one table.
 *
 * The original reaches its 26 command handlers through a jump table at cs:0730, and
 * `asm/0A1F_cmd_dispatch.asm` is the only route to them:
 *
 *   sbb $0x41,%al        ; al = key - 'A'
 *   add al,al            ; *2, a word index
 *   mov $0x0,%ah
 *   mov %ax,%si
 *   mov %cs:0x730(%si),%bx
 *   jmp *%bx
 *
 * Two things about that shape are load-bearing for this file and are the reason it exists
 * separately from overworld.c, which no longer handles any command letter inline: all six
 * that were there (A, E, P, Q, U, Z) have moved.
 *
 * First, the range test happens BEFORE the dispatch, in two pieces: `cmp $0x41,%al ;
 * jae L_000c18` (asm/09FB) and `cmp $0x5a,%al ; jbe L_000c1f` (asm/0A18). A key outside
 * A-Z never reaches a handler; it takes the other arm at asm/0A1C. So "not a command
 * letter" is a distinct outcome from "a command letter whose handler does nothing", and a
 * C switch that lumps them together cannot express the difference.
 *
 * Second, handlers do not return. Every one of them ends in `jmp L_000c33`
 * (asm/0A33_cmd_done), which prints CR and falls through to asm/0A38_post_command_tail --
 * the tail that runs the creature sweep and therefore one wandering-spawn attempt after
 * every command (docs/monster-spawn.md section 15.2). The epilogue is shared and
 * unconditional, which is why this dispatcher owns it rather than each caller.
 *
 * The migration is deliberate and partial. Six letters (A, E, P, Q, U, Z) were already
 * implemented inside u2_overworld_loop with hard-won evidence in their comments, and they
 * move here one at a time under the cover of the existing tests, not in one sweep. Q moved
 * first, on 2026-10-25, because its entire body was already a call to a public function, so
 * the move tested the dispatch rather than the behaviour; P followed, needing a shared
 * public body because the space bar reaches the same result from a different site; then Z,
 * which needed a new result code because it can end the session from inside itself; then E,
 * whose body moved to a public u2_overworld_enter() for the same reason P's did. A and U
 * remain, and they are last on purpose: they leave the loop waiting for a direction key that
 * arrives turns later, so they need the pending state to move before they can. What lands
 * here alongside them is the letters that were never implemented at all.
 */

#include "core/ctx.h"
#include "game/overworld.h"

/* What the loop needs to know after a handler runs. */
typedef enum {
    U2_CMD_DONE,     /* finished; run the post-command tail (CR, prompt, sweep) */
    U2_CMD_AWAIT,    /* printed a prompt and is waiting for a direction key; the
                        original consumed the next key inside the handler chain, so
                        no turn passes and the sweep does not run yet */
    U2_CMD_UNKNOWN   /* not a command letter at all -- the asm/0A1C arm, NOT the same
                        thing as a handler that declined to act */
} u2_cmd_result;

/* The shared command epilogue: CR, then the "CMD: " prompt. asm/0A33_cmd_done,
 * which every completed handler reaches, including the refusals. Public because
 * the dungeon commands in game/dungeon.c complete commands too. */
void u2_cmd_done(u2_ctx *ctx);

/*
 * Dispatch one command letter. `letter` is the raw key byte; the range test is inside,
 * because that is where the original has it. Returns U2_CMD_UNKNOWN for anything outside
 * A-Z, and the caller decides what the illegal-command path prints.
 */
u2_cmd_result u2_cmd_dispatch(u2_ctx *ctx, u2_overworld *ow, uint8_t letter);

/* Which letters this table actually implements, for the tests and the marker gate.
 * Returns the number of letters handled, and writes a NUL-terminated letter list into
 * `out` (capacity >= 27). */
int u2_cmd_implemented(char *out, int cap);

/*
 * A command that ended by waiting for a direction.
 *
 * Six letters print a prompt and consume the NEXT key as an aim rather than as a
 * step: UNLOCK (U), TRANSACT (T), STEAL DIRECT (S), OFFER GOLD DIRECT (O), FIRE
 * DIRECT (F) and ATTACK (A). The shape is the original's and not a convenience:
 * the handler chain calls the asm/2362 aim gate and every exit from it is a tail
 * jump to asm/0A33_cmd_done, so the direction word lands on the command line, the
 * epilogue is the shared one, and a turn passes even when the aim was refused.
 *
 * The table lives here because which letter continues how is command knowledge.
 * The loop that runs it lives in game/overworld.c because the epilogue ends in the
 * creature sweep, which belongs to the overworld. The two meet at
 * u2_cmd_continuation, and neither side has to know the other's list.
 *
 * Three live fields. A fourth was expected to be `is_turn` and is not needed:
 * every one of these commands passes a turn, refusals included, which is a finding
 * rather than an omission -- see the bytes and the corpus cited beside the table.
 * FIRE is not in the table: its epilogue is interleaved with its own result
 * handling, so forcing it in would need a field meaning "this entry ignores the
 * epilogue", which would be the table admitting it is the wrong shape.
 */
typedef struct u2_cmd_cont {
    char letter;
    int print_word;      /* echo the direction word on the prompt line, no CR */
    int redraw_status;   /* the handler can move a number the status band shows */
    void (*on_direction)(u2_ctx *ctx, u2_overworld *ow, int dx, int dy,
                         const char *word);
} u2_cmd_cont;

/* The continuation for a pending letter, or NULL when the letter does not
 * continue. The caller clears its own pending flag; this only answers the
 * question of how a letter continues, if it does. */
const u2_cmd_cont *u2_cmd_continuation(char letter);

#endif /* U2_COMMANDS_H */
