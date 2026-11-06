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
static void usage(const char *argv0)
{
    fprintf(stderr,
        "u2 — portable C engine (Phase 4); spec = asm/ tree\n"
        "usage: %s [--headless] [--script FILE] [--dump-state] [--selftest] [--trace[=FILE]]\n"
        "\n"
        "  --headless      null renderer + script input + deterministic clock\n"
        "  --script FILE   key script (see tests/title.keys)\n"
        "  --dump-state    print the screen grid and a JSON state dump on exit\n"
        "  --renderer=R    sdl | terminal. The default depends on how this binary was\n"
        "                  built: sdl when it was compiled with SDL (`make sdl`,\n"
        "                  `sdl-static`, `sdl-win`), terminal otherwise. So a player\n"
        "                  binary opens a window with no flags at all. Asking for a\n"
        "                  renderer the binary cannot provide is an error.\n"
        "  --sdl           alias for --renderer=sdl\n"
        "  --version       print which build this is: SDL implementation, linkage,\n"
        "                  and whether it is the shipped artifact or a dev build\n"
        "  --licenses      print the full licensing and attribution record, then\n"
        "                  exit. The release ships one file, so this travels with it\n"
        "  --video-drivers list the video drivers compiled into this binary and\n"
        "                  exit. If the list has no x11 or wayland, the binary\n"
        "                  cannot open a window on any desktop.\n"
        "  --play-map      enter the overworld screen directly (CP4 development entry)\n"
        "  --dev           development affordances only (Ctrl-Q ends the run). Off by\n"
        "                  default, and it never changes game behaviour: without it the\n"
        "                  engine implements the game and nothing else.\n"
        "  --enhanced      enhanced fidelity: same rules and state as the original, but\n"
        "                  dead ends the original could not avoid are removed. Today that\n"
        "                  means finishing character creation enters the world instead of\n"
        "                  replaying the CP/M disk-swap restart. Default is original,\n"
        "                  which is what the CP2 byte-comparison baseline measures.\n"
        "  --auto-pass-polls N\n"
        "                  idle polls before the engine auto-PASSes. 0 = the original's\n"
        "                  fixed value, -1 = disabled for pure turn-based play, N = that\n"
        "                  many. Intended for --enhanced; the original offers no such knob.\n"
        "  --keep-site-state\n"
        "                  enhanced only: leave a town, tower or dungeon with its sector\n"
        "                  files updated, so opened doors and wounded creatures persist in\n"
        "                  that sector. The original writes no sector file on a transition\n"
        "                  (measured 2026-10-29: only `player` is ever written back), so\n"
        "                  original mode refuses this flag. Saving with Q always wrote a\n"
        "                  dirty sector and still does.\n"
        "  --dump-map FILE render one frame of the overworld to FILE as PPM\n"
        "                  (needs --data-dir with the game files; no display needed)\n"
        "  --dump-each     print the grid after every screen change (frame log)\n"
        "  --dump-hilite   print the inverse-video cells after every screen change\n"
        "                  as 'HILITE <row> <col>', 0-based. The grid dump carries\n"
        "                  characters only, so this is how a highlight is asserted\n"
        "                  without driving a terminal and parsing its escapes.\n"
        "  --debug cli     write turn-NNNNN.ppm and .json into --debug-path after\n"
        "                  every player turn. This is the only way to observe a\n"
        "                  running session; the dump flags are all run-then-exit.\n"
        "  --debug-path D  directory for --debug output (default /tmp; the project\n"
        "                  overrides it because /tmp is tmpfs on its own host)\n"
        "  --debug-limit N stop --debug after N turns (default 200; 0 = unlimited).\n"
        "                  An idle run auto-PASSes, and a PASS is a turn.\n"
        "  --dump-screen F  (SDL build) save the first presented frame to F as\n"
        "                    PPM and exit; with SDL_VIDEODRIVER=dummy this\n"
        "                    proves the window pixel path with no display\n"
        "  --trace-rolls F   write one line per rand_byte() call: '<roll> <function>'. The roll"
        "                    COUNT matching the original was not the diagnosis — same count,"
        "                    different creature positions, so the rolls are spent on different"
        "                    decisions. Only a per-site trace shows that.\n"
        "  --dump-ds F     write the first 0x280 bytes of the DS mirror as flat hex at exit."
        "                  The counterpart to --load-ds, so a test can read a state, patch one"
        "                  byte, and feed it back.\n"
        "  --selftest      exercise every seam once on an engine-authored screen\n"
        "  --replay-report F  write a verifiable playthrough report (digests, no game text) to F\n"
        "  --seed N          PRNG seed byte, as the 0x40:6C value the original reads\n"
        "  --rng-state HEX   resume from a captured register: 12 hex digits for DS:23F..244,\n"
        "                    e.g. 8282ffc7cda4. Overrides --seed. This is how a recording taken\n"
        "                    from the original is replayed from an arbitrary turn.\n"
        "  --load-ds FILE    resume the whole DS mirror from a checkpoint file: whitespace-\n"
        "                    separated hex bytes starting at DS:0000. Applied after the save\n"
        "                    load, so it carries the creature arrays, wander vectors, RNG\n"
        "                    register and status counters that the 256-byte record does not.\n"
        "  --rng-sequence N  print N rand_byte values and exit (test vector)\n"
        "  --cleanroom       use the engine's own independently created assets in\n"
        "                    place of the game's: substitute tiles now, and other\n"
        "                    media (sound, music) as they are added. Opt-in, and\n"
        "                    always announced — a screen drawn with substitute\n"
        "                    assets is never presented as the real thing.\n"
        "  --save-dir DIR    player-record directory, the modern default drive\n"
        "                    (default: ./saves; holds the byte-compatible PLAYER file)\n"
        "  --data-dir DIR    world content directory: mapxNN / monxNN / tlkxNN.\n"
        "                    The map is runtime data, never extracted content, so the\n"
        "                    engine reads the player's own files the way the original\n"
        "                    read them from its default drive (default: .). It is read\n"
        "                    AND written: the original rewrites its own sectors.\n"
        "  --ega             use the fan pack's 16-colour EGA art (EGATILES tiles and\n"
        "                    PIC*-E pictures) instead of the original CGA. An art axis\n"
        "                    only: it changes no rules and is available in both fidelity\n"
        "                    modes. Strict: if the theme named is not there the run\n"
        "                    refuses to start rather than quietly drawing CGA.\n"
        "                    PROVISIONAL: superseded by the graphics-set switch in the\n"
        "                    options menu (docs/options-menu.md).\n"
        "  --cga             the original 4-colour art, and the way to say so when a\n"
        "                    themes tree is installed. With neither --ega nor --cga the\n"
        "                    engine picks from what is on disk: EGA if themes/ (or the\n"
        "                    configured themes dir) holds a usable theme, preferring the\n"
        "                    derived FM Towns theme, and CGA if it holds none. Which was\n"
        "                    chosen, and whether it was automatic, is printed at startup.\n"
        "  --ega-theme NAME  which EGA tile theme to load: default, 10, alt, c64\n"
        "                    (default: default). PROVISIONAL, as --ega.\n"
        "  --ega-theme-dir DIR  where to find EGATILES / EGATHEME.* and the PIC*-E\n"
        "                    files. Default order: this flag, then --data-dir, then\n"
        "                    (developer builds only) references/u2upgrade. A release\n"
        "                    artifact carries no such fallback: --install writes a\n"
        "                    themes/ directory and config.ini points at it.\n"
        "  --config FILE   read config.ini from FILE instead of ./config.ini.\n"
        "                    It names the content, saves and themes directories and\n"
        "                    the selected theme; a flag always wins over it, and a\n"
        "                    path in it that does not exist is an error rather than\n"
        "                    a fallback. Absent is normal: the drop-in layout needs\n"
        "                    no config at all.\n"
        "\ninstalling a game directory (no display needed; exits without playing):\n"
        "  --install --to DIR        build a game tree at DIR from the sources below.\n"
        "                    DIR must be empty or nonexistent, or carry this\n"
        "                    installer's own manifest, in which case the missing\n"
        "                    layers are added. A directory that is not ours is never\n"
        "                    merged into, overwritten or deleted.\n"
        "  --install         with no --to: reinstall the tree whose config.ini records\n"
        "                    this install. An install writes the sources it used into\n"
        "                    [install] there, so a binary kept beside the game needs no\n"
        "                    arguments. A recorded source that has moved is reported and\n"
        "                    asked for again, never skipped.\n"
        "  --install --check [--to DIR]\n"
        "                    verify a tree against its manifest and write nothing.\n"
        "                    Content is immutable after install, so a sector file that\n"
        "                    differs is a defect, and the message names --install as the\n"
        "                    repair: play writes into saves/<slot>/ now, not data/.\n"
        "  --dry-run         with --install: plan and report, write nothing. Shows which\n"
        "                    copy of a shared filename wins and what the theme step\n"
        "                    would do, before anything is copied.\n"
        "  --no-prompt         with --install: never ask. Anything a flag did not supply\n"
        "                    is refused rather than prompted for. Prompts are also off\n"
        "                    automatically when stdin is not a terminal.\n"
        "  --original-dir DIR    the original DOS release: an unzipped download, a CD\n"
        "                    rip, or a directory an emulator already uses.\n"
        "  --updated-dir DIR     the Galaxy patch layer (MAPG / MONG / TLKG). If not\n"
        "                    given and <original-dir>/updated exists, that is used:\n"
        "                    the patch ships inside the release directory.\n"
        "  --themes-dir DIR      the fan art pack. Repeatable: no single pack is\n"
        "                    complete, so the upgrade pack (atlases and themes) and\n"
        "                    the Galaxy directory (the full PIC*-E picture set) are\n"
        "                    usually both needed. First source wins per file.\n"
        "  --fmtowns-media-dir DIR\n"
        "                    a FM Towns disc image, or a directory holding one.\n"
        "                    Derives themes/fmtowns from the art on your own disc;\n"
        "                    the DOS-to-FM tile mapping is compiled into the binary,\n"
        "                    so nothing but the disc is needed.\n"
        "\nkeys while playing:\n"
        "  Ctrl-Q  quit (the original has no quit key; 'Q' saves)\n"
        "  Ctrl-T  cycle the EGA tile theme (only with --ega; walks the themes\n"
        "          directory in alphabetical order)\n"
        "  Ctrl-R  start or stop a demo recording at the next prompt. The original\n"
        "          has no record key, and nothing in asm/ compares against 0x12, so\n"
        "          it displaces no shortcut — a gate checks that claim rather than\n"
        "          resting on it (tools/demo_key_gate.py). A recording never starts\n"
        "          with a command half-typed, and quitting while it is running ends\n"
        "          the file cleanly.\n"
        "  --trace[=FILE]  log every seam call with its asm address\n"
        "\ndemo record and playback (docs/demo-record-playback.md):\n"
        "  --playback FILE   replay a recording. It restores the checkpoint the file\n"
        "                    carries, re-injects the recorded keys through the same\n"
        "                    input seam a player's keys arrive on, and verifies every\n"
        "                    record's digest. A demo that plays back wrong is a FAILED\n"
        "                    playback: it names the record and exits 5. A demo written\n"
        "                    by an older digest scope plays unverified and exits 6.\n"
        "                    Writes go to a scratch save directory, so watching a demo\n"
        "                    cannot touch your own saves or sector overlays.\n"
        "  --demo-dir DIR    where recordings go (default: ./demos, created on demand).\n"
        "                    Never /tmp: on this host that is RAM.\n"
        "  --demo-name NAME  pin the filename instead of the host-time stamp, so a gate\n"
        "                    can find the file it just made.\n"
        "  --demo-snapshots  also write a full DS dump after every record. Local\n"
        "                    debugging only; the reader ignores them and they are big.\n"
        "  --salvage         play a recording with no @end, i.e. one an interrupted run\n"
        "                    left behind. Refused without this, because a file that\n"
        "                    silently stops early is indistinguishable from one that\n"
        "                    played correctly up to the point it stopped.\n"
        "  --speed-pin       say it out loud: playback keeps the recorded DS:74 speed\n"
        "                    and tick pair instead of recalibrating. This is the default.\n"
        "  --playback-continue  keep going past the first divergence. Diagnosis only:\n"
        "                    the run has already failed.\n"
        "  --playback-pace     pace a HEADLESS playback at real time. Off by default\n"
        "                    because the gate has to finish; SDL and terminal playback\n"
        "                    pace without being asked, and pacing cannot reach state.\n"
        "  --mode M          reserved for CP11; only 'original' exists today\n",
        argv0);
}

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
        ctx->input    = &u2_input_sdl;
#endif
    } else {
        ctx->renderer = &u2_renderer_term;
        ctx->audio    = &u2_audio_beep;
        ctx->clock    = &U2_CLOCK_HOST;
        ctx->input    = &u2_input_term;
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

/* Parse a captured rand_byte register: 12 hex digits, DS:23F..244 in order.
 * Fails loudly rather than defaulting — a silently-mis-parsed register would make a resumed replay
 * diverge for a reason that looks like an engine bug. Returns 0 on success. */
static int parse_rng_state(const char *s, uint8_t out[6])
{
    unsigned nib = 0, acc = 0;
    const char *p = s;

    while (*p) {
        int d;
        if (*p == ' ' || *p == ':') { p++; continue; }
        if (*p >= '0' && *p <= '9')      d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else { fprintf(stderr, "u2: --rng-state needs 12 hex digits, got '%s'\n", s); return -1; }
        acc = (acc << 4) | (unsigned)d;
        if (++nib % 2 == 0) {
            if (nib / 2 > 6) { fprintf(stderr, "u2: --rng-state too long\n"); return -1; }
            out[nib / 2 - 1] = (uint8_t)acc;
            acc = 0;
        }
        p++;
    }
    if (nib != 12) {
        fprintf(stderr, "u2: --rng-state needs 12 hex digits (6 bytes), got %u nibbles\n", nib);
        return -1;
    }
    return 0;
}

/* The DS checkpoint itself lives in core/state.c (u2_state_load_ds) because it has to be applied
 * after the world and monx load, which is in the overworld, not here. */

int main(int argc, char **argv)
{
    u2_ctx ctx;
    const char *trace_arg = NULL;
    const char *report_path = NULL;
    const char *map_dump_path = NULL;
    unsigned seed = U2_RNG_DEFAULT_SEED;
    int rng_probe = 0;
    int rng_state_given = 0;
    uint8_t rng_state[6];
    const char *roll_trace_path = NULL;
    const char *dump_ds_path = NULL;
    const char *config_path = NULL;
    /* Two separate questions, because they have different answers.
     *   art_mode_forced — the player or the config chose CGA or EGA (or asked for
     *     substitute art), so the mode is settled and only the theme may still be
     *     picked for them.
     *   theme_named — a specific theme was named, which is the one thing that
     *     suppresses the fmtowns-over-default preference. --enhanced forces the mode
     *     but names no theme, so it still gets the preference. */
    int art_mode_forced = 0;
    int theme_named = 0;
    /* Set only when the player *demanded* themed art -- --ega, or a theme named by
     * flag or config. It is what makes the strict launch check fire, and the
     * distinction is load-bearing: --enhanced prefers EGA but must not require it.
     * Before these were separated, --enhanced in a tree with no themes installed
     * refused to start at all ("--ega refused: theme 'default' not found"), because
     * the 2026-11-05 ruling made enhanced imply the EGA axis and the 2026-11-04
     * ruling made that axis strict. themes/ is gitignored, so that was every clean
     * checkout, every CI run, and any player who installed without the fan pack. */
    int ega_required = 0;
    int install_mode = 0, install_check_only = 0, install_dry_run = 0, install_no_prompt = 0;
    const char *install_to = NULL, *install_orig = NULL;
    const char *install_upd = NULL;
    const char *install_themes[U2_THEMES_MAX_SOURCES] = { 0 };
    int n_install_themes = 0;
    const char *install_fmtowns = NULL;
    u2_config cfg;
    memset(&ctx, 0, sizeof ctx);
    ctx.mode = U2_MODE_TERM;
    /* D5 is the default, not an option: a playback that recalibrated the speed
     * fields could diverge for a reason that looks like a deep bug, so pinning is
     * what playback means and --speed-pin only names it out loud. */
    ctx.demo_speed_pin = 1;

    ctx.debug_limit = U2_TURNLOG_DEFAULT_LIMIT;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--headless") == 0) {
            ctx.mode = U2_MODE_HEADLESS;
        } else if (strcmp(a, "--sdl") == 0) {
            ctx.renderer_choice = U2_RENDERER_SDL;      /* legacy alias */
            ctx.renderer_explicit = 1;
        } else if (strncmp(a, "--renderer=", 11) == 0) {
            const char *r = a + 11;
            if (strcmp(r, "sdl") == 0)
                ctx.renderer_choice = U2_RENDERER_SDL;
            else if (strcmp(r, "terminal") == 0)
                ctx.renderer_choice = U2_RENDERER_TERMINAL;
            else {
                fprintf(stderr, "u2: --renderer takes 'sdl' or 'terminal' (got '%s')\n", r);
                return 2;
            }
            ctx.renderer_explicit = 1;
        } else if (strcmp(a, "--play-map") == 0) {
            ctx.play_map = 1;
        } else if (strcmp(a, "--dev") == 0) {
            /* Development affordances. May add observation and an exit path; may never
             * change game behaviour. See u2_ctx::dev. */
            ctx.dev = 1;
        } else if (strcmp(a, "--enhanced") == 0) {
            /* Same rules and state as the original; dead ends the original
             * could not avoid are removed. Default is original, which is what
             * the CP2 byte-comparison baseline is measured against. */
            ctx.fidelity = U2_FIDELITY_ENHANCED;
        } else if (strcmp(a, "--auto-pass-polls") == 0 && i + 1 < argc) {
            /* Idle auto-PASS threshold. 0 restores the faithful default, -1
             * disables it for pure turn-based play. Only meaningful in enhanced
             * mode: the original has one fixed value and no way to change it. */
            ctx.auto_pass_polls = atoi(argv[++i]);
        } else if (strcmp(a, "--keep-site-state") == 0) {
            /* Validated after the loop: --enhanced may appear on either side of it. */
            ctx.keep_site_state = 1;
        } else if (strcmp(a, "--script") == 0 && i + 1 < argc) {
            ctx.script_path = argv[++i];
        } else if (strcmp(a, "--playback") == 0 && i + 1 < argc) {
            /* Replay a recorded demo (docs/demo-record-playback.md). The file names
             * its own starting state, so nothing else on the command line chooses it. */
            ctx.playback_path = argv[++i];
        } else if (strcmp(a, "--demo-dir") == 0 && i + 1 < argc) {
            ctx.demo_dir = argv[++i];
        } else if (strcmp(a, "--demo-name") == 0 && i + 1 < argc) {
            /* Pins the filename a gate needs to find. Without it the name is host
             * time, which is what a person wants and what a test cannot assert on. */
            ctx.demo_name = argv[++i];
        } else if (strcmp(a, "--demo-snapshots") == 0) {
            ctx.demo_snapshots = 1;
        } else if (strcmp(a, "--salvage") == 0) {
            ctx.demo_salvage = 1;
        } else if (strcmp(a, "--speed-pin") == 0) {
            ctx.demo_speed_pin = 1;
        } else if (strcmp(a, "--playback-continue") == 0) {
            ctx.demo_play_continue = 1;
        } else if (strcmp(a, "--playback-pace") == 0) {
            ctx.demo_play_pace = 1;
        } else if (strcmp(a, "--save-dir") == 0 && i + 1 < argc) {
            ctx.save_dir = argv[++i];
        } else if (strcmp(a, "--cleanroom") == 0) {
            ctx.cleanroom = 1;
            art_mode_forced = 1;   /* substitute art is a named choice, not a default */
        } else if (strcmp(a, "--cga") == 0) {
            /* The opt-out. EGA is now picked automatically when a themes tree is
             * installed, so without this flag the original 4-colour art would stop
             * being reachable by anyone whose install has themes — which is the
             * common case, and was the default until now. Named for the art, not
             * for the mode it turns off, so it reads the way --ega reads. */
            art_mode_forced = 1;
            ctx.art_mode = U2_ART_CGA;
        } else if (strcmp(a, "--ega") == 0) {
            /* Art axis only; independent of --enhanced and --cleanroom. Strict
             * (owner, 2026-11-04): an explicit request never downgrades quietly. */
            art_mode_forced = 1;
            ega_required = 1;
            ctx.art_mode = U2_ART_EGA;
        } else if (strcmp(a, "--ega-theme") == 0 && i + 1 < argc) {
            /* Naming a theme is asking for themed art, so it selects the axis too.
             * It used to set only the name and leave the mode on CGA, which was
             * always odd and became actively wrong once the mode started being
             * inferred: --ega-theme default in a tree with themes would have
             * downgraded a run that gets EGA if you say nothing. An explicit request
             * must not be worse than no request. */
            ctx.ega_theme = argv[++i];
            theme_named = 1;
            art_mode_forced = 1;
            ega_required = 1;
            ctx.art_mode = U2_ART_EGA;
        } else if (strcmp(a, "--ega-theme-dir") == 0 && i + 1 < argc) {
            ctx.ega_theme_dir = argv[++i];
        } else if (strcmp(a, "--data-dir") == 0 && i + 1 < argc) {
            ctx.data_dir = argv[++i];
        } else if (strcmp(a, "--config") == 0 && i + 1 < argc) {
            /* Where config.ini lives. Read after the loop, because a flag always
             * outranks the file and the file may only fill what the flags left
             * unset. docs/install-design.md § 4. */
            config_path = argv[++i];
        } else if (strcmp(a, "--fmtowns-media-dir") == 0 && i + 1 < argc) {
            install_fmtowns = argv[++i];
        } else if (strcmp(a, "--install") == 0) {
            install_mode = 1;
        } else if (strcmp(a, "--check") == 0) {
            install_check_only = 1;
        } else if (strcmp(a, "--dry-run") == 0) {
            /* Plan and report, write nothing. Paired with --install, and it is what
             * makes a wrong source cheap to see before it is half-copied. */
            install_dry_run = 1;
        } else if (strcmp(a, "--no-prompt") == 0) {
            install_no_prompt = 1;
        } else if (strcmp(a, "--to") == 0 && i + 1 < argc) {
            install_to = argv[++i];
        } else if (strcmp(a, "--original-dir") == 0 && i + 1 < argc) {
            install_orig = argv[++i];
        } else if (strcmp(a, "--updated-dir") == 0 && i + 1 < argc) {
            install_upd = argv[++i];
        } else if (strcmp(a, "--themes-dir") == 0 && i + 1 < argc) {
            /* Repeatable: no single art pack is complete, so the upgrade pack and
             * the Galaxy directory are usually both needed. */
            if (n_install_themes < U2_THEMES_MAX_SOURCES)
                install_themes[n_install_themes++] = argv[++i];
            else {
                fprintf(stderr, "u2: at most %d --themes-dir values\n",
                        U2_THEMES_MAX_SOURCES);
                return 2;
            }
        } else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
            seed = (unsigned)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(a, "--replay-report") == 0 && i + 1 < argc) {
            report_path = argv[++i];
        } else if (strcmp(a, "--rng-sequence") == 0 && i + 1 < argc) {
            rng_probe = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(a, "--rng-state") == 0 && i + 1 < argc) {
            if (parse_rng_state(argv[++i], rng_state) != 0) return 2;
            rng_state_given = 1;
        } else if (strcmp(a, "--load-ds") == 0 && i + 1 < argc) {
            ctx.load_ds_path = argv[++i];
        } else if (strcmp(a, "--dump-ds") == 0 && i + 1 < argc) {
            dump_ds_path = argv[++i];
        } else if (strcmp(a, "--trace-rolls") == 0 && i + 1 < argc) {
            roll_trace_path = argv[++i];
        } else if (strcmp(a, "--dump-state") == 0) {
            ctx.dump_state = 1;
        } else if (strcmp(a, "--dump-map") == 0 && i + 1 < argc) {
            map_dump_path = argv[++i];
        } else if (strcmp(a, "--dump-frame") == 0 && i + 1 < argc) {
            ctx.frame_dump_path = argv[++i];
        } else if (strcmp(a, "--dump-each") == 0) {
            ctx.dump_each = 1;
        } else if (strcmp(a, "--dump-hilite") == 0) {
            ctx.dump_hilite = 1;
        } else if (strcmp(a, "--dump-screen") == 0 && i + 1 < argc) {
            ctx.dump_screen = argv[++i];
        } else if (strcmp(a, "--debug") == 0) {
            /* `--debug cli`, matching the existing `--trace[=FILE]` shape: the mode
             * is an argument rather than a separate flag so a bare --debug can be
             * rejected instead of silently guessing. */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                if (strcmp(argv[i + 1], "cli") != 0)
                    fprintf(stderr, "u2: --debug takes 'cli' (got '%s')\n", argv[i + 1]);
                else { ctx.debug_cli = 1; ++i; }
            } else {
                fprintf(stderr, "u2: --debug needs a mode; today that is only 'cli'\n");
            }
        } else if (strcmp(a, "--debug-path") == 0 && i + 1 < argc) {
            ctx.debug_path = argv[++i];
            ctx.debug_cli = 1;      /* naming a directory is intent enough */

        } else if (strcmp(a, "--debug-limit") == 0 && i + 1 < argc) {
            /* 0 means unlimited, and has to be asked for by name: an unattended
             * session auto-PASSes forever and wrote 288 file pairs in 45 s before
             * the cap existed. */
            char *end = NULL;
            const unsigned long v = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || (end && *end != '\0'))
                fprintf(stderr, "u2: --debug-limit wants a number (got '%s')\n", argv[i]);
            else
                ctx.debug_limit = (unsigned)v;
        } else if (strcmp(a, "--selftest") == 0) {
            ctx.selftest = 1;
        } else if (strcmp(a, "--trace") == 0) {
            trace_arg = "-";
        } else if (strncmp(a, "--trace=", 8) == 0) {
            trace_arg = a + 8;
        } else if (strcmp(a, "--version") == 0) {
            /* Which build is this. The reason it is a flag rather than a comment:
             * a dev binary and a shipped binary can be built against different
             * implementations of the same SDL2 API, both run, and nothing else
             * about them says so. See docs/sdl2-vs-sdl3.md. */
            char info[192];
            u2_build_info(info, sizeof info);
            printf("%s\n", info);
            return 0;
        } else if (strcmp(a, "--licenses") == 0) {
            /* The release is one file with no accompanying documents, so the
             * obligations and provenance have to travel inside it. Printed to
             * stdout and then a clean exit, because someone asked for a document,
             * not a game. */
            u2_licenses_print(stdout);
            return 0;
        } else if (strcmp(a, "--video-drivers") == 0) {
            /* Diagnostic, and the release gate: a player binary with only dummy
             * and offscreen can never show a window, and nothing else about it
             * says so. Works with no display, so CI can run it. */
#ifdef U2_HAVE_SDL
            u2_sdl_list_video_drivers();
#else
            fprintf(stderr, "u2: this binary was built without SDL, so it has no "
                            "video drivers to list.\n");
            return 2;
#endif
            return 0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "u2: unknown argument '%s'\n", a);
            usage(argv[0]);
            return 2;
        }
    }

    /* --install is a tool mode: it runs before config, before video, before the
     * game, and it needs no renderer. That is not tidiness — the terminal build is
     * what CI can execute, so an installer that needed SDL would be an installer
     * nobody could prove. docs/install-design.md § 2. */
    if (install_mode || install_check_only) {
        if (install_check_only && !install_mode) {
            fprintf(stderr, "u2: --check only means something with --install.\n");
            return 2;
        }
        char ierr[512];
        u2_install_request rq;
        u2_config icfg;
        char iconfig[1024];
        memset(&rq, 0, sizeof rq);
        rq.to = install_to;
        rq.original_dir = install_orig;
        rq.updated_dir = install_upd;
        rq.n_themes_dir = n_install_themes;
        rq.fmtowns_media = install_fmtowns;
        rq.dry_run = install_dry_run;
        for (int k = 0; k < n_install_themes; k++)
            rq.themes_dir[k] = install_themes[k];
        rq.self_path = argv[0];

        /* Whatever the command line did not say, the tree may already know: an
         * install records its sources in config.ini, so a binary kept beside the
         * game reinstalls with no arguments. Same precedence as every other path in
         * the engine -- flag > config.ini > prompt -- and a recorded source that has
         * moved is reported and asked for again, never skipped. */
        memset(&icfg, 0, sizeof icfg);
        if (u2_install_find_config(config_path, argv[0], iconfig, sizeof iconfig)) {
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
        if (!install_check_only && !install_no_prompt && isatty(fileno(stdin))) {
            const int go = u2_install_prompt(&rq, &ans, stdout, ierr, sizeof ierr);
            if (go < 0) {
                if (*ierr)
                    fprintf(stderr, "u2: %s\n", ierr);
                return 2;
            }
            if (go == 0)
                return 0;
        }
        const u2_install_result r = install_check_only
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
            config_path ? config_path : U2_CONFIG_DEFAULT_FILE, &cfg, cerr, sizeof cerr);
        if (cr != U2_CONFIG_OK) {
            fprintf(stderr, "u2: %s\n", cerr);
            return 2;
        }
        u2_config_apply(&ctx, &cfg);
        /* A theme named in config.ini must behave exactly like --ega-theme on the
         * command line, and the first version of this only half did it: it set
         * theme_named, which suppresses auto-detection, without selecting the art
         * axis -- so `theme = fmtowns` in a config produced CGA, which is worse
         * than saying nothing at all. Found by deploying to a real installed tree
         * rather than by the fixture tests, which is the whole reason the deploy
         * step exists. Naming a theme is asking for themed art, from either place. */
        if (*cfg.theme) {
            theme_named = 1;
            art_mode_forced = 1;
            ega_required = 1;
            ctx.art_mode = U2_ART_EGA;
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
    /* A key script is read by the script input backend, which is wired only in
     * headless mode. docs/phase4-state.md recorded this as "--sdl cannot take
     * --script" and blamed argument order; the real cause is that no interactive
     * backend reads script_path at all, so the flag was quietly dropped. Silently
     * ignoring a script is how a scripted run ends up waiting for a human. */
    if (ctx.script_path && ctx.mode != U2_MODE_HEADLESS) {
        fprintf(stderr, "u2: --script needs --headless; no interactive backend reads a"
                        " key script, so it would be ignored.\n");
        return 2;
    }

    /* Two input backends is a configuration with no meaning. A script and a demo
     * both claim the keyboard and whichever won would decide what the player saw,
     * so the combination is refused in the same style as the one above rather than
     * resolved by a precedence nobody asked for. */
    if (ctx.playback_path && ctx.script_path) {
        fprintf(stderr, "u2: --playback and --script together is refused: a demo and a"
                        " key script are two sources of the same keystrokes.\n");
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
    if (!theme_named && (ctx.art_mode == U2_ART_EGA || !art_mode_forced))
        art_auto = u2_tiles_ega_autodetect(&ctx, art_theme, sizeof art_theme,
                                           !art_mode_forced);

    /* --enhanced wanted themed art and there is none. Take the CGA tiles and keep
     * the enhanced layout: the layout is a canvas decision (core/layout.h) and does
     * not depend on the art axis, so a 640x480 window drawing 4-colour tiles is a
     * working game rather than a refusal. The ruling that made --ega redundant under
     * --enhanced was about the canvas having room for a 32px theme, not about
     * demanding that one exist. An explicit --ega still refuses, which is the
     * 2026-11-04 ruling and unchanged. */
    if (ctx.art_mode == U2_ART_EGA && !ega_required && !art_auto) {
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
    if (ctx.art_mode == U2_ART_EGA && ega_required) {
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

    if (trace_arg && u2_trace_open(&ctx, trace_arg) != 0) {
        fprintf(stderr, "u2: cannot open trace file '%s'\n", trace_arg);
        return 2;
    }

    /* Per-roll trace. Not closed explicitly: stdio flushes on normal exit, and threading an fclose
     * through every early-return path for a diagnostic buys nothing. Fails loudly if the path is
     * unusable, because a silently-missing trace reads exactly like "no rolls happened". */
    if (roll_trace_path) {
        ctx.roll_trace = fopen(roll_trace_path, "w");
        if (!ctx.roll_trace) {
            fprintf(stderr, "u2: cannot open --trace-rolls file '%s'\n", roll_trace_path);
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
    u2_rng_seed(&ctx, (uint8_t)seed);
    ctx.seed = seed & 0xFFu;
    /* A captured register replaces the seeded one. Applied after the seed so --seed still governs
     * everything else, and before anything rolls. */
    if (rng_state_given)
        u2_rng_load(&ctx, rng_state);

    if (report_path) {
        u2_replay_begin(&ctx);
        u2_replay_set_keys(&ctx, ctx.script_path);
    }

    if (rng_probe > 0) {
        for (int i = 0; i < rng_probe; i++)
            printf("%u%s", u2_rand_byte(&ctx), i + 1 == rng_probe ? "\n" : ",");
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

    if (ctx.play_map || map_dump_path)
        (void)u2_save_load(&ctx);

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

    if (map_dump_path) {
        /* No backends, no display: this path exists so the real extracted tiles
         * can be seen, and diffed, on a machine with no screen at all. */
        const int rc = u2_overworld_dump(&ctx, map_dump_path);
        u2_trace_close(&ctx);
        return rc == 0 ? 0 : 1;
    }

    if (wire_backends(&ctx) != 0) {
        fprintf(stderr, "u2: backend initialisation failed\n");
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
            if (rc == 0)
                u2_overworld_loop(&ctx, &ow);
            else if (u2_demo_overlay_failed(&ctx)) {
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
         * CP5 fight scripts are driven entirely by reading it back. */
        u2_overworld ow;
        const int rc = u2_overworld_init_state(&ctx, &ow);
        fprintf(stderr, "overworld: %s\n", ow.note);
        if (rc == 0)
            u2_overworld_loop(&ctx, &ow);
    } else {
        u2_game_run(&ctx);
    }
    u2_game_shutdown(&ctx);
    seam_present(&ctx);
    release_backends(&ctx);

    if (ctx.dump_state) {
        u2_dump_screen(&ctx, stdout);
        u2_dump_state_json(&ctx, stdout);
    }
    /* The counterpart to --load-ds, which had no partner: it reads a flat hex block into the DS
     * mirror, and until now nothing could produce one from an engine run. That asymmetry is why a
     * gate cannot set a single DS byte (e.g. force arm paralysis) without hand-building a whole
     * checkpoint. Written at exit, same span --load-ds consumes.
     */
    if (dump_ds_path) {
        FILE *f = fopen(dump_ds_path, "w");
        if (!f) {
            fprintf(stderr, "u2: cannot open --dump-ds file '%s'\n", dump_ds_path);
            ctx.exit_code = 1;
        } else {
            const unsigned char *base = (const unsigned char *)&ctx.state;
            for (unsigned n = 0; n < 0x280u; n++)
                fprintf(f, "%02x", base[n]);
            fclose(f);
        }
    }
    if (report_path && u2_replay_write(&ctx, report_path) != 0) {
        fprintf(stderr, "u2: cannot write replay report '%s'\n", report_path);
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
