/*
 * themes.h — building a themes/ tree out of the player's own EGA art. This is the
 * v1 reshape, and it is the only theme builder: the Python tool that used to do it
 * was deleted rather than kept beside this one, because two builders of the same
 * format is two definitions of it, and the format is a contract with the engine
 * (docs/theme-format.md) that only gets one owner.
 *
 * The rules it implements, each learned the expensive way and recorded in
 * docs/archive/handoff-themes-builder.md:
 *
 *   * A theme is a directory whose CONTENTS include a tile atlas. Not one named
 *     `EGATHEME.*` — keying discovery on the vendor's directory name produced a
 *     tree with no themes in it, and the engine then refused every theme by name
 *     while the builder reported success.
 *   * Copy WHOLE theme directories. A theme is EGATILES plus an optional EGACOLOR
 *     plus optional PIC*.EGA, and the optionality is per-theme; selective copying
 *     silently drops a theme's own extras and you get a half-recoloured game.
 *   * Emit every file byte-identical. Nothing here decodes or re-encodes art.
 *   * A build is not done until it has been verified against what it wrote.
 *
 * What is NOT here: the FM Towns v2 derivation. That needs the disc, an ISO reader
 * and the crosswalk, and it was deferred by ruling 4 of docs/install-design.md § 11
 * rather than abandoned; u2_themes_check still verifies a themes/fmtowns tree that
 * something else produced, and docs/fmtowns-graphics.md plus
 * disasm/fmtowns-crosswalk.tsv remain the record of how it is derived.
 */

#ifndef U2_CORE_THEMES_H
#define U2_CORE_THEMES_H

#include <stddef.h>
#include <stdio.h>

#define U2_THEMES_MAX_SOURCES 4
#define U2_THEMES_MANIFEST    "_BUILD.tsv"

typedef struct {
    const char *out;                            /* the themes directory to write */
    const char *sources[U2_THEMES_MAX_SOURCES]; /* art packs; first one wins */
    int n_sources;
} u2_themes_request;

/* 0 on success, non-zero on refusal or failure. `report` receives one line per
 * file and per problem; `err` receives the reason a refusal happened. */
int u2_themes_build(const u2_themes_request *rq, FILE *report, char *err, size_t errsz);

/* Verify an existing themes tree against its own _BUILD.tsv: every row present and
 * byte-identical, every theme directory holding an atlas, and no file the manifest
 * does not account for. Needs no sources, which is what makes `--install --check`
 * work on a tree whose media has since been unplugged. */
int u2_themes_check(const char *out, FILE *report, char *err, size_t errsz);

/* Add every file under `subdir` (relative to the themes root, e.g. "fmtowns") to
 * the themes manifest, rewriting it with the new rows merged in sorted order.
 *
 * This exists so a derived theme — one the installer generated rather than copied
 * from an art pack — is covered by the same manifest as the copied ones. Without
 * it, `--install --fmtowns-media-dir` with no `--themes-dir` would leave a themes
 * tree that `--check` refuses for having no manifest, which is a hole in the one
 * guarantee the manifest exists to provide. It is idempotent by construction: rows
 * already present for a path are left alone, so re-running does not accumulate. */
int u2_themes_record(const char *themes_dir, const char *subdir,
                     const char *origin, char *err, size_t errsz);

/* Exposed for the gate: classify one filename the way the builder does. Returns
 * the role as a short string ("atlas", "color", "picture", "ignore", "bad-*"). */
const char *u2_themes_classify(const char *name, long size);

#endif
