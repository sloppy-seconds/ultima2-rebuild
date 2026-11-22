/*
 * copy.h — the file operations the installer and the theme builder share.
 *
 * These live here rather than inside install.c because there is now a second
 * builder (core/themes.c) that must copy with the same guarantee, and a copy
 * routine that exists twice is a copy routine whose two halves disagree about what
 * "succeeded" means. The guarantee is the point: a file is not copied until it has
 * been hashed back from disk and compared, so a short write, a partial write or a
 * silently truncated file is a failure return rather than a story told afterwards.
 */

#ifndef U2_CORE_COPY_H
#define U2_CORE_COPY_H

#include <stddef.h>

#define U2_HASH_LEN 16      /* sha256 hex prefix, the same width _BUILD.tsv uses */

/* -1 when absent; the size otherwise. Directories report their size too; use
 * u2_is_dir when the question is "is this a directory". */
long u2_file_size(const char *path);

int u2_is_dir(const char *path);

/* A regular file, not a directory and not absent. */
int u2_is_file(const char *path);

/* sha256 of a whole file, first 16 hex characters into `out`, and its size into
 * `out_size` when asked. Returns 0 on success. Refuses anything over 8 MB, which
 * is far above every file either builder handles and stops a wrong path pointing
 * at an archive from being read into memory. */
int u2_hash_file(const char *path, char out[U2_HASH_LEN + 1], long *out_size);

/* mkdir -p. Existing directories are not an error. */
int u2_mkdir_p(const char *path);

/* Copy `src` to `dst` through a temp file and rename, then hash the destination
 * and compare against `want_size` / `want_sha`. Non-zero on any mismatch, with the
 * temp removed and, for any failure found after the rename, the destination removed
 * too: a copy that cannot be proved is not a copy, and leaving unproven bytes at the
 * final path is the one outcome a caller must not be able to ship. */
int u2_copy_verified(const char *src, const char *dst, long want_size,
                     const char want_sha[U2_HASH_LEN + 1]);

/* Write `data` to `dst` the same way (temp, rename, hash back). */
int u2_write_verified(const char *dst, const char *data, size_t len,
                      const char want_sha[U2_HASH_LEN + 1]);

#endif
