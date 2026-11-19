/*
 * config_write.c — the write half of config.ini, kept out of config.c so the
 * reader stays a reader.
 *
 * Why a surgical editor rather than a rewrite: the installer's config.ini is
 * commented on purpose — it shows the player the keys they could set by naming
 * them — and a writer that regenerated the file from the parsed struct would
 * quietly delete that documentation the first time anyone pressed Ctrl-S. So this
 * walks the existing text and touches one line.
 */

#define _POSIX_C_SOURCE 200809L   /* fileno(), fsync(): the durability the rename depends on */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/config.h"
#include "core/ctx.h"

#define U2_CONFIG_MAX_BYTES 65536

/* Does this line set `key`? Returns 1 and sets *commented when the line is
 * `key = ...`, optionally behind one or more leading '#'. The commented form is
 * matched because the installer ships `#theme = alt` as a worked example: a player
 * who picks a theme in game has decided to use that line, and leaving the example
 * commented while appending a second `theme` key would put two values for one key
 * in a file whose loader refuses duplicates. */
static int line_sets_key(const char *line, const char *key, int *commented)
{
    const char *p = line;
    int cmt = 0;
    while (*p == ' ' || *p == '\t') p++;
    while (*p == '#') {
        cmt = 1;
        p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    const size_t n = strlen(key);
    if (strncmp(p, key, n) != 0)
        return 0;
    p += n;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=')
        return 0;
    *commented = cmt;
    return 1;
}

/* Append one line, newline included. Returns 0 on success, 1 if it would not fit. */
static int put_line(char **wp, char *end, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(*wp, (size_t)(end - *wp), fmt, ap);
    va_end(ap);
    if (n < 0 || *wp + n >= end)
        return 1;
    *wp += n;
    return 0;
}

int u2_config_persist(u2_ctx *ctx, const char *section, const char *key,
                      const char *value, char *err, size_t errsz)
{
    if (!ctx || !*ctx->config_path) {
        snprintf(err, errsz, "no config.ini was read, so there is nothing to update");
        return 1;
    }
    return u2_config_set(ctx->config_path, section, key, value, err, errsz);
}

int u2_config_set(const char *path, const char *section, const char *key,
                  const char *value, char *err, size_t errsz)
{
    if (!path || !*path || !section || !*section || !key || !*key || !value) {
        snprintf(err, errsz, "config set: a path, section, key and value are all required");
        return 1;
    }

    /* Read the existing file. Absent is not an error: the setting has to live
     * somewhere, and silently accepting a change that is then lost is the outcome
     * this function exists to prevent. */
    char *text = NULL;
    long have = 0;
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        have = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (have > U2_CONFIG_MAX_BYTES) {
            fclose(f);
            snprintf(err, errsz, "%s: %ld bytes is over the %d the writer will edit",
                     path, have, U2_CONFIG_MAX_BYTES);
            return 1;
        }
        text = calloc((size_t)have + 1, 1);
        if (!text) {
            fclose(f);
            snprintf(err, errsz, "%s: out of memory", path);
            return 1;
        }
        if (have > 0 && fread(text, 1, (size_t)have, f) != (size_t)have) {
            fclose(f);
            free(text);
            snprintf(err, errsz, "%s: read failed", path);
            return 1;
        }
        fclose(f);
    }

    const size_t cap = (size_t)(have > 0 ? have : 0) + 512;
    char *out = calloc(cap, 1);
    if (!out) {
        free(text);
        snprintf(err, errsz, "%s: out of memory", path);
        return 1;
    }
    char *w = out;
    char *end = out + cap - 1;

    int replaced = 0, section_found = 0, in_section = 0;
    /* Where the last non-blank line of the wanted section ended, so a new key lands
     * inside it rather than at the bottom of the file under some other heading. It is
     * recorded after each line is copied, not when the next heading arrives, because
     * the section being edited is often the last one in the file and has no following
     * heading to trigger the recording. */
    size_t insert_at = 0;

    const char *p = text ? text : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        const size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[512];
        if (len >= sizeof line) {
            free(out);
            free(text);
            snprintf(err, errsz, "%s: a line over %zu bytes", path, sizeof line);
            return 1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        p = nl ? nl + 1 : p + len;

        if (line[0] == '[') {
            const size_t n = strlen(section);
            in_section = (size_t)strlen(line) >= n + 2 &&
                         !strncmp(line + 1, section, n) && line[1 + n] == ']';
            if (in_section)
                section_found = 1;
        }

        int commented = 0;
        if (in_section && !replaced && line_sets_key(line, key, &commented)) {
            if (put_line(&w, end, "%s = %s\n", key, value)) {
                free(out); free(text);
                snprintf(err, errsz, "%s: does not fit after the edit", path);
                return 1;
            }
            replaced = 1;
            continue;               /* the old line is gone, commented or not */
        }

        if (put_line(&w, end, "%s\n", line)) {
            free(out); free(text);
            snprintf(err, errsz, "%s: does not fit after the edit", path);
            return 1;
        }
        /* A blank line is left with whatever follows it rather than counted as the
         * section's last line, so an inserted key does not land under a gap. */
        if (in_section && line[0] != '\0')
            insert_at = (size_t)(w - out);
    }

    if (!replaced) {
        char added[256];
        snprintf(added, sizeof added, "%s = %s\n", key, value);
        if (section_found && insert_at <= (size_t)(w - out)) {            const size_t at = insert_at;
            const size_t after = (size_t)(w - out) - at;
            if ((size_t)(w - out) + strlen(added) >= cap - 1) {
                free(out); free(text);
                snprintf(err, errsz, "%s: does not fit after the edit", path);
                return 1;
            }
            memmove(out + at + strlen(added), out + at, after + 1);
            memcpy(out + at, added, strlen(added));
        } else {
            if ((size_t)(w - out) + strlen(added) + 32 >= cap - 1) {
                free(out); free(text);
                snprintf(err, errsz, "%s: does not fit after the edit", path);
                return 1;
            }
            if (*out && out[strlen(out) - 1] != '\n')
                put_line(&w, end, "\n");
            if (!section_found)
                put_line(&w, end, "\n[%s]\n", section);
            put_line(&w, end, "%s", added);
        }
    }

    free(text);

    /* Write beside the file and rename, so a failure leaves the original readable.
     * A half-written config.ini is worse than an old one: the loader refuses a file
     * it cannot parse, and a truncated file is exactly that. */
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.u2tmp", path);
    FILE *g = fopen(tmp, "wb");
    if (!g) {
        free(out);
        snprintf(err, errsz, "%s.u2tmp: cannot be written (%s)", path, strerror(errno));
        return 1;
    }
    const size_t want = strlen(out);
    const size_t got = fwrite(out, 1, want, g);
    const int flushed = fflush(g) == 0 && fsync(fileno(g)) == 0;
    fclose(g);
    free(out);
    if (got != want || !flushed) {
        remove(tmp);
        snprintf(err, errsz, "%s.u2tmp: wrote %zu of %zu bytes", path, got, want);
        return 1;
    }
    if (rename(tmp, path) != 0) {
        const int e = errno;
        remove(tmp);
        snprintf(err, errsz, "%s: cannot be replaced (%s)", path, strerror(e));
        return 1;
    }
    return 0;
}
