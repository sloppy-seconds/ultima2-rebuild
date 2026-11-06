/*
 * title.c — presentation intro, title screen, menu, and the 'D' demonstration.
 *
 * spec (asm/ tree, one module per region as planned):
 *   startup flag      asm/0133_sub_0133.asm        (DS:0x239 = 0)
 *   intro screen 1    asm/0142 + asm/014B, asm/0155/0158/015E
 *   timed hold        asm/0688_sub_0688.asm        (polls ESC, jumps to the
 *                                                    title entry at 0x242)
 *   intro screen 2    asm/0180/0184/018D, asm/0198/019B/01A1,
 *                     asm/01BB/01BE/01C4, asm/01E1/01E4/01EA
 *   screen clear      asm/4A3E_sub_4a3e.asm        (INT 10h AH=00h AL=04h)
 *   story gate        asm/0237_sub_0237.asm        (branch on DS:0x239)
 *   title entry       asm/0242_sub_0242.asm        (target of every ESC / bad choice)
 *   title lines       asm/0246, 025E, 0273, 0288, 02A3, 02CC, 02E6,
 *                     02F8, 031F, 034B, 0375
 *   choice wait       asm/0389/038C/038F           (key_poll until AH=0xFF)
 *   choice dispatch   asm/0394..03A6               ('D' jumps ahead; 'P' calls
 *                                                    asm/0764; 'C' calls
 *                                                    asm/5400 then asm/0764;
 *                                                    anything else redraws)
 *   mode set + flag   asm/03B0 -> asm/4A3E, asm/03B3 (DS:0x239 = 1)
 *   demo poem         asm/03AC_sub_03ac.asm        (12 lines, rows 23 and 24)
 *   row clears        asm/53A0_sub_53a0.asm, asm/538F_sub_538f.asm
 *
 * Every string here comes from the generated header, which the extractor
 * produced from the user's own copy. No game text is written in this file: the
 * only engine-authored strings are the CP3 stub notices, and they say so.
 *
 * Two things the original does that this module does not, on purpose:
 *   - it loads PIC picture files (the FCB names around file 0x419 and the INT 21h
 *     sector read at asm/535A_sub_535a.asm). It now does, for the two sequences whose
 *     pictures were observed: the intro splash and the demonstration poem. See core/pic.h
 *     for the format evidence, which picture goes where, and what is still inferred. When
 *     the player has no picture file, or the backend takes no frames, the text screens
 *     stand alone exactly as they did before.
 *   - asm/03A9 (create) and the call at asm/03AC's head are character creation
 *     and the play setup; they are CP3 and are stubbed below.
 */
#include <stdio.h>
#include "game/title.h"
#include "game/creation.h"
#include "game/overworld.h"
#include "game/status.h"
#include "core/font.h"
#include "core/frame.h"
#include "core/pic.h"
#include "core/pic_ega.h"
#include "core/save.h"
#include "core/seam.h"
#include "core/demo.h"
#include "core/timer.h"
#include "debug/trace.h"
#include "gen_content.h"

/* Positions are read off the code that sets DS:0x2E (column) and DS:0x2F
 * (row) before each str_out; each line below cites the function that sets it. */
typedef struct txt { int col; int row; const char *s; } txt;

static const txt INTRO_1[] = {
    { 16, 10, U2_S_INTRO_ORIGIN },      /* asm/0142: di=0x10 si=0xa */
    { 11, 12, U2_S_INTRO_PROUDLY },     /* asm/0155/0158: di=0xb si=0xc */
};
static const txt INTRO_2[] = {
    { 16,  8, U2_S_INTRO_PART },        /* asm/0184: di=0x10 si=8 */
    {  9, 10, U2_S_INTRO_BESTSELLER },  /* asm/0198/019B: di=9 si=0xa */
    {  7, 12, U2_S_INTRO_GENRE },       /* asm/01BB/01BE: di=7 si=0xc */
    { 12, 14, U2_S_INTRO_AUTHOR },      /* asm/01E1/01E4: di=0xc si=0xe */
};
static const txt TITLE[] = {
    /* Draw order is the order the code sets the cursor and calls str_out:
     * asm/0246, 025E, 0273, 0288, 02A3, 02CC, 02E6, 02F8, 031F, 034B, 0375.
     * The order matters: the choice echo (asm/0394) prints at whatever cursor
     * the last line leaves behind, and CHOICE: is drawn last so the key lands
     * right after it. */
    { 15,  1, U2_S_TITLE_GAME },        /* asm/0246 */
    { 16,  3, U2_S_TITLE_REVENGE },     /* asm/025E */
    { 16,  5, U2_S_TITLE_OF_THE },      /* asm/0273 */
    { 14,  7, U2_S_TITLE_ENCHANTRESS }, /* asm/0288 */
    {  6, 22, U2_S_TITLE_COPYRIGHT },   /* asm/02A3 (inside a mis-decoded run) */
    { 15, 23, U2_S_TITLE_AND_ORIGIN },  /* asm/02CC */
    {  5, 11, U2_S_TITLE_TYPE },        /* asm/02E6 (inside a mis-decoded run) */
    {  6, 13, U2_S_MENU_DEMO },         /* asm/02F8 */
    {  6, 15, U2_S_MENU_PLAY },         /* asm/031F */
    {  6, 17, U2_S_MENU_CREATE },       /* asm/034B */
    {  5, 19, U2_S_MENU_CHOICE },       /* asm/0375 */
};
/* asm/03AC prints each pair into the bottom two rows, waits, then clears them
 * with asm/538F_sub_538f.asm. Columns are the movb $n,0x2e sites. */
static const txt POEM[] = {
    {  0, 23, U2_S_POEM_1A }, {  0, 24, U2_S_POEM_1B },
    {  0, 23, U2_S_POEM_2A }, {  2, 24, U2_S_POEM_2B },
    {  0, 23, U2_S_POEM_3A }, {  4, 24, U2_S_POEM_3B },
    {  0, 23, U2_S_POEM_4A }, {  1, 24, U2_S_POEM_4B },
    {  0, 23, U2_S_POEM_5A }, {  1, 24, U2_S_POEM_5B },
    {  0, 23, U2_S_POEM_6A }, {  8, 24, U2_S_POEM_6B },
};

#define NARR(x) ((int)(sizeof (x) / sizeof (x)[0]))

/* Stop condition for the scripted/headless flow: end of input or a quit. */
static int flow_stopped(const u2_ctx *ctx)
{
    return ctx->exit_code || ctx->input_exhausted;
}

/* The original's intro hold, asm/0688_sub_0688.asm (dx = 0x6000 iterations of
 * an INT 1Ah tick wait). Wall-clock duration on a 1983 machine is unknowable;
 * 1.5 s reads right and is the only number here that is a choice, not a fact. */
#define INTRO_HOLD_MS 1500
#define POEM_HOLD_MS  2000
/* The splash hold is measured, not chosen: five consecutive one-second frames of the
 * original all matched picdra, so the dragon is on screen for about five seconds. */
#define SPLASH_HOLD_MS 5000

static void draw_all(u2_ctx *ctx, const txt *lines, int n)
{
    for (int i = 0; i < n; i++) {
        /* With the asm fallback (no exe) the two strings that live inside the
         * mis-decoded regions come out empty; skip them rather than print a
         * placeholder that would look like game text. */
        if (!lines[i].s || !lines[i].s[0])
            continue;
        seam_str_out_at(ctx, lines[i].col, lines[i].row, lines[i].s);
    }
}

/*
 * asm/0688_sub_0688.asm: hold, polling key_poll each pass, and bail to the
 * title entry (asm/0242) the moment ESC arrives.
 *
 * Headless compresses the hold to a single poll. That is deliberate: the point
 * of the poll is "did the player press ESC", and a script must address the
 * menu, not fight 30 swallowed keystrokes. The terminal build polls at a real
 * rate. Both paths consume at most one key per hold.
 */
static int hold_for_esc(u2_ctx *ctx, unsigned ms)
{
    u2_key k;

    if (ctx->mode == U2_MODE_HEADLESS) {
        if (seam_key_poll(ctx, &k) && k.ch == 0x1B)
            return 1;
        return 0;
    }    for (unsigned waited = 0; waited < ms; waited += 50) {
        /* A termination signal (T4) or a closed window lands on the poll as
         * exit_code / input_exhausted, not as a key. Without this test the
         * hold kept sleeping through its whole budget and the process sat
         * out SIGTERM until SIGKILL — the defect T4 reproduces. */
        if (ctx->exit_code || ctx->input_exhausted)
            return 1;
        if (seam_key_poll(ctx, &k)) {
            /* Any key ends the hold; only ESC aborts to the title. The
             * original's poll consumed the key the same way (asm/069C). */
            return k.ch == 0x1B;
        }
        seam_wait_ms(ctx, 50);
    }
    return 0;
}

/* The original composites a picture by writing it into video memory and then printing
 * its text over the top of it, which is why the demonstration's couplets sit ON the
 * starfield rather than under it. The engine does it in the same order: picture into the
 * frame, text rows over it, one present.
 *
 * seam_present_frame presents whatever ctx->frame points at, and the title flow owns no
 * frame of its own (the overworld loop installs one when it starts), so this borrows the
 * slot for the duration and puts it back. Leaving it installed would be the worse bug: the
 * overworld would then composite into a frame nobody presents.
 *
 * Returns the hold_for_esc result so a caller treats ESC exactly as it already does.
 * Returns 0 without drawing anything when the picture cannot be shown, which leaves the
 * caller's text screen as the whole of what the player sees. */
static int show_pic(u2_ctx *ctx, const char *name, int hold_ms)
{
    static u2_frame pic_frame;      /* one picture on screen at a time, heap-backed
                                     * at the active theme's frame scale */

    if (u2_frame_ensure(&pic_frame, ctx->frame_scale) != 0) return 0;
    u2_frame_set_text_fg(&pic_frame, ctx->text_fg);

    /* EGA first when the art axis asks for it: the fan pack's PIC*-E is a full
     * 16-colour image and the richer art wins. If it is absent the CGA pic is
     * used unchanged, so --ega never blanks a splash that CGA could show. */
    int shown = 0;
    if (ctx->art_mode == U2_ART_EGA && u2_pic_can_show_ega(ctx, name))
        shown = (u2_pic_blit_ega(ctx, name, &pic_frame) == 0);
    if (!shown) {
        if (!u2_pic_can_show(ctx, name))
            return 0;
        if (u2_pic_blit(ctx, name, &pic_frame) != 0)
            return 0;
    }
    u2_font_draw_vram_rows(&pic_frame, &ctx->vram, 0, U2_ROWS - 1);

    struct u2_frame *saved = ctx->frame;
    ctx->frame = &pic_frame;
    const int quit = seam_present_frame(ctx);
    ctx->frame = saved;
    if (quit)
        return 1;                   /* the window closed */
    return hold_for_esc(ctx, hold_ms);
}

/* asm/0142..020F: the two presentation screens. Returns 1 if ESC skipped. */
static int present_intro(u2_ctx *ctx)
{
    u2_trace(ctx, "intro", "0142", NULL);
    u2_vram_clear(&ctx->vram, 0x07);
    draw_all(ctx, INTRO_1, NARR(INTRO_1));
    seam_present(ctx);
    if (hold_for_esc(ctx, INTRO_HOLD_MS))
        return 1;

    u2_vram_clear(&ctx->vram, 0x07);
    draw_all(ctx, INTRO_2, NARR(INTRO_2));
    seam_present(ctx);
    if (hold_for_esc(ctx, INTRO_HOLD_MS))
        return 1;

    /* The dragon logo. Observed between the second presentation screen and the title
     * menu, full screen, no text over it — the lettering in this one is part of the
     * art. With no picture file this is simply skipped and the intro is what it has
     * always been here: two text screens and the menu. */
    if (show_pic(ctx, U2_PIC_TITLE, SPLASH_HOLD_MS))
        return 1;
    return 0;
}

/* asm/03AC: the story poem. The path here first sets the video mode and so
 * clears the screen (asm/03B0 -> asm/4A3E_sub_4a3e.asm), sets the story flag
 * (DS:0x239), waits, and then draws one couplet at a time into rows 23 and 24,
 * clearing them with asm/538F_sub_538f.asm between couplets. */
static int present_poem(u2_ctx *ctx)
{
    u2_vram_clear(&ctx->vram, 0x07);              /* asm/03B0 -> asm/4A3E */
    ctx->state.story_flag = 1;                    /* asm/03B3: mov $1,0x239 */
    seam_present(ctx);
    if (hold_for_esc(ctx, 800))
        return 1;

    /* With a picture the demonstration is the real thing: the whole poem, couplet by
     * couplet, over the starfield and the pyramid. Without one it stays the CP3
     * placeholder, because a wall of scrolling text is not a demonstration and running
     * all twelve lines at a terminal would read as a bug rather than a fallback.
     *
     * The picture-per-couplet split is INFERRED, not observed: the capture was sampled
     * at one second and shows PICSPA first and PICMIN for the rest, which is not fine
     * enough to say which couplet changes it. Recorded as needing a finer capture in
     * docs/needs-gameplay.md rather than presented as settled.
     */
    const int pictures = u2_pic_can_show(ctx, U2_PIC_PYRAMID);
    const int limit = pictures ? NARR(POEM) : 2;

    for (int i = 0; i < limit && i < NARR(POEM); i += 2) {
        seam_clear_row(ctx, 23);
        seam_clear_row(ctx, 24);
        if (POEM[i].s[0])
            seam_str_out_at(ctx, POEM[i].col, POEM[i].row, POEM[i].s);
        if (i + 1 < NARR(POEM) && POEM[i + 1].s[0])
            seam_str_out_at(ctx, POEM[i + 1].col, POEM[i + 1].row, POEM[i + 1].s);
        if (pictures) {
            if (show_pic(ctx, i == 0 ? U2_PIC_STARS : U2_PIC_PYRAMID, POEM_HOLD_MS))
                return 1;
            continue;
        }
        seam_present(ctx);
        if (hold_for_esc(ctx, POEM_HOLD_MS) || flow_stopped(ctx))
            return 1;
    }
    return 0;
}

/* asm/0242: draw the title and menu. */
static void draw_title(u2_ctx *ctx)
{
    /* The menu owns the full width. Anything that draws the status block narrows
     * the right margin to 0x1d on the way out (asm/5396), and returning to the
     * menu without restoring it wrapped the menu text at 25 columns. */
    ctx->state.right_margin = 0x28;
    u2_vram_clear(&ctx->vram, 0x07);
    draw_all(ctx, TITLE, NARR(TITLE));
    seam_present(ctx);
}

/* asm/0389..038F: spin on key_poll until a key arrives (AH=0xFF), then echo it
 * with char_print (asm/0394). Returns -1 when input is finished. */
static int wait_choice(u2_ctx *ctx)
{
    u2_key k;

    /* The title flow's top-level wait, so it is where a Ctrl-R pressed at the menu
     * takes effect (D12). Nothing here is half-typed: the menu reads one key and
     * acts on it, so every point in this loop is quiescent. */
    u2_demo_quiescent(ctx, U2_DEMO_MODE_TITLE, NULL);

    while (!seam_key_poll(ctx, &k)) {
        /* input_exhausted is the flag that actually gets set: a finished
         * script, a closed window and --dump-screen all arrive through
         * seam_key_poll's -1, which raises this flag and leaves exit_code
         * alone unless the backend chose to set it. Testing only exit_code
         * is what made `SDL_VIDEODRIVER=dummy --dump-screen` write its frame
         * and then spin here for ever (T6, measured 2026-10-20: the PPM was
         * on disk, the process had to be killed). */
        if (ctx->exit_code || ctx->input_exhausted)
            return -1;
        /* The headless clause that used to sit in the test above is gone, and its
         * absence is deliberate rather than an oversight: it conflated "no key this
         * poll" with "the player has left", which is exactly the seam contract
         * seam_key_poll documents as three states. A spent script sets
         * input_exhausted, a closed window and --dump-screen set exit_code (T6, the
         * failure this line was written for), and a swallowed control key — Ctrl-S, or
         * Ctrl-R starting a recording — is neither. Answering -1 for a swallow is what
         * made a scripted Ctrl-R look like the player walked away. */
        seam_wait_ms(ctx, 16);
        seam_wait_ms(ctx, 20);
    }    seam_char_print(ctx, k.ch);
    seam_present(ctx);
    return k.ch;
}

/* CP3/CP4 placeholders. Engine-authored on purpose: they must not read like
 * game text, and they name the checkpoint that replaces them. */
static void stub_not_implemented(u2_ctx *ctx, const char *what)
{
    char buf[64];

    snprintf(buf, sizeof buf, "[engine] %s -> CP3+", what);
    seam_clear_row(ctx, 21);
    seam_str_out_at(ctx, 1, 21, buf);
    seam_present(ctx);
    seam_wait_ms(ctx, 600);
}

/* The demo cycle: the poem, then the intro again, until ESC. asm/03AC ends by
 * jumping back to the intro entry at 0x33E, and every ESC lands on the title
 * entry at asm/0242. */
static void run_demo(u2_ctx *ctx)
{
    for (;;) {
        /* ESC abandons the demonstration (deviation D12). The original has no
         * way out once you start it; the picture slideshow arrives at CP9 and
         * until then this is a wall of text with an exit only if you add one. */
        u2_key k;
        if (seam_key_poll(ctx, &k) && k.ch == 0x1b) return;
        if (present_poem(ctx) || flow_stopped(ctx))
            return;
        if (seam_key_poll(ctx, &k) && k.ch == 0x1b) return;
        if (present_intro(ctx) || flow_stopped(ctx))
            return;
    }
}

/* asm/0764 (file 0x0964..0x09CB): the 'P' branch.
 * It clears the screen, positions at row 12 col 10, and jumps straight to the
 * read (0x0976 skips the prompt on the first pass): FCB random read of 256
 * bytes into DS:36. Then `mov al,[0x36]; or al,al; jne 0x9e6` — a non-zero name
 * byte means there is a character, so it continues into the game. A zero name
 * byte prints "(INSERT PLAYER DISK)" and waits for ESC, then retries.
 *
 * The read itself is core/save.c. From 0x9e6 the original reads MONSTERS and
 * the sector named by the record's digits, copies the entry pair to the
 * position (file 0x0A26..0x0A2F) and enters the world — that path is now
 * u2_overworld_init_state + u2_overworld_loop, taken whenever the renderer
 * can show pixels. On cell-only backends the world has nothing to draw to,
 * so those keep the CP3 stopgap: the status block the load proved, plus a
 * note naming the display requirement rather than pretending to play. */
static void run_play(u2_ctx *ctx)
{
    for (;;) {
        u2_key k;
        u2_save_result r = u2_save_load(ctx);

        if (r == U2_SAVE_OK) {
            if (ctx->renderer->present_frame) {
                u2_overworld ow;
                if (u2_overworld_init_state(ctx, &ow) == 0) {
                    u2_overworld_loop(ctx, &ow);
                    return;
                }
                /* The record loaded but the world did not (no --data-dir, no
                 * tiles): say exactly which, and wait, instead of a blank
                 * window or a silent return to the menu. */
                seam_clear_screen(ctx);
                seam_str_out_at(ctx, 1, 12, "[engine] world not available:");
                seam_str_out_at(ctx, 1, 13, ow.note);
                seam_present(ctx);
                (void)wait_choice(ctx);
                return;
            }
            /* The load is real; this backend has no pixel path. CP4 owns the
             * overworld, so the placeholder says so while still showing what
             * was read from disk — the point is visual proof the save
             * round-tripped, not a stub that implies one. */
            seam_clear_screen(ctx);
            u2_status_draw(ctx);
            u2_status_draw_character(ctx);
            seam_str_out_at(ctx, 1, 24, "[engine] OVERWORLD GOES HERE (CP4)");
            seam_present(ctx);
            (void)wait_choice(ctx);
            return;
        }
        if (r == U2_SAVE_ERROR) return;

        /* U2_SAVE_BLANK, or the path is unusable: the original's own response
         * to a disk with no character on it. */
        seam_clear_screen(ctx);
        seam_str_out_at(ctx, 10, 12, U2_S_DISK_PLAYER);
        seam_present(ctx);
        for (;;) {
            if (!seam_key_poll(ctx, &k)) {
                if (ctx->mode == U2_MODE_HEADLESS || ctx->exit_code) return;
                seam_wait_ms(ctx, 20);
                continue;
            }
            if (k.ch == 0x1b) return;            /* asm/0991: ESC to retry */
        }
    }
}

int u2_title_flow(u2_ctx *ctx)
{
    ctx->content_source = U2_CONTENT_SOURCE;    /* from the generated header */
    ctx->state.story_flag = 0;                  /* asm/0133_sub_0133.asm */

    /* Opening presentation, then the title (asm/0237 branches to the title
     * while the story flag is still clear). */
    if (!present_intro(ctx) && ctx->state.story_flag != 0 && !flow_stopped(ctx))
        present_poem(ctx);

    for (;;) {
        draw_title(ctx);
        int key = wait_choice(ctx);

        if (key < 0)
            return 0;
        switch (key) {
        case 'D':                               /* asm/039B: jmp 0x3AF */
            run_demo(ctx);
            break;
        case 'P':                               /* asm/03A0 -> asm/0764 */
            run_play(ctx);
            break;
        case 'C': {                             /* asm/03A4 -> asm/5400 -> 03AC */
            const int made = u2_creation_flow(ctx);
            if (made < 0)
                stub_not_implemented(ctx, "create: save path unusable");
            /* Ctrl-Q inside the form must leave the game, not just the form.
             * The flow records the intent on the context and unwinds here. */
            else if (ctx->exit_code) return 0;
            /* Only a *completed* creation runs the tail. The flow returns 0 when
             * the player abandons it with ESC and 1 when the form is confirmed
             * and saved; treating any non-negative return as success made an
             * abandoned creation restart the program, which then legitimately
             * cleared the story flag and looked like a demo regression. */
            else if (made != 1) break;
            else if (ctx->fidelity == U2_FIDELITY_ENHANCED) {
                /* Enhanced: the disk swap existed because the program and the
                 * data were on separate media. There is no media to swap, so the
                 * whole restart is a dead end inherited from CP/M — skip it and
                 * go straight into the world. */
                ctx->enter_world = 1;
                return 0;
            } else {
                /* Original: asm/5400's tail prints "TO PLAY ULTIMA II", then
                 * "INSERT PROGRAM MASTER", spins on key_poll, and calls the
                 * startup routine at runtime 0x0100 — a full program restart,
                 * which is why finishing creation lands on the splash and the
                 * player must press P to play. Reproduced rather than shortcut,
                 * because it is what the byte-comparison baseline measures. */
                seam_clear_screen(ctx);
                seam_str_out(ctx, U2_S_TO_PLAY);
                seam_str_out(ctx, U2_S_INSERT_PROGRAM);
                seam_present(ctx);
                u2_key k;
                /* The original spins on the non-blocking key_poll (asm/5400's
                 * tail: `call key_poll; jne <loop>`), so the engine does the
                 * same rather than a blocking read. That also avoids a real
                 * footgun: the script backend signals an exhausted run by
                 * setting exit_code = 0, which is falsy, so a guard written as
                 * `!exit_code` can never fire and the loop hangs the harness.
                 * seam_key_poll sets input_exhausted, which is the flag that
                 * actually distinguishes "no key yet" from "no keys ever". */
                for (;;) {
                    const int got = seam_key_poll(ctx, &k);
                    if (got != 0) break;
                    if (ctx->exit_code || ctx->input_exhausted) break;
                    seam_wait_ms(ctx, 16);
                }
                ctx->restart_requested = 1;
                return 0;
            }
            break;
        }
        case 0x11:                              /* Ctrl-Q: quit (deviation D5) */
            /* The original has no quit path: 'Q' in the command set is
             * "QUIT OR SAVE GAME." (string at file 0x3FCA) and it saved rather
             * than exiting, because on a real machine you switched disks or
             * powered off. There is no disk to swap here, so a clean quit key is
             * a gap rather than a deviation. Ctrl-Q rather than Ctrl-C because
             * Ctrl-C is a signal the terminal owns and a harness cannot always
             * deliver. */
            ctx->exit_code = 0;
            return 0;
        default:                                /* asm/03A6: redraw */
            break;
        }
    }
}
