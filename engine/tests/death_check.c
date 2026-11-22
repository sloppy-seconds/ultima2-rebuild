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
#include "core/save.h"
#include "core/state.h"
#include "core/vram.h"
#include "platform/platform.h"
#include "game/overworld.h"
#include "game/status.h"
#include "gen_content.h"

#include "u2_scenario.h"   /* the arming: scripted input, null clock, vram/state init */

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* The script and the clock come from u2_scenario.h — one key, then finished for good,
 * which is the scripted stand-in for a player who walked away. What this file adds is a
 * hook at the instant the wait polls: "the prompt is on screen" cannot be asserted after
 * the handoff returns, because it clears the screen on the way out, and asserting it
 * before the call would only prove u2_death printed. This is the moment the old defect
 * was about: a message written into a frame nobody had shown. */
static char prompt_at_wait[U2_COLS + 1];

static void snapshot_prompt(void *user)
{
    u2_ctx *ctx = (u2_ctx *)user;
    if (prompt_at_wait[0] != '\0') return;      /* the first poll is the one that counts */
    /* Columns 0..29: the message area. Row 23 also carries the status band, and a
     * snapshot of the whole row reads "(PRESS ANY KEY)    GOLD=0000". */
    int n = 0;
    for (int c = 0; c < 30; c++) prompt_at_wait[n++] = (char)ctx->vram.cell[23][c].ch;
    while (n > 0 && prompt_at_wait[n - 1] == ' ') n--;
    prompt_at_wait[n] = '\0';
    char *p = prompt_at_wait;
    while (*p == ' ') p++;
    if (p != prompt_at_wait) memmove(prompt_at_wait, p, strlen(p) + 1);
}

/* Row reads come from u2_scenario.h; the two macros are the column split the status
 * band and the message area actually use. */
#define msg_text(ctx, row, buf)    u2_sc_row((ctx), (row), (buf), 0, 29)
#define status_text(ctx, row, buf) u2_sc_row((ctx), (row), (buf), 30, 39)

/* A character as the weak-fight capture had it: named G23MAX, 30 HP, food and
 * gold non-zero, so a zero field after the call is a store and not a leftover. */
static void arm(u2_ctx *ctx)
{
    u2_sc_arm_ctx(ctx, "x");
    u2_sc_in.on_poll = snapshot_prompt;
    u2_sc_in.user = ctx;

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

int main(int argc, char **argv)
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

    /* --- the death handoff: press any key, reload the last save -------------
     * Owner ruling 2026-11-11, because the original's tail falls through into a loop
     * that has never been seen to return and the engine was simply ending. These assert
     * the ruling, not a decode, and they are here rather than only in a scripted run
     * because the scripted run cannot reach the --play-map entry that was the one
     * missing the wait. */
    arm(&ctx);
    u2_sc_replay(&ctx, "x");
    prompt_at_wait[0] = '\0';
    u2_death(&ctx);
    CHECK(ctx.death_pending == 1,
          "death records that the session ended dead, so the loops unwind instead of ending");
    CHECK(msg_text(&ctx, 23, buf)[0] == '\0',
          "death alone does not print the press-any-key line");

    u2_death_handoff(&ctx);
    CHECK(strcmp(prompt_at_wait, U2_S_PRESS_ANY_KEY) == 0,
          "the prompt is on screen while the wait polls -- the defect being replaced was "
          "the game ending with this message still in an unshown frame");
    CHECK(ctx.death_pending == 1,
          "the handoff leaves the flag set: it is how the caller knows to reload the save");
    CHECK(u2_sc_in.at == 1,
          "the handoff waited for a key rather than falling straight through");
    CHECK(msg_text(&ctx, 22, buf)[0] != '\0'
          && strcmp(msg_text(&ctx, 23, buf), U2_S_PRESS_ANY_KEY) == 0,
          "the handoff leaves the whole death frame intact -- announcement and prompt "
          "both still on screen -- because the clear belongs to whoever redraws");

    /* Control: a spent script must not hang the harness. seam_key_poll returns -1 and
       the handoff has to come back anyway. */
    arm(&ctx);
    u2_sc_replay(&ctx, "");
    prompt_at_wait[0] = '\0';
    u2_death(&ctx);
    u2_death_handoff(&ctx);
    /* Reaching this line at all is the assertion: with no key coming, a wait written as
     * `while (!seam_key_poll(...))` would spin here forever, because seam_key_poll
     * returns 0 for "no key right now" and the exhausted signal is a separate flag.
     * The clear is unconditional, so the screen is blank here too. */
    CHECK(ctx.death_pending == 1,
          "control: an exhausted script ends the wait without clearing the record or "
          "hanging the harness");
    CHECK(strcmp(prompt_at_wait, U2_S_PRESS_ANY_KEY) != 0,
          "control: with no key the wait never shows the prompt, so nothing is printed "
          "that the player cannot have pressed through");

    /* --- the two facts the reload depends on ---------------------------------
     * The shipped entries rescue a dead player by re-reading the record from disk.
     * That is only a rescue if death never wrote the record, and if reading it back
     * puts back what death zeroed. Both are asserted here rather than by driving the
     * menu, because no scripted entry reaches the menu's world path: --headless has no
     * pixel renderer so 'P' takes the cell-only branch, and --play-map deliberately
     * does not reload (main.c names the gates that depend on that). argv[1] is the
     * save directory the runner passes; a missing one is a failure, not a skip, so the
     * wiring cannot be dropped quietly. */
    if (argc < 2) {
        CHECK(0, "death_check needs a save directory as argv[1] -- the runner must pass "
                 "one, or the reload's two facts go unproven");
    } else {
        arm(&ctx);
        ctx.save_dir = argv[1];
        ctx.state.hp = 0x99;
        ctx.state.unk_52 = 0x97;              /* the pair the band reads as H.P.=9997 */
        CHECK(u2_save_store(&ctx) == U2_SAVE_OK, "the harness wrote a record to %s", argv[1]);

        u2_death(&ctx);
        CHECK(ctx.state.hp == 0 && ctx.state.unk_52 == 0,
              "death zeroed the pair in memory (the precondition for restoring it)");

        char rec[512];
        u2_save_path(&ctx, rec, sizeof rec);
        FILE *rf = fopen(rec, "rb");
        unsigned char disk[384] = {0};
        size_t got = rf ? fread(disk, 1, sizeof disk, rf) : 0;
        if (rf) fclose(rf);
        CHECK(got >= 0x1D, "the record on disk is readable at %s", rec);
        CHECK(disk[0x1B] == 0x99 && disk[0x1C] == 0x97,
              "death left the record alone: DS:51/52 on disk are still 99 97, so the "
              "reload has a live character to read (asm/0C62 never calls the save body)");

        CHECK(u2_save_load(&ctx) == U2_SAVE_OK, "the record reads back as a character");
        CHECK(ctx.state.hp == 0x99 && ctx.state.unk_52 == 0x97,
              "the reload restores exactly what death zeroed -- this is the whole rescue");
    }

    printf("death_check: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
