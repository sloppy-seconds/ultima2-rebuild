/* turnlog.c — per-turn frame + state dump. See turnlog.h for why this exists. */
#include "debug/turnlog.h"
#include "core/compat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef _POSIX_C_SOURCE         /* mkdir under -std=c11, same stated exception
                                 * core/save.c makes. Guarded because the SDL build
                                 * passes -D_GNU_SOURCE, which already pulls in a
                                 * _POSIX_C_SOURCE from features.h and an unguarded
                                 * define is a redefinition, and -Werror makes that
                                 * a build failure that silently leaves the old
                                 * binary in place. */
#define _POSIX_C_SOURCE 200809L
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include "debug/dump_state.h"

static int warn_once;

static void warn(const char *dir, int err)
{
    if (warn_once) return;
    warn_once = 1;
    fprintf(stderr, "u2: --debug cannot write into %s (%s); logging disabled\n",
            dir, strerror(err));
}

void u2_turnlog(const u2_ctx *ctx, const u2_frame *frame, unsigned turn)
{
    char path[512];
    char json[U2_STATE_JSON_MAX];

    if (!ctx->debug_cli || ctx->debug_path == NULL || frame == NULL) return;

    /* The cap says so once and then goes quiet, rather than either filling the disk
     * or stopping silently -- a silent stop would look like a hung logger. */
    if (ctx->debug_limit != 0 && turn > ctx->debug_limit) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "u2: --debug stopped after %u turns (--debug-limit, "
                            "0 lifts the cap)\n", ctx->debug_limit);
        }
        return;
    }

    /* The launch directory is <base>/<timestamp>, so the base may itself be
     * missing. Create the parent first and ignore its failure: if the full path
     * still cannot be made, the error below is the one worth reporting. */
    {
        const char *slash = strrchr(ctx->debug_path, '/');
        if (slash && slash != ctx->debug_path) {
            char parent[512];
            size_t n = (size_t)(slash - ctx->debug_path);
            if (n < sizeof parent) {
                memcpy(parent, ctx->debug_path, n);
                parent[n] = '\0';
                mkdir(parent, 0755);
            }
        }
    }
    if (mkdir(ctx->debug_path, 0755) != 0 && errno != EEXIST) {
        warn(ctx->debug_path, errno);
        return;
    }

    snprintf(path, sizeof path, "%s/turn-%05u.ppm", ctx->debug_path, turn);
    uint8_t pal[4][3];
    u2_frame_palette(pal);
    if (u2_frame_write_ppm(frame, path, &pal[0][0]) != 0) {
        warn(ctx->debug_path, errno);
        return;
    }

    /* Same builder --dump-state uses, so a turn log and an end-of-run dump are
     * the same schema and a harness can read either without a second parser. */
    if (u2_state_json(ctx, json, sizeof json) > 0) {
        snprintf(path, sizeof path, "%s/turn-%05u.json", ctx->debug_path, turn);
        FILE *fh = fopen(path, "wb");
        if (fh) {
            fputs(json, fh);
            fclose(fh);
        } else {
            warn(ctx->debug_path, errno);
        }
    }
}
