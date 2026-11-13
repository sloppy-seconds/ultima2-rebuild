/*
 * artauto_check.c — which art axis and which theme a run gets when it says nothing.
 *
 * Opened 2026-11-06. An --install tree holding themes still started in CGA unless
 * the player typed --ega, so the better art sat on disk unused.
 *
 * The preference order was reversed by the owner on 2026-11-13: the fan pack's `10`
 * is now the preferred default (best-looking of the four), then the pack's
 * `default`, then the derived FM Towns theme, then CGA. The earlier rule put
 * fmtowns first; that is gone. The tests below pin the current order.
 *
 * The two properties that matter most are the ones about NOT firing. Auto-detection
 * must never change what a developer's checkout or the test suite draws, because
 * the CP2 byte-comparison baseline is measured in original CGA mode; and an explicit
 * choice must never end up worse than silence, which is the trap in a feature whose
 * whole job is to supply a default.
 *
 * Fixtures are v1 themes (a bare EGATILES), which the loader accepts on size alone,
 * so this needs no art and no media. theme_relpath maps the name "fmtowns" to
 * EGATHEME.FMTOWNS/EGATILES, which is what makes the preference testable without a
 * derived v2 tree.
 *
 * Both lookup paths are driven: the configured directory (what config.ini's
 * [themes] dir becomes) and the bare `themes` beside the launch directory, which
 * is reached by chdir into a fixture root.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "core/ctx.h"
#include "core/tiles_ega.h"
#include "core/fmtowns.h"

static int checks, fails;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { fails++; \
    printf("  NOT OK  %s\n", (msg)); } } while (0)

static void mkpath(const char *p) { mkdir(p, 0755); }

/* One v1 theme under `root`: 128 tiles x 128 B, well over the 64x128 the size
 * check requires. rel may name a subdirectory (EGATHEME.X/EGATILES), whose parent
 * has to be created first — mkdir takes a directory, not a file path. */
static void mk_theme(const char *root, const char *rel)
{
    char path[960];
    const char *slash = strchr(rel, '/');
    if (slash)
        snprintf(path, sizeof path, "%s/%.*s", root, (int)(slash - rel), rel);
    else
        path[0] = '\0';
    if (path[0]) mkpath(path);

    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "wb");
    if (f) {
        for (int i = 0; i < 128 * 128; i++) fputc(0, f);
        fclose(f);
    }
}

static void reset(u2_ctx *c)
{
    memset(c, 0, sizeof *c);
    c->art_mode = U2_ART_CGA;
}

/* Build `<base>/<case>/themes` holding the named themes. `case_dir` receives the
 * game directory (what you chdir into for the drop-in case) and `th_dir` the
 * themes directory itself (what config.ini's [themes] dir names). */
static void fixture(char *case_dir, size_t cn, char *th_dir, size_t tn,
                    const char *base, const char *case_name,
                    int have_default, int have_fmtowns, int have_10)
{
    snprintf(case_dir, cn, "%s/%s", base, case_name);
    mkpath(case_dir);
    snprintf(th_dir, tn, "%s/themes", case_dir);
    mkpath(th_dir);
    if (have_10)       mk_theme(th_dir, "EGATHEME.10/EGATILES");
    if (have_default)  mk_theme(th_dir, "EGATILES");
    if (have_fmtowns)  mk_theme(th_dir, "EGATHEME.FMTOWNS/EGATILES");
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "/var/tmp/u2-artauto";
    char dir[900], th[900];
    const char *const dirp = th;      /* the configured [themes] dir */
    u2_ctx ctx;
    char theme[64];

    mkpath(base);

    /* --- the configured-directory path (what config.ini's [themes] dir becomes) --- */

    /* 1. empty themes dir: no change, and CGA is not a refusal. */
    fixture(dir, sizeof dir, th, sizeof th, base, "none", 0, 0, 0);
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1) == 0,
          "empty themes dir: auto-detect finds nothing");
    CHECK(ctx.art_mode == U2_ART_CGA,
          "empty themes dir: the run stays CGA, silently and correctly");

    /* 2. only the fan pack's `default`: that is what you get. */
    fixture(dir, sizeof dir, th, sizeof th, base, "only-default", 1, 0, 0);
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1) == 1,
          "a default theme is found");
    CHECK(ctx.art_mode == U2_ART_EGA, "finding a theme selects the EGA axis");
    CHECK(strcmp(theme, "default") == 0, "with only `default` installed, that is the theme");

    /* 3. THE PREFERENCE (owner ruling 2026-11-13): the fan pack beats the derived
     *    FM Towns theme. With `default` and fmtowns and no `10`, `default` wins. */
    fixture(dir, sizeof dir, th, sizeof th, base, "both", 1, 1, 0);
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1) == 1
          && strcmp(theme, "default") == 0,
          "with `default` and fmtowns installed the fan pack's default is preferred");

    /* 3b. `10` is the preferred default when present. */
    fixture(dir, sizeof dir, th, sizeof th, base, "only-10", 0, 0, 1);
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1) == 1
          && strcmp(theme, "10") == 0,
          "with only `10` installed, that is the theme");

    /* 3c. THE HEADLINE: `10` beats the derived FM Towns theme even together. */
    fixture(dir, sizeof dir, th, sizeof th, base, "10-fmtowns", 0, 1, 1);
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1) == 1
          && strcmp(theme, "10") == 0,
          "`10` is preferred over the derived FM Towns theme");

    /* 3d. all three present: `10` still wins (the full-install case). */
    fixture(dir, sizeof dir, th, sizeof th, base, "all-three", 1, 1, 1);
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1) == 1
          && strcmp(theme, "10") == 0,
          "with 10, default and fmtowns all installed, `10` is the default");

    /* 4. an explicit theme outranks the preference — it is a choice, not a default. */
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    ctx.ega_theme = "default";
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1) == 1
          && strcmp(theme, "default") == 0,
          "an explicitly named theme is honoured over the preference");

    /* 5. may_change_mode = 0 on CGA: nothing to pick, so nothing is set. The mode
     *    was decided against themed art, so a theme would be meaningless. */
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    ctx.art_mode = U2_ART_CGA;
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 0) == 0
          && ctx.art_mode == U2_ART_CGA,
          "CGA with the mode already decided picks no theme");

    /* 6. may_change_mode = 0 on EGA: the mode is the player's, the theme is open.
     *    The active fixture is "all-three" (10 + default + fmtowns), so `10` is the
     *    preference. */
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    ctx.art_mode = U2_ART_EGA;
    theme[0] = '\0';
    CHECK(u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 0) == 1
          && ctx.art_mode == U2_ART_EGA
          && strcmp(theme, "10") == 0,
          "--ega with no theme named still gets the preference, and keeps EGA");

    /* 7. the directory that worked is recorded, or the strict launch check looks in
     *    the dev fallbacks and refuses over a theme that was just loaded. */
    reset(&ctx);
    ctx.ega_theme_dir = dirp;
    theme[0] = '\0';
    (void)u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1);
    CHECK(ctx.ega_theme_dir && strcmp(ctx.ega_theme_dir, dirp) == 0,
          "a configured themes dir stays the one on the context");

    /* --- the bare `themes` beside the launch directory (the drop-in case) --- */

    /* 8. chdir into the "both" fixture and set nothing: same answer, and the
     *    recorded dir is the relative name the engine will resolve again later. */
    char both[900];
    snprintf(both, sizeof both, "%s/both", base);
    if (chdir(both) == 0) {
        (void)0;
        reset(&ctx);
        theme[0] = '\0';
        const int hit = u2_tiles_ega_autodetect(&ctx, theme, sizeof theme, 1);
        CHECK(hit == 1 && strcmp(theme, "default") == 0,
              "themes/ beside the launch directory is found with nothing configured");
        CHECK(ctx.ega_theme_dir && strcmp(ctx.ega_theme_dir, U2_EGA_THEMES_DIR_NAME) == 0,
              "and the relative dir it used is recorded for the later open()");
    } else {
        printf("  NOT OK  cannot chdir to the fixture root: %s\n", both);
        fails++; checks++;
    }

    printf("  ok  %d checks: art axis and theme picked from what is installed\n", checks);
    return fails ? 1 : 0;
}
