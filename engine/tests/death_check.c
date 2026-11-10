/*
 * tests/death_check.c — CP5 W8: the death tail, against the real overworld.c.
 *
 * asm/0C62..0C99 as one span: zero DS:51..DS:59, repaint the status band, three
 * newlines, the name from DS:36, then " IS DEAD!" and a newline. The reference
 * frame is `captures/spice86/cp5-weak-fight/death1.png`, and it is the reason
 * two of the assertions below are where they are:
 *
 *   - The announcement is on row 22, NOT the bottom row. The trailing 0x8d in
 *     asm/0C8C's db is a newline, so the line scrolls one row up the moment it
 *     finishes, and row 23 is left blank. Intuition says "the last line is the
 *     bottom line"; the pixels say otherwise, and the pixels win.
 *   - There is no `CMD: ` prompt after it. The prompt travels with a command's
 *     compound echo, and the death tail has no command.
 *   - All four status fields read 0000, which is the observation that makes this
 *     file repaint the whole band where asm/0C62 calls only the H.P. line.
 *
 * Build and run by tests/run_cp5.sh.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/font.h"
#include "core/seam.h"
#include "core/state.h"
#include "core/vram.h"
#include "platform/platform.h"
#include "game/overworld.h"
#include "game/status.h"
#include "gen_content.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* A hand-built input backend, because this test links the game core and not the
 * platform layer: the handoff waits on seam_key_poll, and with no input wired that
 * poll dereferences a NULL pointer. keys_left is the whole script: one key, then
 * finished for good, which is the scripted stand-in for a player who walked away. */
static int keys_left = 1;

static int handoff_poll(u2_ctx *ctx, u2_key *out)
{
    (void)ctx;
    if (keys_left <= 0) return -1;
    keys_left--;
    memset(out, 0, sizeof *out);
    out->ch = 'x';
    return 1;
}

static const u2_input handoff_input = {
    .name = "death-check-script",
    .poll = handoff_poll,
};

static const char *row_text(u2_ctx *ctx, int row, char *buf, int c0, int c1)
{
    int n = 0;
    for (int c = c0; c <= c1 && n < U2_COLS; c++)
        buf[n++] = (char)ctx->vram.cell[row][c].ch;
    while (n > 0 && buf[n - 1] == ' ') n--;
    buf[n] = '\0';
    char *p = buf;
    while (*p == ' ') p++;
    return p;
}

#define msg_text(ctx, row, buf)    row_text((ctx), (row), (buf), 0, 29)
#define status_text(ctx, row, buf) row_text((ctx), (row), (buf), 30, 39)

/* A character as the weak-fight capture had it: named G23MAX, 30 HP, food and
 * gold non-zero, so a zero field after the call is a store and not a leftover. */
static void arm(u2_ctx *ctx)
{
    memset(ctx, 0, sizeof *ctx);
    u2_vram_init(&ctx->vram);
    u2_state_init(&ctx->state);

    memcpy(ctx->state.name, "G23MAX", 6);          /* NUL-terminated by the memset */
    ctx->state.hp       = 0x00;
    ctx->state.unk_52   = 0x30;                    /* H.P.=0030, as captured */
    ctx->state.food     = 0x03;
    ctx->state.food_hi  = 0x59;
    ctx->state.food_lo  = 0x90;
    ctx->state.exp_lo   = 0x12;
    ctx->state.exp_hi   = 0x34;
    ctx->state.gold_lo  = 0x56;
    ctx->state.gold_hi  = 0x78;

    /* The window as the walk loop leaves it: a completed command above, the
     * live prompt on the bottom row. */
    u2_status_draw(ctx);
    seam_goto(ctx, 0, U2_MSG_BOTTOM);
    seam_str_out(ctx, U2_S_CMD_PROMPT);
    seam_str_out(ctx, U2_S_DIR_EAST);
    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_CMD_PROMPT);
}

int main(void)
{
    u2_ctx ctx;
    char buf[64];

    /* --- the tail, as the capture shows it --------------------------------- */
    arm(&ctx);
    u2_death(&ctx);

    CHECK(ctx.state.hp == 0 && ctx.state.unk_52 == 0, "death zeroes HP (DS:51/52)");
    CHECK(ctx.state.food == 0 && ctx.state.food_hi == 0 && ctx.state.food_lo == 0,
          "death zeroes all three FOOD bytes including the invisible DS:55");
    CHECK(ctx.state.exp_lo == 0 && ctx.state.exp_hi == 0, "death zeroes EXP (DS:56/57)");
    CHECK(ctx.state.gold_lo == 0 && ctx.state.gold_hi == 0, "death zeroes GOLD (DS:58/59)");

    int zeroed = 1;
    zeroed &= strcmp(status_text(&ctx, 20, buf), "H.P.=0000") == 0;
    zeroed &= strcmp(status_text(&ctx, 21, buf), "FOOD=0000") == 0;
    zeroed &= strcmp(status_text(&ctx, 22, buf), "EXP.=0000") == 0;
    zeroed &= strcmp(status_text(&ctx, 23, buf), "GOLD=0000") == 0;
    CHECK(zeroed, "the whole status band is repainted to 0000, as death1.png shows");

    CHECK(strcmp(msg_text(&ctx, 22, buf), "G23MAX IS DEAD!") == 0,
          "the announcement reads '<NAME> IS DEAD!' on row 22 — the trailing "
          "newline of asm/0C8C scrolls it off the bottom row");
    CHECK(msg_text(&ctx, 23, buf)[0] == 0,
          "row 23 is left blank: no CMD: prompt follows a death");
    CHECK(strstr(msg_text(&ctx, 22, buf), "CMD:") == NULL,
          "the death line carries no prompt of its own");

    /* --- the name comes from DS:36 and stops at its NUL -------------------- */
    arm(&ctx);
    memset(ctx.state.name, 0, sizeof ctx.state.name);
    memcpy(ctx.state.name, "AB", 2);
    u2_death(&ctx);
    CHECK(strcmp(msg_text(&ctx, 22, buf), "AB IS DEAD!") == 0,
          "the name prints up to its NUL, with the space from the string itself");

    /* --- controls: prove this file can fail -------------------------------- */
    /* A food spend that survives must not announce anything. Without this, a
       u2_death that ran on every subtraction would look identical here. */
    arm(&ctx);
    CHECK(u2_overworld_spend_food(&ctx, 10) == 0,
          "control: a survivable spend returns 0");
    CHECK(ctx.state.hp == 0x00 && ctx.state.unk_52 == 0x30,
          "control: a survivable spend leaves HP alone");
    CHECK(strcmp(msg_text(&ctx, 22, buf), "CMD: EAST") == 0
          && strcmp(msg_text(&ctx, 23, buf), "CMD:") == 0,
          "control: a survivable spend prints no death line and leaves the log alone");

    /* And the kill side of the same wrapper must reach the same tail, so the
       borrow-out cannot be wired to the zeroing but not the announcement. */
    arm(&ctx);
    CHECK(u2_overworld_spend_food(&ctx, 999999) == 1, "control: an underflowing spend returns 1");
    CHECK(strcmp(msg_text(&ctx, 22, buf), "G23MAX IS DEAD!") == 0,
          "the food borrow-out reaches the same tail, not just the zeroing");

    /* --- the death handoff: press any key, back to the menu -----------------
     * Owner ruling 2026-11-11, because the original's tail falls through into a loop
     * that has never been seen to return and the engine was simply ending. These assert
     * the ruling, not a decode, and they are here rather than only in a scripted run
     * because the scripted run cannot reach the --play-map entry that was the one
     * missing the wait. */
    arm(&ctx);
    keys_left = 1;
    ctx.input = &handoff_input;
    u2_death(&ctx);
    CHECK(ctx.death_pending == 1,
          "death records that the session ended dead, so the loops unwind instead of ending");
    CHECK(msg_text(&ctx, 23, buf)[0] == '\0',
          "death alone does not print the press-any-key line");

    u2_death_handoff(&ctx);
    CHECK(strcmp(msg_text(&ctx, 23, buf), U2_S_PRESS_ANY_KEY) == 0,
          "the handoff prints (PRESS ANY KEY) on the row below the announcement");
    CHECK(ctx.death_pending == 1,
          "the handoff leaves the flag set: it is how the caller knows to go to the menu");
    CHECK(keys_left == 0,
          "the handoff waited for a key rather than falling straight through");

    /* Control: a spent script must not hang the harness. seam_key_poll returns -1 and
       the handoff has to come back anyway. */
    arm(&ctx);
    keys_left = 0;
    ctx.input = &handoff_input;
    u2_death(&ctx);
    u2_death_handoff(&ctx);
    /* Reaching this line at all is the assertion: with no key coming, a wait written as
     * `while (!seam_key_poll(...))` would spin here forever, because seam_key_poll
     * returns 0 for "no key right now" and the exhausted signal is a separate flag. */
    CHECK(ctx.death_pending == 1,
          "control: an exhausted script ends the wait without clearing the record or "
          "hanging the harness");
    CHECK(strcmp(msg_text(&ctx, 23, buf), U2_S_PRESS_ANY_KEY) == 0,
          "the prompt is printed before the wait, so it is on screen even if no key comes");

    printf("death_check: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
