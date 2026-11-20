/*
 * roots.h — the content roots, in precedence order. One list, one owner.
 *
 * docs/install-design.md § 3 rules the layout: `--install` writes explicit layers
 * (`data/updated` over `data/original`) and the drop-in case has the same two
 * layers at the game root (`updated` over `.`). The mechanism that makes both work
 * is an ordered root list rather than a branch on "which layout is this", and the
 * moment two files each carry their own copy of that list, they will disagree —
 * which is the failure mode this project keeps hitting and keeps naming.
 *
 * So world.c, tiles.c and pic.c all ask here. world.c needs the roots paired with
 * per-kind filename prefixes and so uses the table directly; the rest want the
 * simpler question, "where is this file", which is u2_root_find().
 */

#ifndef U2_CORE_ROOTS_H
#define U2_CORE_ROOTS_H

#include <stddef.h>

typedef struct u2_ctx u2_ctx;

typedef struct {
    const char *sub;      /* subdirectory of the content root; "" is the root */
    int patched;          /* 1 for a Galaxy patch layer, 0 for a release layer */
} u2_root;

/* Compile-time ceiling on the rows in u2_roots, so a caller that needs a fixed
 * array of directories can size one. roots.c static_asserts the table fits, so
 * growing the table without raising this fails the build rather than dropping a
 * root silently -- which is the difference between "the picture was not found"
 * and "the picture was never looked for". */
#define U2_ROOT_MAX 4

extern const u2_root u2_roots[];
extern const size_t  u2_root_count;

/* The content root itself: --data-dir, or "." when unset. The original's FCB drive
 * byte is 0 — the default drive — so this is the directory the game plays in. */
const char *u2_content_root(const u2_ctx *ctx);

/* The directory one root resolves to, with no doubled slash when the root is the
 * content root itself. */
void u2_root_path(char *buf, size_t bufsz, const u2_ctx *ctx, const char *sub);

/* The first existing file named `name` under any root, in root order. Returns 1
 * and writes the path, or 0 and leaves `out` empty. `release_only` restricts the
 * search to release layers, which is what content with no Galaxy spelling wants:
 * a patch layer is not where an unpatched filename lives. */
int u2_root_find(const u2_ctx *ctx, const char *name, int release_only,
                 char *out, size_t bufsz);

/* As above but trying each name in a NULL-terminated list, so a caller that
 * carries case spellings gets one loop instead of two. */
int u2_root_find_any(const u2_ctx *ctx, const char *const *names, size_t count,
                     int release_only, char *out, size_t bufsz);

#endif
