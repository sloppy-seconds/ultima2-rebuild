/*
 * install.h — building a game directory out of a pile of media.
 * docs/install-design.md is the design; this is its § 2, § 3, § 6 and § 8.
 *
 * This is a tool mode, not a game mode: it runs before video init, needs no
 * renderer, and lives in core/ rather than platform/ so that the terminal build
 * the whole test suite drives can execute it. That is not a preference — a
 * shipped installer nobody can run in CI is an installer nobody can prove.
 *
 * Three rules shape everything below, and each exists because the project has
 * been burned by its absence:
 *
 *   1. NEVER write into a source. The player's download comes out untouched, and
 *      `--check` can prove it because the manifest records sources by path and
 *      hash without ever opening them for writing.
 *   2. Copy verbatim or do not copy. No renames, no case-fixing, no running the
 *      fan patch's RENGAL.BAT. A transformation is a claim, and claims need
 *      evidence; the engine's own layer precedence (core/world.c) is the evidence
 *      that verbatim is enough.
 *   3. Verify by re-derivation, not by "the copy returned 0". Every file written
 *      is hashed back from disk against the hash of its source, and the manifest
 *      is what a later `--check` re-derives against.
 */

#ifndef U2_CORE_INSTALL_H
#define U2_CORE_INSTALL_H

#include <stddef.h>
#include <stdio.h>

#include "core/config.h"
#include "core/themes.h"

typedef enum {
    U2_INSTALL_OK      = 0,   /* did the work, or the tree already matches */
    U2_INSTALL_REFUSED = 1,   /* a rule stopped it: foreign target, bad source */
    U2_INSTALL_ERROR   = 2    /* I/O, or the impossible */
} u2_install_result;

/* The manifest, at the target root. One row per file the installer wrote, with
 * the source it came from and both sizes and hashes. It is what makes a target
 * recognisable as ours (D7a: empty installs, manifested completes, foreign
 * non-empty refuses) and what `--check` re-derives against. Same device as
 * themes/_BUILD.tsv, for the same reason: emit, record, re-derive, diff. */
#define U2_INSTALL_MANIFEST "_INSTALL.tsv"

/* The layout, fixed by the § 3 ruling and by docs/save-scoped-worlds.md. The
 * engine finds these through its ordered content roots, so the installer writes
 * them and does not have to explain them. `saves` is created by migration or by
 * the engine, never seeded: a slot that holds nothing is a lie about a game. */
#define U2_INSTALL_DATA_ORIG  "data/original"
#define U2_INSTALL_DATA_UPD   "data/updated"
#define U2_INSTALL_SAVES      "saves"
#define U2_INSTALL_THEMES     U2_CONFIG_THEMES_DIR
#define U2_INSTALL_CONFIG     "config.ini"

typedef struct {
    const char *to;            /* the target game directory (required) */
    const char *original_dir;  /* the DOS release: an IA copy, a CD rip, a DOSBox dir */
    const char *updated_dir;   /* the Galaxy patch layer */
    /* The fan art packs. More than one because no single source is complete: the
     * upgrade pack ships the atlases and the themes, and the Galaxy directory is
     * the only source with the full PIC*-E picture set. First source wins per
     * destination, and the shadow is reported. */
    const char *themes_dir[U2_THEMES_MAX_SOURCES];
    int n_themes_dir;
    /* The FM Towns disc image, or a directory holding one. When set, themes/fmtowns
     * is derived from it in the same run (core/fmtowns.c): the art comes from the
     * player's disc and the mapping is compiled into the binary, so neither the
     * repository nor Python is needed to produce it. */
    const char *fmtowns_media;
    /* Plan and report, write nothing. The interactive installer shows this before
     * asking for permission, and it is what makes a mistake cheap to see. */
    int dry_run;
    /* Path to the running executable, normally argv[0]. It is the one exception to
     * "the target must be empty": a player who drops this binary into a new folder
     * and runs it should not have to move it out first. Compared by device and
     * inode, not by name, so a copy sitting elsewhere does not qualify. */
    const char *self_path;
} u2_install_request;

/* Storage for the answers a prompt collects. The caller owns the bytes; the
 * request points into them, so they must outlive the install call. */
typedef struct {
    char to[1024];
    char original[1024];
    char themes[1024];
    char fmtowns[1024];
} u2_install_answers;

/* Fill in whatever the command line did not supply by asking, show the plan, and
 * ask for permission to write. Only ever called when stdin is a terminal and
 * --no-prompt was not given: a piped or redirected stdin is a script, and a script
 * that blocks on a question it cannot answer is the failure this gate exists to
 * prevent. Returns 1 to proceed, 0 if the player declined, -1 to refuse. */
int u2_install_prompt(u2_install_request *rq, u2_install_answers *ans,
                      FILE *out, char *err, size_t errsz);

/* Where --install should look for the paths it recorded on a previous run.
 *
 * The rule is "keep the binary beside the game and --install needs no arguments",
 * so the candidates are, in order: an explicit --config, `config.ini` in the
 * directory being run from, and `config.ini` beside the running executable
 * (resolved through PATH and symlinks, because argv[0] is often bare). Writes
 * `out` with the candidate that exists, or with the plain default and returns 0
 * when none does — a tree that was never installed has no opinion, which is not an
 * error. Returns 1 when a file was found. */
int u2_install_find_config(const char *explicit_path, const char *self_path,
                           char *out, size_t n);

/* Fill whatever the request leaves unset from config.ini's [install] section.
 *
 * Precedence is the engine's standing one and nothing else: flag > config.ini >
 * prompt. A recorded source that no longer exists is reported and left unset, so
 * the prompt asks for it or --no-prompt refuses; it is never silently dropped, and
 * an install that quietly skipped the theme source it used last time would be a
 * different tree wearing the same manifest. */
void u2_install_fill_from_config(u2_install_request *rq, const u2_config *cfg,
                                 FILE *out);

/* Install, or complete an install that already carries our manifest. With
 * themes_dir supplied, themes/ is built in the same run by core/themes.c. */
u2_install_result u2_install_apply(const u2_install_request *rq, FILE *out,
                                   char *err, size_t errsz);

/* Report on a tree without writing anything. With no manifest it says so and
 * refuses to guess; with one it verifies every row. Since sector writes moved into
 * the save slot there is no tolerated difference any more: content is immutable
 * after install, so a sector file that differs from its manifest row is a defect
 * and not a state, and the message names --install as the repair. */
u2_install_result u2_install_check(const char *to, FILE *out, char *err, size_t errsz);

/* Exposed for the test: is this filename game content at all, and which layer
 * does its spelling belong to? Returns 0 for anything the installer must not
 * copy — patcher binaries, READMEs, DOS launcher scaffolding. */
int u2_install_classify(const char *basename, int *galaxy_spelling);

#endif
