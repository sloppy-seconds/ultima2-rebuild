/*
 * main.c — engine entry point and backend wiring.
 *
 * Modes (docs/phase4-plan.md §2): the headless build is a first-class mode,
 * not a debug patch. --headless selects the null renderer, the script input
 * (or piped stdin), the silent audio backend and the deterministic clock, so a
 * run is reproducible and its stdout is machine-readable.
 *
 *   ./u2                                  interactive terminal play
 *   ./u2 --headless --script k.keys       scripted playthrough
 *   ./u2 --headless --script k.keys --dump-state
 *   ./u2 --trace trace.log                seam log with asm citations
 */
#define _POSIX_C_SOURCE 200809L   /* gmtime_r, strdup under -std=c11;
                                  * stated rather than assumed, as in core/save.c */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>          /* strcasecmp, for a mode word from any source */
#include "core/api.h"
#include "core/ctx.h"
#include "core/version.h"
#include "core/licenses.h"
#include "core/config.h"
#include "core/install.h"
#include "core/seam.h"
#include "core/timer.h"
#include "core/state.h"
#include "core/save.h"
#include "core/slot.h"
#include "core/tiles_ega.h"
#include "core/rng.h"
#include "core/replay.h"
#include "core/demo.h"
#include "debug/trace.h"
#include "debug/dump_state.h"
#include "debug/turnlog.h"
#include "game/game.h"
#include "game/overworld.h"
#include "cli.h"

static int wire_backends(u2_ctx *ctx)
{
    /* Every backend here is chosen by mode. The wall clock is the exception and
     * is chosen by the host OS: mingw has no clock_gettime or nanosleep to link,
     * so a Windows build gets time_win32.c and nothing else. */
#ifdef _WIN32
#  define U2_CLOCK_HOST u2_clock_win32
#else
#  define U2_CLOCK_HOST u2_clock_posix
#endif

    if (ctx->mode == U2_MODE_HEADLESS) {
        ctx->renderer = &u2_renderer_null;
        ctx->audio    = &u2_audio_null;
        ctx->clock    = &u2_clock_script;
        ctx->input    = ctx->script_path ? &u2_input_script : &u2_input_term;
        /* Headless names the null renderer because nothing is looking at the screen,
         * and that stays the default. An explicit --renderer=terminal is honoured on
         * top of it: the grid still composes, the escapes still go to stdout, and the
         * input and clock stay scripted so the run is still reproducible. The demo
         * gate needs exactly this combination, because "the renderer cannot reach game
         * state" is a claim that can only be made by running a second renderer and
         * comparing digests (docs/demo-record-playback.md W8). */
        if (ctx->renderer_explicit && ctx->renderer_choice == U2_RENDERER_TERMINAL)
            ctx->renderer = &u2_renderer_term;
#ifdef U2_HAVE_SDL
    } else if (ctx->renderer_choice == U2_RENDERER_SDL) {
        /* The bitmap shell. It only moves pixels the engine already composed;
         * choosing it changes nothing about what the game draws. */
        ctx->renderer = &u2_renderer_sdl;
        ctx->audio    = &u2_audio_sdl;
        ctx->clock    = &U2_CLOCK_HOST;
        /* A key script stays a headless affordance UNLESS the SDL renderer was chosen.
         * The reason is that the shipped menu path only enters the world when the
         * renderer can present pixels (title.c's 'P' branch checks present_frame), so
         * with no scripted input for that renderer there is no scripted route to the
         * menu at all -- which is how the death reload shipped for a day with no gate
         * able to reach it. With SDL_VIDEODRIVER=dummy the combination is still
         * headless and still reproducible: the frame composes and uploads, the keys
         * come from a file. CP4's SDL gate already proves the dummy driver presents. */
        ctx->input    = ctx->script_path ? &u2_input_script : &u2_input_sdl;
#endif
    } else {
        ctx->renderer = &u2_renderer_term;
        ctx->audio    = &u2_audio_beep;
        ctx->clock    = &U2_CLOCK_HOST;
        ctx->input    = &u2_input_term;
    }

    /* --api replaces the input backend the same way --playback does, and for the same
     * reason: the caller owns time. There is no wall-clock idle between commands, so the
     * idle auto-PASS has nothing to fire on and must be stood down rather than left to
     * spin. Everything else — renderer, clock, audio — stays whatever the mode chose. */
    if (ctx->api_mode) {
        ctx->input = &u2_input_api;
        ctx->auto_pass_polls = -1;
        /* Checked here rather than off the flag, because --headless resolves the renderer:
         * what matters is whether something is actually going to draw onto the protocol's
         * stdout, not which word the player typed. */
        if (ctx->renderer == &u2_renderer_term) {
            fprintf(stderr, "u2: --api with the terminal renderer is refused: it draws to"
                            " the same stdout the protocol speaks on. Use --headless.\n");
            return -1;
        }
    }

    /* --playback replaces the input backend and nothing else. The renderer stays
     * whatever the mode chose, because a demo is watchable by ruling (D11) and the
     * picture is presentation; the clock stays whatever the mode chose for the same
     * reason, which is why headless playback is unthrottled (the script clock's
     * sleep is instant) and SDL playback is not. Forcing the script clock here would
     * have quietly deleted the pacing D11 asks for.
     *
     * auto_pass_polls goes to -1 for the session (D4): the playback loop polls for
     * keys between records, each poll adds to the idle counter, and at the threshold
     * the engine would perform a PASS that was never in the recording. The auto-PASS
     * turns a demo does carry arrive through u2_demo_auto_pass_due instead. */
    if (ctx->playback_path) {
        ctx->input = &u2_input_demo;
        ctx->auto_pass_polls = -1;
        /* --playback-pace asks for real-time pacing in a run that would otherwise be
         * unthrottled, and the script clock's sleep is instant by design. So the flag
         * takes the host clock: pacing is presentation, and a presentation knob that
         * silently does nothing is worse than one that is not offered. The state is
         * driven from the file either way, which is what W15 measures. */
        if (ctx->mode == U2_MODE_HEADLESS && ctx->demo_play_pace)
            ctx->clock = &U2_CLOCK_HOST;
    }

    if (ctx->clock->init && ctx->clock->init(ctx) != 0) return -1;
    if (ctx->renderer->init && ctx->renderer->init(ctx) != 0) return -1;
    if (ctx->input->init && ctx->input->init(ctx) != 0) return -1;
    if (ctx->audio->init && ctx->audio->init(ctx) != 0) return -1;
    return 0;
}

static void release_backends(u2_ctx *ctx)
{
    if (ctx->input->shutdown) ctx->input->shutdown(ctx);
    if (ctx->audio->shutdown) ctx->audio->shutdown(ctx);
    if (ctx->renderer->shutdown) ctx->renderer->shutdown(ctx);
    if (ctx->clock->shutdown) ctx->clock->shutdown(ctx);
}

/* The DS checkpoint itself lives in core/state.c (u2_state_load_ds) because it has to be applied
 * after the world and monx load, which is in the overworld, not here. */

/* One mode, chosen from one of four places, applied by one rule.
 *
 * `classic` is the whole original presentation and so settles the art axis with the
 * fidelity, exactly as --classic does on the command line. Leaving it unset for
 * enhanced is deliberate: enhanced *prefers* themed art but must not require it, and
 * the auto-detect further down is what grants that preference quietly when a theme is
 * installed and falls back to CGA with a printed line when none is.
 *
 * Returns -1 for a word that is neither mode. Every source that can name a mode is
 * checked with the same refusal, because a misspelt mode is the mistake that plays
 * for twenty minutes before anyone notices the wrong layout. */
static int apply_mode_name(u2_ctx *ctx, const char *name, int *art_forced)
{
    if (!name || !*name)
        return 0;
    if (!strcasecmp(name, "classic")) {
        ctx->fidelity = U2_FIDELITY_ORIGINAL;
        ctx->art_mode = U2_ART_CGA;
        *art_forced = 1;
        return 0;
    }
    if (!strcasecmp(name, "enhanced")) {
        ctx->fidelity = U2_FIDELITY_ENHANCED;
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    u2_ctx ctx;
    u2_cli cli;
    u2_config cfg;
    /* Defaults with a ruling behind them, so they stay here rather than in the parser:
     * enhanced is the default mode (owner, 2026-11-17) and D5 speed-pinning is what
     * playback means (docs/demo-record-playback.md). The parser records what the player
     * said; what the run is by default is main's business. */
    memset(&ctx, 0, sizeof ctx);
    ctx.mode = U2_MODE_TERM;
    ctx.fidelity = U2_FIDELITY_ENHANCED;
    ctx.demo_speed_pin = 1;
    ctx.debug_limit = U2_TURNLOG_DEFAULT_LIMIT;

    u2_cli_action act = u2_cli_parse(argc, argv, &ctx, &cli);
    if (act == U2_CLI_REFUSE) {
        fprintf(stderr, "%s\n", cli.err);
        if (cli.usage_on_refuse) u2_cli_usage(argv[0]);
        return 2;
    }
    /* The info paths print and leave. They are dispositions rather than work done inside
     * the parser so that parsing writes no stream, which is what makes a refusal assertable
     * without capturing a pipe. */
    if (act == U2_CLI_INFO_HELP) {
        u2_cli_usage(argv[0]);
        return 0;
    }
    if (act == U2_CLI_INFO_VERSION) {
        char info[192];
        u2_build_info(info, sizeof info);
        printf("%s\n", info);
        return 0;
    }
    if (act == U2_CLI_INFO_LICENSES) {
        u2_licenses_print(stdout);
        return 0;
    }
    if (act == U2_CLI_INFO_VIDEO_DRIVERS) {
#ifdef U2_HAVE_SDL
        u2_sdl_list_video_drivers();
#endif
        return 0;
    }


    /* --install is a tool mode: it runs before config, before video, before the
     * game, and it needs no renderer. That is not tidiness — the terminal build is
     * what CI can execute, so an installer that needed SDL would be an installer
     * nobody could prove. docs/install-design.md § 2. */
    if (act == U2_CLI_INSTALL) {
        char ierr[512];
        u2_install_request rq;
        u2_config icfg;
        char iconfig[1024];
        memset(&rq, 0, sizeof rq);
        rq.to = cli.to;
        rq.original_dir = cli.orig_dir;
        rq.updated_dir = cli.upd_dir;
        rq.n_themes_dir = cli.n_themes;
        rq.fmtowns_media = cli.fmtowns;
        rq.dry_run = cli.dry_run;
        for (int k = 0; k < cli.n_themes; k++)
            rq.themes_dir[k] = cli.themes[k];
        rq.self_path = argv[0];

        /* Whatever the command line did not say, the tree may already know: an
         * install records its sources in config.ini, so a binary kept beside the
         * game reinstalls with no arguments. Same precedence as every other path in
         * the engine -- flag > config.ini > prompt -- and a recorded source that has
         * moved is reported and asked for again, never skipped. */
        memset(&icfg, 0, sizeof icfg);
        if (u2_install_find_config(cli.config_path, argv[0], iconfig, sizeof iconfig)) {
            char cerr[512];
            const u2_config_result cr = u2_config_load(iconfig, &icfg, cerr, sizeof cerr);
            if (cr != U2_CONFIG_OK) {
                fprintf(stderr, "u2: %s\n", cerr);
                return 2;
            }
            u2_install_fill_from_config(&rq, &icfg, stdout);
        }

        /* Prompts are the fallback for a flag that was not given, never a replacement
         * for one, and they are off unless stdin is really a terminal. A piped or
         * redirected stdin is a script, and a script that blocks on a question is a
         * worse failure than one that refuses. --no-prompt turns them off by hand so
         * a human can get the old behaviour back. */
        u2_install_answers ans;
        memset(&ans, 0, sizeof ans);
        if (!cli.check_only && !cli.no_prompt && isatty(fileno(stdin))) {
            const int go = u2_install_prompt(&rq, &ans, stdout, ierr, sizeof ierr);
            if (go < 0) {
                if (*ierr)
                    fprintf(stderr, "u2: %s\n", ierr);
                return 2;
            }
            if (go == 0)
                return 0;
        }
        const u2_install_result r = cli.check_only
            ? u2_install_check(rq.to, stdout, ierr, sizeof ierr)
            : u2_install_apply(&rq, stdout, ierr, sizeof ierr);
        if (r != U2_INSTALL_OK && *ierr)
            fprintf(stderr, "u2: %s\n", ierr);
        return (int)r;
    }

    /* config.ini, if there is one. Absent is normal — the drop-in case has never
     * had one, and --install is additive — so this is a fill-in step, not a
     * requirement. A file that exists and is wrong stops the run: a typo in a path
     * is reported, never papered over by finding the thing somewhere else, which is
     * the same [LOCKED] rule --ega-theme-dir already follows. */
    memset(&cfg, 0, sizeof cfg);
    {
        char cerr[512];
        const u2_config_result cr = u2_config_load(
            cli.config_path ? cli.config_path : U2_CONFIG_DEFAULT_FILE, &cfg, cerr, sizeof cerr);
        if (cr != U2_CONFIG_OK) {
            fprintf(stderr, "u2: %s\n", cerr);
            return 2;
        }
        /* Remembered whether or not the file existed: an in-game change is written
         * back to this path, and creating the file is better than accepting a
         * preference and losing it. */
        snprintf(ctx.config_path, sizeof ctx.config_path, "%s",
                 cli.config_path ? cli.config_path : U2_CONFIG_DEFAULT_FILE);
        u2_config_apply(&ctx, &cfg);
        /* The mode, from the three places that are not the command line, in the same
         * precedence the paths follow: a flag outranks the environment, the
         * environment outranks config.ini, and config.ini outranks the built-in
         * default of enhanced. `cli.mode_given` is why the flag has to be tracked
         * separately now that the default is non-zero.
         *
         * U2_MODE exists for the test harness, and it is worth saying why a knob
         * nobody plays with is in the shipped binary. CP5 is the original-fidelity
         * checkpoint, and roughly a third of its gates drive the engine through the
         * python helpers under tests/, which pass the binary down as one string;
         * per-invocation flags would mean editing every one of those helpers, and a
         * helper that quietly ran in the other mode is exactly the silent-wrong-verdict
         * failure this project has already been bitten by twice. One exported variable
         * at the top of run_cp5.sh pins the whole checkpoint, helpers included, and is
         * greppable. A player never sets it. */
        const char *mode_env = getenv("U2_MODE");
        if (!cli.mode_given) {
            char mode_src[64];
            const char *mode_word = *cfg.mode ? cfg.mode : NULL;
            if (mode_env && *mode_env) {
                snprintf(mode_src, sizeof mode_src, "%.60s", mode_env);
                mode_word = mode_src;
            }
            if (mode_word &&
                apply_mode_name(&ctx, mode_word, &cli.art_mode_forced) != 0) {
                fprintf(stderr, "u2: %s = %s is neither 'classic' nor 'enhanced'\n",
                        (mode_env && *mode_env) ? "U2_MODE" : "[game] mode",
                        mode_word);
                return 2;
            }
        }
        /* A theme named in config.ini must behave exactly like --ega-theme on the
         * command line, and the first version of this only half did it: it set
         * cli.theme_named, which suppresses auto-detection, without selecting the art
         * axis -- so `theme = fmtowns` in a config produced CGA, which is worse
         * than saying nothing at all. Found by deploying to a real installed tree
         * rather than by the fixture tests, which is the whole reason the deploy
         * step exists. Naming a theme is asking for themed art, from either place.
         *
         * But it must not beat a flag. The block used to apply unconditionally, and it
         * runs after argument parsing, so a config that names a theme overwrote
         * `--classic` and `--cga` outright: the flag set CGA, this set EGA, and this
         * ran last -- the opposite of the precedence the usage text promises ("a flag
         * always wins over it"), and it also set cli.ega_required on a run that had asked
         * for the original art. Owner-found 2026-11-20 on a tree whose config selects
         * the `alt` theme: `--classic --cga` still printed `art mode: ega (theme alt)`.
         *
         * What loses is the *axis* decision, not the name: `--ega` with `theme = alt`
         * still gets the named theme, because the flag chose the axis and the config
         * supplied a detail. And `[game] mode = classic` in the same file is the more
         * specific statement of intent than `[themes] theme`, so it wins too -- which
         * falls out of testing cli.art_mode_forced rather than only the flags.
         */
        const int theme_overridden = *cfg.theme && cli.art_mode_forced
                                     && ctx.art_mode != U2_ART_EGA;
        if (*cfg.theme && !theme_overridden) {
            cli.theme_named = 1;
            cli.art_mode_forced = 1;
            cli.ega_required = 1;
            ctx.art_mode = U2_ART_EGA;
        } else if (theme_overridden) {
            fprintf(stderr, "u2: NOTE [themes] theme = %s ignored: the art mode was"
                            " already chosen by a flag or by [game] mode"
                            " (--classic/--cga/--cleanroom)\n", cfg.theme);
        }
    }

    /* The renderer default is a property of the *build*, not of the run. The
     * binary a player downloads is the SDL one, and it should open a window when
     * double-clicked with no flags and no terminal; the cc-only build the whole
     * test suite drives keeps the text grid it has always had. */
#ifdef U2_HAVE_SDL
    if (ctx.renderer_choice == U2_RENDERER_AUTO) ctx.renderer_choice = U2_RENDERER_SDL;
#else
    if (ctx.renderer_choice == U2_RENDERER_AUTO) ctx.renderer_choice = U2_RENDERER_TERMINAL;
    if (ctx.renderer_choice == U2_RENDERER_SDL) {
        /* This used to be ignored in silence, which made the wrong binary plus the
         * right flag look exactly like a window that failed to open. */
        fprintf(stderr, "u2: this binary was built without an SDL backend, so "
                        "--renderer=sdl cannot be honoured. Build it with `make sdl`,\n"
                        "or use --renderer=terminal.\n");
        return 2;
    }
#endif

    /* Two input backends is a configuration with no meaning. A script and a demo
     * both claim the keyboard and whichever won would decide what the player saw,
     * so the combination is refused in the same style as the one above rather than
     * resolved by a precedence nobody asked for. */
    if (ctx.playback_path && ctx.script_path) {
        fprintf(stderr, "u2: --playback and --script together is refused: a demo and a"
                        " key script are two sources of the same keystrokes.\n");
        return 2;
    }

    /* A8 (docs/play-api.md): --api owns stdin for commands and stdout for observations.
     * Anything else that claims either one is refused rather than resolved by a precedence
     * nobody asked for -- the same shape as the two refusals above. The terminal renderer
     * is refused because it draws to stdout, and one stray escape sequence corrupts a
     * protocol line mid-way and the caller sees garbage instead of an error. */
    if (ctx.api_mode && ctx.script_path) {
        fprintf(stderr, "u2: --api and --script together is refused: the API and a key"
                        " script are two sources of the same keystrokes.\n");
        return 2;
    }
    if (ctx.api_mode && ctx.playback_path) {
        fprintf(stderr, "u2: --api and --playback together is refused: a replay and a"
                        " live caller cannot both deliver the next key.\n");
        return 2;
    }

    /* Playback must not touch the player's saves or overlays (§ 7.4). A demo
     * containing a `Q`, or simply a site entry — the original auto-saves on site
     * entry and on rocket launch, measured 2026-11-03 — would otherwise mutate the
     * viewer's own slot while they were only watching. The scratch directory sits
     * under the demo's own directory so a shared demo brings its scratch with it,
     * and the redirect is announced once because a save that silently went somewhere
     * else is the kind of surprise this project refuses elsewhere.
     *
     * It is set here, before the slot migration below, because `u2_slot_adopt_legacy`
     * reads the same field: adopting into a scratch directory the player never named
     * would move their real record. */
    if (ctx.playback_path) {
        static char scratch[520];
        char base[430];
        const char *slash = strrchr(ctx.playback_path, '/');

        if (ctx.demo_dir && *ctx.demo_dir)
            snprintf(base, sizeof base, "%.400s", ctx.demo_dir);
        else if (slash) {
            size_t n = (size_t)(slash - ctx.playback_path);
            if (n >= sizeof base) n = sizeof base - 1;
            memcpy(base, ctx.playback_path, n);
            base[n] = '\0';
            if (n == 0) snprintf(base, sizeof base, ".");
        } else
            snprintf(base, sizeof base, "demos");

        snprintf(scratch, sizeof scratch, "%.429s/playback", base);
        ctx.save_dir = scratch;
        fprintf(stderr, "u2: playback writes go to the scratch save directory '%s'; "
                        "your own saves are untouched.\n", scratch);
    }

    /* --keep-site-state is an enhanced-mode knob. Refusing it in original mode is the
     * whole point: the original writes no sector file on a transition, so a default-mode
     * run that persisted town state would be inventing behaviour, quietly, and the CP2
     * byte-comparison baseline would be measuring something the game never did. */
    if (ctx.keep_site_state && ctx.fidelity != U2_FIDELITY_ENHANCED) {
        fprintf(stderr, "u2: --keep-site-state needs --enhanced; the original persists "
                        "no sector state on a transition (docs/save-format.md \u00a7 10)\n");
        return 2;
    }

    /* --enhanced drives the screen layout as well as the fidelity rules, and it
     * implies the EGA art axis: a 640x480 canvas has room for a 32px theme by
     * definition, so --ega is redundant under it rather than orthogonal (owner,
     * 2026-11-05). Done after the argument loop because --enhanced may appear on
     * either side of --ega, and an explicit --ega-theme still selects the theme. */
    if (ctx.fidelity == U2_FIDELITY_ENHANCED) {
        ctx.enhanced_layout = 1;
        ctx.art_mode = U2_ART_EGA;
    }

    /* Nothing named an art axis, so take it from what is installed. Opened
     * 2026-11-06: an --install tree with themes in it still started in CGA unless
     * the player typed --ega, so the better art sat on disk unused. When a themes
     * tree is found this also prefers the derived FM Towns theme over the fan
     * pack's `default`, because deriving it means the player went and pointed the
     * installer at a disc they own.
     *
     * Runs before the strict check below, and is not itself strict: finding nothing
     * leaves the run on CGA, which is the default being unfilled rather than a
     * request being downgraded. --enhanced reaches here with the mode already EGA,
     * so it gets the theme preference without changing its art mode. */
    char art_theme[64] = "";
    int art_auto = 0;
    if (!cli.theme_named && (ctx.art_mode == U2_ART_EGA || !cli.art_mode_forced))
        art_auto = u2_tiles_ega_autodetect(&ctx, art_theme, sizeof art_theme,
                                           !cli.art_mode_forced);

    /* --enhanced wanted themed art and there is none. Take the CGA tiles and keep
     * the enhanced layout: the layout is a canvas decision (core/layout.h) and does
     * not depend on the art axis, so a 640x480 window drawing 4-colour tiles is a
     * working game rather than a refusal. The ruling that made --ega redundant under
     * --enhanced was about the canvas having room for a 32px theme, not about
     * demanding that one exist. An explicit --ega still refuses, which is the
     * 2026-11-04 ruling and unchanged. */
    if (ctx.art_mode == U2_ART_EGA && !cli.ega_required && !art_auto) {
        ctx.art_mode = U2_ART_CGA;
        fprintf(stderr, "u2: no EGA theme installed, so --enhanced draws the original "
                        "CGA art (the enhanced layout is unaffected)\n");
    }

    /* --ega is strict (owner, 2026-11-04): if the themes directory does not exist
     * or the chosen theme is not in it, refuse to start rather than fall back to
     * CGA. This supersedes the plan's original "never fail a launch" default; the
     * point is that asking for EGA art and quietly getting CGA is the failure mode
     * worth avoiding, and it is checked here at launch so the run dies before any
     * screen is drawn, not after the title flow. */
    if (ctx.art_mode == U2_ART_EGA && cli.ega_required) {
        char ega_err[512];
        if (u2_tiles_ega_validate(&ctx, ega_err, sizeof ega_err) != U2_TILES_EGA_OK) {
            fprintf(stderr, "u2: --ega refused: %s\n", ega_err);
            return 2;
        }
    }

    /* Announce which art this run was asked for, at startup rather than when the
     * overworld first draws. deploy/novnc/up.sh verifies the artifact by making the
     * binary say its art mode — a check that exists because substitute art once
     * reached a player unnoticed — and a run that stops at the title and splash
     * screens never reaches the overworld line, so the guarantee has to hold before
     * any screen is drawn. The wording deliberately keeps the two discriminators that
     * script greps for: "cleanroom" for the substitute set, "tiles extracted" for
     * the user's own art. */
    fprintf(stderr, "u2: art source: %s\n",
            ctx.cleanroom ? "cleanroom (engine's own substitute tiles)"
                          : "original tiles extracted from the user's copy");

    /* Which art axis, and how it was arrived at. A separate line rather than a
     * clause on the one above, because deploy/novnc/up.sh greps that line for
     * "cleanroom" / "tiles extracted" to verify the artifact, and a check that
     * silently stopped matching is the failure mode that script exists to prevent.
     * Saying "auto-selected" matters: art that changes because of what happens to
     * be on disk is exactly the kind of thing a player should be told, not told
     * nothing about. */
    if (ctx.cleanroom)
        fprintf(stderr, "u2: art mode: cga (cleanroom)\n");
    else if (ctx.art_mode == U2_ART_EGA)
        fprintf(stderr, "u2: art mode: ega (theme %s%s)\n",
                *art_theme ? art_theme
                           : ((ctx.ega_theme && *ctx.ega_theme) ? ctx.ega_theme
                                                                : U2_EGA_THEME_DEFAULT),
                art_auto ? ", auto-selected" : "");
    else
        fprintf(stderr, "u2: art mode: cga (original 4-colour)\n");

    if (cli.trace_arg && u2_trace_open(&ctx, cli.trace_arg) != 0) {
        fprintf(stderr, "u2: cannot open trace file '%s'\n", cli.trace_arg);
        return 2;
    }

    /* Per-roll trace. Not closed explicitly: stdio flushes on normal exit, and threading an fclose
     * through every early-return path for a diagnostic buys nothing. Fails loudly if the path is
     * unusable, because a silently-missing trace reads exactly like "no rolls happened". */
    if (cli.roll_trace_path) {
        ctx.roll_trace = fopen(cli.roll_trace_path, "w");
        if (!ctx.roll_trace) {
            fprintf(stderr, "u2: cannot open --trace-rolls file '%s'\n", cli.roll_trace_path);
            return 2;
        }
    }

    u2_vram_init(&ctx.vram);
    u2_state_init(&ctx.state);
    /* asm/0100_sub_0100.asm seeds the shift register once, at program launch,
     * from the BIOS keyboard flags at 0x40:6C. Nothing re-seeds it per game;
     * the only other caller of that stub is the "INSERT PROGRAM MASTER"
     * restart at asm/5400's tail. --seed overrides it so a run can be made
     * reproducible or deliberately different. */
    u2_rng_seed(&ctx, (uint8_t)cli.seed);
    ctx.seed = cli.seed & 0xFFu;
    /* A captured register replaces the seeded one. Applied after the seed call so --seed still governs
     * everything else, and before anything rolls. */
    if (cli.rng_state_given)
        u2_rng_load(&ctx, cli.rng_state);

    if (cli.report_path) {
        u2_replay_begin(&ctx);
        u2_replay_set_keys(&ctx, ctx.script_path);
    }

    if (cli.rng_probe > 0) {
        for (int i = 0; i < cli.rng_probe; i++)
            printf("%u%s", u2_rand_byte(&ctx), i + 1 == cli.rng_probe ? "\n" : ",");
        return 0;
    }

    /* CP5: both development entries -- --play-map and --dump-map -- used to run on
     * a zeroed state, which made a scripted fight unshowable (agi 0 never lands a
     * hit) and put the player in whatever sector the zero bytes named. The title's
     * own P path loads the PLAYER record first (title.c run_play); do the same
     * here, once, so the dev entries play the save actually on disk and cannot
     * drift from the real one.
     *
     * A blank or missing record is NOT an error and the zeroed state stands: that
     * is the original's own "no character yet" condition -- DS:36 is the first byte
     * of the name, and asm/5400 tests it for zero -- and the CP4 development
     * scripts depend on it. See docs/save-format.md.
     *
     * Before any of it reads a record, a tree an older engine left behind has to be
     * adopted: its record is a FILE where the slot directory goes, and adopting
     * after the load would leave the game holding a blank state and the first save
     * overwriting the record it had just moved. core/slot.h says why the engine may
     * move a record and not a content file. */
    if (u2_slot_adopt_legacy(&ctx) != 0)
        fprintf(stderr, "u2: %s holds an interrupted save migration; refusing to guess. "
                        "Run --install to migrate this tree.\n",
                        (ctx.save_dir && *ctx.save_dir) ? ctx.save_dir : "saves");

    if (ctx.play_map || cli.map_dump_path)
        (void)u2_save_load(&ctx);

    /* --start-at, applied to the loaded record rather than to the file: the point is to play
     * a run from a chosen cell, and the sector override is what lets a script stand beside a
     * rocket pad in a wilderness sector instead of wherever the save happens to be. */
    if (ctx.start_at) {
        ctx.state.sector_x = (uint8_t)ctx.start_x;
        ctx.state.sector_y = (uint8_t)ctx.start_y;
        if (ctx.start_sector >= 0) {
            ctx.state.sector_first = (uint8_t)(ctx.start_sector / 10);
            ctx.state.sector_second = (uint8_t)(ctx.start_sector % 10);
        }
        ctx.enter_world = 1;
    }

    /* Parse the demo before a single backend is wired, so a file that cannot be
     * played is refused before anything has been opened or drawn. The refusal rules
     * — an unknown format version, a missing checkpoint, a file with no `@end`
     * unless --salvage, a checkpoint that disagrees with its own digest — live in
     * core/demo.c and every one of them prints its reason and exits 2. */
    if (ctx.playback_path) {
        if (u2_demo_begin_playback(&ctx, ctx.playback_path) != 0) {
            u2_trace_close(&ctx);
            return 2;
        }
    }

    if (cli.map_dump_path) {
        /* No backends, no display: this path exists so the real extracted tiles
         * can be seen, and diffed, on a machine with no screen at all. */
        const int rc = u2_overworld_dump(&ctx, cli.map_dump_path);
        u2_trace_close(&ctx);
        return rc == 0 ? 0 : 1;
    }

    if (ctx.api_mode && u2_api_attach(&ctx) != 0) {
        fprintf(stderr, "u2: could not start the API session.\n");
        return 1;
    }

    if (wire_backends(&ctx) != 0) {
        fprintf(stderr, "u2: backend initialisation failed\n");
        u2_trace_close(&ctx);
        return 1;
    }
    /* --ds-json: decode a raw DS blob with the engine's own decoder and exit.
     *
     * This is the seam that lets a Python bot read the ORIGINAL's memory and the engine's
     * through one decoder (docs/bot-driver.md § 1). The blob comes from Spice86 over HTTP;
     * the output is the shape `--api` emits, so a policy written against one target reads the
     * other without knowing which it has.
     *
     * Placed here, after wire_backends, for the reason api.c spells out above u2_api_attach:
     * u2_state_json reads the renderer, input and clock names out of ctx and segfaults on the
     * nulls that precede them. The first attempt put this right after u2_state_init and died
     * exactly as that comment predicted.
     *
     * enter_world is not part of DS — it is the engine's own request-to-play flag — so mode
     * would otherwise report "created" for a blob that is plainly mid-game. Deriving it from
     * the presence of a name is the same test emit_obs already uses for "title", and it is
     * not a guess about a phase the blob does not carry: a blob with no character in it is
     * not in a world.
     */
    if (act == U2_CLI_DECODE) {
        /* Both forms, because the two callers hold different ones. `--load-ds` takes
         * whitespace-separated hex, which is what `--dump-ds` writes and what a demo
         * checkpoint carries. A live Spice86 read is raw bytes, and so is every .ds file
         * already under captures/ — 10240 of them, larger than the mirror, so the leading
         * sizeof(u2_state) bytes are the DS segment and the rest is whatever the dump
         * spanned. Sniffed on content rather than on a caller-declared mode: a flag that
         * silently parsed binary as hex would return plausible garbage fields instead of an
         * error, and that is the failure shape this project keeps being bitten by. */
        int rc = -1;
        FILE *ds = fopen(cli.ds_json_path, "rb");
        if (ds) {
            static unsigned char raw[sizeof(u2_state)];
            size_t got = fread(raw, 1, sizeof raw, ds);
            fclose(ds);
            int hexy = 1;
            for (size_t k = 0; k < got && hexy; k++)
                /* The explicit `!= 0` is not paranoia. strchr(s, '\0') returns a pointer to
                 * the terminating NUL, which is non-NULL and therefore true, so a naive
                 * `strchr(set, byte)` reports every NUL byte as a hex digit — and a blob of
                 * all zeros, which is a real and common case (a blank DS mirror), was then
                 * handed to the text loader and rejected as "not whitespace-separated hex".
                 * Found by the gate, not by reading: the first blob tried had non-zero bytes
                 * in it and took the correct branch by accident. */
                hexy = raw[k] != 0 && strchr("0123456789abcdefABCDEF \t\r\n", raw[k]) != NULL;
            if (got == sizeof raw && !hexy)
                rc = u2_state_load_ds_buf(&ctx.state, raw, got);
        }
        if (rc != 0) rc = u2_state_load_ds(&ctx.state, cli.ds_json_path);

        if (cli.passmap) {
            /* The passability map for the sector in this blob, at the mount mode in this blob.
             *
             * Why the engine answers this rather than the bot applying the rule: the verdict is
             * asm/2423's mode dispatch plus a default-deny set and a threshold divert. A Python
             * copy would be a second hand-maintained version of that, and the two would drift.
             * The bot gets a bitmap and does search, which is the half it is good at.
             *
             * Needs a blob larger than the mirror, because the stamped grid lives at DS:1800 and
             * u2_state stops at 0x650. A live read of the whole DS segment is 10240 bytes, so a
             * caller that reads generously gets this; a caller that reads exactly the mirror gets
             * a refusal rather than a map computed off the end of the buffer.
             *
             * 4096 cells, row-major, one bit each, MSB first: 512 bytes, 1024 hex chars. */
            enum { PASS_GRID_OFF = 0x1800, PASS_GRID_LEN = 64 * 64 };
            FILE *pf = fopen(cli.ds_json_path, "rb");
            if (pf == NULL) {
                fprintf(stderr, "u2: --passmap could not re-read '%s'\n", cli.ds_json_path);
                return 2;
            }
            static unsigned char big[PASS_GRID_OFF + PASS_GRID_LEN];
            size_t gotb = fread(big, 1, sizeof big, pf);
            fclose(pf);
            if (gotb < sizeof big) {
                fprintf(stderr,
                        "u2: --passmap needs %zu bytes of DS (the stamped grid is at 0x1800); "
                        "the blob has %zu. Read the whole segment, not just the mirror.\n",
                        sizeof big, gotb);
                return 2;
            }
            const uint8_t stamp = ctx.state.player_stamp;
            unsigned char bits[512];
            memset(bits, 0, sizeof bits);
            for (int i = 0; i < 4096; i++) {
                if (u2_grid_passable(big + PASS_GRID_OFF, i % 64, i / 64, stamp))
                    bits[i / 8] |= (unsigned char)(0x80 >> (i % 8));
            }
            printf("{\"u2api\":%d,\"event\":\"passmap\",\"stamp\":%u,\"cells\":4096,\"bits\":\"",
                   U2_API_PROTO, (unsigned)stamp);
            for (size_t k = 0; k < sizeof bits; k++) printf("%02x", bits[k]);
            printf("\"}\n");
            return 0;
        }
        if (rc != 0) { release_backends(&ctx); u2_trace_close(&ctx); return 2; }
        if (ctx.state.name[0]) ctx.enter_world = 1;
        const int orc = u2_api_emit_observation(&ctx);
        release_backends(&ctx);
        u2_trace_close(&ctx);
        return orc == 0 ? 0 : 2;
    }
    /* After the backends, before anything runs: the first observation names the renderer,
     * input and clock, and those only exist once wire_backends has chosen them. */
    if (ctx.api_mode && u2_api_begin(&ctx) != 0) {
        fprintf(stderr, "u2: could not open the API channel.\n");
        release_backends(&ctx);
        u2_trace_close(&ctx);
        return 1;
    }
    if (u2_game_init(&ctx) != 0) {
        fprintf(stderr, "u2: game init failed\n");
        release_backends(&ctx);
        u2_trace_close(&ctx);
        return 1;
    }

    /* A demo that started outside the world has no sector to wait for, and one that
     * started inside it needs its sector digits in place before the world is opened.
     * Both mount at the first quiescent point of the flow entered below, which is the
     * instant the recorder took them (core/demo.h). */
    {
        int dhi = 0, dlo = 0;
        if (u2_demo_preload_sector(&ctx, &dhi, &dlo)) {
            ctx.state.sector_first = (uint8_t)dhi;
            ctx.state.sector_second = (uint8_t)dlo;
        }
    }

    /* One directory per launch, named by its launch time in ISO 8601, always —
     * whether the base came from --debug or --debug-path. A fixed directory is a
     * trap: turn-00001.ppm from yesterday sits next to today's and an agent
     * reading "the newest file" cannot tell which game it came from, which is the
     * stale-evidence class this feature exists to close. */
    if (ctx.debug_cli) {
        char stamp[40], full[600];
        time_t now = time(NULL);
        struct tm gm;
        gmtime_r(&now, &gm);
        strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &gm);
        snprintf(full, sizeof full, "%s/%s",
                 ctx.debug_path ? ctx.debug_path : U2_TURNLOG_DEFAULT_DIR, stamp);
        ctx.debug_path = strdup(full);
        fprintf(stderr, "u2: --debug cli writing to %s\n", full);
    }

    if (ctx.playback_path) {
        /* Playback enters the recorded mode's own flow rather than the title menu,
         * because the checkpoint stands in for every key that would have got there.
         * Running the title flow and letting it spend the demo's first keys reaching a
         * menu would replay input the recording never made and verify a state the
         * recording never had, which is the opposite of what a digest is for. */
        const u2_demo_mode dm = u2_demo_playback_mode(&ctx);
        if (u2_demo_mode_needs_world(dm)) {
            u2_overworld ow;
            const int rc = u2_overworld_init_state(&ctx, &ow);
            fprintf(stderr, "overworld: %s\n", ow.note);
            if (rc == 0) {
                u2_overworld_loop(&ctx, &ow);
                /* The loop also ends when a sector switch could not rebuild the world.
                 * For a playback that is a refusal in exactly the sense the mount refusal
                 * below is: the demo was never played, the content disagreed. It has to
                 * name the sector, because "the window closed" is the only other reason
                 * this loop stops and from the player's chair the two look identical —
                 * which is precisely how "the playback died after coming back from the
                 * town" ended up with no explanation anywhere. */
                if (ow.reload_failed) {
                    fprintf(stderr, "u2: the world could not be rebuilt: %s\n", ow.note);
                    if (ctx.playback_path) {
                        (void)u2_demo_finish(&ctx);
                        release_backends(&ctx);
                        u2_trace_close(&ctx);
                        return 2;
                    }
                    ctx.exit_code = 1;
                }
            } else if (u2_demo_overlay_failed(&ctx)) {
                /* The reason was already printed and named the file. This is a
                 * refusal, not a divergence: the demo was never played, so reporting
                 * it with the divergence code would say the engine disagreed when in
                 * fact the content did. */
                (void)u2_demo_finish(&ctx);
                release_backends(&ctx);
                u2_trace_close(&ctx);
                return 2;
            } else
                ctx.exit_code = 1;
        } else {
            /* Unreachable today, and kept as a refusal rather than a silent fall
             * through: the only non-world mode the recorder will start is one that
             * playback can enter, and if that ever changes the wrong answer should be
             * a loud one. core/demo.h names the reason creation is refused. */
            fprintf(stderr, "u2: no playback entry for start mode '%s'.\n",
                    u2_demo_mode_name(dm));
            ctx.exit_code = 2;
        }
    } else if (ctx.play_map) {
        /* The record is already loaded (above), so this enters the world as the
         * character on disk rather than as a blank state. The note goes to stderr
         * unconditionally: on this development entry "sector 31 ... 32 creatures"
         * is the difference between a silent no-op and a diagnosable one, and the
         * CP5 fight scripts are driven entirely by reading it back.
         *
         * Death reloads here too. This branch used to be the one place that did not,
         * which was invisible until it wasn't: the noVNC play session boots through
         * --play-map, so the entry with the reload switched off was the entry the
         * owner was playing in, and dying quit the game. The gates that read the
         * corpse out of the final dump now say so explicitly with --quit-on-death.
         *
         * The loop cannot spin. Each pass ends in the death handoff, which waits for a
         * key; a spent script makes the world loop return at once, and input_exhausted
         * breaks here rather than re-entering a world nothing can play in. A save that
         * cannot be read is also a break, not a retry. */
        for (;;) {
            u2_overworld ow;
            const int rc = u2_overworld_init_state(&ctx, &ow);
            fprintf(stderr, "overworld: %s\n", ow.note);
            if (rc != 0)
                break;
            u2_overworld_loop(&ctx, &ow);
            /* Victory ends the run here too, not just in u2_game_run. This is the entry
             * the noVNC play session uses, so leaving it out is the difference between
             * "the game ends when you win" and "the game hands you a menu with the ending
             * still loaded" on the machine the owner actually plays on.
             */
            if (ctx.victory_quit)
                break;
            /* One handoff, one ordering, shared with u2_game_run: see
             * u2_game_death_handoff in game.c for why the record is restored and the
             * screen cleared before a spent script is allowed to end the run. The one
             * way this entry differs is what happens when the record cannot be read at
             * all — the driver falls back to the menu, and this entry has no menu, so
             * U2_HANDOFF_NO_SAVE simply ends. */
            if (u2_game_death_handoff(&ctx) != U2_HANDOFF_REENTER)
                break;
        }
    } else {
        u2_game_run(&ctx);
    }
    u2_game_shutdown(&ctx);
    seam_present(&ctx);
    release_backends(&ctx);

    if (ctx.api_mode) u2_api_end(&ctx, "end");

    /* Hazard 1 of docs/play-api.md: one stray write to stdout corrupts a protocol line
     * mid-way and the caller sees garbage instead of an error. The debug dumps are the
     * obvious way to do that from inside main itself, so in an API session they are
     * refused rather than silently redirected -- a flag that looks like it is working and
     * is quietly dropped is the failure this file refuses elsewhere. */
    if (ctx.api_mode && ctx.dump_state) {
        fprintf(stderr, "u2: --dump-state writes to stdout, which --api owns. The"
                        " observation already carries the screen and the state.\n");
        return 1;
    }

    if (ctx.dump_state) {
        u2_dump_screen(&ctx, stdout);
        u2_dump_state_json(&ctx, stdout);
    }
    /* The counterpart to --load-ds, which had no partner: it reads a flat hex block into the DS
     * mirror, and until now nothing could produce one from an engine run. That asymmetry is why a
     * gate cannot set a single DS byte (e.g. force arm paralysis) without hand-building a whole
     * checkpoint. Written at exit, same span --load-ds consumes.
     */
    if (cli.dump_ds_path) {
        FILE *f = fopen(cli.dump_ds_path, "w");
        if (!f) {
            fprintf(stderr, "u2: cannot open --dump-ds file '%s'\n", cli.dump_ds_path);
            ctx.exit_code = 1;
        } else {
            const unsigned char *base = (const unsigned char *)&ctx.state;
            for (unsigned n = 0; n < 0x280u; n++)
                fprintf(f, "%02x", base[n]);
            fclose(f);
        }
    }
    if (cli.report_path && u2_replay_write(&ctx, cli.report_path) != 0) {
        fprintf(stderr, "u2: cannot write replay report '%s'\n", cli.report_path);
        ctx.exit_code = 1;
    }
    /* The graceful quit while recording, which is an owner requirement rather than a
     * nicety: quitting while the red light is on must end the file cleanly. This is
     * also the SIGINT/SIGTERM path — input_sdl.c installs the handlers and turns the
     * signal into a quit request, so a killed run reaches this line the same way a
     * closed window does, and the epilogue gets written. Only SIGKILL, which has no
     * handler, skips it, and that is the case D8's "incomplete, and says so" covers.
     *
     * For playback this is also where the verdict lands: a divergent demo exits
     * non-zero naming the record (D6), and a demo from another digest scope exits
     * with its own distinct code (D13). */
    {
        const int demo_rc = u2_demo_finish(&ctx);
        if (demo_rc != 0 && ctx.exit_code == 0)
            ctx.exit_code = demo_rc;
    }
    free(ctx.replay);
    ctx.replay = NULL;
    u2_trace_close(&ctx);
    return ctx.exit_code;
}
