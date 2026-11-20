/*
 * roots.c — see roots.h. The table moved out of world.c intact; the ordering and
 * the meaning of `.patched` are unchanged, and the comment that explains them
 * travelled with it rather than being rewritten.
 */

#define _POSIX_C_SOURCE 200809L   /* stat() under -std=c11 */

#include "core/roots.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "core/ctx.h"

const u2_root u2_roots[] = {
    { "data/updated",  1 },
    { "updated",       1 },
    { "data/original", 0 },
    { "",              0 },
};
const size_t u2_root_count = sizeof u2_roots / sizeof u2_roots[0];

/* See U2_ROOT_MAX in roots.h: a caller sizes a fixed array from it, so a table
 * that outgrew the ceiling must fail here and not drop a root at runtime. */
_Static_assert(sizeof u2_roots / sizeof u2_roots[0] <= U2_ROOT_MAX,
               "u2_roots grew past U2_ROOT_MAX; raise it in roots.h");

const char *u2_content_root(const u2_ctx *ctx)
{
    return (ctx && ctx->data_dir && *ctx->data_dir) ? ctx->data_dir : ".";
}

void u2_root_path(char *buf, size_t bufsz, const u2_ctx *ctx, const char *sub)
{
    if (sub && *sub)
        snprintf(buf, bufsz, "%.400s/%.100s", u2_content_root(ctx), sub);
    else
        snprintf(buf, bufsz, "%.400s", u2_content_root(ctx));
}

static int path_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && !S_ISDIR(st.st_mode);
}

int u2_root_find(const u2_ctx *ctx, const char *name, int release_only,
                 char *out, size_t bufsz)
{
    for (size_t r = 0; r < u2_root_count; r++) {
        if (release_only && u2_roots[r].patched)
            continue;
        char dir[448];
        u2_root_path(dir, sizeof dir, ctx, u2_roots[r].sub);
        if (snprintf(out, bufsz, "%.447s/%.127s", dir, name) >= (int)bufsz)
            return 0;
        if (path_exists(out))
            return 1;
    }
    out[0] = '\0';
    return 0;
}

int u2_root_find_any(const u2_ctx *ctx, const char *const *names, size_t count,
                     int release_only, char *out, size_t bufsz)
{
    for (size_t i = 0; i < count; i++)
        if (u2_root_find(ctx, names[i], release_only, out, bufsz))
            return 1;
    out[0] = '\0';
    return 0;
}
