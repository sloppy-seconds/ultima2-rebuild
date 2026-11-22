/* The command line: what the player asked for, and whether it is coherent.
 *
 * Split out of main() so the flag contract is testable in-process. Before this, every
 * refusal rule was only observable by exec'ing the binary and grepping stderr.
 *
 * Note the two kinds of output. `ctx` is what the run is; `u2_cli` is what the player
 * said, plus the inputs to modes that are not the game (installer, decoders, diagnostics).
 * The split matters because some questions have no answer in `ctx`: `--enhanced` prefers
 * themed art and `--ega` demands it, and `ctx.art_mode` can read the same either way.
 */
#ifndef U2_CLI_H
#define U2_CLI_H

#include <stddef.h>
#include <stdint.h>

#include "core/ctx.h"
#include "core/themes.h"     /* U2_THEMES_MAX_SOURCES */

#define U2_CLI_ERR_MAX 512

typedef enum {
    U2_CLI_PLAY = 0,        /* run the game; main() carries on wiring and looping */
    U2_CLI_INSTALL,         /* the installer ran its own argument checks; act and exit */
    U2_CLI_DECODE,          /* --ds-json: decode a DS blob and exit, no game loop */
    U2_CLI_INFO_VERSION,    /* print the build identity, exit 0 */
    U2_CLI_INFO_LICENSES,   /* print the attribution record, exit 0 */
    U2_CLI_INFO_VIDEO_DRIVERS,
    U2_CLI_INFO_HELP,
    U2_CLI_REFUSE           /* bad or incoherent flags; cli->err says why */
} u2_cli_action;

typedef struct u2_cli {
    /* --- installer mode. The installer is a tool mode: no renderer, no config, no game. */
    int install, check_only, dry_run, no_prompt;
    const char *to, *orig_dir, *upd_dir, *fmtowns;
    const char *themes[U2_THEMES_MAX_SOURCES];
    int n_themes;

    /* --- what the player said, as distinct from what the run ended up as.
     *   mode_given      — a mode flag appeared, so config.ini may not fill it in.
     *   art_mode_forced — art was chosen (including --cleanroom), so only the theme
     *                     may still be picked for them.
     *   theme_named     — a specific theme was named: the one thing that suppresses the
     *                     fmtowns-over-default preference.
     *   ega_required    — themed art is demanded, which is what makes the strict launch
     *                     check fire. --enhanced prefers EGA and must not require it;
     *                     conflating the two made every clean checkout refuse to start. */
    int mode_given, art_mode_forced, theme_named, ega_required;

    /* --- diagnostics and development entries, none of which are run properties. */
    const char *config_path, *ds_json_path, *dump_ds_path, *map_dump_path;
    const char *roll_trace_path, *report_path, *trace_arg;
    int passmap;

    /* --- rng staging for the replay probes. */
    unsigned seed;
    int rng_probe, rng_state_given;
    uint8_t rng_state[6];

    /* The refusal reason, filled on U2_CLI_REFUSE and on the info paths that can fail.
     * main() prints it; the parser does not write to a stream, which is what lets a test
     * assert on a refusal without capturing a pipe. */
    char err[U2_CLI_ERR_MAX];
    /* Set when the refusal was an unrecognised word rather than a bad value: only then
     * does the usage text help, and printing 15 KB of help at someone who typed a valid
     * flag with a bad argument is noise that buries the actual message. */
    int usage_on_refuse;
} u2_cli;

/* Parse argv into ctx and cli and decide what main() should do.
 *
 * On U2_CLI_REFUSE, cli->err holds the message and the caller prints it — and for an
 * unknown flag, the usage text as well. Nothing here opens a window, reads a file, or
 * starts a game: the dispositions that would are returned, not performed.
 *
 * ctx and cli are written from zero; the caller need not pre-clear them, and the defaults
 * this sets (terminal mode, enhanced fidelity, the default seed) are the ones main() used
 * to set before its loop. */
u2_cli_action u2_cli_parse(int argc, char **argv, u2_ctx *ctx, u2_cli *cli);

/* The help text, on stderr. Six fprintf/fputs calls rather than one literal because the
 * whole text is ~15.6 KB and ISO C only requires 4095 bytes per string literal. */
void u2_cli_usage(const char *argv0);

/* Record a refusal reason. printf-style, so -Wformat proves the arguments match the
 * message — the check that a hand-copied stderr string never gets. */
void cli_err(u2_cli *cli, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif /* U2_CLI_H */
