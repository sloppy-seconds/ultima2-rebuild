/*
 * game.c — main driver.
 *
 * CP1 shape: the engine starts in the presentation/title flow (game/title.c),
 * which is what the original does after asm/0133. The command loop, the
 * overworld and the rest hang off the choices that title flow currently stubs.
 *
 * --selftest keeps the CP0 automation-substrate probe available: it exercises
 * every seam once on a screen of engine-authored text, so `make test` can
 * verify the platform layer without depending on game content or on the title
 * flow's key handling. That is what engine/tests/run_cp0.sh runs.
 *
 * Everything drawn here is engine-authored placeholder text. No Ultima II
 * string is in this file: content is extracted at build time from the user's
 * own copy (tools/extract/extractor.py) into gitignored headers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/game.h"
#include "game/title.h"
#include "game/overworld.h"
#include "core/seam.h"
#include "core/demo.h"
#include "debug/trace.h"

typedef struct game_ctx {
    int   frames;
    int   keys_seen;
    int   quit;
    char  last[3];
} game_ctx;

/* ---- CP0 substrate self-test ------------------------------------------- */

static void draw_skeleton_screen(u2_ctx *ctx)
{
    /* Kept inside 40 columns: the grid is 40x25 (see core/vram.h). */
    seam_str_out_at(ctx, 2, 2, "U2 ENGINE - CP0 SKELETON");
    seam_str_out_at(ctx, 2, 4, "portable C engine for the U2 binary");
    seam_str_out_at(ctx, 2, 5, "spec: asm/ tree (byte-proven)");
    seam_str_out_at(ctx, 2, 7, "no game content - see CP1 extractor");
    seam_str_out_at(ctx, 2, 9, "seams: char_out str_out key_poll");
    seam_str_out_at(ctx, 2, 10, "       rand_byte timed_wait speaker");
    seam_str_out_at(ctx, 2, 12, "press Q to quit");
    /* Exercise the cursor seam here, over the last character of that line —
     * "press Q to quit" is 15 characters from column 2, so the final 't' is
     * column 16 and not 17. The cursor must sit ON a glyph, not after one: a block
     * over a blank cell is a solid rectangle, and that cannot distinguish "cursor
     * drawn" from "cursor drawn on top of the character it is supposed to invert",
     * which is half of what went wrong under the FM Towns theme.
     *
     * Every seam but this one was already on the skeleton, and the cursor is the
     * one whose correctness no text assertion can see: it is the same glyph
     * repainted inverse video by the renderer, so a block drawn in the background
     * colour is invisible while the grid stays byte-for-byte right. It stayed right
     * through the FM Towns cursor defect of 2026-11-06. --dump-screen saves the
     * FIRST presented frame and this is the only screen a test can reach on a first
     * present, so without a cursor here no gate could ever inspect one. */
    ctx->vram.visible = 1;
    seam_goto(ctx, 16, 12);
}

static void draw_probe_line(u2_ctx *ctx, game_ctx *g)
{
    char buf[64];

    /* rand_byte probe: proves the tick mixer and the deterministic clock. */
    unsigned r1 = seam_rand_byte(ctx);
    unsigned r2 = seam_rand_byte(ctx);
    snprintf(buf, sizeof buf, "rand_byte(5217) -> %u %u f%d k%d %s",
             r1, r2, g->frames, g->keys_seen, g->last);
    seam_str_out_at(ctx, 1, 14, buf);

    /* timed_wait + speaker_freq probes: the two remaining platform seams. */
    seam_wait_ms(ctx, 1);
    seam_tone(ctx, 0x0830, 5); /* divisor for ~750 Hz, asm/5CD2_speaker_freq.asm */
    snprintf(buf, sizeof buf, "timed_wait(52CA) ok speaker_freq(5CD2)");
    seam_str_out_at(ctx, 1, 16, buf);
}

static void run_selftest(u2_ctx *ctx)
{
    game_ctx *g = ctx->be_game;

    draw_skeleton_screen(ctx);
    seam_present(ctx);
    for (;;) {
        u2_key k;

        u2_demo_quiescent(ctx, U2_DEMO_MODE_TITLE, NULL);
        if (ctx->exit_code || g->quit)
            break;
        if (!seam_key_poll(ctx, &k)) {
            /* A headless run ends when the script is spent. An SDL run has no script
             * (main refuses --script outside headless), so the only thing that can
             * end it is --dump-screen having saved the frame it came to save;
             * without this the loop waits for a key a dummy video driver will never
             * produce and the gate times out. Same shape as T6: an observation flag
             * that could not stop the run it was asked to observe. */
            if (ctx->mode == U2_MODE_HEADLESS || ctx->dump_screen_done)
                break;
            seam_wait_ms(ctx, 10);
            continue;
        }
        g->keys_seen++;
        g->last[0] = (k.ch >= 0x20 && k.ch < 0x7F) ? (char)k.ch : '.';
        g->last[1] = '\0';
        if (k.ch == 'Q' || k.ch == 0x03) {
            g->quit = 1;
            continue;
        }
        g->frames++;
        draw_probe_line(ctx, g);
        seam_present(ctx);
    }
}

/* ---- driver ------------------------------------------------------------- */

int u2_game_init(u2_ctx *ctx)
{
    game_ctx *g = calloc(1, sizeof *g);
    if (!g) return -1;
    snprintf(g->last, sizeof g->last, "-");
    ctx->be_game = g;
    return 0;
}

void u2_game_run(u2_ctx *ctx)
{
    if (ctx->selftest) {
        run_selftest(ctx);
        return;
    }
    /* The original's creation tail restarts the program by calling its own
     * startup routine, so re-entering the title flow is the faithful shape of
     * "the player finished creating a character": splash again, then the menu,
     * then P to play. Guarded on exit_code so a closed window or an exhausted
     * script can never spin here. */
    for (;;) {
        ctx->restart_requested = 0;
        u2_title_flow(ctx);
        if (!ctx->restart_requested || ctx->exit_code) break;
    }

    /* Enhanced mode's creation tail sets this instead of restarting: straight
     * into the world, no disk-swap theatre.
     *
     * It must spawn from the player record, exactly as the 'P' path does, because
     * that is the same record creation just wrote. u2_overworld_run makes that
     * choice itself now (see overworld_enter) rather than leaving it to each call
     * site; it used to call u2_overworld_init, the probe entry, which put every
     * enhanced-mode start in sector 30 near Lord British's castle instead of
     * sector 20 at 20,20, the pair creation writes at asm/5BDC..5BE8. Original
     * mode never showed the defect only because its tail restarts and the player
     * comes back in through 'P'. */
    if (ctx->enter_world && !ctx->exit_code) {
        u2_overworld ow;
        (void)u2_overworld_run(ctx, &ow);
    }
}

void u2_game_shutdown(u2_ctx *ctx)
{
    free(ctx->be_game);
    ctx->be_game = NULL;
}
