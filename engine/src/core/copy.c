/*
 * copy.c — see copy.h. Moved out of install.c intact; the guarantees are the ones
 * the installer gate was written against, and the theme builder now inherits them
 * rather than re-deriving them.
 */

#define _POSIX_C_SOURCE 200809L   /* stat, mkdir, rename, unlink under -std=c11 */

#include "core/copy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/compat.h"
#include "core/sha256.h"

#define MAX_FILE_COPY (8u * 1024u * 1024u)

long u2_file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

int u2_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int u2_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int u2_hash_file(const char *path, char out[U2_HASH_LEN + 1], long *out_size)
{
    const long sz = u2_file_size(path);
    if (sz < 0 || (unsigned long)sz > MAX_FILE_COPY)
        return -1;
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    unsigned char *buf = malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) { fclose(f); return -1; }
    const size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return -1; }

    char hex[65];
    sha256_hex(buf, (size_t)sz, hex);
    free(buf);
    memcpy(out, hex, U2_HASH_LEN);
    out[U2_HASH_LEN] = '\0';
    if (out_size)
        *out_size = sz;
    return 0;
}

int u2_mkdir_p(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%.1023s", path);
    for (char *s = buf + 1; *s; s++) {
        if (*s != '/')
            continue;
        *s = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST)
            return -1;
        *s = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* Write `src`'s bytes (or the given buffer) to a temp beside `dst`, rename over
 * `dst`, then hash the result. The rename is what makes a crash leave either the
 * old file or the new one and never a half-written one, which is the same rule the
 * engine's own sector write-back follows. */
static int write_atomic(const char *dst, FILE *src_stream, const char *buf, size_t len)
{
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%.1190s.u2tmp", dst);
    /* The two sources are exclusive: a buffer, or a stream to copy. Neither means
     * the fread below would run on a null stream, which is what -fanalyzer reports
     * through u2_write_verified's callers. No caller does it today, and a refusal is
     * cheaper than proving that every future one cannot. */
    if (!buf && !src_stream)
        return -1;
    FILE *o = fopen(tmp, "wb");
    if (!o)
        return -1;

    size_t total = 0;
    int ok = 1;
    if (buf) {
        ok = (len == 0 || fwrite(buf, 1, len, o) == len);
        total = len;
    } else {
        unsigned char *chunk = malloc(65536);
        ok = chunk != NULL;
        while (ok) {
            const size_t n = fread(chunk, 1, 65536, src_stream);
            if (n == 0) break;
            if (fwrite(chunk, 1, n, o) != n) { ok = 0; break; }
            total += n;
        }
        free(chunk);
    }
    if (fclose(o) != 0)
        ok = 0;
    if (!ok || rename(tmp, dst) != 0) {
        unlink(tmp);
        return -1;
    }
    return (int)total;
}

/* copy.h's guarantee is "a copy that cannot be proved is not a copy", and the proof
 * happens AFTER the rename, because the rename is what makes a crash leave either the
 * old file or the new one and never a half-written one. So every failure found after
 * this point must remove the destination as well as report the failure: leaving the
 * unproven bytes at `dst` means a caller that ignores the return ships a corrupt file,
 * and the old file at that path is already gone either way. Measured before this was
 * written: a wrong expected hash left a full-size dst on disk, and copy_check now
 * asserts that it does not. */
static int unproven(const char *dst)
{
    unlink(dst);
    return -1;
}

int u2_copy_verified(const char *src, const char *dst, long want_size,
                     const char want_sha[U2_HASH_LEN + 1])
{
    const long sz = u2_file_size(src);
    if (sz < 0 || (unsigned long)sz > MAX_FILE_COPY || sz != want_size)
        return -1;
    FILE *f = fopen(src, "rb");
    if (!f)
        return -1;
    const int wrote = write_atomic(dst, f, NULL, 0);
    fclose(f);
    if (wrote < 0 || (long)wrote != want_size)
        return unproven(dst);

    char sha[U2_HASH_LEN + 1];
    long after = 0;
    if (u2_hash_file(dst, sha, &after) != 0)
        return unproven(dst);
    if (after != want_size || strcmp(sha, want_sha) != 0)
        return unproven(dst);
    return 0;
}

int u2_write_verified(const char *dst, const char *data, size_t len,
                      const char want_sha[U2_HASH_LEN + 1])
{
    if (write_atomic(dst, NULL, data, len) < 0)
        return -1;
    char sha[U2_HASH_LEN + 1];
    long after = 0;
    if (u2_hash_file(dst, sha, &after) != 0)
        return unproven(dst);
    if ((size_t)after != len || strcmp(sha, want_sha) != 0)
        return unproven(dst);
    return 0;
}
