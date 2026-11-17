/*
 * slot.c — see slot.h. The path arithmetic for one save game, and nothing else.
 *
 * It is its own file rather than a few statics inside save.c because two other
 * files need the same answer and must not be allowed to invent their own: world.c
 * reads its overlay from here and writes its sectors here, and the installer
 * migrates a flat save into this shape. "Where does a save live" existing twice
 * is how a player's world changes end up beside the release files again, which is
 * the defect docs/save-scoped-worlds.md was written to close.
 */

#define _POSIX_C_SOURCE 200809L   /* stat, mkdir under -std=c11 */

#include "core/slot.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "core/compat.h"
#include "core/ctx.h"

const char *u2_slot_name(const u2_ctx *ctx)
{
    (void)ctx;                 /* no --save flag yet (D5); this is where it lands */
    return U2_SAVE_SLOT_DEFAULT;
}

void u2_save_dir(const u2_ctx *ctx, char *buf, size_t bufsz)
{
    if (ctx && ctx->save_dir && *ctx->save_dir)
        snprintf(buf, bufsz, "%.400s", ctx->save_dir);
    else
        snprintf(buf, bufsz, "%s", U2_SAVE_DEFAULT_DIR);
}

/* Every snprintf below bounds each field explicitly, because the Makefile builds
 * with -Werror and gcc's format-truncation check only accepts the arithmetic when
 * it can bound every part of it. A slot name is at most 8 characters and a save
 * directory at most 400, which is well past any real path here and small enough
 * for gcc to prove. */
void u2_slot_dir(const u2_ctx *ctx, char *buf, size_t bufsz)
{
    char dir[430];
    u2_save_dir(ctx, dir, sizeof dir);
    snprintf(buf, bufsz, "%.429s/%.8s", dir, u2_slot_name(ctx));
}

/* The one repair the engine is allowed to make to a save directory.
 *
 * An older build kept the record as a FILE at `<save_dir>/PLAYER`, which is
 * exactly where the slot directory has to go, so until it moves nothing at all can
 * be saved — and a player who upgraded the binary without running --install would
 * find a game that cannot write, in the drop-in layout where there is no installer
 * to run. So this adopts it: two renames inside one directory, never an overwrite,
 * and it never touches the content tree.
 *
 * That last clause is why the repair of played-in CONTENT files stays in the
 * installer and does not come here: telling a release map from a stamped one needs
 * _INSTALL.tsv, and the engine has no such record. Moving a record needs nothing
 * but the fact that a directory cannot take a file's name while the file holds it.
 *
 * If the intermediate name already exists, a previous move was interrupted in a way
 * this code cannot reason about, so it refuses and leaves everything alone rather
 * than guessing. The installer's migration is the tool for that tree.
 */
static int adopt_flat_record(const char *dir, const char *slot)
{
    char tmp[456], rec[456];
    struct stat st;

    snprintf(tmp, sizeof tmp, "%.429s/.PLAYER.migrating", dir);
    if (stat(tmp, &st) == 0)
        return -1;                                  /* half-done: refuse */
    if (rename(slot, tmp) != 0)
        return -1;
    if (mkdir(slot, 0755) != 0 && errno != EEXIST) {
        rename(tmp, slot);                          /* put it back as it was */
        return -1;
    }
    snprintf(rec, sizeof rec, "%.437s/PLAYER", slot);
    if (rename(tmp, rec) != 0) {
        rmdir(slot);
        rename(tmp, slot);
        return -1;
    }
    fprintf(stderr, "[engine] moved the old flat save record into %s\n", rec);
    fflush(stderr);            /* gates merge stderr into stdout and read it in order */
    return 0;
}

int u2_slot_adopt_legacy(const u2_ctx *ctx)
{
    char dir[430], slot[448];
    struct stat st;

    u2_save_dir(ctx, dir, sizeof dir);
    u2_slot_dir(ctx, slot, sizeof slot);
    if (stat(slot, &st) != 0 || S_ISDIR(st.st_mode))
        return 0;                       /* nothing to adopt, and nothing created */
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
        return -1;
    return adopt_flat_record(dir, slot);
}

/* Create every missing component of `dir`. u2_slot_ensure used to mkdir only the last one,
 * so `--save-dir /var/tmp/newthing/save` failed with ENOENT whenever /var/tmp/newthing did
 * not exist, and the caller saw "the save could not be written" -- true but not the reason,
 * and not something a caller can act on. A bot harness hands the engine a fresh scratch
 * directory far more often than a person does, because a harness makes one per run. */
static int mkdir_parents(const char *dir)
{
    char buf[430];
    size_t n = strlen(dir);
    if (n == 0 || n >= sizeof buf) return -1;
    memcpy(buf, dir, n + 1);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

int u2_slot_ensure(const u2_ctx *ctx)
{
    char dir[430], slot[448];
    struct stat st;

    u2_save_dir(ctx, dir, sizeof dir);
    if (stat(dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode))
            return -1;             /* a file where the save directory belongs */
    } else if (mkdir_parents(dir) != 0) {
        return -1;
    }

    u2_slot_dir(ctx, slot, sizeof slot);
    if (stat(slot, &st) == 0) {
        /* A directory is the layout already adopted; a file is the layout that
         * predates slots, and it has to move or nothing can ever be saved. */
        return S_ISDIR(st.st_mode) ? 0 : adopt_flat_record(dir, slot);
    }
    if (mkdir(slot, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

void u2_slot_path(char *buf, size_t bufsz, const u2_ctx *ctx, const char *name)
{
    char dir[448];                        /* u2_slot_dir writes at most 438 */
    u2_slot_dir(ctx, dir, sizeof dir);
    snprintf(buf, bufsz, "%.437s/%.16s", dir, name ? name : "");
}

int u2_slot_find(const u2_ctx *ctx, const char *name, char *out, size_t bufsz)
{
    struct stat st;
    u2_slot_path(out, bufsz, ctx, name);
    if (name && *name && stat(out, &st) == 0 && !S_ISDIR(st.st_mode))
        return 1;
    out[0] = '\0';
    return 0;
}

int u2_slot_rotate(u2_ctx *ctx, char *out, size_t outsz)
{
    char dir[430], slot[448], rec[464], arch[512];
    struct stat st;
    struct tm lt;
    time_t now;
    char stamp[24];

    if (out && outsz > 0)
        out[0] = '\0';

    u2_save_dir(ctx, dir, sizeof dir);
    u2_slot_dir(ctx, slot, sizeof slot);
    if (stat(slot, &st) != 0 || !S_ISDIR(st.st_mode))
        return 0;                   /* no slot at all: there is no world to inherit */

    /* A directory that holds no record is not a game. D6 says an empty slot is a
     * lie about a save, and rotating one would put a timestamped empty directory
     * in the save directory for every launch that never got as far as a save. */
    snprintf(rec, sizeof rec, "%.447s/PLAYER", slot);
    if (stat(rec, &st) != 0 || st.st_size == 0)
        return 0;

    now = time(NULL);
    localtime_r(&now, &lt);
    strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%S", &lt);

    /* Two saves in the same second are possible — a scripted harness does it — so
     * the suffix gets a counter rather than the rename failing or, worse, silently
     * landing on the archive that already exists. */
    int taken = 0;
    for (int n = 0; n < 100; n++) {
        if (n == 0)
            snprintf(arch, sizeof arch, "%.429s/%.8s.%.16s",
                     dir, u2_slot_name(ctx), stamp);
        else
            snprintf(arch, sizeof arch, "%.429s/%.8s.%.16s.%02d",
                     dir, u2_slot_name(ctx), stamp, n);
        if (stat(arch, &st) != 0) { taken = 0; break; }
        taken = 1;
    }
    if (taken)
        return -1;                  /* a hundred archives in one second is not a race */

    if (rename(slot, arch) != 0)
        return -1;

    if (out && outsz > 0)
        snprintf(out, outsz, "%.511s", arch);
    return 1;
}
