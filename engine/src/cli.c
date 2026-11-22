/* The command line, as one module.
 *
 * This file used to be the first 270 lines of main() plus a 245-line usage(). It is
 * separate for one reason: the flag contract was only observable by exec'ing the binary.
 * Every refusal rule — `--script` needs `--headless`, `--renderer` takes two words,
 * `--start-at` wants 0..63, `--enhanced` implies the EGA axis but does not require it —
 * was tested by launching `./u2` and grepping stderr, so `run_cp0.sh` matched the
 * substring `--script needs --headless` and `run_api.sh` checked an exit code. That is a
 * test reading a process boundary because no interface could express "these arguments are
 * a refusal, and here is why".
 *
 * So parsing is a function now: u2_cli_parse() fills a u2_ctx and a u2_cli and returns a
 * disposition, and the refusal reason is a string in the struct rather than bytes on a
 * pipe. main() keeps the acting (install, decode, play); this file keeps deciding which of
 * them the player asked for and whether the request is coherent.
 *
 * Two kinds of state come out, and the split is the point:
 *   ctx        — what the run is. Mode, art, paths, demo knobs.
 *   cli        — what the player SAID. `mode_given`, `ega_required`, `theme_named`, and the
 *                installer and diagnostic inputs. These are not run properties: `--enhanced`
 *                prefers EGA and `--ega` demands it, and the value of ctx.art_mode is the
 *                same in both cases, so the distinction cannot survive in ctx. It used to
 *                live in main()'s locals, which is why nothing outside main() could see it.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "core/licenses.h"
#include "core/rng.h"
#include "core/state.h"
#include "core/version.h"
#include "platform/platform.h"

void cli_err(u2_cli *cli, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cli->err, sizeof cli->err, fmt, ap);
    va_end(ap);
}

/* --- usage --------------------------------------------------------------- */

void u2_cli_usage(const char *argv0)
{
    /* Split into six calls on purpose. Concatenated, this text is a single string
     * literal of about 15.6 KB, and ISO C only requires a compiler to accept 4095
     * bytes in one literal (5.2.4.1), so as written it was not conforming C11.
     * Six calls print exactly the same bytes. Only the first carries a format
     * spec (%s for argv0); the rest go through fputs, so a stray % added to the
     * later text cannot be read as a conversion with no argument. */
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
        "  --start-at X,Y[,SEC]\n"
        "                  stage the character at a position on the live state, the live\n"
        "                  equivalent of tools/u2_edit.py setpos. Implies --play-map. This\n"
        "                  is how a scripted run gets placed beside a vehicle pad so it can\n"
        "                  BOARD and reach the space loop on its own -- --load-ds cannot do\n"
        "                  it, because that is applied from inside the overworld and so only\n"
        "                  resumes a run already in the world.\n"
        "  --quit-on-death dev: stop at death instead of reloading the last save\n"
        "  --dev           development affordances only (Ctrl-Q ends the run). Off by\n"
        "                  default, and it never changes game behaviour: without it the\n"
        "                  engine implements the game and nothing else.\n"
        "  --enhanced      enhanced mode: the original's rules and state, with the dead\n"
        "                  ends the original could not avoid removed, and the 640x480\n"
        "                  layout. This is the DEFAULT (owner, 2026-11-17); the flag is\n"
        "                  kept so a script written before it still says what it means.\n"
        "                  --classic is the opt-out, and config.ini [game] mode is the\n"
        "                  place to choose it without a flag.\n",
        argv0);

    fputs(
        "  --classic       original mode: the original's CGA art AND the original screen\n"
        "                  layout, in one flag (--cga is an alias). Opting out of the\n"
        "                  presentation means opting out of all of it; art alone is\n"
        "                  --ega in one direction and this in the other. This is also\n"
        "                  the mode the CP2 byte-comparison baseline is recorded under,\n"
        "                  so a replay of committed evidence asks for it explicitly.\n"
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
        "  --dump-screen-every N F\n"
        "                    (SDL build) save every Nth presented frame to F, which\n"
        "                    holds a %d for the frame index (or the index is put\n"
        , stderr);

    fputs(
        "                    before the extension). Does not end the run: this is how\n"
        "                    you look at an animation, e.g. the 509-frame warp.\n"
        "  --trace-rolls F   write one line per rand_byte() call: '<roll> <function>'. The roll"
        "                    COUNT matching the original was not the diagnosis — same count,"
        "                    different creature positions, so the rolls are spent on different"
        "                    decisions. Only a per-site trace shows that.\n"
        "  --dump-ds F     write the first 0x280 bytes of the DS mirror as flat hex at exit."
        "                  The counterpart to --load-ds, so a test can read a state, patch one"
        "                  byte, and feed it back.\n"
        "  --selftest      exercise every seam once on an engine-authored screen\n"
        "  --replay-report F  write a verifiable playthrough report (digests, no game text) to F\n"
        "  --seed N             PRNG seed byte, as the 0x40:6C value the original reads\n"
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
        "  --ega             force the fan pack's 16-colour EGA art (EGATILES tiles and\n"
        "                    PIC*-E pictures). An art axis only: it changes no rules\n"
        , stderr);

    fputs(
        "                    and is available in both fidelity modes. Strict: if the\n"
        "                    theme named is not there the run refuses to start rather\n"
        "                    than quietly drawing CGA. Not needed to GET EGA - with no\n"
        "                    flag the engine already picks the best theme on disk.\n"
        "                    PROVISIONAL: superseded by the graphics-set switch in the\n"
        "                    options menu (docs/options-menu.md).\n"
        "  --classic         the original 4-colour CGA art, and the way to force it when\n"
        "                    a themes tree is installed (--cga is an alias). Since the\n"
        "                    2026-11-17 ruling this is also the layout opt-out: it asks\n"
        "                    for original fidelity and the original screen, not the art\n"
        "                    alone. With neither --ega nor --classic the engine picks from\n"
        "                    what is on disk: the fan pack's `10` theme, then its\n"
        "                    `default`, then the derived FM Towns theme, and CGA if it\n"
        "                    holds none. Which was chosen, and whether it was automatic,\n"
        "                    is printed at startup.\n"
        "  --ega-theme NAME  which EGA tile theme to load: 10, default, alt, c64\n"
        "                    (default: 10). PROVISIONAL, as --ega.\n"
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
        , stderr);

    fputs(
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
        , stderr);

    fputs(
        "\ndemo record and playback (docs/demo-record-playback.md):\n"
        "  --playback FILE   replay a recording. It restores the checkpoint the file\n"
        "                    carries, re-injects the recorded keys through the same\n"
        "                    input seam a player's keys arrive on, and verifies every\n"
        "                    record's digest. A demo that plays back wrong is a FAILED\n"
        "                    playback: it names the record and exits 5. A demo written\n"
        "                    by an older digest scope plays unverified and exits 6.\n"
        "                    A sector the recording cannot rebuild is a REFUSAL\n"
        "                    rather than a divergence: it exits 2 and names the\n"
        "                    sector, whether the mount or a later load refused.\n"
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
        "  --playback-ignore-content  play even though a sector file disagrees with the\n"
        "                    digest the recording carried. The refusal is the default on\n"
        "                    purpose — those bytes reach the world — but a recording made\n"
        "                    from a save slot the viewer cannot have is otherwise stuck,\n"
        "                    and a demo nobody can play is not a shareable demo. If a\n"
        "                    digest then diverges, it may be the content and not the\n"
        "                    engine, and the run says so on the way in.\n"
        "  --speed-pin       say it out loud: playback keeps the recorded DS:74 speed\n"
        "                    and tick pair instead of recalibrating. This is the default.\n"
        "  --playback-continue  keep going past the first divergence. Diagnosis only:\n"
        "                    the run has already failed.\n"
        "  --playback-pace     pace a HEADLESS playback at real time. Off by default\n"
        "                    because the gate has to finish; SDL and terminal playback\n"
        "                    pace without being asked, and pacing cannot reach state.\n"
        "  --mode M          reserved for CP11; only 'original' exists today\n"
        , stderr);
}
/* --- --rng-state --------------------------------------------------------- */

/* --rng-state takes the six RNG registers as hex, space or colon separated.
 * Fails loudly rather than defaulting — a silently-mis-parsed register would make a
 * resumed replay diverge for a reason that looks like an engine bug. Returns 0 on
 * success, and reports through cli->err. */
static int parse_rng_state(u2_cli *cli, const char *s, uint8_t out[6])
{
    unsigned nib = 0, acc = 0;
    const char *p = s;

    while (*p) {
        int d;
        if (*p == ' ' || *p == ':') { p++; continue; }
        if (*p >= '0' && *p <= '9')      d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else { cli_err(cli, "u2: --rng-state needs 12 hex digits, got '%s'", s); return -1; }
        acc = (acc << 4) | (unsigned)d;
        if (++nib % 2 == 0) {
            if (nib / 2 > 6) { cli_err(cli, "u2: --rng-state too long"); return -1; }
            out[nib / 2 - 1] = (uint8_t)acc;
            acc = 0;
        }
        p++;
    }
    if (nib != 12) {
        cli_err(cli, "u2: --rng-state needs 12 hex digits (6 bytes), got %u nibbles", nib);
        return -1;
    }
    return 0;
}

/* --- the parse ----------------------------------------------------------- */

u2_cli_action u2_cli_parse(int argc, char **argv, u2_ctx *ctx, u2_cli *cli)
{
    memset(cli, 0, sizeof *cli);
    cli->seed = U2_RNG_DEFAULT_SEED;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--headless") == 0) {
            ctx->mode = U2_MODE_HEADLESS;
        } else if (strcmp(a, "--sdl") == 0) {
            ctx->renderer_choice = U2_RENDERER_SDL;      /* legacy alias */
            ctx->renderer_explicit = 1;
        } else if (strncmp(a, "--renderer=", 11) == 0) {
            const char *r = a + 11;
            if (strcmp(r, "sdl") == 0)
                ctx->renderer_choice = U2_RENDERER_SDL;
            else if (strcmp(r, "terminal") == 0)
                ctx->renderer_choice = U2_RENDERER_TERMINAL;
            else {
                cli_err(cli, "u2: --renderer takes 'sdl' or 'terminal' (got '%s')", r);
                return U2_CLI_REFUSE;
            }
            ctx->renderer_explicit = 1;
        } else if (strcmp(a, "--play-map") == 0) {
            ctx->play_map = 1;
        } else if (strcmp(a, "--start-at") == 0 && i + 1 < argc) {
            /* X,Y or X,Y,SEC. Refuse a malformed argument rather than defaulting: a staging
             * flag that silently ignored its own value would place the character somewhere
             * plausible and every conclusion from the run would be about the wrong cell. */
            int sx = -1, sy = -1, ss = -1;
            const char *cs = argv[++i];
            if (sscanf(cs, "%d,%d,%d", &sx, &sy, &ss) < 2 || sx < 0 || sx > 63
                || sy < 0 || sy > 63) {
                cli_err(cli, "u2: --start-at wants X,Y or X,Y,SEC with 0..63, got '%s'", cs);
                return U2_CLI_REFUSE;
            }
            if (ss < 0 || ss > 99) {
                cli_err(cli, "u2: --start-at sector must be 0..99, got %d", ss);
                return U2_CLI_REFUSE;
            }
            ctx->start_at = 1;
            ctx->start_x = sx;
            ctx->start_y = sy;
            ctx->start_sector = ss;
            ctx->play_map = 1;
        } else if (strcmp(a, "--quit-on-death") == 0) {
            /* Dev affordance for the scripted gates only. Death reloads the last save
             * unconditionally otherwise; see u2_ctx::quit_on_death. */
            ctx->quit_on_death = 1;
        } else if (strcmp(a, "--dev") == 0) {
            /* Development affordances. May add observation and an exit path; may never
             * change game behaviour. See u2_ctx::dev. */
            ctx->dev = 1;
        } else if (strcmp(a, "--enhanced") == 0) {
            /* The default since 2026-11-17, and still an explicit statement of it.
             * Same rules and state as the original; dead ends the original could not
             * avoid are removed. Today that means finishing character creation enters
             * the world instead of replaying the CP/M disk-swap restart. */
            ctx->fidelity = U2_FIDELITY_ENHANCED;
            cli->mode_given = 1;
        } else if (strcmp(a, "--auto-pass-polls") == 0 && i + 1 < argc) {
            /* Idle auto-PASS threshold. 0 restores the faithful default, -1
             * disables it for pure turn-based play. Only meaningful in enhanced
             * mode: the original has one fixed value and no way to change it. */
            ctx->auto_pass_polls = atoi(argv[++i]);
        } else if (strcmp(a, "--keep-site-state") == 0) {
            /* Validated after the loop: --enhanced may appear on either side of it. */
            ctx->keep_site_state = 1;
        } else if (strcmp(a, "--script") == 0 && i + 1 < argc) {
            ctx->script_path = argv[++i];
        } else if (strcmp(a, "--playback") == 0 && i + 1 < argc) {
            /* Replay a recorded demo (docs/demo-record-playback.md). The file names
             * its own starting state, so nothing else on the command line chooses it. */
            ctx->playback_path = argv[++i];
        } else if (strcmp(a, "--demo-dir") == 0 && i + 1 < argc) {
            ctx->demo_dir = argv[++i];
        } else if (strcmp(a, "--demo-name") == 0 && i + 1 < argc) {
            /* Pins the filename a gate needs to find. Without it the name is host
             * time, which is what a person wants and what a test cannot assert on. */
            ctx->demo_name = argv[++i];
        } else if (strcmp(a, "--demo-snapshots") == 0) {
            ctx->demo_snapshots = 1;
        } else if (strcmp(a, "--salvage") == 0) {
            ctx->demo_salvage = 1;
        } else if (strcmp(a, "--playback-ignore-content") == 0) {
            ctx->demo_ignore_content = 1;
        } else if (strcmp(a, "--speed-pin") == 0) {
            ctx->demo_speed_pin = 1;
        } else if (strcmp(a, "--playback-continue") == 0) {
            ctx->demo_play_continue = 1;
        } else if (strcmp(a, "--playback-pace") == 0) {
            ctx->demo_play_pace = 1;
        } else if (strcmp(a, "--save-dir") == 0 && i + 1 < argc) {
            ctx->save_dir = argv[++i];
        } else if (strcmp(a, "--cleanroom") == 0) {
            ctx->cleanroom = 1;
            cli->art_mode_forced = 1;   /* substitute art is a named choice, not a default */
        } else if (strcmp(a, "--classic") == 0 || strcmp(a, "--cga") == 0) {
            /* The opt-out, and it is one flag for both halves: original fidelity and
             * the original screen layout as well as the original 4-colour art. Making
             * it art-only would leave "classic art in the enhanced layout" reachable
             * as an accident of flag order, and nobody has drawn or gated that pair.
             * --cga stays an alias of the same thing, not of art-only. */
            ctx->fidelity = U2_FIDELITY_ORIGINAL;
            cli->mode_given = 1;
            cli->art_mode_forced = 1;
            ctx->art_mode = U2_ART_CGA;
        } else if (strcmp(a, "--ega") == 0) {
            /* Art axis only; independent of --enhanced and --cleanroom. Strict
             * (owner, 2026-11-04): an explicit request never downgrades quietly. */
            cli->art_mode_forced = 1;
            cli->ega_required = 1;
            ctx->art_mode = U2_ART_EGA;
        } else if (strcmp(a, "--ega-theme") == 0 && i + 1 < argc) {
            /* Naming a theme is asking for themed art, so it selects the axis too.
             * It used to set only the name and leave the mode on CGA, which was
             * always odd and became actively wrong once the mode started being
             * inferred: --ega-theme default in a tree with themes would have
             * downgraded a run that gets EGA if you say nothing. An explicit request
             * must not be worse than no request. */
            ctx->ega_theme = argv[++i];
            cli->theme_named = 1;
            cli->art_mode_forced = 1;
            cli->ega_required = 1;
            ctx->art_mode = U2_ART_EGA;
        } else if (strcmp(a, "--ega-theme-dir") == 0 && i + 1 < argc) {
            ctx->ega_theme_dir = argv[++i];
        } else if (strcmp(a, "--data-dir") == 0 && i + 1 < argc) {
            ctx->data_dir = argv[++i];
        } else if (strcmp(a, "--config") == 0 && i + 1 < argc) {
            /* Where config.ini lives. Read after the loop, because a flag always
             * outranks the file and the file may only fill what the flags left
             * unset. docs/install-design.md § 4. */
            cli->config_path = argv[++i];
        } else if (strcmp(a, "--fmtowns-media-dir") == 0 && i + 1 < argc) {
            cli->fmtowns = argv[++i];
        } else if (strcmp(a, "--install") == 0) {
            cli->install = 1;
        } else if (strcmp(a, "--check") == 0) {
            cli->check_only = 1;
        } else if (strcmp(a, "--dry-run") == 0) {
            /* Plan and report, write nothing. Paired with --install, and it is what
             * makes a wrong source cheap to see before it is half-copied. */
            cli->dry_run = 1;
        } else if (strcmp(a, "--no-prompt") == 0) {
            cli->no_prompt = 1;
        } else if (strcmp(a, "--to") == 0 && i + 1 < argc) {
            cli->to = argv[++i];
        } else if (strcmp(a, "--original-dir") == 0 && i + 1 < argc) {
            cli->orig_dir = argv[++i];
        } else if (strcmp(a, "--updated-dir") == 0 && i + 1 < argc) {
            cli->upd_dir = argv[++i];
        } else if (strcmp(a, "--themes-dir") == 0 && i + 1 < argc) {
            /* Repeatable: no single art pack is complete, so the upgrade pack and
             * the Galaxy directory are usually both needed. */
            if (cli->n_themes < U2_THEMES_MAX_SOURCES)
                cli->themes[cli->n_themes++] = argv[++i];
            else {
                cli_err(cli, "u2: at most %d --themes-dir values",
                        U2_THEMES_MAX_SOURCES);
                return U2_CLI_REFUSE;
            }
        } else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
            cli->seed = (unsigned)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(a, "--replay-report") == 0 && i + 1 < argc) {
            cli->report_path = argv[++i];
        } else if (strcmp(a, "--rng-sequence") == 0 && i + 1 < argc) {
            cli->rng_probe = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(a, "--rng-state") == 0 && i + 1 < argc) {
            if (parse_rng_state(cli, argv[++i], cli->rng_state) != 0)
                return U2_CLI_REFUSE;
            cli->rng_state_given = 1;
        } else if (strcmp(a, "--load-ds") == 0 && i + 1 < argc) {
            ctx->load_ds_path = argv[++i];
        } else if (strcmp(a, "--ds-json") == 0 && i + 1 < argc) {
            cli->ds_json_path = argv[++i];
        } else if (strcmp(a, "--passmap") == 0) {
            /* Requires --ds-json: the map is a property of a sector, and only a loaded DS blob
             * says which sector and which mount mode. */
            cli->passmap = 1;
        } else if (strcmp(a, "--dump-ds") == 0 && i + 1 < argc) {
            cli->dump_ds_path = argv[++i];
        } else if (strcmp(a, "--trace-rolls") == 0 && i + 1 < argc) {
            cli->roll_trace_path = argv[++i];
        } else if (strcmp(a, "--api") == 0) {
            /* The play API: line-delimited JSON on stdin/stdout. docs/play-api.md. */
            ctx->api_mode = 1;
        } else if (strcmp(a, "--dump-state") == 0) {
            ctx->dump_state = 1;
        } else if (strcmp(a, "--dump-map") == 0 && i + 1 < argc) {
            cli->map_dump_path = argv[++i];
        } else if (strcmp(a, "--dump-frame") == 0 && i + 1 < argc) {
            ctx->frame_dump_path = argv[++i];
        } else if (strcmp(a, "--dump-each") == 0) {
            ctx->dump_each = 1;
        } else if (strcmp(a, "--dump-hilite") == 0) {
            ctx->dump_hilite = 1;
        } else if (strcmp(a, "--dump-screen") == 0 && i + 1 < argc) {
            ctx->dump_screen = argv[++i];
        } else if (strcmp(a, "--dump-screen-every") == 0 && i + 2 < argc) {
            ctx->dump_every = atoi(argv[++i]);
            ctx->dump_screen = argv[++i];
            if (ctx->dump_every <= 0) {
                cli_err(cli, "u2: --dump-screen-every wants a positive count");
                return U2_CLI_REFUSE;
            }
        } else if (strcmp(a, "--debug") == 0) {
            /* `--debug cli`, matching the existing `--trace[=FILE]` shape: the mode
             * is an argument rather than a separate flag so a bare --debug can be
             * rejected instead of silently guessing. */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                if (strcmp(argv[i + 1], "cli") != 0)
                    fprintf(stderr, "u2: --debug takes 'cli' (got '%s')\n", argv[i + 1]);
                else { ctx->debug_cli = 1; ++i; }
            } else {
                fprintf(stderr, "u2: --debug needs a mode; today that is only 'cli'\n");
            }
        } else if (strcmp(a, "--debug-path") == 0 && i + 1 < argc) {
            ctx->debug_path = argv[++i];
            ctx->debug_cli = 1;      /* naming a directory is intent enough */

        } else if (strcmp(a, "--debug-limit") == 0 && i + 1 < argc) {
            /* 0 means unlimited, and has to be asked for by name: an unattended
             * session auto-PASSes forever and wrote 288 file pairs in 45 s before
             * the cap existed. */
            char *end = NULL;
            const unsigned long v = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || (end && *end != '\0'))
                fprintf(stderr, "u2: --debug-limit wants a number (got '%s')\n", argv[i]);
            else
                ctx->debug_limit = (unsigned)v;
        } else if (strcmp(a, "--selftest") == 0) {
            ctx->selftest = 1;
        } else if (strcmp(a, "--trace") == 0) {
            cli->trace_arg = "-";
        } else if (strncmp(a, "--trace=", 8) == 0) {
            cli->trace_arg = a + 8;
        } else if (strcmp(a, "--version") == 0) {
            /* Which build is this. The reason it is a flag rather than a comment:
             * a dev binary and a shipped binary can be built against different
             * implementations of the same SDL2 API, both run, and nothing else
             * about them says so. See docs/sdl2-vs-sdl3.md. */
            return U2_CLI_INFO_VERSION;
        } else if (strcmp(a, "--licenses") == 0) {
            /* The release is one file with no accompanying documents, so the
             * obligations and provenance have to travel inside it. Printed to
             * stdout and then a clean exit, because someone asked for a document,
             * not a game. */
            return U2_CLI_INFO_LICENSES;
        } else if (strcmp(a, "--video-drivers") == 0) {
            /* Diagnostic, and the release gate: a player binary with only dummy
             * and offscreen can never show a window, and nothing else about it
             * says so. Works with no display, so CI can run it. */
#ifdef U2_HAVE_SDL
            return U2_CLI_INFO_VIDEO_DRIVERS;
#else
            cli_err(cli, "u2: this binary was built without SDL, so it has no "
                            "video drivers to list");
            return U2_CLI_REFUSE;
#endif
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            return U2_CLI_INFO_HELP;
        } else {
            cli_err(cli, "u2: unknown argument '%s'", a);
            cli->usage_on_refuse = 1;
            return U2_CLI_REFUSE;
        }
    }
    /* Cross-flag rules, checked here rather than in main() because they are part of the
     * contract and belong with the flags. */

    /* --install is a tool mode: it runs before config, before video, before the game, and
     * it needs no renderer. That is not tidiness — the terminal build is what CI can
     * execute, so an installer that needed SDL would be an installer nobody could prove.
     * docs/install-design.md § 2. */
    if (cli->check_only && !cli->install) {
        cli_err(cli, "u2: --check only means something with --install.");
        return U2_CLI_REFUSE;
    }
    if (cli->install || cli->check_only) return U2_CLI_INSTALL;

    /* A script is only ever read by the headless backend, and SDL when it was asked for
     * explicitly. Silently ignoring one is how a scripted run ends up waiting for a human,
     * and the report this drew was "the engine does not take --script" blamed on argument
     * order. */
    if (ctx->script_path && ctx->mode != U2_MODE_HEADLESS
#ifdef U2_HAVE_SDL
        && ctx->renderer_choice != U2_RENDERER_SDL
#endif
       ) {
        cli_err(cli, "u2: --script needs --headless (or --renderer=sdl); no other"
                     " backend reads a key script, so it would be ignored.");
        return U2_CLI_REFUSE;
    }

    /* --passmap renders the sector map, which is a property of a sector, and only a loaded
     * DS blob says which sector and which mount mode. */
    if (cli->passmap && !cli->ds_json_path) {
        cli_err(cli, "u2: --passmap needs --ds-json FILE: the map is a property of a"
                     " sector, and only a loaded DS blob says which one.");
        return U2_CLI_REFUSE;
    }

    if (cli->ds_json_path) return U2_CLI_DECODE;
    return U2_CLI_PLAY;
}
