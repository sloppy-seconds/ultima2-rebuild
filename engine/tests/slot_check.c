/* slot_check.c — where a save lives, and what may rotate, at their own boundary
 * (issue #8, 4 of 4).
 *
 * core/slot.c carries a rule in its own header that had no test behind it: slot.h:104-115
 * explains that a rotated directory keeps the slot's name with a timestamp suffix and
 * stays inside the save directory, and that when D5 lands and something scans slots, it
 * must skip anything carrying the suffix "or a rotated world becomes a selectable save
 * that is really a corpse". Nothing checked what rotate does, so the hazard was handed on
 * as prose only.
 *
 * run_api.sh guarded rotation by grepping the source for the symbol name, which a
 * wrong-but-present function passes forever. This is the behavioural version.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/slot.h"
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

#define ROOT "/var/tmp/u2_slot_check"

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int is_file(const char *p) { struct stat st; return stat(p, &st) == 0 && !S_ISDIR(st.st_mode); }
static long size_of(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

static void put(const char *rel, const char *body)
{
    char path[700];
    snprintf(path, sizeof path, "%s/%s", ROOT, rel);
    char dir[700];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir(dir, 0755); }
    FILE *f = fopen(path, "wb");
    if (f) { fputs(body, f); fclose(f); }
}

/* Wipe and remake the fixture so a leftover archive from a previous run cannot make a
 * rotate assertion pass vacuously. */
static void fresh(void)
{
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", ROOT);
    if (system(cmd) != 0) { printf("slot_check: cannot clear fixture\n"); }
    mkdir(ROOT, 0755);
}

int main(void)
{
    /* --- the default names, with no ctx set at all ------------------------------ */
    {
        u2_ctx c;
        memset(&c, 0, sizeof c);
        CHECK(!strcmp(u2_slot_name(&c), U2_SAVE_SLOT_DEFAULT),
              "the slot name must default to %s, got '%s'",
              U2_SAVE_SLOT_DEFAULT, u2_slot_name(&c));
        char b[256];
        u2_save_dir(&c, b, sizeof b);
        CHECK(!strcmp(b, U2_SAVE_DEFAULT_DIR), "save dir must default to '%s', got '%s'",
              U2_SAVE_DEFAULT_DIR, b);
        u2_slot_dir(&c, b, sizeof b);
        CHECK(!strcmp(b, U2_SAVE_DEFAULT_DIR "/" U2_SAVE_SLOT_DEFAULT),
              "slot dir must be save_dir/slot, got '%s'", b);
    }

    fresh();
    u2_ctx c;
    memset(&c, 0, sizeof c);
    c.save_dir = ROOT;

    /* --- ensure: two plain mkdirs, and a file where the save dir should be fails -- */
    {
        char b[256];
        u2_slot_dir(&c, b, sizeof b);
        CHECK(!is_dir(b), "u2_slot_dir must not create anything");
        CHECK(u2_slot_ensure(&c) == 0, "ensure must create the slot");
        CHECK(is_dir(b), "the slot directory must exist after ensure");
        CHECK(u2_slot_ensure(&c) == 0, "ensure on an existing slot is not an error");
    }

    /* --- slot_path and slot_find -------------------------------------------------- */
    {
        char b[256];
        u2_slot_path(b, sizeof b, &c, "PLAYER");
        CHECK(strstr(b, "PLAYER/PLAYER") != NULL, "slot_path must be slot/name, got '%s'", b);
        CHECK(!is_file(b), "slot_path must not create anything");

        char out[256];
        CHECK(!u2_slot_find(&c, "PLAYER", out, sizeof out),
              "an absent record must not be found");
        CHECK(out[0] == '\0', "a miss must leave `out` empty, got '%s'", out);

        put("PLAYER/PLAYER", "a record, not empty");
        CHECK(u2_slot_find(&c, "PLAYER", out, sizeof out), "the record must be found");
        CHECK(is_file(out), "find must return a real path, got '%s'", out);

        /* A directory is not a record. The slot directory itself is named PLAYER, so
         * this is not a hypothetical: find(PLAYER) at the save-dir level would match
         * the directory if the check were `stat` alone. */
        CHECK(!u2_slot_find(&c, ".", out, sizeof out),
              "a directory must never be found as a record");
    }

    /* --- adopt_legacy: rename only, and nothing created when there is nothing ------ */
    {
        fresh();
        u2_ctx a;
        memset(&a, 0, sizeof a);
        a.save_dir = ROOT;
        CHECK(u2_slot_adopt_legacy(&a) == 0, "nothing to adopt must be ok, not an error");
        CHECK(!is_dir(ROOT "/PLAYER"),
              "adopt must create nothing when there is nothing to adopt (D6: an empty "
              "slot is a lie)");

        /* The old layout wrote a FILE at <save_dir>/PLAYER, exactly where the slot
         * directory has to go. */
        put("PLAYER", "the pre-slot record");
        CHECK(is_file(ROOT "/PLAYER"), "fixture: the legacy record must be a file");
        CHECK(u2_slot_adopt_legacy(&a) == 0, "adopting a legacy record must succeed");
        CHECK(is_dir(ROOT "/PLAYER"), "after adopt the slot must be a directory");
        CHECK(is_file(ROOT "/PLAYER/PLAYER"), "the record must be inside the slot");
        CHECK(size_of(ROOT "/PLAYER/PLAYER") == (long)strlen("the pre-slot record"),
              "adopt must move the bytes, not truncate them");
        /* Idempotent: a second call sees a directory and has nothing to do. */
        CHECK(u2_slot_adopt_legacy(&a) == 0, "adopt twice must be ok");
    }

    /* --- rotate: what counts as a game ------------------------------------------- */
    {
        char out[256];
        fresh();
        u2_ctx r;
        memset(&r, 0, sizeof r);
        r.save_dir = ROOT;

        CHECK(u2_slot_rotate(&r, out, sizeof out) == 0,
              "an absent slot rotates to nothing");
        CHECK(out[0] == '\0', "no rotation must leave `out` empty");
        CHECK(!is_dir(ROOT "/PLAYER"), "rotate must not create a slot that was absent");

        u2_slot_ensure(&r);
        CHECK(u2_slot_rotate(&r, out, sizeof out) == 0,
              "an empty slot is not a game (D6) and must not rotate");
        CHECK(!is_dir(ROOT "/PLAYER.2"), "no archive may appear for an empty slot");

        put("PLAYER/PLAYER", "");
        CHECK(size_of(ROOT "/PLAYER/PLAYER") == 0, "fixture: zero-length record");
        CHECK(u2_slot_rotate(&r, out, sizeof out) == 0,
              "a zero-length record is not a game and must not rotate");

        /* Now a real game. */
        put("PLAYER/PLAYER", "a real character");
        CHECK(u2_slot_rotate(&r, out, sizeof out) == 1, "a slot with a record must rotate");
        CHECK(out[0] != '\0', "a rotation must report where it moved the world");
        CHECK(is_dir(out), "the archive must exist");
        CHECK(strstr(out, ROOT) == out, "the archive must stay inside the save directory, got '%s'", out);
        {
            char arec[512];
            snprintf(arec, sizeof arec, "%s/PLAYER", out);
            CHECK(is_file(arec), "the archive must still hold the record, got '%s'", arec);
        }
        CHECK(!is_dir(ROOT "/PLAYER"),
              "the slot must be gone after rotating, so the new character cannot read "
              "the old world");

        /* The suffix rule slot.h hands on: the archive carries the slot name plus a
         * stamp, and it is NOT named exactly the slot. A future scanner that lists
         * slots must be able to tell the two apart, and this is the only thing that
         * makes that distinguishable today. */
        {
            const char *base = strrchr(out, '/');
            base = base ? base + 1 : out;
            CHECK(strncmp(base, U2_SAVE_SLOT_DEFAULT, strlen(U2_SAVE_SLOT_DEFAULT)) == 0,
                  "the archive must be recognisably the slot it came from, got '%s'", base);
            CHECK(strcmp(base, U2_SAVE_SLOT_DEFAULT) != 0,
                  "the archive must not be named exactly the slot, or a scanner cannot "
                  "tell a corpse from a game");
            CHECK(strchr(base, '.') != NULL,
                  "the archive must carry a suffix, got '%s'", base);
        }

        /* Two rotations in one second are possible — a scripted harness does it — and
         * the second must not land on the first. */
        {
            char second[256];
            u2_slot_ensure(&r);
            put("PLAYER/PLAYER", "another character");
            CHECK(u2_slot_rotate(&r, second, sizeof second) == 1,
                  "a second rotation must also succeed");
            CHECK(strcmp(second, out) != 0,
                  "two archives in one second must not collide: both are '%s'", out);
            CHECK(is_dir(second) && is_dir(out), "both archives must survive");
        }
    }

    printf("slot_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
