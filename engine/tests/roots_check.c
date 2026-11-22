/* roots_check.c — the content roots, at their own boundary (issue #8, 2 of 4).
 *
 * core/roots.c owns the ordered root list that makes both install layouts work:
 * `data/updated` over `data/original`, and the drop-in case's `updated` over `.`. The
 * header's whole argument for existing is that two files carrying their own copy of that
 * order will disagree, and until now nothing tested the order itself — world.c, tiles.c
 * and pic.c consumed it, and a reordering would have shown up as a wrong picture or a
 * missing file rather than as a failing test.
 *
 * Temp dirs only. The module reads ctx->data_dir and nothing else, so a zeroed ctx with
 * one field set is the whole fixture.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/roots.h"
#include "core/ctx.h"

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

static char ROOT[512];

/* mkdir with the parents, so the fixture does not need a shell. */
static void mkdir_path(const char *p)
{
    char buf[700];
    snprintf(buf, sizeof buf, "%s", p);
    for (char *s = buf + 1; *s; s++)
        if (*s == '/') { *s = '\0'; mkdir(buf, 0755); *s = '/'; }
    mkdir(buf, 0755);
}

static void put(const char *rel, const char *body)
{
    char path[700];
    snprintf(path, sizeof path, "%s/%s", ROOT, rel);
    char dir[700];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir_path(dir); }
    FILE *f = fopen(path, "w");
    if (f) { fputs(body, f); fclose(f); }
}

int main(void)
{
    /* --- the table: order, patch flags, and the sizing contract ---------------- */
    CHECK(u2_root_count <= U2_ROOT_MAX,
          "u2_root_count %zu exceeds U2_ROOT_MAX %d", u2_root_count, U2_ROOT_MAX);
    CHECK(u2_root_count == 4, "expected 4 roots, got %zu", u2_root_count);
    if (u2_root_count == 4) {
        /* The order IS the precedence rule, so each row is named rather than sampled.
         * Patched layers first, release layers last: an installed tree puts Galaxy
         * content over release content, and the drop-in case is the same shape one
         * level up. */
        CHECK(!strcmp(u2_roots[0].sub, "data/updated") && u2_roots[0].patched,
              "root 0 must be the installed patch layer, got '%s' patched=%d",
              u2_roots[0].sub, u2_roots[0].patched);
        CHECK(!strcmp(u2_roots[1].sub, "updated") && u2_roots[1].patched,
              "root 1 must be the drop-in patch layer, got '%s' patched=%d",
              u2_roots[1].sub, u2_roots[1].patched);
        CHECK(!strcmp(u2_roots[2].sub, "data/original") && !u2_roots[2].patched,
              "root 2 must be the installed release layer, got '%s' patched=%d",
              u2_roots[2].sub, u2_roots[2].patched);
        CHECK(!strcmp(u2_roots[3].sub, "") && !u2_roots[3].patched,
              "root 3 must be the content root itself, got '%s' patched=%d",
              u2_roots[3].sub, u2_roots[3].patched);
    }

    /* --- the content root ------------------------------------------------------ */
    {
        u2_ctx c;
        memset(&c, 0, sizeof c);
        c.data_dir = NULL;
        CHECK(!strcmp(u2_content_root(&c), "."), "NULL data_dir must fall back to '.'");
        c.data_dir = (char *)"";
        CHECK(!strcmp(u2_content_root(&c), "."), "empty data_dir must fall back to '.'");
        c.data_dir = (char *)"/tmp/whatever";
        CHECK(!strcmp(u2_content_root(&c), "/tmp/whatever"), "data_dir must be returned as-is");
        CHECK(!strcmp(u2_content_root(NULL), "."), "NULL ctx must fall back to '.'");
    }

    /* --- path building: no doubled slash, no trailing slash -------------------- */
    {
        u2_ctx c;
        memset(&c, 0, sizeof c);
        c.data_dir = (char *)"/data";
        char b[256];
        u2_root_path(b, sizeof b, &c, "");
        CHECK(!strcmp(b, "/data"), "the content root itself must not gain a slash, got '%s'", b);
        u2_root_path(b, sizeof b, &c, "updated");
        CHECK(!strcmp(b, "/data/updated"), "a sub must join with one slash, got '%s'", b);
    }

    /* --- find: precedence, and release_only ------------------------------------ */
    snprintf(ROOT, sizeof ROOT, "%s", "/var/tmp/u2_roots_check");
    mkdir_path(ROOT);
    put("updated/patched_only.dat", "p");
    put("both.dat", "release");
    put("updated/both.dat", "patch");
    put("data/original/installed_only.dat", "i");
    /* The fixture must exist before any assertion reads it, or a missing directory reads
     * as a precedence bug. */
    CHECK(access("/var/tmp/u2_roots_check/updated/both.dat", 0) == 0,
          "fixture: the patch layer file must exist");
    CHECK(access("/var/tmp/u2_roots_check/data/original/installed_only.dat", 0) == 0,
          "fixture: the installed release layer file must exist");

    u2_ctx c;
    memset(&c, 0, sizeof c);
    c.data_dir = ROOT;

    {
        char out[512];
        CHECK(u2_root_find(&c, "both.dat", 0, out, sizeof out), "both.dat must be found");
        CHECK(strstr(out, "/updated/both.dat") != NULL,
              "the patch layer must win, got '%s'", out);
        /* release_only is the rule that keeps an unpatched filename out of the Galaxy
         * layer. With it, the same name resolves one layer down. */
        CHECK(u2_root_find(&c, "both.dat", 1, out, sizeof out),
              "both.dat must be found in a release layer too");
        CHECK(!strcmp(out, "both.dat") || strstr(out, "/both.dat") != NULL,
              "release_only must resolve to the content root, got '%s'", out);
        CHECK(strstr(out, "/updated/") == NULL,
              "release_only must not reach a patch layer, got '%s'", out);

        CHECK(u2_root_find(&c, "patched_only.dat", 0, out, sizeof out),
              "a patch-only file must be found without the restriction");
        CHECK(!u2_root_find(&c, "patched_only.dat", 1, out, sizeof out),
              "a patch-only file must NOT be found with release_only");

        CHECK(u2_root_find(&c, "installed_only.dat", 0, out, sizeof out),
              "data/original must be searched");
    }

    /* --- find_any: one loop over several spellings ----------------------------- */
    {
        char out[512];
        const char *names[] = { "nope.dat", "both.dat", "also-nope.dat" };
        CHECK(u2_root_find_any(&c, names, 3, 0, out, sizeof out),
              "find_any must reach the present name in the middle of the list");
        CHECK(strstr(out, "both.dat") != NULL, "find_any wrote '%s'", out);
        const char *none[] = { "nope.dat", "also-nope.dat" };
        CHECK(!u2_root_find_any(&c, none, 2, 0, out, sizeof out),
              "find_any must report failure when no name exists");
    }

    /* --- what a miss leaves behind ---------------------------------------------
     * roots.h promises a miss "returns 0 and leaves `out` empty". Measured rather than
     * taken on trust, because the loop writes `out` on every root it tries and only
     * returns on a hit — the clearing happens after the loop, in both find and find_any.
     * Callers branch on this, so it is pinned, not noted. */
    {
        char out[512];
        memset(out, 'Z', sizeof out);
        out[sizeof out - 1] = '\0';
        CHECK(!u2_root_find(&c, "definitely_absent.dat", 0, out, sizeof out),
              "a missing name must return 0");
        CHECK(out[0] == '\0',
              "a miss must clear `out` as the header promises, got '%s'", out);
        memset(out, 'Z', sizeof out);
        out[sizeof out - 1] = '\0';
        const char *gone[] = { "nope.dat" };
        CHECK(!u2_root_find_any(&c, gone, 1, 0, out, sizeof out),
              "find_any with no present name must return 0");
        CHECK(out[0] == '\0',
              "a find_any miss must clear `out` too, got '%s'", out);
    }

    printf("roots_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
