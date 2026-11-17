/*
 * install.c — see install.h. The rules are in the header and in
 * docs/install-design.md; this file is their implementation and nothing else.
 */

#define _POSIX_C_SOURCE 200809L   /* dirent, stat, mkdir, rename under -std=c11 */
#define _DEFAULT_SOURCE           /* realpath, for matching the running binary */

#include "core/install.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/compat.h"
#include "core/copy.h"
#include "core/fmtowns.h"
#include "core/sha256.h"
#include "core/slot.h"
#include "core/tiles_ega.h"   /* U2_FMTOWNS_THEME, and the dir-name assert below */

#define MAX_ROWS     1024
#define NAME_LEN     128
#define HASH_LEN     U2_HASH_LEN   /* the width core/copy.h hashes to */
#define MAX_FILE_COPY (8u * 1024u * 1024u)

typedef struct {
    char dst[512];                /* path relative to the target root */
    char src[600];                /* where it came from, as the player wrote it */
    long size;
    char sha[HASH_LEN + 1];
} row;

/* ---------------------------------------------------------------- classify */

/* The picture bases the pack ships, lowercase. */
static const char *const k_pic_bases[] = { "cas", "dng", "dra", "min", "out", "spa", "twn" };

static int two_digits(const char *s)
{
    return isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]);
}

int u2_install_classify(const char *name, int *galaxy_spelling)
{
    char lo[NAME_LEN];
    const size_t n = strlen(name);

    if (galaxy_spelling)
        *galaxy_spelling = 0;
    if (!name || n == 0 || n >= sizeof lo)
        return 0;
    for (size_t i = 0; i < n; i++)
        lo[i] = (char)tolower((unsigned char)name[i]);
    lo[n] = '\0';

    /* The Galaxy spellings are what the engine's patch layer is consulted for, so
     * they are also what decides which layer a file belongs in. */
    if (n == 6 && two_digits(lo + 4) &&
        (!strncmp(lo, "mapg", 4) || !strncmp(lo, "mong", 4) || !strncmp(lo, "tlkg", 4))) {
        if (galaxy_spelling)
            *galaxy_spelling = 1;
        return 1;
    }
    if (n == 6 && two_digits(lo + 4) &&
        (!strncmp(lo, "mapx", 4) || !strncmp(lo, "monx", 4) || !strncmp(lo, "tlkx", 4)))
        return 1;

    if (!strcmp(lo, "monsters") || !strcmp(lo, "player") || !strcmp(lo, "ultimaii.exe"))
        return 1;

    /* piccas / PICDNG / PIC*-E and friends: the sidecar art the pack ships beside
     * the maps. Both the plain and the -E spelling are content. */
    if (!strncmp(lo, "pic", 3) && (n == 6 || (n == 8 && !strncmp(lo + 6, "-e", 2))))
        for (size_t i = 0; i < sizeof k_pic_bases / sizeof k_pic_bases[0]; i++)
            if (!strncmp(lo + 3, k_pic_bases[i], 3))
                return 1;

    return 0;
}

/* ------------------------------------------------------------------ files
 * The primitives are core/copy.c, shared with the theme builder so the two
 * builders cannot disagree about what "copied" means. */

static long file_size(const char *p) { return u2_file_size(p); }

static int is_dirpath(const char *p) { return u2_is_dir(p); }

static int hash_file(const char *p, char out[HASH_LEN + 1], long *out_size)
{
    return u2_hash_file(p, out, out_size);
}

static int mkdir_p(const char *path) { return u2_mkdir_p(path); }

/* Copy src to dst byte-for-byte, then prove it by hashing the destination back.
 * A copy that "succeeded" and landed wrong is the failure this whole tool is built
 * to refuse, so the proof is not optional diagnostics — it is the return value. */
static int copy_verified(const char *src, const char *dst, long want_size,
                         const char want_sha[HASH_LEN + 1])
{
    return u2_copy_verified(src, dst, want_size, want_sha);
}

/* ------------------------------------------------------------------ target */

typedef enum { T_ABSENT, T_EMPTY, T_MINE, T_SELFONLY, T_FOREIGN, T_NOTDIR } t_state;

/* Is this directory's only content the running binary? That is the one exception
 * to "the target must be empty", and it exists for the shape people actually use:
 * make a folder, drop the engine in it, run --install. Matching by device and inode
 * rather than by name is what keeps a same-named file somewhere else from qualifying,
 * and a subdirectory disqualifies, because a subdirectory is content. */
static int only_self_binary(const char *dir, const char *self)
{
    if (!self || !*self)
        return 0;
    struct stat ss;
    if (stat(self, &ss) != 0) {
#ifdef _WIN32
        return 0;
#else
        /* argv[0] is often bare when the binary came from PATH, so resolve it before
         * comparing; a failed stat here just means no exception applies. */
        char resolved[1024];
        if (!realpath(self, resolved) || stat(resolved, &ss) != 0)
            return 0;
#endif
    }
    if (!S_ISREG(ss.st_mode))
        return 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    int only = 1, seen = 0;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char full[1024];
        snprintf(full, sizeof full, "%.890s/%.127s", dir, e->d_name);
        struct stat fs;
        if (stat(full, &fs) != 0 || !S_ISREG(fs.st_mode) ||
            fs.st_dev != ss.st_dev || fs.st_ino != ss.st_ino) {
            only = 0;
            break;
        }
        seen++;
    }
    closedir(d);
    return only && seen > 0;
}

static int dir_is_empty(const char *to)
{
    DIR *d = opendir(to);
    if (!d)
        return 0;
    struct dirent *e;
    int empty = 1;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        empty = 0;
        break;
    }
    closedir(d);
    return empty;
}

static t_state inspect_target(const char *to, const char *self)
{
    if (!is_dirpath(to)) {
        struct stat st;
        if (stat(to, &st) == 0)
            return T_NOTDIR;
        return T_ABSENT;
    }
    if (dir_is_empty(to))
        return T_EMPTY;
    char m[1024];
    snprintf(m, sizeof m, "%.1000s/%s", to, U2_INSTALL_MANIFEST);
    if (file_size(m) > 0)
        return T_MINE;
    return only_self_binary(to, self) ? T_SELFONLY : T_FOREIGN;
}

/* ---------------------------------------------------------------- manifest */

static void manifest_path(char *out, size_t n, const char *to)
{
    snprintf(out, n, "%.599s/%s", to, U2_INSTALL_MANIFEST);
}

static int manifest_write(const char *to, const row *rows, int n)
{
    char path[1024], tmp[1024];
    manifest_path(path, sizeof path, to);
    snprintf(tmp, sizeof tmp, "%.1010s.u2tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    fprintf(f, "# u2 install manifest, format 1\n");
    fprintf(f, "# dst\tkind\tsource\tsize\tsha256:16\n");
    for (int i = 0; i < n; i++)
        fprintf(f, "%s\tcontent\t%s\t%ld\t%s\n", rows[i].dst, rows[i].src,
                rows[i].size, rows[i].sha);
    if (fclose(f) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* Read the manifest back. Returns the row count, or -1 if it cannot be read. */
static int manifest_read(const char *to, row *rows, int max)
{
    char path[1024];
    manifest_path(path, sizeof path, to);
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof line, f) && n < max) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char *dst = strtok(line, "\t");
        char *kind = strtok(NULL, "\t");
        char *src = strtok(NULL, "\t");
        char *size = strtok(NULL, "\t");
        char *sha = strtok(NULL, "\t");
        if (!dst || !kind || !src || !size || !sha)
            continue;
        snprintf(rows[n].dst, sizeof rows[n].dst, "%.*s", 511, dst);
        snprintf(rows[n].src, sizeof rows[n].src, "%.*s", 599, src);
        rows[n].size = strtol(size, NULL, 10);
        snprintf(rows[n].sha, sizeof rows[n].sha, "%.*s", HASH_LEN, sha);
        /* strip the trailing newline that the last field carries */
        rows[n].sha[strcspn(rows[n].sha, "\r\n")] = '\0';
        n++;
    }
    fclose(f);
    return n;
}

/* Is a manifest destination one of the sector files the game rewrites as you
 * play? It no longer decides what --check tolerates: content is immutable after
 * install (docs/save-scoped-worlds.md), so a sector file that differs is a defect
 * and not a state to accept. It survives because two messages still need to tell
 * a player *why* a difference is tolerable nowhere and what repairs it, and
 * because the migration below has to know which files a played-in world dirtied. */
static int is_writable_sector(const char *dst)
{
    const char *base = strrchr(dst, '/');
    base = base ? base + 1 : dst;
    return !strncasecmp(base, "mapx", 4) || !strncasecmp(base, "mapg", 4) ||
           !strncasecmp(base, "monx", 4) || !strncasecmp(base, "mong", 4);
}

/* --------------------------------------------------------------- migration
 *
 * docs/save-scoped-worlds.md § 4. An existing install has `saves/PLAYER` as a
 * FILE, and the new layout needs `saves/PLAYER/` as a directory, so the two
 * cannot coexist: this is the one place that can move a player's record, and the
 * one place allowed to, because it is the only place that can say what the file
 * is. The engine never moves a save; it refuses.
 *
 * Four rules, and the last one outranks the others:
 *
 *   - move, never copy, and never overwrite. A rename that would land on an
 *     existing file is refused rather than resolved.
 *   - print every move, and print the plan under --dry-run, because the pre-flight
 *     is the only chance to catch a migration that would move the wrong thing.
 *   - idempotent: a second run finds nothing to do.
 *   - a real save is never deleted. Anything this code cannot prove is the release
 *     blank becomes its own slot, even when that leaves it unselectable. Losing a
 *     game to a cleanup is the failure the whole document exists to prevent.
 *
 * A crash between the two renames of step 1 leaves the record under a temporary
 * name in the same directory; the next run puts it back and does the move
 * properly, so an interrupted migration is recoverable rather than half-gone.
 */

#define MIGRATE_TMP ".PLAYER.migrating"
#define MIGRATE_TMP_LOW ".player.migrating"

static int is_regular(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int files_identical(const char *a, const char *b)
{
    char sa[HASH_LEN + 1], sb[HASH_LEN + 1];
    if (hash_file(a, sa, NULL) != 0 || hash_file(b, sb, NULL) != 0)
        return 0;
    return strcmp(sa, sb) == 0;
}

static int move_file(const char *from, const char *to, int dry, FILE *out)
{
    if (dry) {
        fprintf(out, "  migrate %s -> %s (planned, nothing written)\n", from, to);
        return 0;
    }
    if (rename(from, to) != 0) {
        fprintf(out, "  error   %s could not be moved to %s (%s)\n", from, to, strerror(errno));
        return -1;
    }
    fprintf(out, "  migrate %s -> %s\n", from, to);
    return 0;
}

static int migrate_to_slots(const char *to, const row *rows, int nrows, int dry,
                            FILE *out, char *err, size_t errsz)
{
    char saves[1024], slot[1024], rec[1024], low[1024], tmp[1024], blank[1024];
    char tmp_low[1024];

    snprintf(saves, sizeof saves, "%.1000s/%s", to, U2_INSTALL_SAVES);
    snprintf(slot,  sizeof slot,  "%.1000s/%.16s", saves, U2_SAVE_SLOT_DEFAULT);
    snprintf(rec,   sizeof rec,   "%.1015s/PLAYER", slot);
    snprintf(low,   sizeof low,   "%.1000s/player", saves);
    snprintf(blank, sizeof blank, "%.1000s/%s/player", to, U2_INSTALL_DATA_ORIG);
    snprintf(tmp,   sizeof tmp,   "%.1000s/%s", saves, MIGRATE_TMP);
    snprintf(tmp_low, sizeof tmp_low, "%.1000s/%s", saves, MIGRATE_TMP_LOW);

    if (!is_dirpath(saves) && !is_regular(slot))
        return 0;                       /* nothing here is old-layout save data */

    fprintf(out, "-- save-scoped layout\n");

    /* 0. an interrupted previous run */
    if (is_regular(tmp) && !is_regular(slot)) {
        if (move_file(tmp, slot, dry, out) != 0) {
            snprintf(err, errsz, "cannot recover %s from an interrupted migration", tmp);
            return -1;
        }
    }
    if (is_regular(tmp_low) && !is_regular(low)) {
        if (move_file(tmp_low, low, dry, out) != 0) {
            snprintf(err, errsz, "cannot recover %s from an interrupted migration", tmp_low);
            return -1;
        }
    }

    /* 1. the record: a file at saves/PLAYER becomes saves/PLAYER/PLAYER.
     * Two renames because a directory cannot take a file's name while the file
     * holds it, and the intermediate stays inside the same directory so both
     * renames are same-filesystem and neither can lose the record. */
    int rec_there = is_regular(rec);
    if (is_regular(slot)) {
        if (move_file(slot, tmp, dry, out) != 0) {
            snprintf(err, errsz, "cannot move %s aside to make room for its slot", slot);
            return -1;
        }
        if (!dry && mkdir_p(slot) != 0) {
            snprintf(err, errsz, "cannot create %s", slot);
            return -1;
        }
        if (move_file(tmp, rec, dry, out) != 0) {
            snprintf(err, errsz, "cannot move the player record into its slot");
            return -1;
        }
        rec_there = 1;
    }

    /* 2. the release-spelled lowercase record.
     * The old installer seeded `saves/player` from the release copy, which is how
     * a played install ended up holding two files differing only by case. Which
     * one is the player's game is not guesswork: if the slot has no record yet,
     * the lowercase file IS the record and moves into the slot. If it does, then
     * a lowercase file identical to the release blank is the installer's own seed
     * and goes, and anything else is a real save and gets promoted. */
    if (is_regular(low)) {
        if (!rec_there) {
            if (!dry && mkdir_p(slot) != 0) {
                snprintf(err, errsz, "cannot create %s", slot);
                return -1;
            }
            if (move_file(low, rec, dry, out) != 0) {
                snprintf(err, errsz, "cannot move the release-spelled record into its slot");
                return -1;
            }
        } else if (is_regular(blank) && files_identical(low, blank)) {
            if (!dry && unlink(low) != 0) {
                snprintf(err, errsz, "cannot remove the seeded blank %s", low);
                return -1;
            }
            fprintf(out, "  remove  %s (byte-identical to %s: the old installer's seed, "
                         "not a game)%s\n", low, blank, dry ? " (planned, nothing written)" : "");
        } else {
            /* The same two-rename shape as step 1, for the same reason: the slot
             * directory cannot take the file's name while the file holds it, and
             * mkdir over an existing file is not a thing — u2_mkdir_p tolerates
             * EEXIST whatever the existing thing happens to be, so a mkdir here
             * would succeed as a no-op and the rename would then fail with ENOTDIR
             * and take the whole install down with it. */
            char pslot[1024], prec[1024];
            snprintf(pslot, sizeof pslot, "%.1000s/player", saves);
            snprintf(prec, sizeof prec, "%.1015s/PLAYER", pslot);
            if (move_file(low, tmp_low, dry, out) != 0) {
                snprintf(err, errsz, "cannot move %s aside to promote it", low);
                return -1;
            }
            if (!dry && mkdir_p(pslot) != 0) {
                snprintf(err, errsz, "cannot create %s", pslot);
                return -1;
            }
            if (move_file(tmp_low, prec, dry, out) != 0) {
                snprintf(err, errsz, "cannot promote %s to its own slot", low);
                return -1;
            }
            fprintf(out, "  note    %s is not the release blank, so it is kept as its own "
                         "slot. Nothing selects a second slot yet (--save is later, D5).\n",
                    low);
        }
    }

    /* 3. content a played world dirtied.
     * Detected against the manifest, which is the only record of what the
     * installer wrote. The dirty file moves into the slot, and the copy loop that
     * follows restores the content copy from the source the manifest names — so
     * the move is only offered when that source still hashes to what was
     * recorded. If it does not, the content file stays where it is: a half-repair
     * is worse than a reported one, and the old layout must keep working. */
    for (int i = 0; i < nrows; i++) {
        if (!is_writable_sector(rows[i].dst))
            continue;
        char full[1024], sha[HASH_LEN + 1], src_sha[HASH_LEN + 1];
        long sz = 0;
        snprintf(full, sizeof full, "%.500s/%.511s", to, rows[i].dst);
        if (hash_file(full, sha, &sz) != 0)
            continue;                       /* absent: --check already calls that out */
        if (sz == rows[i].size && strcmp(sha, rows[i].sha) == 0)
            continue;                       /* clean */
        if (hash_file(rows[i].src, src_sha, NULL) != 0 ||
            strcmp(src_sha, rows[i].sha) != 0) {
            fprintf(out, "  refuse  %s differs from its manifest hash, but its recorded "
                         "source %s is gone or has changed too; left in place\n",
                    full, rows[i].src);
            continue;
        }
        const char *base = strrchr(rows[i].dst, '/');
        char dst[1024];
        snprintf(dst, sizeof dst, "%.890s/%.127s", slot, base ? base + 1 : rows[i].dst);
        if (is_regular(dst)) {
            fprintf(out, "  note    %s is already overlaid in the slot; the content copy "
                         "is restored from its source\n", dst);
            continue;
        }
        if (!dry && mkdir_p(slot) != 0) {
            snprintf(err, errsz, "cannot create %s", slot);
            return -1;
        }
        if (move_file(full, dst, dry, out) != 0) {
            snprintf(err, errsz, "cannot move a played-in sector into the save slot");
            return -1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------- scan */

typedef struct {
    row *rows;
    int n;
    int shadowed;
    int patched_over;
    int skipped;
} plan;

static int plan_find(const plan *p, const char *dst)
{
    for (int i = 0; i < p->n; i++)
        if (!strcasecmp(p->rows[i].dst, dst))
            return i;
    return -1;
}

/* Does this filename look like a patch delta rather than content? The upgrade
 * package ships Moonstone Dragon's Map Patch as u2-map.pat, and the installer
 * cannot apply it, so silence here would be the worst of the three answers: the
 * player has the fix, we skip it, and nothing says so. */
static int is_patch_delta(const char *name)
{
    const size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".pat");
}

/* Scan one source directory into the plan.
 *
 * `may_override` is set only for the Galaxy patch source, and it is what makes a
 * patch mean what it says. The rule was "first source wins", and the release is
 * scanned first, so a corrupt release file beat the patched copy sitting beside it.
 * That is not hypothetical: of the 59 filenames the release and updated/ share, 11
 * differ, and one is mapx15, where 260 bytes at the end of the file are the
 * corruption GCHANGES.TXT -- the Moonstone Dragon patch's own readme -- was written
 * to fix. First-wins re-introduced a 1996 bug.
 *
 * The comparison has to be case-insensitive, because the two layers spell the same
 * dialogue files differently (release `tlkx03`, patch `TLKX03`). Copying both
 * spellings leaves two files where the engine asks for one, and it may find the
 * corrupt one.
 *
 * So: a patch source may replace a planned release-layer file when the bytes
 * differ, and says so. Identical copies are still just shadowed. */
static void scan_source(const char *dir, plan *p, FILE *out, int may_override)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(out, "  cannot read source %s\n", dir);
        return;
    }
    struct dirent *e;
    char names[512][NAME_LEN];
    int count = 0;
    while ((e = readdir(d)) && count < (int)(sizeof names / sizeof names[0])) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char full[1024];
        snprintf(full, sizeof full, "%.890s/%.127s", dir, e->d_name);
        if (!is_dirpath(full))
            snprintf(names[count++], NAME_LEN, "%.127s", e->d_name);
    }
    closedir(d);

    for (int i = 0; i < count; i++) {
        int galaxy = 0;
        if (!u2_install_classify(names[i], &galaxy)) {
            p->skipped++;
            if (is_patch_delta(names[i]))
                fprintf(out, "  skip  %s (a patch delta; the installer applies only an "
                             "already-patched updated/ directory -- docs/install-design.md)\n",
                        names[i]);
            else
                fprintf(out, "  skip  %s (not game content)\n", names[i]);
            continue;
        }
        char dst[512];
        snprintf(dst, sizeof dst, "%s/%.127s",
                 galaxy ? U2_INSTALL_DATA_UPD : U2_INSTALL_DATA_ORIG, names[i]);
        char full[1024];
        snprintf(full, sizeof full, "%.890s/%.127s", dir, names[i]);
        long sz = 0;
        char sha[HASH_LEN + 1];
        if (hash_file(full, sha, &sz) != 0) {
            fprintf(out, "  error %s could not be read\n", names[i]);
            continue;
        }

        const int existing = plan_find(p, dst);
        if (existing >= 0) {
            const int differs = strcmp(p->rows[existing].sha, sha) != 0;
            if (differs && may_override) {
                snprintf(p->rows[existing].src, sizeof p->rows[existing].src,
                         "%.599s", full);
                p->rows[existing].size = sz;
                memcpy(p->rows[existing].sha, sha, sizeof sha);
                p->patched_over++;
                /* Two causes look identical here and only one is a fix. A sector
                 * file is the one the game rewrites as you play, so a *source* copy
                 * that differs may simply be someone's mid-game world — the pack on
                 * the right is often the one the original was played in — and saying
                 * "patch" about that would be a claim this code cannot make. The
                 * behaviour is the same either way, because the patch layer is
                 * pristine by construction, so preferring it is right whether the
                 * release copy was corrupt or played-in; but the report has to name
                 * the ambiguity rather than resolve it silently. Note this is about
                 * the source: the installed tree no longer drifts at all, because
                 * every write goes to a save slot (docs/save-scoped-worlds.md). */
                if (is_writable_sector(dst))
                    fprintf(out, "  patch   %s taken from the patch source; the "
                                 "release copy differed, which is either a fix or a "
                                 "world played in the original\n", names[i]);
                else
                    fprintf(out, "  patch   %s taken from this source, replacing a "
                                 "different copy (D8)\n", names[i]);
            } else {
                p->shadowed++;
                fprintf(out, "  note  %s shadowed by an earlier source%s\n",
                        names[i], differs ? " and the copies DIFFER" : "");
            }
            continue;
        }
        if (p->n >= MAX_ROWS) {
            fprintf(out, "  error plan is full (%d rows)\n", MAX_ROWS);
            break;
        }
        snprintf(p->rows[p->n].dst, sizeof p->rows[p->n].dst, "%s", dst);
        snprintf(p->rows[p->n].src, sizeof p->rows[p->n].src, "%.599s", full);
        p->rows[p->n].size = sz;
        memcpy(p->rows[p->n].sha, sha, sizeof sha);
        p->n++;
    }
}

/* --------------------------------------------------------- recorded sources
 *
 * `--install` writes what it was given into config.ini's [install] section, so a
 * binary kept beside the game can reinstall with no arguments at all. That is the
 * whole point of the section, and it comes with one obligation: a recorded path
 * that has since moved must be said out loud, not skipped. An install that quietly
 * dropped the theme source it used last time would produce a different tree under
 * the same manifest, which is the exact shape of "installed fine, planets do not
 * work" this installer was written to make impossible.
 *
 * The target is recorded as `.` rather than an absolute path, because config.ini
 * lives in the target: the game directory can be moved and the record moves with
 * it. The sources are recorded absolute, because they live outside it and a path
 * relative to a directory that is not this one would mean nothing after a move.
 */

static void record_as_absolute(char *out, size_t n, const char *p)
{
    char resolved[1024];
    if (!p || !*p) { *out = '\0'; return; }
    if (realpath(p, resolved))
        snprintf(out, n, "%.1023s", resolved);
    else
        snprintf(out, n, "%.447s", p);     /* gone already: record what we were told */
}

static int dir_of_self(const char *self, char *out, size_t n)
{
    char buf[1024];
    struct stat st;

    if (!self || !*self)
        return 0;
    if (stat(self, &st) == 0)
        snprintf(buf, sizeof buf, "%.1023s", self);
    else if (!realpath(self, buf))
        return 0;                            /* bare argv[0] from PATH: no clue */

    char *slash = strrchr(buf, '/');
    if (!slash)
        snprintf(buf, sizeof buf, ".");
    else if (slash == buf)
        buf[1] = '\0';
    else
        *slash = '\0';
    snprintf(out, n, "%.1023s", buf);
    return 1;
}

int u2_install_find_config(const char *explicit_path, const char *self_path,
                           char *out, size_t n)
{
    if (explicit_path && *explicit_path) {
        snprintf(out, n, "%.511s", explicit_path);
        return u2_is_file(out) ? 1 : 0;
    }

    snprintf(out, n, "%s", U2_CONFIG_DEFAULT_FILE);
    if (u2_is_file(out))
        return 1;

    char dir[1024], cand[1152];
    if (dir_of_self(self_path, dir, sizeof dir)) {
        snprintf(cand, sizeof cand, "%.1000s/%s", dir, U2_CONFIG_DEFAULT_FILE);
        if (u2_is_file(cand)) {
            snprintf(out, n, "%.511s", cand);
            return 1;
        }
    }
    return 0;
}

/* One field, filled the only two ways a path may arrive: a flag, or a recorded
 * source that still exists. Anything else is reported and left unset, which sends
 * it on to the prompt (or to --no-prompt's refusal) rather than guessing. */
static const char *pick_source(const char *flag, const char *recorded,
                               const char *label, const u2_config *cfg, FILE *out)
{
    if (flag && *flag)
        return flag;
    if (!recorded || !*recorded)
        return NULL;
    if (!u2_is_dir(recorded)) {
        fprintf(out, "  note    %s records %s = %s, which is not a directory any more; "
                     "it will be asked for again\n", cfg->path, label, recorded);
        return NULL;
    }
    fprintf(out, "  config  %s = %s (from %s)\n", label, recorded, cfg->path);
    return recorded;
}

void u2_install_fill_from_config(u2_install_request *rq, const u2_config *cfg, FILE *out)
{
    if (!rq || !cfg || !cfg->present)
        return;

    rq->to           = pick_source(rq->to,           cfg->inst_to,       "to",       cfg, out);
    rq->original_dir = pick_source(rq->original_dir, cfg->inst_original, "original", cfg, out);
    rq->updated_dir  = pick_source(rq->updated_dir,  cfg->inst_updated,  "updated",  cfg, out);
    if (!rq->fmtowns_media || !*rq->fmtowns_media)
        rq->fmtowns_media = pick_source(rq->fmtowns_media, cfg->inst_fmtowns,
                                        "fmtowns", cfg, out);

    if (rq->n_themes_dir == 0) {
        int n = 0;
        for (int i = 0; i < cfg->n_inst_themes && n < U2_THEMES_MAX_SOURCES; i++) {
            const char *t = pick_source(NULL, cfg->inst_themes[i], "themes", cfg, out);
            if (t)
                rq->themes_dir[n++] = t;
        }
        rq->n_themes_dir = n;
    }
}

/* ------------------------------------------------------------------ config */

/* `have_themes` decides whether a [themes] section is written at all; with no
 * themes on disk there is nothing to point at. The installer does NOT record an
 * active theme (owner ruling 2026-11-13): the engine auto-picks `10` -> `default`
 * -> fmtowns -> CGA at launch, and a recorded `theme =` line is an explicit choice
 * as far as auto-detection is concerned, so it would freeze that order and override
 * a later Ctrl-T or a changed install. The theme line is left as a commented
 * example so a player who wants to pin one has the syntax to hand. */
/* The installer writes `dir = themes`; the engine's auto-detection looks for
 * `themes` when nothing named a directory. Two literals in two files is a drift
 * waiting to happen, so they are tied here rather than left to agreement. */
_Static_assert(0 == strcmp(U2_INSTALL_THEMES, U2_EGA_THEMES_DIR_NAME),
               "the installed themes directory name must match the engine's default");

static int write_config(const char *to, int have_themes, const u2_install_request *rq)
{
    char path[1024], tmp[1024];
    char orig[1024], upd[1024], fm[1024], th[U2_INSTALL_MAX_RECORDED][1024];

    snprintf(path, sizeof path, "%.1000s/%s", to, U2_INSTALL_CONFIG);
    snprintf(tmp, sizeof tmp, "%.1010s.u2tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    fputs("# u2 engine configuration, written by --install.\n"
          "# Precedence: command line > this file > nothing.\n"
          "# A relative path here is relative to this file, not to the directory\n"
          "# you launch from, so the game runs correctly from anywhere.\n"
          "# An unknown key, a duplicated key or a path that does not exist is an\n"
          "# error at startup rather than a silent default.\n"
          "\n"
          "[paths]\n"
          "data = .\n"
          "saves = saves\n"
          "\n", f);
    if (have_themes)
        fprintf(f, "[themes]\ndir = %s\n"
                  "# theme = 10   # uncomment to pin a theme; the engine otherwise\n"
                  "#              # auto-picks 10, then default, then fmtowns, then CGA\n",
                U2_INSTALL_THEMES);

    /* Written as a commented example rather than a pinned value, for the same reason
     * the theme line is: a value here is an explicit choice that outranks the
     * engine's own default and would have to be edited by hand to change again.
     * Enhanced is the default, so the file says nothing and the default stands; the
     * comment is there so a player who opens this file can find the knob at all. */
    fputs("[game]\n"
          "# mode = enhanced   # or classic. enhanced is the default: fan art, the\n"
          "#                   # 640x480 layout, and the dead ends the original could\n"
          "#                   # not avoid removed. classic is the original\n"
          "#                   # presentation in one word, the same thing --classic\n"
          "#                   # does on the command line.\n"
          "\n", f);

    /* What --install was given, so running it again from here needs no flags.
     * `to` is this directory by construction; the sources are absolute because
     * they live outside it. A source that has since moved is reported by the next
     * run and asked for again, never skipped -- see the section comment. */
    fputs("\n[install]\n"
          "# Recorded by --install so a re-run from this directory needs no arguments.\n"
          "# Delete this section and the installer asks again. `to` is this directory;\n"
          "# the sources are absolute because they live outside it.\n",
          f);
    fputs("to = .\n", f);
    record_as_absolute(orig, sizeof orig, rq ? rq->original_dir : NULL);
    if (*orig) fprintf(f, "original = %s\n", orig);
    record_as_absolute(upd, sizeof upd, rq ? rq->updated_dir : NULL);
    if (*upd) fprintf(f, "updated = %s\n", upd);
    static const char *const k_theme_keys[] = { "themes", "themes2", "themes3", "themes4" };
    for (int i = 0; rq && i < rq->n_themes_dir && i < U2_INSTALL_MAX_RECORDED; i++) {
        record_as_absolute(th[i], sizeof th[i], rq->themes_dir[i]);
        if (*th[i])
            fprintf(f, "%s = %s\n", k_theme_keys[i], th[i]);
    }
    record_as_absolute(fm, sizeof fm, rq ? rq->fmtowns_media : NULL);
    if (*fm) fprintf(f, "fmtowns = %s\n", fm);
    fputs("\n", f);

    if (fclose(f) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* ------------------------------------------------------------------ apply */

u2_install_result u2_install_apply(const u2_install_request *rq, FILE *out,
                                   char *err, size_t errsz)
{
    if (!rq || !rq->to || !*rq->to) {
        snprintf(err, errsz, "--install needs --to DIR");
        return U2_INSTALL_REFUSED;
    }
    if ((!rq->original_dir || !*rq->original_dir) &&
        (!rq->updated_dir || !*rq->updated_dir)) {
        snprintf(err, errsz,
                 "--install needs at least one source: --original-dir DIR and/or "
                 "--updated-dir DIR");
        return U2_INSTALL_REFUSED;
    }

    const t_state st = inspect_target(rq->to, rq->self_path);
    if (st == T_NOTDIR) {
        snprintf(err, errsz, "%s exists and is not a directory", rq->to);
        return U2_INSTALL_REFUSED;
    }
    if (st == T_FOREIGN) {
        snprintf(err, errsz,
                 "%s is not empty and carries no %s, so it is not an install of "
                 "ours. Nothing here is merged, overwritten or deleted: point --to "
                 "at an empty or new directory and the old one is left alone. If "
                 "this is already a game you downloaded, you can play it in place "
                 "with --data-dir instead of installing it.",
                 rq->to, U2_INSTALL_MANIFEST);
        return U2_INSTALL_REFUSED;
    }

    if (!rq->dry_run && mkdir_p(rq->to) != 0) {
        snprintf(err, errsz, "cannot create %s", rq->to);
        return U2_INSTALL_ERROR;
    }

    row *rows = calloc(MAX_ROWS, sizeof *rows);
    if (!rows) {
        snprintf(err, errsz, "out of memory");
        return U2_INSTALL_ERROR;
    }
    plan p = { rows, 0, 0, 0, 0 };

    fprintf(out, "u2 install: %s\n", rq->to);
    if (st == T_MINE)
        fprintf(out, "  an existing install of ours; completing it\n");
    if (st == T_SELFONLY)
        fprintf(out, "  %s holds only this executable, which is treated as empty\n", rq->to);

    /* D9: the Galaxy patch lives INSIDE the release directory. `updated/` is a
     * subdirectory of the IA pack, not a separate download, so making a player
     * point at it separately asks them to know the pack's internal layout. If
     * --updated-dir was not given and <original>/updated exists, that is the patch
     * layer and we use it. An explicit --updated-dir always wins. */
    const char *upd = rq->updated_dir && *rq->updated_dir ? rq->updated_dir : NULL;
    char auto_upd[1024];
    if (!upd && rq->original_dir && *rq->original_dir) {
        snprintf(auto_upd, sizeof auto_upd, "%.890s/updated", rq->original_dir);
        if (is_dirpath(auto_upd)) {
            upd = auto_upd;
            fprintf(out, "  found the Galaxy patch inside the release at %s\n", auto_upd);
        }
    }

    /* Release first, so a file both layers carry is seen from the release and the
     * shadow -- or, per D8, the patch winning -- is reported rather than decided by
     * readdir order. */
    if (rq->original_dir && *rq->original_dir) {
        if (!is_dirpath(rq->original_dir)) {
            snprintf(err, errsz, "--original-dir %s is not a directory", rq->original_dir);
            free(rows);
            return U2_INSTALL_REFUSED;
        }
        fprintf(out, "-- source %s (release)\n", rq->original_dir);
        scan_source(rq->original_dir, &p, out, 0);
    }
    if (upd) {
        if (!is_dirpath(upd)) {
            snprintf(err, errsz, "--updated-dir %s is not a directory", upd);
            free(rows);
            return U2_INSTALL_REFUSED;
        }
        fprintf(out, "-- source %s (Galaxy patch)\n", upd);
        scan_source(upd, &p, out, 1);
    }
    if (p.n == 0) {
        snprintf(err, errsz, "no game content found in the sources given");
        free(rows);
        return U2_INSTALL_REFUSED;
    }

    fprintf(out, "-- %d file(s) to write, %d shadowed, %d patched over, %d skipped\n",
            p.n, p.shadowed, p.patched_over, p.skipped);

    /* Migration runs before a single byte is copied, for two reasons. The dirty
     * sector files have to leave the content tree before the copy loop overwrites
     * them, or the repair destroys the very bytes it is meant to preserve; and
     * --dry-run has to show the plan, because the pre-flight is the only place a
     * migration that would move the wrong thing can be caught before it moves it.
     *
     * Only a tree that carries our manifest can be migrated: the manifest is what
     * records what the installer wrote and from where, and without it there is
     * nothing to compare a possibly-played-in file against. */
    if (st == T_MINE) {
        row *old = calloc(MAX_ROWS, sizeof *old);
        const int n_old = old ? manifest_read(rq->to, old, MAX_ROWS) : -1;
        int mres = 0;
        if (n_old > 0)
            mres = migrate_to_slots(rq->to, old, n_old, rq->dry_run, out, err, errsz);
        else
            fprintf(out, "  note    %s could not be read, so nothing was migrated\n",
                    U2_INSTALL_MANIFEST);
        free(old);
        if (mres != 0) {
            free(rows);
            return U2_INSTALL_ERROR;
        }
    }

    if (rq->dry_run) {
        /* The data layer is planned file by file above, because that is where the
         * interesting decisions are (which copy of a shared filename wins). Themes
         * and the FM Towns derivation are deterministic given their sources, so a
         * dry run names the sources rather than re-planning every file. */
        fprintf(out, "-- dry run: nothing was written\n");
        if (rq->n_themes_dir)
            for (int i = 0; i < rq->n_themes_dir; i++)
                fprintf(out, "   themes would be built from %s\n", rq->themes_dir[i]);
        else
            fprintf(out, "   no theme source given, so themes/ would not be written\n");
        if (rq->fmtowns_media && *rq->fmtowns_media)
            fprintf(out, "   themes/fmtowns would be derived from %s\n", rq->fmtowns_media);
        free(rows);
        return U2_INSTALL_OK;
    }

    int written = 0, failed = 0;
    for (int i = 0; i < p.n; i++) {
        char full[1024];
        snprintf(full, sizeof full, "%.500s/%.511s", rq->to, p.rows[i].dst);
        char parent[1024];
        const char *slash = strrchr(full, '/');
        snprintf(parent, sizeof parent, "%.*s", slash ? (int)(slash - full) : 0, full);
        if (mkdir_p(parent) != 0) {
            fprintf(out, "  error cannot create %s\n", parent);
            failed++;
            continue;
        }
        if (copy_verified(p.rows[i].src, full, p.rows[i].size, p.rows[i].sha) != 0) {
            fprintf(out, "  error %s did not copy byte-identically\n", p.rows[i].dst);
            failed++;
            continue;
        }
        written++;
    }
    if (failed) {
        snprintf(err, errsz, "%d file(s) failed to copy; nothing was recorded as installed",
                 failed);
        free(rows);
        return U2_INSTALL_ERROR;
    }

    /* saves/ is no longer seeded. A new game starts with no slot at all and reads
     * pure content, which is what a new game should do, and the engine creates the
     * slot the first time it has something to put in it (D6). The old seed copied
     * the release's lowercase `player` beside the uppercase record the engine
     * writes, which is how a played install ended up holding two files differing
     * only by case; removing the seed removes that by construction rather than by
     * the resolve_save_name() workaround. Migration (§ 4) deals with trees that
     * were installed the old way. */

    int have_themes = 0;
    int have_fmtowns = 0;
    if (rq->n_themes_dir > 0) {
        char tdir[1024];
        snprintf(tdir, sizeof tdir, "%.1000s/%s", rq->to, U2_INSTALL_THEMES);
        u2_themes_request trq;
        memset(&trq, 0, sizeof trq);
        trq.out = tdir;
        trq.n_sources = rq->n_themes_dir;
        for (int i = 0; i < rq->n_themes_dir && i < U2_THEMES_MAX_SOURCES; i++)
            trq.sources[i] = rq->themes_dir[i];
        char terr[512];
        if (u2_themes_build(&trq, out, terr, sizeof terr) != 0) {
            snprintf(err, errsz, "%.511s", terr);
            free(rows);
            return U2_INSTALL_ERROR;
        }
        have_themes = 1;
    }

    /* The FM Towns theme is derived, not copied: the art comes off the player's own
     * disc and the mapping is compiled into the binary, so this works on a machine
     * that has the disc and nothing else. It lands inside themes/ and is recorded in
     * the same manifest, so --check covers it exactly as it covers a copied theme --
     * including when --themes-dir was not given at all. */
    if (rq->fmtowns_media && *rq->fmtowns_media) {
        char tdir[1024], fm[1300];
        snprintf(tdir, sizeof tdir, "%.1000s/%s", rq->to, U2_INSTALL_THEMES);
        snprintf(fm, sizeof fm, "%.1200s/%s", tdir, U2_FMTOWNS_THEME);
        if (u2_mkdir_p(tdir) != 0) {
            snprintf(err, errsz, "cannot create %s", tdir);
            free(rows);
            return U2_INSTALL_ERROR;
        }
        char ferr[512];
        if (u2_fmtowns_build(rq->fmtowns_media, fm, out, ferr, sizeof ferr) != 0) {
            snprintf(err, errsz, "%.511s", ferr);
            free(rows);
            return U2_INSTALL_ERROR;
        }
        char terr[512];
        if (u2_themes_record(tdir, U2_FMTOWNS_THEME, rq->fmtowns_media,
                             terr, sizeof terr) != 0) {
            snprintf(err, errsz, "%.511s", terr);
            free(rows);
            return U2_INSTALL_ERROR;
        }
        have_themes = 1;
        have_fmtowns = 1;
    }

    /* Do not pin a theme (owner ruling 2026-11-13): the engine auto-picks
     * `10` -> `default` -> fmtowns -> CGA at launch, so a recorded theme would
     * freeze that choice and override a later Ctrl-T or a changed install. The
     * [themes] dir line is still written when themes exist; the theme line is a
     * commented example. */
    if (write_config(rq->to, (have_fmtowns || have_themes) ? 1 : 0, rq) != 0) {
        snprintf(err, errsz, "cannot write %s", U2_INSTALL_CONFIG);
        free(rows);
        return U2_INSTALL_ERROR;
    }
    if (manifest_write(rq->to, rows, p.n) != 0) {
        snprintf(err, errsz, "cannot write %s", U2_INSTALL_MANIFEST);
        free(rows);
        return U2_INSTALL_ERROR;
    }

    fprintf(out, "installed: %d file(s), manifest %s, config %s\n",
            written, U2_INSTALL_MANIFEST, U2_INSTALL_CONFIG);
    if (!have_themes)
        fprintf(out, "note: no themes installed; --ega needs a themes directory "
                     "(--themes-dir, or docs/install-design.md § 10)\n");
    free(rows);
    return U2_INSTALL_OK;
}

/* ------------------------------------------------------------------- check */

u2_install_result u2_install_check(const char *to, FILE *out, char *err, size_t errsz)
{
    if (!to || !*to) {
        snprintf(err, errsz, "--install --check needs --to DIR");
        return U2_INSTALL_REFUSED;
    }
    const t_state st = inspect_target(to, NULL);
    if (st == T_ABSENT || st == T_EMPTY || st == T_SELFONLY) {
        snprintf(err, errsz, "%s is empty or absent; there is nothing to check", to);
        return U2_INSTALL_REFUSED;
    }
    if (st == T_FOREIGN) {
        snprintf(err, errsz, "%s has no %s, so this installer cannot say anything "
                 "about it", to, U2_INSTALL_MANIFEST);
        return U2_INSTALL_REFUSED;
    }

    row *rows = calloc(MAX_ROWS, sizeof *rows);
    if (!rows) {
        snprintf(err, errsz, "out of memory");
        return U2_INSTALL_ERROR;
    }
    const int n = manifest_read(to, rows, MAX_ROWS);
    if (n <= 0) {
        snprintf(err, errsz, "%s could not be read", U2_INSTALL_MANIFEST);
        free(rows);
        return U2_INSTALL_ERROR;
    }

    int missing = 0, wrong = 0, ok = 0;
    for (int i = 0; i < n; i++) {
        char full[1024];
        snprintf(full, sizeof full, "%.500s/%.511s", to, rows[i].dst);
        char sha[HASH_LEN + 1];
        long sz = 0;
        if (hash_file(full, sha, &sz) != 0) {
            fprintf(out, "  missing %s\n", rows[i].dst);
            missing++;
            continue;
        }
        if (sz != rows[i].size) {
            fprintf(out, "  wrong   %s is %ld B, manifest says %ld B\n",
                    rows[i].dst, sz, rows[i].size);
            wrong++;
            continue;
        }
        if (strcmp(sha, rows[i].sha) != 0) {
            /* Content is immutable after install now that every write goes to a
             * save slot, so a difference is a defect and not a state to tolerate.
             * This flips the meaning of the old `played-in` verdict: it used to
             * mean "expected after play", and after this change it can only mean
             * something wrote where it must not. */
            if (is_writable_sector(rows[i].dst))
                fprintf(out, "  wrong   %s differs from install. Content is never written "
                             "after install, so this is a defect: run --install to move it "
                             "into the save slot and restore the content copy "
                             "(docs/save-scoped-worlds.md)\n", rows[i].dst);
            else
                fprintf(out, "  wrong   %s differs from install and is not a sector file\n",
                        rows[i].dst);
            wrong++;
            continue;
        }
        ok++;
    }

    /* Strays: anything under data/ that the manifest does not know about. saves/
     * is excluded because it is the mutable area by design, and a stray there is
     * the player's own save, not a broken install. */
    int strays = 0;
    {
        char root[1024];
        snprintf(root, sizeof root, "%.1000s/data", to);
        DIR *d = opendir(root);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                    continue;
                char layer[1024];
                snprintf(layer, sizeof layer, "%.890s/%.127s", root, e->d_name);
                if (!is_dirpath(layer)) {
                    fprintf(out, "  stray   %s/%s\n", "data", e->d_name);
                    strays++;
                    continue;
                }
                DIR *l = opendir(layer);
                if (!l) continue;
                struct dirent *f;
                while ((f = readdir(l))) {
                    if (!strcmp(f->d_name, ".") || !strcmp(f->d_name, ".."))
                        continue;
                    char dst[512];
                    snprintf(dst, sizeof dst, "data/%.127s/%.127s", e->d_name, f->d_name);
                    int known = 0;
                    for (int i = 0; i < n; i++)
                        if (!strcmp(rows[i].dst, dst)) { known = 1; break; }
                    if (!known) {
                        fprintf(out, "  stray   %s\n", dst);
                        strays++;
                    }
                }
                closedir(l);
            }
            closedir(d);
        }
    }

    fprintf(out, "check %s: %d ok, %d missing, %d wrong, %d strays\n",
            to, ok, missing, wrong, strays);

    /* The themes subtree carries its own manifest, because it is the theme
     * builder's output rather than a copy of the player's release, and each
     * manifest stays authoritative for its own half of the tree. */
    int themes_bad = 0;
    {
        char tdir[1024];
        snprintf(tdir, sizeof tdir, "%.1000s/%s", to, U2_INSTALL_THEMES);
        if (is_dirpath(tdir)) {
            char terr[512];
            if (u2_themes_check(tdir, out, terr, sizeof terr) != 0) {
                fprintf(out, "  themes: %s\n", terr);
                themes_bad = 1;
            }
        }
    }

    free(rows);
    if (missing || wrong || strays || themes_bad) {
        snprintf(err, errsz, "the tree does not match its manifest");
        return U2_INSTALL_REFUSED;
    }
    return U2_INSTALL_OK;
}

/* ------------------------------------------------------------------ prompts */

/* Precedence, stated once because every question below depends on it:
 * flag > prompt > nothing. Anything a flag already answered is never asked, so a
 * fully-specified command line behaves exactly as it did before prompts existed,
 * and a script's behaviour cannot change because a terminal appeared. */

static void trim_tail(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                 s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
}

/* Ask one question. Returns 0 with the answer or the default in buf, or -1 on EOF.
 * EOF is the shape of "stdin is not really here", and it is an error rather than an
 * empty answer precisely so that a missing answer cannot be mistaken for a declined
 * optional one. */
static int ask(FILE *out, const char *q, const char *dflt, char *buf, size_t n)
{
    if (dflt && *dflt)
        fprintf(out, "%s [%s] ", q, dflt);
    else
        fprintf(out, "%s ", q);
    fflush(out);
    if (!fgets(buf, (int)n, stdin)) {
        *buf = '\0';
        return -1;
    }
    trim_tail(buf);
    if (!*buf && dflt && *dflt)
        snprintf(buf, n, "%.1023s", dflt);
    return 0;
}

/* Ask until the answer names a directory, or give up. A wrong path is reported and
 * asked again rather than being papered over, which is the same rule --ega-theme
 * follows for an explicit directory: an answer is not a hint. */
static int ask_dir(FILE *out, const char *q, int required, char *out_buf, size_t n)
{
    char a[1024];
    for (int tries = 0; tries < 5; tries++) {
        if (ask(out, q, required ? NULL : "none", a, sizeof a) != 0)
            return -1;
        if (!*a) {
            if (required)
                fprintf(out, "  that is required; the installer cannot guess where "
                             "your copy of the game lives\n");
            else
                return 0;
            continue;
        }
        if (!strcmp(a, "none") && !required)
            return 0;
        if (!is_dirpath(a)) {
            fprintf(out, "  %s is not a directory, try again\n", a);
            continue;
        }
        snprintf(out_buf, n, "%.1023s", a);
        return 0;
    }
    return -1;
}

int u2_install_prompt(u2_install_request *rq, u2_install_answers *ans, FILE *out,
                      char *err, size_t errsz)
{
    char a[1024];
    memset(ans, 0, sizeof *ans);

    /* The target. "." is the default because the common shape is a fresh folder
     * holding this binary, and T_SELFONLY makes that legal. */
    if (rq->to && *rq->to)
        snprintf(ans->to, sizeof ans->to, "%.1023s", rq->to);
    else if (ask(out, "Install into which directory?", ".", ans->to, sizeof ans->to) != 0) {
        snprintf(err, errsz, "no answer for the target directory");
        return -1;
    }
    if (!*ans->to)
        snprintf(ans->to, sizeof ans->to, ".");
    rq->to = ans->to;

    if (!rq->original_dir || !*rq->original_dir) {
        if (ask_dir(out, "Where is your original Ultima II (DOS) directory?",
                    1, ans->original, sizeof ans->original) != 0) {
            snprintf(err, errsz, "no release directory given");
            return -1;
        }
        if (!*ans->original) {
            snprintf(err, errsz, "--install needs a release directory to copy from");
            return -1;
        }
        rq->original_dir = ans->original;
    }

    /* The Galaxy layer is not a question: D9 finds <original>/updated on its own,
     * and asking would require the player to know the pack's internal layout. The
     * dry run below reports whether it was found. */

    if (rq->n_themes_dir == 0) {
        if (ask_dir(out, "Any fan art pack to install themes from (the U2Upgrade "
                         "directory)?", 0, ans->themes, sizeof ans->themes) != 0) {
            snprintf(err, errsz, "no answer for the theme source");
            return -1;
        }
        if (*ans->themes) {
            rq->themes_dir[0] = ans->themes;
            rq->n_themes_dir = 1;
        }
    }

    if (!rq->fmtowns_media || !*rq->fmtowns_media) {
        if (ask(out, "An FM Towns disc image or directory? This one builds the "
                     "FM Towns theme", "none", a, sizeof a) != 0) {
            snprintf(err, errsz, "no answer for the FM Towns media");
            return -1;
        }
        if (*a && strcmp(a, "none")) {
            struct stat st;
            if (stat(a, &st) != 0)
                fprintf(out, "  %s does not exist, so the FM Towns theme is skipped\n", a);
            else {
                snprintf(ans->fmtowns, sizeof ans->fmtowns, "%.1023s", a);
                rq->fmtowns_media = ans->fmtowns;
            }
        }
    }

    /* Pre-flight. The plan is the dry run, so the player reads the same report a
     * --dry-run produces rather than a second, weaker summary written for the
     * prompt path. One code path, one set of decisions on screen. */
    fputc('\n', out);
    const int saved = rq->dry_run;
    rq->dry_run = 1;
    const u2_install_result pr = u2_install_apply(rq, out, err, errsz);
    rq->dry_run = saved;
    if (pr != U2_INSTALL_OK)
        return -1;

    fputc('\n', out);
    if (ask(out, "Write that?", "y", a, sizeof a) != 0) {
        snprintf(err, errsz, "no answer to the confirmation");
        return -1;
    }
    if (a[0] != 'y' && a[0] != 'Y') {
        fprintf(out, "cancelled: nothing was written\n");
        return 0;
    }
    return 1;
}
