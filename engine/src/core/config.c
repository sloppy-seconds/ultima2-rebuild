/*
 * config.c — see config.h. Parsing is delegated to core/ini.c so that config.ini
 * and THEME.INI cannot disagree about what a malformed line means; what lives here
 * is the key set, the refusals, and the precedence rule.
 */

#define _POSIX_C_SOURCE 200809L   /* stat() under -std=c11 */

#include "core/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "core/ctx.h"
#include "core/ini.h"
#include "core/themes.h"

/* The known key set, per section. Anything else in the file is a refusal, because
 * a key nobody reads is indistinguishable from a typo, and a typo that is silently
 * ignored is a player editing the wrong line forever. */
static const char *const k_paths_keys[] = { "data", "saves" };
static const char *const k_theme_keys[] = { "dir", "theme" };
/* The presentation mode, which is neither a path nor an art choice, so it gets its
 * own section rather than being filed under one that would make a reader guess. */
static const char *const k_game_keys[] = { "mode", "sound" };
static const char *const k_modes[] = { "classic", "enhanced" };
static const char *const k_sounds[] = { "on", "off" };
/* What --install recorded about itself. `themes2..4` rather than one
 * comma-separated list because the ini grammar has one value per key and the
 * parser refuses rather than splits; four is U2_THEMES_MAX_SOURCES, asserted below
 * so the two cannot drift. */
static const char *const k_install_keys[] = {
    "to", "original", "updated", "fmtowns",
    "themes", "themes2", "themes3", "themes4",
};

_Static_assert(U2_INSTALL_MAX_RECORDED == U2_THEMES_MAX_SOURCES,
               "config.h's recorded theme-source count must match the theme builder's");

static int known(const char *const *list, size_t n, const char *key)
{
    for (size_t i = 0; i < n; i++)
        if (strcasecmp(list[i], key) == 0)
            return 1;
    return 0;
}

static int count_keys(const char *buf, const char *section)
{
    size_t cur = 0;
    char key[64];
    int n = 0;
    while (u2_ini_next_key(buf, section, &cur, key, sizeof key))
        n++;
    return n;
}

/* How many times one key is written. u2_ini_get returns the first match and moves
 * on, so a duplicated line is otherwise silent, and two `data =` lines is exactly
 * the kind of edit that happens when someone adds a line instead of changing one.
 * The reader picks one and the player believes the other. */
static int count_key(const char *buf, const char *section, const char *want)
{
    size_t cur = 0;
    char key[64];
    int n = 0;
    while (u2_ini_next_key(buf, section, &cur, key, sizeof key))
        if (strcasecmp(key, want) == 0)
            n++;
    return n;
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* The directory containing the config file, which is what a relative path inside
 * it means. Without this rule `--config /games/ultima2/config.ini` from a home
 * directory would resolve `data = data` to /home/paul/data, and the file would
 * describe a layout it does not have. An absolute value is taken exactly as
 * written, and a config named as plain `config.ini` resolves against `.` — which
 * is the original's default-drive model, so the drop-in case is unchanged.
 *
 * This is a decision, not a convention nobody noticed: docs/install-design.md § 4
 * fixes precedence but says nothing about what a relative path is relative to, and
 * the two candidate answers differ in whether the installer's output works when the
 * player launches it from anywhere. */
static void config_base(const char *path, char *out, size_t n)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
        snprintf(out, n, ".");
    else if (slash == path)
        snprintf(out, n, "/");
    else
        snprintf(out, n, "%.*s", (int)(slash - path), path);
}

static void resolve_against_file(char *val, size_t valsz, const char *base)
{
    char out[448];

    if (!*val || val[0] == '/' || !*base)
        return;
    /* `[install] to = .` means "the directory this file is in", and saying so
     * directly beats producing "./." in every message that prints it. */
    if (!strcmp(val, ".")) {
        snprintf(out, sizeof out, "%.447s", base);
    } else {
        const int n = snprintf(out, sizeof out, "%.300s/%.140s", base, val);
        if (n < 0 || (size_t)n >= valsz)
            return;     /* leave it as written; the existence check below will refuse it */
    }
    if ((size_t)strlen(out) >= valsz)
        return;
    memcpy(val, out, strlen(out) + 1);
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    const size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

u2_config_result u2_config_load(const char *path, u2_config *out,
                                char *err, size_t errsz)
{
    memset(out, 0, sizeof *out);
    snprintf(out->path, sizeof out->path, "%s", path ? path : "");

    struct stat st;
    if (!path || stat(path, &st) != 0)
        return U2_CONFIG_OK;            /* absent is the drop-in case, not a fault */
    out->present = 1;

    char *buf = slurp(path);
    if (!buf) {
        snprintf(err, errsz, "%s exists but could not be read", path);
        return U2_CONFIG_ERROR;
    }

    u2_config_result r = U2_CONFIG_OK;
    char key[64];

    /* 1. every key must belong to the section it is written under, and every
     *    section's keys must be known. The third loop is the one that catches a key
     *    sitting outside any section, which the two scoped walks cannot see. */
    size_t cur = 0;
    while (u2_ini_next_key(buf, "paths", &cur, key, sizeof key)) {
        if (!known(k_paths_keys, sizeof k_paths_keys / sizeof k_paths_keys[0], key)) {
            snprintf(err, errsz, "%s: unknown key '%s' in [paths]", path, key);
            r = U2_CONFIG_REFUSED;
            break;
        }
    }
    cur = 0;
    while (r == U2_CONFIG_OK && u2_ini_next_key(buf, "themes", &cur, key, sizeof key)) {
        if (!known(k_theme_keys, sizeof k_theme_keys / sizeof k_theme_keys[0], key)) {
            snprintf(err, errsz, "%s: unknown key '%s' in [themes]", path, key);
            r = U2_CONFIG_REFUSED;
            break;
        }
    }
    cur = 0;
    while (r == U2_CONFIG_OK && u2_ini_next_key(buf, "game", &cur, key, sizeof key)) {
        if (!known(k_game_keys, sizeof k_game_keys / sizeof k_game_keys[0], key)) {
            snprintf(err, errsz, "%s: unknown key '%s' in [game]", path, key);
            r = U2_CONFIG_REFUSED;
            break;
        }
    }
    if (r == U2_CONFIG_OK &&
        count_keys(buf, NULL) !=
            count_keys(buf, "paths") + count_keys(buf, "themes") +
            count_keys(buf, "game") + count_keys(buf, "install")) {
        snprintf(err, errsz,
                 "%s: a key sits outside [paths], [themes], [game] and [install]; every "
                 "setting belongs under one of them", path);
        r = U2_CONFIG_REFUSED;
    }

    /* 1b. the [install] section, same refusals for the same reason. */
    cur = 0;
    while (r == U2_CONFIG_OK && u2_ini_next_key(buf, "install", &cur, key, sizeof key)) {
        if (!known(k_install_keys, sizeof k_install_keys / sizeof k_install_keys[0], key)) {
            snprintf(err, errsz, "%s: unknown key '%s' in [install]", path, key);
            r = U2_CONFIG_REFUSED;
            break;
        }
    }

    /* 2. read the values we know about */
    if (r == U2_CONFIG_OK) {
        struct { const char *sec, *key; char *dst; size_t n; } want[] = {
            { "paths",   "data",    out->data,  sizeof out->data  },
            { "paths",   "saves",   out->saves, sizeof out->saves },
            { "themes",  "dir",     out->themes,sizeof out->themes},
            { "themes",  "theme",   out->theme, sizeof out->theme },
            { "game",    "mode",    out->mode,  sizeof out->mode  },
            { "game",    "sound",   out->sound, sizeof out->sound },
            { "install", "to",      out->inst_to,      sizeof out->inst_to },
            { "install", "original",out->inst_original,sizeof out->inst_original },
            { "install", "updated", out->inst_updated, sizeof out->inst_updated },
            { "install", "fmtowns", out->inst_fmtowns, sizeof out->inst_fmtowns },
            { "install", "themes",  out->inst_themes[0], sizeof out->inst_themes[0] },
            { "install", "themes2", out->inst_themes[1], sizeof out->inst_themes[1] },
            { "install", "themes3", out->inst_themes[2], sizeof out->inst_themes[2] },
            { "install", "themes4", out->inst_themes[3], sizeof out->inst_themes[3] },
        };
        for (size_t i = 0; i < sizeof want / sizeof want[0] && r == U2_CONFIG_OK; i++) {
            const int dup = count_key(buf, want[i].sec, want[i].key);
            if (dup > 1) {
                snprintf(err, errsz, "%s: '%s' is written %d times in [%s]",
                         path, want[i].key, dup, want[i].sec);
                r = U2_CONFIG_REFUSED;
            } else if (dup == 1) {
                const int got = u2_ini_get(buf, want[i].sec, want[i].key,
                                           want[i].dst, want[i].n);
                if (got < 0) {
                    snprintf(err, errsz, "%s: value for '%s' is too long",
                             path, want[i].key);
                    r = U2_CONFIG_REFUSED;
                }
            }
        }
        /* Keep the recorded theme sources contiguous: `themes2` without `themes`
         * is a hole, and a reader that walked the array would stop at the empty
         * first slot and never see the value that is there. */
        for (int i = 0; i < U2_INSTALL_MAX_RECORDED && r == U2_CONFIG_OK; i++) {
            if (*out->inst_themes[i])
                out->n_inst_themes = i + 1;
        }

        /* [game] mode is one of two words, and a third word is a refusal rather
         * than a shrug. A misspelt mode is the specific mistake that costs a whole
         * play session: the game starts, it looks like a game, and the layout or the
         * fidelity rules are quietly the other mode's. Case is not part of the
         * meaning, so `Enhanced` is accepted and stored lower-case for the one
         * reader below that compares it. */
        if (r == U2_CONFIG_OK && *out->mode) {
            int known_mode = 0;
            for (size_t i = 0; i < sizeof k_modes / sizeof k_modes[0]; i++)
                if (!strcasecmp(out->mode, k_modes[i]))
                    known_mode = 1;
            if (!known_mode) {
                snprintf(err, errsz, "%s: [game] mode = %s is neither 'classic' "
                         "nor 'enhanced'", path, out->mode);
                r = U2_CONFIG_REFUSED;
            } else {
                for (char *c = out->mode; *c; c++)
                    if (*c >= 'A' && *c <= 'Z')
                        *c = (char)(*c + ('a' - 'A'));
            }
        }

        /* [game] sound is one of two words, for the same reason mode is: the
         * failure it prevents is a player who believes they turned the speaker off
         * and has not. Stored lower-case for the one reader in u2_config_apply. */
        if (r == U2_CONFIG_OK && *out->sound) {
            int known_sound = 0;
            for (size_t i = 0; i < sizeof k_sounds / sizeof k_sounds[0]; i++)
                if (!strcasecmp(out->sound, k_sounds[i]))
                    known_sound = 1;
            if (!known_sound) {
                snprintf(err, errsz, "%s: [game] sound = %s is neither 'on' nor 'off'",
                         path, out->sound);
                r = U2_CONFIG_REFUSED;
            } else {
                for (char *c = out->sound; *c; c++)
                    if (*c >= 'A' && *c <= 'Z')
                        *c = (char)(*c + ('a' - 'A'));
            }
        }
    }

    /* 3. a relative path means relative to the file, then every path that is
     *    named and does not exist is an error at startup, not a silent default.
     *    `saves` is resolved but exempt from the check: it is output, and save.c
     *    creates it on first write the way the original would have.
     *
     *    The [install] paths are exempt for a different reason and it is worth
     *    keeping them apart: they name the player's *media*, which lives on a stick
     *    or a download folder that is not always mounted. A game you can play must
     *    not depend on the disc being present; only a re-install does, and
     *    u2_install_fill_from_config reports a recorded source it cannot find
     *    instead of pretending the section was empty. */
    char base[300];
    config_base(path, base, sizeof base);
    resolve_against_file(out->data,  sizeof out->data,  base);
    resolve_against_file(out->saves, sizeof out->saves, base);
    resolve_against_file(out->themes,sizeof out->themes,base);
    resolve_against_file(out->inst_to,       sizeof out->inst_to,       base);
    resolve_against_file(out->inst_original, sizeof out->inst_original, base);
    resolve_against_file(out->inst_updated,  sizeof out->inst_updated,  base);
    resolve_against_file(out->inst_fmtowns,  sizeof out->inst_fmtowns,  base);
    for (int i = 0; i < U2_INSTALL_MAX_RECORDED; i++)
        resolve_against_file(out->inst_themes[i], sizeof out->inst_themes[i], base);

    if (r == U2_CONFIG_OK && *out->data && !is_dir(out->data)) {
        snprintf(err, errsz, "%s: [paths] data = %s is not a directory", path, out->data);
        r = U2_CONFIG_REFUSED;
    }
    if (r == U2_CONFIG_OK && *out->themes && !is_dir(out->themes)) {
        snprintf(err, errsz, "%s: [themes] dir = %s is not a directory", path, out->themes);
        r = U2_CONFIG_REFUSED;
    }

    free(buf);
    return r;
}

void u2_config_apply(u2_ctx *ctx, const u2_config *cfg)
{
    if (!ctx || !cfg || !cfg->present)
        return;

    /* Flag first, always: this runs after flag parsing and touches only what the
     * command line left unset. */
    if ((!ctx->data_dir || !*ctx->data_dir) && *cfg->data)
        ctx->data_dir = cfg->data;
    if ((!ctx->save_dir || !*ctx->save_dir) && *cfg->saves)
        ctx->save_dir = cfg->saves;
    if ((!ctx->ega_theme_dir || !*ctx->ega_theme_dir) && *cfg->themes)
        ctx->ega_theme_dir = cfg->themes;
    if ((!ctx->ega_theme || !*ctx->ega_theme) && *cfg->theme)
        ctx->ega_theme = cfg->theme;
    /* Sound is the one setting the game writes back, so the flag has to be set here
     * as well as read: a player who turned it off and came back must not hear it
     * again. A command-line override does not exist, so there is no precedence to
     * preserve — the file either says `off` or it does not. */
    if (!strcmp(cfg->sound, "off"))
        ctx->sound_off = 0xff;
}
