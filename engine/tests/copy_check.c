/* copy_check.c — verified copy and write, at their own boundary (issue #8, 3 of 4).
 *
 * core/copy.h states the guarantee plainly: "a copy that cannot be proved is not a copy".
 * The destination is hashed back from disk and compared, so a short write or a silently
 * truncated file is a failure return rather than a story told afterwards. Nothing tested
 * that until now — install_check exercised the happy paths, which is exactly the half that
 * would keep passing if the verification were deleted.
 *
 * The two cases this exists to make impossible are a wrong expected size and a wrong
 * expected hash. Both must return non-zero, and neither may leave a temp file behind.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/copy.h"

static int checks, fails;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            fails++;                                                           \
            printf("  FAIL  ");                                                \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#define ROOT "/var/tmp/u2_copy_check"

/* How many entries in DIR match PRED-ish: here, just "count files whose name contains
 * `needle`". Used to prove no temp file survives a failed copy. */
static int count_matching(const char *dir, const char *needle)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strstr(e->d_name, needle)) n++;
    closedir(d);
    return n;
}

static long write_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return (long)w;
}

int main(void)
{
    u2_mkdir_p(ROOT);

    /* --- size and kind ---------------------------------------------------------- */
    CHECK(u2_file_size(ROOT "/absent") == -1, "absent file must report -1");
    CHECK(u2_is_dir(ROOT "/absent") == 0, "absent path is not a directory");
    CHECK(u2_is_file(ROOT "/absent") == 0, "absent path is not a regular file");

    char body[4096];
    for (size_t i = 0; i < sizeof body; i++) body[i] = (char)('a' + (i % 26));
    write_file(ROOT "/src.bin", body, sizeof body);

    CHECK(u2_file_size(ROOT "/src.bin") == (long)sizeof body,
          "size must be the byte count, got %ld", u2_file_size(ROOT "/src.bin"));
    CHECK(u2_is_file(ROOT "/src.bin"), "a written file must be a regular file");
    CHECK(u2_is_dir(ROOT "/src.bin") == 0, "a file must not report as a directory");
    CHECK(u2_is_dir(ROOT), "the fixture dir must report as a directory");
    CHECK(u2_is_file(ROOT) == 0, "a directory must not report as a regular file");

    /* --- mkdir -p --------------------------------------------------------------- */
    CHECK(u2_mkdir_p(ROOT "/a/b/c") == 0, "mkdir -p must create nested dirs");
    CHECK(u2_is_dir(ROOT "/a/b/c"), "the deepest directory must exist");
    CHECK(u2_mkdir_p(ROOT "/a/b/c") == 0, "an existing directory is not an error");

    /* --- hashing ----------------------------------------------------------------- */
    char h1[U2_HASH_LEN + 1], h2[U2_HASH_LEN + 1], h3[U2_HASH_LEN + 1];
    long sz = -1;
    CHECK(u2_hash_file(ROOT "/src.bin", h1, &sz) == 0, "hashing a real file must succeed");
    CHECK(strlen(h1) == U2_HASH_LEN, "hash must be %d hex chars, got '%s'", U2_HASH_LEN, h1);
    CHECK(sz == (long)sizeof body, "hash must report the size, got %ld", sz);
    CHECK(u2_hash_file(ROOT "/src.bin", h2, NULL) == 0, "size is optional");
    CHECK(!strcmp(h1, h2), "hashing the same file twice must agree");
    write_file(ROOT "/other.bin", body, sizeof body - 1);   /* one byte short */
    CHECK(u2_hash_file(ROOT "/other.bin", h3, NULL) == 0, "second hash must succeed");
    CHECK(strcmp(h1, h3) != 0,
          "a one-byte difference must change the hash — both are the same length, so a "
          "length-only check would pass this");
    CHECK(u2_hash_file(ROOT "/absent", h1, &sz) != 0, "hashing an absent file must fail");

    /* The 8 MB ceiling is a refusal, not a slow success. */
    {
        char big[512];
        snprintf(big, sizeof big, "%s/big.bin", ROOT);
        FILE *f = fopen(big, "wb");
        if (f) {
            if (ftruncate(fileno(f), 9 * 1024 * 1024) != 0) printf("  note  ftruncate failed\n");
            fclose(f);
            char hb[U2_HASH_LEN + 1];
            CHECK(u2_hash_file(big, hb, NULL) != 0,
                  "a file over the ceiling must be refused, not read");
        }
    }

    /* --- the guarantee: verified copy ------------------------------------------- */
    {
        char want[U2_HASH_LEN + 1];
        long wsz = -1;
        u2_hash_file(ROOT "/src.bin", want, &wsz);

        CHECK(u2_copy_verified(ROOT "/src.bin", ROOT "/dst-good.bin", wsz, want) == 0,
              "a copy with the right size and hash must succeed");
        CHECK(u2_file_size(ROOT "/dst-good.bin") == (long)sizeof body,
              "the destination must hold the whole file");

        /* The two cases the module exists for. */
        CHECK(u2_copy_verified(ROOT "/src.bin", ROOT "/dst-size.bin", wsz + 1, want) != 0,
              "a wrong expected size must fail — this is 'cannot be proved'");
        char wrong[U2_HASH_LEN + 1];
        strcpy(wrong, want);
        wrong[0] = (wrong[0] == 'f') ? 'e' : (char)(wrong[0] + 1);
        CHECK(u2_copy_verified(ROOT "/src.bin", ROOT "/dst-sha.bin", wsz, wrong) != 0,
              "a wrong expected hash must fail");

        /* And neither may leave litter: the contract says the temp is removed. */
        CHECK(count_matching(ROOT, "dst-size") == 0,
              "a failed copy must leave no dst-size* file behind, found %d",
              count_matching(ROOT, "dst-size"));
        CHECK(count_matching(ROOT, "dst-sha") == 0,
              "a failed copy must leave no dst-sha* file behind, found %d",
              count_matching(ROOT, "dst-sha"));
        CHECK(count_matching(ROOT, ".tmp") == 0,
              "no temp file may survive, found %d", count_matching(ROOT, ".tmp"));
    }

    /* --- the guarantee: verified write ------------------------------------------ */
    {
        const char msg[] = "the quick brown fox";
        char want[U2_HASH_LEN + 1];
        long wsz = -1;
        /* Hash what we intend to write by writing it somewhere provable first. */
        write_file(ROOT "/probe.bin", msg, sizeof msg - 1);
        u2_hash_file(ROOT "/probe.bin", want, &wsz);

        CHECK(u2_write_verified(ROOT "/out-good.txt", msg, sizeof msg - 1, want) == 0,
              "a write with the right hash must succeed");
        char got[64];
        FILE *f = fopen(ROOT "/out-good.txt", "rb");
        if (f) {
            size_t n = fread(got, 1, sizeof got - 1, f);
            got[n] = '\0';
            fclose(f);
            CHECK(!strcmp(got, msg), "the file must hold exactly what was asked, got '%s'", got);
        } else {
            CHECK(0, "the destination must exist after a verified write");
        }

        char wrong[U2_HASH_LEN + 1];
        strcpy(wrong, want);
        wrong[1] = (wrong[1] == 'f') ? 'e' : (char)(wrong[1] + 1);
        CHECK(u2_write_verified(ROOT "/out-bad.txt", msg, sizeof msg - 1, wrong) != 0,
              "a write with the wrong hash must fail");
        CHECK(count_matching(ROOT, "out-bad") == 0,
              "a failed write must leave nothing behind, found %d",
              count_matching(ROOT, "out-bad"));
    }

    printf("copy_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
