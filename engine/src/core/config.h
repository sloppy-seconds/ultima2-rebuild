/*
 * config.h — `config.ini`: where the content lives, and which theme the player
 * picked. docs/install-design.md § 4.
 *
 * This is the global file. It is NOT the theme manifest: `<theme>/THEME.INI`
 * describes the art inside its own directory and is written by the builder, while
 * this file is written by the installer and the player and says where things are.
 * Different subjects, different writers, different lifetimes, and folding one into
 * the other would make installing a theme a write to a shared file — see the § 4
 * ruling for why that is the worse of the two.
 *
 * PRECEDENCE, and it is the whole reason this file exists as its own module:
 *
 *     command line  >  config.ini  >  nothing
 *
 * "nothing" matters as much as the other two. There is no baked-in developer
 * default behind a shipped path: if neither a flag nor the ini names a directory,
 * the engine keeps the plain default it has always had (the game directory itself)
 * and reports what it could not find rather than going hunting.
 *
 * An explicit path from either source is authoritative with no fall-through. That
 * is the `[LOCKED]` rule from docs/ega-graphics-plan.md § 2, and a config file is
 * only a second place a path can come from, so it inherits the rule verbatim: a
 * typo is reported at startup, never papered over by finding the thing somewhere
 * else.
 */

#ifndef U2_CORE_CONFIG_H
#define U2_CORE_CONFIG_H

#include <stddef.h>

typedef struct u2_ctx u2_ctx;

/* How many theme sources config.ini can record. Must stay in step with
 * U2_THEMES_MAX_SOURCES in core/themes.h; it is spelled out here rather than
 * included because config.h must not depend on the theme builder to describe a
 * config file, and the compile-time check lives in config.c where a mismatch
 * cannot be missed. */
#define U2_INSTALL_MAX_RECORDED 4

typedef enum {
    U2_CONFIG_OK      = 0,   /* read, or simply absent (the drop-in case) */
    U2_CONFIG_REFUSED = 1,   /* read but unusable; `err` names the key or line */
    U2_CONFIG_ERROR   = 2    /* present but unreadable */
} u2_config_result;

typedef struct {
    char data[448];      /* [paths] data   — the content root */
    char saves[448];     /* [paths] saves  — where the player record goes */
    char themes[448];    /* [themes] dir   — the themes directory */
    char theme[64];      /* [themes] theme — the selected theme name */
    /* [game] mode — "classic" or "enhanced", the presentation mode. Empty means
     * the file says nothing and the engine's own default stands, which since the
     * 2026-11-17 ruling is enhanced. A flag always wins, so `--classic` on the
     * command line beats `mode = enhanced` here exactly as it beats nothing.
     * Any value other than the two words is refused at startup rather than
     * guessed, because a typo in a mode is the kind of mistake that otherwise
     * plays for twenty minutes before anyone notices the wrong layout. */
    char mode[16];
    /* [install] — the sources `--install` was given, recorded so the same command
     * works again with no flags from beside the game directory. The game never
     * reads these; the installer does, through core/install.h.
     *
     * They are exempt from the startup existence check, for the same reason `saves`
     * is: a source on a stick that is not plugged in must not stop a game being
     * played. The installer reports a recorded source it cannot find and asks for
     * it again — see u2_install_fill_from_config. */
    char inst_to[448];
    char inst_original[448];
    char inst_updated[448];
    char inst_fmtowns[448];
    char inst_themes[U2_INSTALL_MAX_RECORDED][448];
    int  n_inst_themes;
    char path[512];      /* where this came from, for the message */
    int  present;        /* 0 when the file did not exist at all */
} u2_config;

/* Read `path`. A missing file is not an error: `--install` is additive and the
 * drop-in case has no config.ini at all, so `out->present` says which happened and
 * every field stays empty, meaning "the ini had no opinion".
 *
 * Refusals, all of them reported rather than defaulted:
 *   - a key outside the known set, which is how a typo shows up instead of vanishing
 *   - `[paths] data` or `[themes] dir` naming something that is not a directory
 *   - a value too long for the parser (see ini.h)
 * `err` receives the reason in every refusal. */
u2_config_result u2_config_load(const char *path, u2_config *out,
                                char *err, size_t errsz);

/* Fill whatever the command line left unset. Call it AFTER flag parsing, because
 * a flag is the higher precedence and this must not overwrite one. */
void u2_config_apply(u2_ctx *ctx, const u2_config *cfg);

/* The default location: `config.ini` in the game directory, which is the
 * directory the engine was launched from — the same default-drive model the
 * original's FCB drive byte 0 implies. `--config` overrides it. */
#define U2_CONFIG_DEFAULT_FILE "config.ini"

#endif
