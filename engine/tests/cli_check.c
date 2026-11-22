/* The flag contract, asserted in-process.
 *
 * Before cli.c existed, every rule below was tested by exec'ing ./u2 and grepping stderr:
 * run_cp0.sh matched the substring `--script needs --headless`, run_api.sh checked that
 * --api with --label exits 2. That is a test reading a process boundary, and it has two
 * failure modes this one does not. A refusal whose message was reworded silently stops
 * being asserted, and a flag that refuses for the wrong reason looks identical from the
 * exit code. Here the disposition and the reason are values, so both are assertable.
 *
 * The rules worth the most are the mode-intent ones (the --enhanced / --ega block). They
 * exist because conflating "prefers themed art" with "requires themed art" made every
 * clean checkout, every CI run, and any player without the fan pack refuse to start.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "core/rng.h"

static int checks, fails;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* Parse one argv and report. The {NULL} terminator is the caller's, so argc is derived
 * here rather than hand-counted — a hand-counted argc silently drops the last flag, and
 * the test then asserts on a command line nobody wrote. */
static u2_cli_action run(u2_ctx *ctx, u2_cli *cli, const char *a0, ...)
{
    const char *av[32];
    int n = 0;
    av[n++] = a0;
    va_list ap;
    va_start(ap, a0);
    const char *s;
    while (n < 31 && (s = va_arg(ap, const char *)) != NULL) av[n++] = s;
    va_end(ap);
    memset(ctx, 0, sizeof *ctx);
    ctx->mode = U2_MODE_TERM;
    ctx->fidelity = U2_FIDELITY_ENHANCED;
    return u2_cli_parse(n, (char **)av, ctx, cli);
}

static int err_has(const u2_cli *cli, const char *needle)
{
    return strstr(cli->err, needle) != NULL;
}

int main(void)
{
    u2_ctx ctx;
    u2_cli cli;

    /* --- defaults: what a bare `./u2` means --------------------------------- */
    CHECK(run(&ctx, &cli, "u2", NULL) == U2_CLI_PLAY, "no flags plays");
    CHECK(ctx.mode == U2_MODE_TERM, "a bare run is the terminal renderer");
    CHECK(ctx.fidelity == U2_FIDELITY_ENHANCED, "enhanced is the default mode (owner 2026-11-17)");
    CHECK(cli.seed == U2_RNG_DEFAULT_SEED, "the seed default is the parser's, not main's");
    CHECK(cli.err[0] == '\0', "and a bare run reports nothing");

    /* --seed changes the seed, and the flag name survived the extraction. That is not a
     * joke: the mechanical rename that moved this code also rewrote the literal "--seed"
     * into "--cli->seed", and the only thing that caught it was the gate that execs the
     * binary. This assertion is that accident made permanent at the level it happened at. */
    CHECK(run(&ctx, &cli, "u2", "--seed", "0x20", NULL) == U2_CLI_PLAY, "--seed is recognised");
    CHECK(cli.seed == 0x20u, "--seed 0x20 parsed as a number, got %u", cli.seed);

    /* --- a script needs a backend that reads one ---------------------------- */
    CHECK(run(&ctx, &cli, "u2", "--script", "k.keys", NULL) == U2_CLI_REFUSE,
          "--script without --headless is refused");
    CHECK(err_has(&cli, "--script needs --headless"),
          "and the reason names the flag and the fix, not just 'bad arguments'");
    CHECK(run(&ctx, &cli, "u2", "--script", "k.keys", "--headless", NULL) == U2_CLI_PLAY,
          "control: with --headless the same flags play");
    CHECK(ctx.script_path && strcmp(ctx.script_path, "k.keys") == 0,
          "and the path reached the ctx");

    /* --- a value flag that got a bad value ---------------------------------- */
    CHECK(run(&ctx, &cli, "u2", "--renderer=bogus", NULL) == U2_CLI_REFUSE,
          "--renderer rejects a word it does not have");
    CHECK(err_has(&cli, "sdl"), "and the refusal lists the accepted words");
    CHECK(cli.usage_on_refuse == 0,
          "a bad value does NOT print the 15 KB usage text — only an unknown word does");
    run(&ctx, &cli, "u2", "--bogus", NULL);
    CHECK(cli.usage_on_refuse == 1, "an unknown word does, because nothing else tells them");
    CHECK(run(&ctx, &cli, "u2", "--renderer=sdl", NULL) == U2_CLI_PLAY
          && ctx.renderer_choice == U2_RENDERER_SDL && ctx.renderer_explicit == 1,
          "--renderer=sdl sets the choice and says it was asked for");

    /* --- --start-at stages, and implies the entry it needs ------------------ */
    CHECK(run(&ctx, &cli, "u2", "--start-at", "5,7,21", NULL) == U2_CLI_PLAY,
          "--start-at X,Y,SEC parses");
    CHECK(ctx.start_at && ctx.start_x == 5 && ctx.start_y == 7 && ctx.start_sector == 21,
          "and all three coordinates landed");
    CHECK(ctx.play_map == 1, "--start-at implies --play-map, or the staging is never reached");
    CHECK(run(&ctx, &cli, "u2", "--start-at", "99,0", NULL) == U2_CLI_REFUSE,
          "a coordinate outside 0..63 is refused, not defaulted");
    CHECK(run(&ctx, &cli, "u2", "--start-at", "5", NULL) == U2_CLI_REFUSE,
          "and a missing Y is refused rather than guessed");

    /* --- the intent split: preferred is not required ------------------------ */
    run(&ctx, &cli, "u2", "--enhanced", NULL);
    CHECK(ctx.fidelity == U2_FIDELITY_ENHANCED && cli.mode_given == 1,
          "--enhanced states the mode explicitly");
    CHECK(cli.ega_required == 0,
          "--enhanced PREFERS themed art and must not REQUIRE it: themes/ is gitignored, so "
          "requiring it made every clean checkout and CI run refuse to start");
    run(&ctx, &cli, "u2", "--ega", NULL);
    CHECK(cli.ega_required == 1 && ctx.art_mode == U2_ART_EGA,
          "--ega demands themed art, and an explicit request never downgrades quietly");
    run(&ctx, &cli, "u2", "--classic", NULL);
    CHECK(ctx.fidelity == U2_FIDELITY_ORIGINAL && ctx.art_mode == U2_ART_CGA
          && cli.art_mode_forced == 1,
          "--classic is one decision about three things: fidelity, layout and art");
    CHECK(run(&ctx, &cli, "u2", "--cga", NULL) == U2_CLI_PLAY
          && ctx.art_mode == U2_ART_CGA, "--cga is an alias of --classic, not of art-only");
    run(&ctx, &cli, "u2", "--cleanroom", NULL);
    CHECK(cli.art_mode_forced == 1, "substitute art is a named choice, not a default");
    run(&ctx, &cli, "u2", "--ega-theme", "fmtowns", NULL);
    CHECK(cli.theme_named == 1 && cli.ega_required == 1,
          "naming a theme selects the axis too — an explicit request must not be worse "
          "than no request");

    /* --- mode dispositions --------------------------------------------------- */
    CHECK(run(&ctx, &cli, "u2", "--install", "--check", NULL) == U2_CLI_INSTALL,
          "--install --check is the installer");
    CHECK(run(&ctx, &cli, "u2", "--check", NULL) == U2_CLI_REFUSE,
          "--check alone means nothing and says so");
    CHECK(run(&ctx, &cli, "u2", "--ds-json", "b.bin", NULL) == U2_CLI_DECODE,
          "--ds-json decodes and exits, no game loop");
    CHECK(run(&ctx, &cli, "u2", "--passmap", NULL) == U2_CLI_REFUSE,
          "--passmap without --ds-json is refused: the map is a property of a sector, and "
          "only a loaded blob says which one");
    CHECK(run(&ctx, &cli, "u2", "--ds-json", "b.bin", "--passmap", NULL) == U2_CLI_DECODE,
          "control: with the blob it is a decode");
    CHECK(run(&ctx, &cli, "u2", "--help", NULL) == U2_CLI_INFO_HELP, "--help is a disposition");
    CHECK(run(&ctx, &cli, "u2", "-h", NULL) == U2_CLI_INFO_HELP, "and so is -h");
    CHECK(run(&ctx, &cli, "u2", "--version", NULL) == U2_CLI_INFO_VERSION,
          "--version asks for the build identity");
    CHECK(run(&ctx, &cli, "u2", "--licenses", NULL) == U2_CLI_INFO_LICENSES,
          "--licenses asks for the attribution record");

    /* --- repeatable and capped ---------------------------------------------- */
    run(&ctx, &cli, "u2", "--install", "--themes-dir", "a", "--themes-dir", "b", NULL);
    CHECK(cli.n_themes == 2, "two --themes-dir values are two sources, got %d", cli.n_themes);
    CHECK(run(&ctx, &cli, "u2", "--install",
              "--themes-dir", "a", "--themes-dir", "b",
              "--themes-dir", "c", "--themes-dir", "d",
              "--themes-dir", "e", NULL) == U2_CLI_REFUSE,
          "and the cap is a refusal, not a silent drop of the fifth");

    /* --- numeric guards ------------------------------------------------------ */
    CHECK(run(&ctx, &cli, "u2", "--dump-screen-every", "0", "s.txt", NULL) == U2_CLI_REFUSE,
          "--dump-screen-every 0 is refused rather than spinning");
    CHECK(run(&ctx, &cli, "u2", "--rng-state", "abc", NULL) == U2_CLI_REFUSE,
          "--rng-state rejects a short register instead of defaulting");
    CHECK(err_has(&cli, "12 hex digits"), "and says what it wanted");
    CHECK(run(&ctx, &cli, "u2", "--rng-state", "8282ffc7cda4", NULL) == U2_CLI_PLAY
          && cli.rng_state_given == 1 && cli.rng_state[0] == 0x82 && cli.rng_state[5] == 0xa4,
          "a real register parses to six bytes in order");

    printf("cli_check: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
