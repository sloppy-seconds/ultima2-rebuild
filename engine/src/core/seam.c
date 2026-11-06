/*
 * seam.c — the seam layer (see core/seam.h).
 *
 * Every function here is the engine's counterpart of one named routine in the
 * byte-proven asm tree. Game logic calls these; nothing in src/game/ may call
 * a renderer, an input backend, sleep(), or a terminal escape directly. Each
 * call traces with its asm citation so a --trace log lines up with asm/.
 *
 * The text printer reproduces char_print's control flow byte for byte:
 *
 *   asm/5003_char_print.asm  and $0x7f,%al ; cmp $0xd ; je  cr
 *   asm/500D_sub_500d.asm cr: al = DS:0x54F (left margin) ; DS:0x2E = al ; jmp nl
 *   asm/5016_sub_5016.asm    cmp $0xa ; jne emit
 *   asm/501A_sub_501a.asm nl: incb DS:0x2F ; cmpb $0x18,DS:0x2F ; jne sync
 *   asm/5025_sub_5025.asm      INT 10h AH=06h scroll rows 0x14..0x17, cols 0..DS:0x31
 *   asm/5036_sub_5036.asm      decb DS:0x2F ; jmp sync
 *   asm/503D_sub_503d.asm emit: INT 10h AH=09h, AL=char, BL=DS:0x23E (attribute)
 *   asm/504A_sub_504a.asm      incb DS:0x2E ; if DS:0x31 < DS:0x2E goto cr
 *   asm/5057_sub_5057.asm sync: call str_out_core (INT 10h AH=02h cursor)
 */
#include <string.h>
#include "core/seam.h"
#include "core/rng.h"
#include "core/replay.h"
#include "core/timer.h"
#include "debug/dump_state.h"
#include "debug/trace.h"

static void text_sync_cursor(u2_ctx *ctx)
{
    /* str_out_core (asm/51FD) is the cursor sync at the end of char_print. */
    u2_vram_goto(&ctx->vram, ctx->state.cursor_row, ctx->state.cursor_col);
}

/* asm/501A_sub_501a.asm: the row advance shared by CR, LF and line wrap. */
static void text_next_row(u2_ctx *ctx, int reset_col)
{
    u2_state *s = &ctx->state;

    if (reset_col)
        s->cursor_col = s->left_margin;          /* asm/500D: al = DS:0x54F */
    s->cursor_row = (uint8_t)(s->cursor_row + 1); /* asm/501A: incb 0x2f */
    if (s->cursor_row >= U2_SCROLL_ROW) {         /* asm/501A: cmpb $0x18 */
        /* asm/5025: INT 10h AH=06h, AL=1 line, CH=0x14 DH=0x17, CL=0 DL=DS:0x31 */
        u2_vram_scroll_window(&ctx->vram, U2_MSG_TOP, U2_MSG_BOTTOM, 0,
                              s->right_margin, s->text_attr);
        s->cursor_row = (uint8_t)(s->cursor_row - 1); /* asm/5036: decb 0x2f */
    }
    text_sync_cursor(ctx);
}

void seam_char_print(u2_ctx *ctx, uint8_t ch)
{
    u2_state *s = &ctx->state;

    ch &= 0x7F;                                   /* asm/5003: and $0x7f,%al */
    u2_trace(ctx, "char_print", "5003", "ch=0x%02X at=%u,%u", ch,
             s->cursor_row, s->cursor_col);
    if (ch == '\r') {                             /* asm/5003: cmp $0xd */
        text_next_row(ctx, 1);
        return;
    }
    if (ch == '\n') {                             /* asm/5016: cmp $0xa */
        text_next_row(ctx, 0);
        return;
    }
    /* asm/503D: INT 10h AH=09h — write char+attribute at the cursor. */
    u2_vram_poke(&ctx->vram, s->cursor_row, s->cursor_col, ch, s->text_attr);
    s->cursor_col = (uint8_t)(s->cursor_col + 1); /* asm/504A: incb 0x2e */
    if ((uint8_t)s->right_margin < s->cursor_col) {/* asm/504A: cmp 0x2e,%al; jb */
        text_next_row(ctx, 1);
        return;
    }
    text_sync_cursor(ctx);                        /* asm/5057 -> asm/51FD */
}

/* Engine primitive, not an asm seam: one glyph at the cursor with none of char_print's
 * CR / margin / sync behaviour. asm=- because there is no located original site for it. */
void seam_char_out(u2_ctx *ctx, uint8_t ch)
{
    u2_trace(ctx, "char_out", "-", "ch=0x%02X", ch);
    u2_vram_put(&ctx->vram, ch, ctx->state.text_attr);
}

void seam_str_out(u2_ctx *ctx, const char *s)
{
    u2_trace(ctx, "str_out", "4FDF", "len=%zu", strlen(s));
    /* The message band is four rows and scrolls, so a frame shows only the tail of a long
     * exchange. Logging the string itself makes the trace a complete transcript of what the game
     * was asked to print, which is what the CP6 gate asserts against. Newlines are shown as '|'
     * so a phrase stays on one trace line and can be grepped whole. u2_trace is a no-op when
     * tracing is off, so this costs nothing on a normal run. */
    {
        char flat[160];
        size_t n = 0;
        for (const char *p = s; *p && n + 1 < sizeof flat; p++)
            flat[n++] = (*p == '\r' || *p == '\n') ? '|' : (char)(*p & 0x7F);
        flat[n] = '\0';
        u2_trace(ctx, "str_out", "4FDF", "text=%s", flat);
    }
    /* str_out's loop (asm/4FF0..asm/4FFE) calls char_print per byte. */
    for (const char *p = s; *p; p++)
        seam_char_print(ctx, (uint8_t)*p);
}

void seam_goto(u2_ctx *ctx, int col, int row)
{
    ctx->state.cursor_col = (uint8_t)col;
    ctx->state.cursor_row = (uint8_t)row;
    u2_vram_goto(&ctx->vram, row, col);
    u2_trace(ctx, "goto", "51FD", "col=%d row=%d", col, row);
}

void seam_str_out_at(u2_ctx *ctx, int col, int row, const char *s)
{
    u2_trace(ctx, "str_out_at", "51EB", "col=%d row=%d len=%zu", col, row, strlen(s));
    seam_goto(ctx, col, row);
    seam_str_out(ctx, s);
}

/* asm/53A0_sub_53a0.asm: col=0, print 0x20 forty times. */
void seam_clear_row(u2_ctx *ctx, int row)
{
    seam_goto(ctx, 0, row);
    for (int i = 0; i < U2_COLS; i++)
        seam_char_print(ctx, ' ');
}

/* seam: clear screen (asm/4C1C, inside asm/4BE0_sub_4be0.asm) — the whole grid
 * is blanked and the cursor goes home. Reached from the title flow, from the
 * disk prompts and from the tail of character creation, so it is a real seam
 * rather than something game code should open-code with u2_vram_clear. */
void seam_clear_screen(u2_ctx *ctx)
{
    u2_vram_clear(&ctx->vram, ctx->state.text_attr);
    ctx->state.cursor_col = 0;
    ctx->state.cursor_row = 0;
    u2_trace(ctx, "clear_screen", "4C1C", "whole grid blanked");
}

/* asm/4F4D is bigger than the function boundary Phase 3 drew: the routine consumes the key
 * (INT 16h AH=00h), maps a few control bytes to NUL, and at asm/4FD3..asm/4FDB folds a-z to
 * A-Z with `and $0xdf`. The command dispatch therefore only ever sees uppercase.
 *
 * The range guard is load-bearing, not tidiness. asm/4FD3 tests `cmp $0x61,%al ; jb` and
 * asm/4FD7 tests `cmp $0x7a,%al ; ja`, so bytes outside a-z pass through untouched — which is
 * what lets the arrow codes survive the fold. The original's arrows are control letters
 * (0x0E/0x19/0x05/0x17, all below 0x61); the engine's are 0xF0..0xF3, all above 0x7a. Both
 * land outside the range, and an unguarded `& 0xDF` would have silently corrupted every arrow
 * press into a letter.
 *
 * Shared by poll AND wait because the original has ONE seam and its header records that
 * "both returns fall into the upper-case tail". Having it on only the poll path was the bug:
 * the command loop polls, so everything looked correct, while every blocking prompt waits —
 * read_yes, read_digit and cmd_Y's yell loop — and so the shops saw a raw 0x79, compared it
 * against the 0x59 asm/2017 tests for, and refused to sell anything to a human. No scripted
 * capture could ever have shown this, because tools/spice86_type.py sends the SHIFTED key. */
static void key_uppercase_fold(u2_key *k)
{
    if (k->ch >= 'a' && k->ch <= 'z')
        k->ch = (uint8_t)(k->ch & 0xDF);
}

/* Ctrl-S: save the frame that is on screen and swallow the keystroke, so no game
 * handler ever sees it and no letter shortcut is displaced. Handled here rather
 * than in each screen because this is the one place every screen's input passes --
 * title, creation, overworld and dungeon alike -- and a bug can be on any of them.
 * A backend with no pixels leaves the hook NULL, and the key then reaches the game
 * as the control byte it is, which is what happened before it meant anything. */
#define U2_KEY_SCREENSHOT 0x13

/* Returns 1 when the key was consumed here and the caller must not deliver it. */
static int take_screenshot_key(u2_ctx *ctx, const u2_key *out)
{
    if (out->ch != U2_KEY_SCREENSHOT) return 0;
    if (!ctx->renderer || !ctx->renderer->screenshot) return 0;
    ctx->renderer->screenshot(ctx);
    return 1;
}

int seam_key_poll(u2_ctx *ctx, u2_key *out)
{
    int got = ctx->input->poll(ctx, out);
    if (got > 0 && take_screenshot_key(ctx, out))
        return 0;
    if (got > 0) {
        key_uppercase_fold(out);
        /* No "last key" state byte: DS:0x4A is the sector's second digit /
         * site kind (see state.h). Storing the polled key here corrupted the
         * saved sector digit in engine-written PLAYER records. */
        u2_trace(ctx, "key_poll", "4F4D", "ready=1 ch=0x%02X", out->ch);
    } else {
        /* "input finished for good" is the scripted stand-in for a player who
         * walked away; "no key right now" (a WAIT) is not. */
        if (got < 0)
            ctx->input_exhausted = 1;
        u2_trace(ctx, "key_poll", "4F4D", got < 0 ? "ready=end" : "ready=0");
    }
    return got > 0;
}

int seam_key_wait(u2_ctx *ctx, u2_key *out)
{
    int got;
    for (;;) {
        got = ctx->input->wait(ctx, out);
        /* Ctrl-S is consumed here and never reaches the game, so keep waiting for
         * a key the game should actually see. */
        if (!(got > 0 && take_screenshot_key(ctx, out))) break;
    }
    if (got > 0) {
        key_uppercase_fold(out);   /* same tail as the poll path; see key_uppercase_fold */
    } else if (got < 0) {
        /* Collapse to the same two-state answer seam_key_poll gives, and set the
         * same flag. This is the whole fix for "closing the window or pressing
         * Ctrl-C on the creation screen acts like the Down arrow".
         *
         * The SDL backend returns -1 for a quit request, and every caller of wait
         * asks either `if (!got)` or `if (got == 0)`, because the header documented
         * wait as "returns 0 on end of input" and only 0. So -1 was read as "a key
         * arrived", and the caller then read out->ch, which the backend had not
         * written. That is not random: `u2_key` sits at the same stack offset on
         * every iteration, so it still holds the LAST key the player pressed. On the
         * creation form the last key is normally Down or Tab, which is exactly the
         * report. commands.c:250 even asserts the wrong contract in a comment --
         * "returns NON-ZERO when it has a key" -- and four other sites cite it.
         *
         * Normalising here rather than at five call sites is the point: the original
         * has ONE seam (asm/4F4D, and its header records that both returns fall into
         * the upper-case tail), so the engine's two entries must answer in the same
         * alphabet. A negative stays available to the trace line below. */
        ctx->input_exhausted = 1;
    }
    u2_trace(ctx, "key_wait", "4F4D", "got=%d ch=0x%02X", got, got > 0 ? out->ch : 0);
    return got > 0;
}

int seam_key_wait_shown(u2_ctx *ctx, u2_key *out)
{
    /* The trace token is `present=menu` rather than a bare present because the gate that
     * guards this contract reads the trace and has to distinguish "a prompt was put on
     * screen before the blocking read" from every other present in the run. */
    if (ctx->present_now) {
        u2_trace(ctx, "present", "-", "present=menu before=key_wait");
        ctx->present_now(ctx);
    } else {
        u2_trace(ctx, "present", "-", "present=skipped no_present_now");
    }
    return seam_key_wait(ctx, out);
}

uint8_t seam_rand_byte(u2_ctx *ctx)
{
    uint8_t r = u2_rand_byte(ctx);
    /* asm/5217 leaves the new byte in DS:0x23F, which is rng_state[0]; the
     * asm/5236 tail does `or %al,%al`, so callers branch on a zero result. */
    u2_trace(ctx, "rand_byte", "5217", "value=%u zero=%d rolls=%u",
             r, r == 0, ctx->rng_rolls);
    return r;
}

void seam_wait_ms(u2_ctx *ctx, uint32_t ms)
{
    u2_trace(ctx, "timed_wait", "52CA", "ms=%u", ms);
    u2_wait_ms(ctx, ms);
}

void seam_tone(u2_ctx *ctx, uint16_t divisor, uint32_t ms)
{
    u2_trace(ctx, "speaker_freq", "5CD2", "divisor=%u ms=%u", divisor, ms);
    ctx->audio->tone(ctx, divisor, ms);
}

void seam_present(u2_ctx *ctx)
{
    int changed = ctx->vram.dirty;

    u2_vram_flush(&ctx->vram, ctx);
    if (changed && ctx->replay)
        u2_replay_record(ctx);
    if (ctx->dump_each) {
        u2_dump_screen(ctx, stdout);
        fflush(stdout);
    }
    if (ctx->dump_hilite) {
        u2_dump_hilite(ctx, stdout);
        fflush(stdout);
    }
}

int seam_present_frame(u2_ctx *ctx)
{
    if (!ctx || !ctx->renderer || !ctx->renderer->present_frame || !ctx->frame)
        return 0;
    return ctx->renderer->present_frame(ctx, ctx->frame);
}
