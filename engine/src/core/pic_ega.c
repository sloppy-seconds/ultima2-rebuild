/*
 * pic_ega.c — read one of the fan pack's EGA full-screen pictures into the frame.
 *
 * See pic_ega.h for the format evidence and the copyright posture. The whole
 * decode is a linear copy: 64000 bytes, one 0..15 index per byte, top-down,
 * which is why there is no interleave handling here the way core/pic.c has for
 * the CGA planar files.
 */

#include <stdio.h>
#include <string.h>

#include "core/pic_ega.h"

#include "core/ctx.h"
#include "core/artifact.h"   /* a release artifact does not know about references/ */
#include "core/frame.h"
#include "core/roots.h"
#include "debug/trace.h"

#define PIC_EGA_BYTES (U2_SCREEN_H * U2_SCREEN_W)   /* 200 * 320 * 1 = 64000 */

/* Build one candidate filename from a lowercase base ("picdra") and a suffix
 * ("-E" or ".EGA"), uppercasing the base when `upper` is set. The pack spells
 * both the base and the extension differently across copies, and a FAT volume
 * can uppercase the whole name, so the caller tries all four combinations. */
static void make_name(char *out, size_t n, const char *base,
                      const char *suffix, int upper)
{
    char buf[40];
    size_t i = 0;
    for (; base[i] && i + 1 < sizeof buf; i++) {
        const char c = base[i];
        buf[i] = (upper && c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    buf[i] = '\0';
    snprintf(out, n, "%s%s", buf, suffix);
}

/* Read exactly PIC_EGA_BYTES from `path` into f if the file is that size.
 * Returns 0 on success, 1 if absent, 2 if present but the wrong size. */
static int try_pic(const char *path, struct u2_frame *f)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 1; }
    const long sz = ftell(fp);
    rewind(fp);
    if (sz < (long)PIC_EGA_BYTES) { fclose(fp); return 2; }

    /* A short read is refused, not zero-padded: a half-loaded splash would draw a
     * plausible picture with a band of black across it, which reads as the game
     * rather than as a missing file (the same rule core/pic.c applies).
     * The row lands in a scratch line first: the frame is 320 px wide only at
     * scale 1, and a large-tile theme composites at 640. */
    uint8_t rowbuf[U2_SCREEN_W];
    for (int y = 0; y < U2_SCREEN_H; y++) {
        if (fread(rowbuf, 1, U2_SCREEN_W, fp) != (size_t)U2_SCREEN_W) {
            fclose(fp);
            return 2;
        }
        for (int x = 0; x < U2_SCREEN_W; x++)
            /* Clamp any stray byte above 15 into range so a corrupt file cannot
             * index past the 16-entry palette in the renderer. */
            u2_frame_put_logical(f, x, y, (uint8_t)(rowbuf[x] & 15));
    }
    fclose(fp);
    return 0;
}

/* The directories an EGA picture may live in, in precedence order.
 *
 * The explicit themes dir first, because that is where a hand-placed picture
 * goes and it is the narrowest scope. Then EVERY content root, which means the
 * themes dir plus the content root plus its `data/updated`, `updated` and
 * `data/original` sublayers, from core/roots.h.
 *
 * The sublayers are the whole point of this function and the reason it used to be
 * wrong. It read `ctx->data_dir`, which is the content root and nothing else, so
 * in an installed tree (`[paths] data = .`, pictures under `data/original/`) the
 * pack's EGA pictures were invisible: the three that also happen to sit in
 * `themes/` resolved and the rest fell back to CGA, which then drew through the
 * EGA palette and looked like noise. Owner-reported 2026-11-20 as "the demo
 * pictures are messed up in EGA mode"; the dragon was fine for no reason other
 * than that the pack copied it into `themes/` too. roots.h already says pic.c
 * asks here, and pic_ega.c was the one file that did not.
 *
 * The dev-checkout fallback stays last, and stays absent from a release artifact.
 */
#define PIC_MAX_DIRS (1 + U2_ROOT_MAX + 2)

static int candidate_dirs(u2_ctx *ctx, char dirs[PIC_MAX_DIRS][448])
{
    int nc = 0;
    if (ctx && ctx->ega_theme_dir && *ctx->ega_theme_dir)
        snprintf(dirs[nc++], 448, "%.400s", ctx->ega_theme_dir);
    for (size_t r = 0; r < u2_root_count && nc < PIC_MAX_DIRS; r++)
        u2_root_path(dirs[nc++], 448, ctx, u2_roots[r].sub);
    /* A release artifact does not know where this repository keeps its copy of the
     * upgrade package; core/artifact.h gives the reason. */
    if (!u2_is_release_artifact() && nc + 1 < PIC_MAX_DIRS) {
        snprintf(dirs[nc++], 448, "../references/u2upgrade");
        snprintf(dirs[nc++], 448, "references/u2upgrade");
    }
    return nc;
}

/* Try every (dir, name-form) pair; call `each` on each path. Returns the first
 * path that `each` accepts (0), or NULL. Shared by can_show and blit so the two
 * cannot disagree about which file counts as present. */
typedef int (*pic_probe)(const char *path, void *user);

static const char *first_pic(u2_ctx *ctx, const char *base,
                             pic_probe each, void *user,
                             char *pathbuf, size_t bufsz)
{
    char dirs[PIC_MAX_DIRS][448];
    const int nc = candidate_dirs(ctx, dirs);
    static const char *const suffixes[2] = { "-E", ".EGA" };

    for (int d = 0; d < nc; d++) {
        for (int s = 0; s < 2; s++) {
            for (int up = 0; up < 2; up++) {
                char name[48];
                make_name(name, sizeof name, base, suffixes[s], up);
                if (snprintf(pathbuf, bufsz, "%.447s/%s", dirs[d], name) >= (int)bufsz)
                    continue;
                if (each(pathbuf, user) == 0)
                    return pathbuf;
            }
        }
    }
    return NULL;
}

static int probe_readable(const char *path, void *user)
{
    (void)user;
    FILE *fp = fopen(path, "rb");
    if (!fp) return 1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 1; }
    const long sz = ftell(fp);
    fclose(fp);
    return sz >= (long)PIC_EGA_BYTES ? 0 : 1;
}

static int probe_blit(const char *path, void *user)
{
    return try_pic(path, (struct u2_frame *)user);
}

int u2_pic_can_show_ega(u2_ctx *ctx, const char *base)
{
    if (!base) return 0;
    char path[600];
    return first_pic(ctx, base, probe_readable, NULL, path, sizeof path) ? 1 : 0;
}

int u2_pic_blit_ega(u2_ctx *ctx, const char *base, struct u2_frame *f)
{
    if (!ctx || !base || !f) return 1;
    char path[600];
    /* Blit into a scratch frame first so a failure partway leaves f untouched,
     * matching core/pic.c's contract that a refused blit does not corrupt f. */
    static struct u2_frame scratch;
    /* The scratch has to be the same size as the destination, and a Ctrl-T cycle
     * can change the frame scale between two blits, so a scale change re-allocs
     * rather than reusing a scratch that would then be half the picture. */
    if (u2_frame_ensure(&scratch, f->scale ? f->scale : 1) != 0)
        return 1;
    if (!first_pic(ctx, base, probe_blit, &scratch, path, sizeof path)) {
        u2_trace(ctx, "pic_ega", "-", "base=%s loaded=0", base);
        return 1;
    }
    for (int y = 0; y < f->h && y < scratch.h; y++)
        memcpy(f->px[y], scratch.px[y], (size_t)(f->w < scratch.w ? f->w : scratch.w));
    u2_trace(ctx, "pic_ega", "-", "base=%s loaded=1 bytes=%d", base, PIC_EGA_BYTES);
    return 0;
}
