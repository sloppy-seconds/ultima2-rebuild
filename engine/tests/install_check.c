/*
 * tests/install_check.c — the gate from docs/install-design.md § 7.
 *
 * The fixtures are engine-authored filler, not game content: a release directory
 * and a Galaxy patch directory are synthesised under the work dir, so this runs in
 * CI with no media present, and the byte-identity assertions are still real because
 * they compare the install against the bytes that went in.
 *
 * The assertion that matters most is the engine one: the installed tree, pointed at
 * with no other flags, loads Earth AND an outer planet. Everything else can pass
 * while the install is useless; that one cannot.
 *
 * Build and run by tests/run_cp4.sh.
 */

#define _POSIX_C_SOURCE 200809L   /* mkdir, unlink, opendir under -std=c11 */
#define _DEFAULT_SOURCE           /* realpath, for the recorded-source comparison */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/config.h"
#include "core/ctx.h"
#include "core/install.h"
#include "core/world.h"

static int fails, checks;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        checks++;                                                       \
        if (cond) printf("  ok    %s\n", msg);                          \
        else      { printf("  FAIL  %s\n", msg); fails++; }             \
    } while (0)

static long size_of(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static void write_bytes(const char *p, int fill, size_t n)
{
    FILE *f = fopen(p, "wb");
    if (!f) { printf("  FAIL  cannot write %s\n", p); fails++; return; }
    unsigned char *buf = malloc(n ? n : 1);
    for (size_t i = 0; i < n; i++)
        buf[i] = (unsigned char)((fill + i) & 0xFF);
    fwrite(buf, 1, n, f);
    free(buf);
    fclose(f);
}

static int copy_file(const char *src, const char *dst)
{
    FILE *i = fopen(src, "rb");
    if (!i) return -1;
    FILE *o = fopen(dst, "wb");
    if (!o) { fclose(i); return -1; }
    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, i)) > 0)
        if (fwrite(buf, 1, n, o) != n) { fclose(i); fclose(o); return -1; }
    fclose(i);
    fclose(o);
    return 0;
}

static int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int same_bytes(const char *a, const char *b)
{
    const long sa = size_of(a), sb = size_of(b);
    if (sa < 0 || sa != sb)
        return 0;
    unsigned char ba[4096], bb[4096];
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }
    int same = 1;
    for (;;) {
        const size_t na = fread(ba, 1, sizeof ba, fa);
        const size_t nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb || memcmp(ba, bb, na) != 0) { same = 0; break; }
        if (na == 0) break;
    }
    fclose(fa); fclose(fb);
    return same;
}

typedef struct { char name[512]; } entry;

static int collect(const char *root, const char *rel, entry *out, int n, int max)
{
    char path[1024];
    if (*rel) snprintf(path, sizeof path, "%.880s/%.130s", root, rel);
    else      snprintf(path, sizeof path, "%.1023s", root);
    DIR *d = opendir(path);
    if (!d)
        return n;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char child[600], full[1024];
        if (*rel) snprintf(child, sizeof child, "%.460s/%.127s", rel, e->d_name);
        else      snprintf(child, sizeof child, "%.127s", e->d_name);
        snprintf(full, sizeof full, "%.880s/%.130s", root, child);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            n = collect(root, child, out, n, max);
        else {
            snprintf(out[n].name, sizeof out[n].name, "%.511s", child);
            n++;
        }
    }
    closedir(d);
    return n;
}

static int has(entry *list, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(list[i].name, name))
            return 1;
    return 0;
}

/* The fixture contents. Both spellings of a shadowed name, tooling that must be
 * skipped, and a subdirectory that must not be walked into. */
static const char *const k_release[] = {
    "mapx00", "mapx40", "monx00", "monx40", "tlkx03", "monsters",
    "player", "ultimaii.exe", "picdng", "PICDNG-E",
};
#define N_PATCH_CONTENT 4
static const char *const k_patch[] = {
    "MAPG00", "MAPG40", "MONG40", "TLKG40",
    "mapx00",           /* also in the release: the patch copy wins (D8) */
    "cgapatch.exe",     /* patcher tooling, not content */
    "RENGAL.BAT",       /* a script that renames things, never run by us */
    /* The real release spells these lowercase and updated/ spells them uppercase,
     * so a case-sensitive plan would write both and leave the engine holding the
     * corrupt one. This is the mapx15 / TLKX03 case from the shipped media. */
    "TLKX03",
};

static int rel_fill(int i)  { return 0x10 + i * 7; }
static int rel_size(int i)  { return (i % 3) ? 4224 : 4096; }
static int upd_fill(int i)  { return 0x80 + i * 5; }

static void build_fixtures(const char *base_in)
{
    /* Copied into a bounded array so every snprintf below is provably large
     * enough: gcc's format-truncation check cannot bound a bare `const char *`
     * parameter, and -Werror is one of the engine's gates, not a suggestion. */
    char base[512];
    snprintf(base, sizeof base, "%.511s", base_in);

    char dir[1024];
    snprintf(dir, sizeof dir, "%.1000s/rel", base); mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%.1000s/upd", base); mkdir(dir, 0755);

    for (size_t i = 0; i < sizeof k_release / sizeof k_release[0]; i++) {
        snprintf(dir, sizeof dir, "%.1000s/rel/%.127s", base, k_release[i]);
        write_bytes(dir, rel_fill((int)i), (size_t)rel_size((int)i));
    }
    for (size_t i = 0; i < sizeof k_patch / sizeof k_patch[0]; i++) {
        snprintf(dir, sizeof dir, "%.1000s/upd/%.127s", base, k_patch[i]);
        write_bytes(dir, upd_fill((int)i), 4096);
    }
    snprintf(dir, sizeof dir, "%.1000s/rel/README.txt", base); write_bytes(dir, 1, 32);
    snprintf(dir, sizeof dir, "%.1000s/rel/run.bat", base);    write_bytes(dir, 2, 32);
    snprintf(dir, sizeof dir, "%.1000s/rel/updated", base);    mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%.1000s/rel/updated/MAPG99", base); write_bytes(dir, 3, 64);
}

int main(int argc, char **argv)
{
    char base[512];
    snprintf(base, sizeof base, "%.511s", argc > 1 ? argv[1] : ".");
    char to[1024], rel[1024], upd[1024], err[512], p[1024], q[1024];
    FILE *log = getenv("U2_LOG") ? fopen(getenv("U2_LOG"), "w") : fopen("/dev/null", "w");
    entry files[512];

    snprintf(to, sizeof to, "%.1000s/game", base);
    snprintf(rel, sizeof rel, "%.1000s/rel", base);
    snprintf(upd, sizeof upd, "%.1000s/upd", base);
    build_fixtures(base);

    /* --- classification: content by name, and which layer it belongs to ---- */
    {
        int g = -1;
        CHECK(u2_install_classify("MAPG40", &g) && g == 1, "MAPG40 is content, patch layer");
        CHECK(u2_install_classify("mapx00", &g) && g == 0, "mapx00 is content, release layer");
        CHECK(u2_install_classify("TLKG03", &g) && g == 1, "TLKG03 is content, patch layer");
        CHECK(u2_install_classify("PICDNG-E", &g) && g == 0, "PICDNG-E is content");
        CHECK(u2_install_classify("monsters", &g) && g == 0, "monsters is content");
        CHECK(u2_install_classify("ultimaii.exe", &g), "ultimaii.exe is content");
        CHECK(!u2_install_classify("cgapatch.exe", &g), "a patcher binary is not content");
        CHECK(!u2_install_classify("RENGAL.BAT", &g), "a batch script is not content");
        CHECK(!u2_install_classify("README.txt", &g), "a README is not content");
        CHECK(!u2_install_classify("mapx0", &g), "a malformed sector name is not content");
        CHECK(!u2_install_classify("mapxy0", &g), "nor is a near miss");
    }

    /* --- a foreign non-empty target is refused, and left alone ------------- */
    {
        char foreign[1024], inside[1024];
        snprintf(foreign, sizeof foreign, "%.1000s/foreign", base); mkdir(foreign, 0755);
        snprintf(inside, sizeof inside, "%.1000s/foreign/keep.me", base);
        write_bytes(inside, 0x5A, 7);

        u2_install_request rq = { .to = foreign, .original_dir = rel, .updated_dir = upd };
        CHECK(u2_install_apply(&rq, log, err, sizeof err) == U2_INSTALL_REFUSED,
              "a non-empty directory with no manifest is refused");
        CHECK(size_of(inside) == 7, "  and nothing in it was touched");
        CHECK(strstr(err, "not empty") != NULL, "  and the refusal says why");
    }

    /* Snapshot two sources so "the source is untouched" is a comparison and not
     * a re-stat of the same thing twice. */
    snprintf(p, sizeof p, "%.1000s/snap-mapx00", base);
    snprintf(q, sizeof q, "%.1000s/snap-MAPG40", base);
    {
        char src[1024];
        snprintf(src, sizeof src, "%.1000s/rel/mapx00", base);   copy_file(src, p);
        snprintf(src, sizeof src, "%.1000s/upd/MAPG40", base);   copy_file(src, q);
    }

    /* --- greenfield install ------------------------------------------------ */
    u2_install_request rq = { .to = to, .original_dir = rel, .updated_dir = upd };
    CHECK(u2_install_apply(&rq, log, err, sizeof err) == U2_INSTALL_OK,
          "install into a nonexistent directory creates it");

    int n = collect(to, "", files, 0, 512);

    /* assertion 1: exactly the expected file set */
    CHECK(has(files, n, "_INSTALL.tsv"), "the manifest is written");
    CHECK(has(files, n, "config.ini"), "config.ini is written");
    CHECK(has(files, n, "data/original/mapx00"), "the release layer holds mapx00");
    CHECK(has(files, n, "data/updated/MAPG40"), "the patch layer holds MAPG40");
    CHECK(has(files, n, "data/original/PICDNG-E"), "picture sidecars are content");
    CHECK(!has(files, n, "saves/player") && !has(files, n, "saves/PLAYER"),
          "the installer seeds no save at all: a slot that holds nothing is a lie (D6)");
    CHECK(!has(files, n, "data/original/README.txt"), "a README is not copied");
    CHECK(!has(files, n, "data/original/run.bat"), "DOS launcher scaffolding is not copied");
    CHECK(!has(files, n, "data/updated/cgapatch.exe"), "patcher tooling is not copied");
    CHECK(!has(files, n, "data/updated/RENGAL.BAT"), "  nor the rename script");
    CHECK(!has(files, n, "data/updated/MAPG99"), "a subdirectory of a source is not walked into");
    CHECK(!has(files, n, "data/original/mapx99"), "  and nothing leaked from it");
    CHECK(n == 16, "the tree holds exactly the expected number of files");
    if (n != 16)
        for (int i = 0; i < n; i++)
            printf("        have %s\n", files[i].name);

    /* assertion 2: byte-identical copies */
    {
        int all_same = 1;
        for (size_t i = 0; i < sizeof k_release / sizeof k_release[0]; i++) {
            /* mapx00 and tlkx03 exist in both fixtures, and per D8 the installed
             * copy is the patch's, so the release comparison cannot hold for them.
             * They get their own assertions below. */
            if (!strcmp(k_release[i], "mapx00") || !strcmp(k_release[i], "tlkx03"))
                continue;
            char a[1024], b[1024];
            snprintf(a, sizeof a, "%.1000s/rel/%.127s", base, k_release[i]);
            snprintf(b, sizeof b, "%.480s/data/original/%.127s", to, k_release[i]);
            if (!same_bytes(a, b)) { printf("        differs: %s\n", k_release[i]); all_same = 0; }
        }
        for (int i = 0; i < N_PATCH_CONTENT; i++) {
            char a[1024], b[1024];
            snprintf(a, sizeof a, "%.1000s/upd/%.127s", base, k_patch[i]);
            snprintf(b, sizeof b, "%.480s/data/updated/%.127s", to, k_patch[i]);
            if (!same_bytes(a, b)) { printf("        differs: %s\n", k_patch[i]); all_same = 0; }
        }
        CHECK(all_same, "every copied file is byte-identical to its source");
    }

    /* D8: a file in both sources takes the PATCH copy when the bytes differ, and
     * keeps the release's spelling of the name. The old rule was first-source-wins
     * with the release scanned first, which meant the corrupt release mapx15 beat
     * the copy written to fix it. */
    {
        char a[1024], b[1024], c[1024];
        snprintf(a, sizeof a, "%.1000s/rel/mapx00", base);
        snprintf(b, sizeof b, "%.1000s/upd/mapx00", base);
        snprintf(c, sizeof c, "%.480s/data/original/mapx00", to);
        CHECK(same_bytes(b, c) && !same_bytes(a, c),
              "a file in both sources is taken from the patch, not the release (D8)");
    }

    /* ...and the case difference does not fork the destination: TLKX03 from the
     * patch lands on the release's tlkx03, one file, patched bytes. */
    {
        char b[1024], c[1024], dup[1024];
        snprintf(b, sizeof b, "%.1000s/upd/TLKX03", base);
        snprintf(c, sizeof c, "%.480s/data/original/tlkx03", to);
        snprintf(dup, sizeof dup, "%.480s/data/original/TLKX03", to);
        CHECK(same_bytes(b, c), "the uppercase patch name lands on the release spelling");
        CHECK(!file_exists(dup), "the two casings collapse to one file, not two");
    }

    /* D9: with no --updated-dir, <original>/updated is the patch layer. The Galaxy
     * patch ships inside the release directory, so asking for it separately asks
     * the player to know the pack's layout. */
    {
        char game9[1024], want[1024];
        snprintf(game9, sizeof game9, "%.480s/game-d9", base);
        snprintf(want, sizeof want, "%.480s/game-d9/data/updated/MAPG99", base);
        u2_install_request d9 = { .to = game9, .original_dir = rel };
        CHECK(u2_install_apply(&d9, log, err, sizeof err) == U2_INSTALL_OK,
              "an install with no --updated-dir still finds the patch inside the release");
        CHECK(file_exists(want), "  and the patch layer it found is installed");
    }

    /* The target may hold one foreign file: this executable. That is the shape of
     * "make a folder, drop the engine in it, run --install", and without the
     * exception the first thing a player meets is a refusal to write into the only
     * directory they have. Matched by inode, so the fixture hard-links rather than
     * copies: a copy is a different file and must not qualify. */
    {
        char self[1024];
        const ssize_t sn = readlink("/proc/self/exe", self, sizeof self - 1);
        if (sn > 0) {
            self[sn] = '\0';
            char only[1024], both[1024], lnk[1024];
            snprintf(only, sizeof only, "%.480s/t-selfonly", base);
            snprintf(both, sizeof both, "%.480s/t-selfplus", base);
            mkdir(only, 0755);
            mkdir(both, 0755);
            snprintf(lnk, sizeof lnk, "%.480s/t-selfonly/u2check", base);
            const int l1 = link(self, lnk);
            snprintf(lnk, sizeof lnk, "%.480s/t-selfplus/u2check", base);
            const int l2 = link(self, lnk);
            if (l1 == 0 && l2 == 0) {
                snprintf(lnk, sizeof lnk, "%.480s/t-selfplus/someone-elses.bin", base);
                write_bytes(lnk, 0x5a, 64);
                u2_install_request a = { .to = only, .original_dir = rel, .self_path = self };
                CHECK(u2_install_apply(&a, log, err, sizeof err) == U2_INSTALL_OK,
                      "a target holding only this executable is treated as empty");
                u2_install_request b = { .to = both, .original_dir = rel, .self_path = self };
                CHECK(u2_install_apply(&b, log, err, sizeof err) == U2_INSTALL_REFUSED,
                      "one more file beside the executable and the target is foreign again");
            } else {
                CHECK(1, "self-binary exception skipped (hard link unavailable)");
            }
        }
    }

    /* A prompt with no terminal behind it must refuse, not block. Piped stdin is how
     * CI drives this binary, and a question that waits forever is the failure mode
     * the tty gate exists to prevent; this proves the gate holds even if something
     * calls the prompt anyway. */
    {
        FILE *devnull = fopen("/dev/null", "r");
        if (devnull) {
            FILE *saved = stdin;
            stdin = devnull;
            u2_install_request preq;
            memset(&preq, 0, sizeof preq);
            u2_install_answers ans;
            const int pr = u2_install_prompt(&preq, &ans, log, err, sizeof err);
            stdin = saved;
            fclose(devnull);
            CHECK(pr == -1, "a prompt with no terminal refuses instead of blocking");
        }
    }

    /* assertion 6: the sources are byte-for-byte untouched */
    {
        char src[1024];
        snprintf(src, sizeof src, "%.1000s/rel/mapx00", base);
        CHECK(same_bytes(src, p), "the release source is byte-identical after an install");
        snprintf(src, sizeof src, "%.1000s/upd/MAPG40", base);
        CHECK(same_bytes(src, q), "the patch source is byte-identical after an install");
    }

    /* assertion 4: re-running is a no-op (D7a) */
    {
        entry pre[512];
        const int npre = collect(to, "", pre, 0, 512);
        CHECK(u2_install_apply(&rq, log, err, sizeof err) == U2_INSTALL_OK,
              "re-running into our own tree is allowed (manifest recognised)");
        const int npost = collect(to, "", files, 0, 512);
        CHECK(npre == npost, "  and a second run changes nothing");
    }

    /* assertion 3: the engine reads the installed tree with no other flags */
    {
        u2_ctx ctx;
        u2_world w;
        char sv[1024];
        memset(&ctx, 0, sizeof ctx);
        ctx.data_dir = to;
        /* Pinned, not left to the "saves" default: the default is relative to the
         * cwd, and a gate that writes a slot into whatever directory it happens to
         * be run from is a gate that dirties the engine tree. */
        snprintf(sv, sizeof sv, "%.1000s/saves", to);
        ctx.save_dir = sv;
        CHECK(u2_world_open(&ctx) == U2_WORLD_OK, "the installed tree opens as a content dir");

        u2_sector_id earth = { .hi = 0, .lo = 0 };
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, earth) == U2_WORLD_OK, "Earth loads from the installed tree");
        CHECK(w.map_root && strcmp(w.map_root, "data/original") == 0, "  from the release layer");

        ctx.state.world = 4;
        u2_sector_id jup = { .hi = 4, .lo = 0 };
        memset(&w, 0, sizeof w);
        CHECK(u2_world_load(&ctx, &w, 1, jup) == U2_WORLD_OK,
              "an outer planet loads from the installed tree, which is the whole point");
        CHECK(w.map_root && strcmp(w.map_root, "data/updated") == 0,
              "  from the patch layer, not the Earth clone beside it");
        ctx.state.world = 0;
    }

    /* config.ini is real: config.c reads it and resolves it against the file */
    {
        char path[1024];
        u2_config cfg;
        char cerr[512];
        snprintf(path, sizeof path, "%.1000s/config.ini", to);
        CHECK(u2_config_load(path, &cfg, cerr, sizeof cerr) == U2_CONFIG_OK,
              "the generated config.ini loads and refuses nothing");
        u2_ctx ctx;
        memset(&ctx, 0, sizeof ctx);
        u2_config_apply(&ctx, &cfg);
        CHECK(ctx.data_dir && strstr(ctx.data_dir, "game") != NULL,
              "  and it pins the content root to the installed tree");
        CHECK(ctx.save_dir && strstr(ctx.save_dir, "saves") != NULL,
              "  and the saves directory");
    }

    /* --- the installer records what it was given ----------------------------
     *
     * The point of [install] is that a binary kept beside the game can reinstall
     * with no arguments, so the assertions are about the whole loop: the section is
     * written, it is found beside the executable, it fills an empty request, and a
     * request filled from it reproduces the same tree. Precedence is the engine's
     * standing flag > config.ini > nothing, and a source that has moved is left
     * unset for the prompt rather than silently dropped. */
    {
        char cpath[1024], line[1024], relabs[1024], selfside[1024];
        int has_to = 0, has_orig = 0;
        snprintf(cpath, sizeof cpath, "%.1000s/config.ini", to);
        FILE *cf = fopen(cpath, "rb");
        while (cf && fgets(line, sizeof line, cf)) {
            if (!strncmp(line, "to = .", 6)) has_to = 1;
            if (!strncmp(line, "original = ", 11)) has_orig = 1;
        }
        if (cf) fclose(cf);
        CHECK(has_to && has_orig,
              "config.ini records the target and the source --install used");

        CHECK(u2_install_find_config(NULL, NULL, line, sizeof line) == 0
              || strcmp(line, U2_CONFIG_DEFAULT_FILE) == 0,
              "with no config beside the run, --install falls back to asking");
        snprintf(selfside, sizeof selfside, "%.1000s/u2check", to);
        write_bytes(selfside, 0x7f, 8);
        CHECK(u2_install_find_config(NULL, selfside, cpath, sizeof cpath) == 1
              && strstr(cpath, "config.ini") != NULL,
              "a binary kept inside the game directory finds its config.ini");

        u2_config icfg;
        char ierr2[512];
        CHECK(u2_config_load(cpath, &icfg, ierr2, sizeof ierr2) == U2_CONFIG_OK,
              "  and that file loads through the game's own config reader");
        u2_install_request filled;
        memset(&filled, 0, sizeof filled);
        u2_install_fill_from_config(&filled, &icfg, log);
        CHECK(filled.to && strcmp(filled.to, to) == 0,
              "  an empty request gets the target back, resolved to the tree itself");
        if (!realpath(rel, relabs)) snprintf(relabs, sizeof relabs, "%.1023s", rel);
        CHECK(filled.original_dir && strcmp(filled.original_dir, relabs) == 0,
              "  and the source it was built from");

        u2_install_request flagged;
        memset(&flagged, 0, sizeof flagged);
        flagged.to = "somewhere-else";
        u2_install_fill_from_config(&flagged, &icfg, log);
        CHECK(flagged.to && strcmp(flagged.to, "somewhere-else") == 0,
              "  a flag still outranks the recorded path (flag > config.ini)");

        /* The reinstall with no arguments at all: same tree, nothing changed. */
        entry pre2[512];
        const int npre2 = collect(to, "", pre2, 0, 512);
        CHECK(u2_install_apply(&filled, log, err, sizeof err) == U2_INSTALL_OK,
              "--install with no arguments reinstalls the recorded tree");
        const int npost2 = collect(to, "", files, 0, 512);
        CHECK(npre2 == npost2, "  and it changes nothing (idempotent through the record)");
        unlink(selfside);

        /* A source that has moved is reported and left unset, so the prompt asks or
         * --no-prompt refuses. Silently dropping it would build a different tree
         * under the same manifest. */
        {
            u2_config stale;
            memset(&stale, 0, sizeof stale);
            stale.present = 1;
            snprintf(stale.inst_original, sizeof stale.inst_original,
                     "%.400s/no-such-source-anywhere", base);
            snprintf(stale.inst_to, sizeof stale.inst_to, "%.447s", to);
            u2_install_request srq;
            memset(&srq, 0, sizeof srq);
            u2_install_fill_from_config(&srq, &stale, log);
            CHECK(srq.original_dir == NULL,
                  "a recorded source that no longer exists is not used");
            CHECK(srq.to && strcmp(srq.to, to) == 0,
                  "  while the sources that are still there still fill");
        }
    }

    /* --- themes: the v1 reshape, built by the same installer ---------------- */
    {
        char art[1024], d[1024], e[1024];
        snprintf(art, sizeof art, "%.480s/art", base);            mkdir(art, 0755);
        snprintf(d, sizeof d, "%.480s/EGATILES", art);            write_bytes(d, 0x21, 8320);
        snprintf(d, sizeof d, "%.480s/EGACOLOR", art);            write_bytes(d, 0x22, 32);
        snprintf(d, sizeof d, "%.480s/PICDNG.EGA", art);          write_bytes(d, 0x23, 64000);
        snprintf(d, sizeof d, "%.480s/CGATILES", art);            write_bytes(d, 0x24, 8192);
        snprintf(d, sizeof d, "%.480s/README.txt", art);          write_bytes(d, 0x25, 40);
        snprintf(d, sizeof d, "%.480s/u2cfg.exe", art);           write_bytes(d, 0x26, 40);
        snprintf(d, sizeof d, "%.480s/EGATHEME.C64", art);        mkdir(d, 0755);
        snprintf(e, sizeof e, "%.480s/EGATILES", d);               write_bytes(e, 0x31, 8320);
        snprintf(e, sizeof e, "%.480s/EGACOLOR", d);               write_bytes(e, 0x32, 32);
        snprintf(d, sizeof d, "%.480s/EGATHEME.ALT", art);        mkdir(d, 0755);
        snprintf(e, sizeof e, "%.480s/EGATILES", d);               write_bytes(e, 0x41, 8320);

        /* A second source with no atlas at all, which is how the Galaxy directory
         * arrives: it ships the full PIC*-E picture set and no themes, and that is
         * a complete contribution rather than a defect. PICDNG-E is the same
         * picture as art's PICDNG.EGA in another spelling. */
        char art2[1024];
        snprintf(art2, sizeof art2, "%.480s/art2", base);          mkdir(art2, 0755);
        snprintf(d, sizeof d, "%.480s/PICDNG-E", art2);            write_bytes(d, 0x61, 64000);
        snprintf(d, sizeof d, "%.480s/PICMIN-E", art2);            write_bytes(d, 0x62, 64000);

        u2_install_request with_themes = { .to = to, .original_dir = rel, .updated_dir = upd,
                                           .themes_dir = { art, art2 }, .n_themes_dir = 2 };
        CHECK(u2_install_apply(&with_themes, log, err, sizeof err) == U2_INSTALL_OK,
              "install with --themes-dir builds themes/");

        int tn = collect(to, "", files, 0, 512);
        CHECK(has(files, tn, "themes/_BUILD.tsv"), "the themes manifest is written");
        CHECK(has(files, tn, "themes/default/TILES"),
              "the default theme is reshaped to a v2 directory (EGATILES becomes TILES)");
        CHECK(has(files, tn, "themes/default/THEME.INI"),
              "  and gains a THEME.INI describing it");
        CHECK(has(files, tn, "themes/default/EGACOLOR"), "  and keeps its colour remap");
        CHECK(has(files, tn, "themes/EGATHEME.C64/EGATILES") &&
              has(files, tn, "themes/EGATHEME.C64/EGACOLOR"),
              "a named theme is copied whole, extras included");
        CHECK(has(files, tn, "themes/EGATHEME.ALT/EGATILES"),
              "a theme that ships an atlas alone is still a theme");
        CHECK(has(files, tn, "themes/PICDNG.EGA"),
              "the default theme's picture is hoisted to the root, where pic_ega.c looks");
        CHECK(has(files, tn, "themes/PICMIN-E"),
              "a picture only the second source ships still lands");
        /* PICDNG.EGA and PICDNG-E are the same picture in two spellings and
         * pic_ega.c tries both, so the root holds one per base and the first
         * source wins. Keying the dedup on the filename instead fills the root
         * with duplicates that differ only in a dash -- the exact divergence the
         * C builder had against the Python one it replaces. */
        CHECK(!has(files, tn, "themes/PICDNG-E"),
              "two spellings of one picture base do not both land at the themes root");
        CHECK(has(files, tn, "themes/default/PICDNG.EGA"),
              "  and stays inside its own theme directory too");
        CHECK(!has(files, tn, "themes/CGATILES") && !has(files, tn, "themes/default/CGATILES"),
              "the CGA atlas is not theme content");
        CHECK(!has(files, tn, "themes/README.txt") && !has(files, tn, "themes/u2cfg.exe"),
              "docs and patcher binaries stay out of the themes tree");

        /* The installer does NOT pin a theme (owner ruling 2026-11-13): the engine
         * auto-picks `10` -> `default` -> fmtowns -> CGA at launch, and a recorded
         * `theme =` line is an explicit choice as far as auto-detection is
         * concerned, so it would freeze that order and override a later Ctrl-T or a
         * changed install. The [themes] dir line is still written; the theme line is
         * a commented example. Assert both halves: the dir is named, and no active
         * theme is set (a commented `# theme =` line is allowed). */
        {
            char cpath[1200];
            snprintf(cpath, sizeof cpath, "%.1000s/config.ini", to);
            FILE *cf = fopen(cpath, "rb");
            char buf[4096];
            size_t got = cf ? fread(buf, 1, sizeof buf - 1, cf) : 0;
            if (cf) fclose(cf);
            if (got) buf[got] = '\0';
            CHECK(cf && got && strstr(buf, "dir = themes"),
                  "config.ini names the themes directory");
            CHECK(cf && got && !strstr(buf, "\ntheme = "),
                  "config.ini pins no active theme (the engine auto-picks)");
        }

        /* the atlas landed byte-identical, which is the whole no-re-encode rule */
        {
            char a[1024], b[1024];
            snprintf(a, sizeof a, "%.480s/EGATILES", art);
            snprintf(b, sizeof b, "%.480s/themes/default/TILES", to);
            CHECK(same_bytes(a, b), "the reshaped atlas is byte-identical to its source");
            snprintf(a, sizeof a, "%.480s/EGATHEME.C64/EGATILES", art);
            snprintf(b, sizeof b, "%.480s/themes/EGATHEME.C64/EGATILES", to);
            CHECK(same_bytes(a, b), "  and so is a named theme's");
        }

        /* config.ini gains [themes] only because themes actually exist now */
        {
            char path[1024];
            u2_config cfg2;
            char cerr[512];
            snprintf(path, sizeof path, "%.480s/config.ini", to);
            CHECK(u2_config_load(path, &cfg2, cerr, sizeof cerr) == U2_CONFIG_OK &&
                  *cfg2.themes && strstr(cfg2.themes, "themes") != NULL,
                  "config.ini names the themes directory once there is one");
        }

        CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_OK,
              "--check verifies the themes subtree too");

        /* a half-copied theme must not pass */
        {
            char victim[1024], keep[1024];
            snprintf(victim, sizeof victim, "%.480s/themes/EGATHEME.C64/EGACOLOR", to);
            snprintf(keep, sizeof keep, "%.480s/snap-c64color", base);
            copy_file(victim, keep);
            unlink(victim);
            CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_REFUSED,
                  "--check goes red when a theme loses a file its source ships");
            copy_file(keep, victim);
        }

        /* a themes source with no atlas anywhere in it is refused rather than
         * producing an empty themes tree, which the engine would report as
         * "no such theme" while the installer claimed success. */
        {
            char bare[1024], game3[1024];
            snprintf(bare, sizeof bare, "%.480s/artbare", base);   mkdir(bare, 0755);
            snprintf(d, sizeof d, "%.480s/README.txt", bare);      write_bytes(d, 0x71, 20);
            snprintf(game3, sizeof game3, "%.480s/game3", base);
            u2_install_request bare_rq = { .to = game3, .original_dir = rel, .updated_dir = upd,
                                           .themes_dir = { bare }, .n_themes_dir = 1 };
            CHECK(u2_install_apply(&bare_rq, log, err, sizeof err) != U2_INSTALL_OK,
                  "a themes source containing no atlas is refused, not silently empty");
        }

        /* a source whose atlas is the wrong size is refused before anything is
         * written, and the tree is not claimed as installed: no manifest, so a
         * later --check cannot mistake a half-built tree for a good one. */
        {
            char bad[1024], game2[1024];
            snprintf(bad, sizeof bad, "%.480s/artbad", base);
            snprintf(game2, sizeof game2, "%.480s/game2", base);
            mkdir(bad, 0755);
            snprintf(d, sizeof d, "%.480s/EGATILES", bad);
            write_bytes(d, 0x51, 100);          /* below the size the engine reads */
            u2_install_request bad_rq = { .to = game2, .original_dir = rel, .updated_dir = upd,
                                          .themes_dir = { bad }, .n_themes_dir = 1 };
            CHECK(u2_install_apply(&bad_rq, log, err, sizeof err) != U2_INSTALL_OK,
                  "a themes source with a short atlas is refused");
            char m[1024];
            snprintf(m, sizeof m, "%.480s/_INSTALL.tsv", game2);
            CHECK(size_of(m) < 0,
                  "  and the half-built tree carries no manifest to mistake it for good");
        }
    }

    /* --- assertion 5: --check, and the ways it goes red -------------------- */
    CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_OK,
          "--check passes a freshly installed tree");

    {
        char victim[1024], keep[1024];
        snprintf(victim, sizeof victim, "%.480s/data/original/tlkx03", to);
        snprintf(keep, sizeof keep, "%.1000s/snap-tlkx03", base);
        copy_file(victim, keep);
        unlink(victim);
        CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_REFUSED,
              "--check goes red on a missing file");
        copy_file(keep, victim);
        CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_OK,
              "  and goes green again once it is restored");
    }
    {
        snprintf(p, sizeof p, "%.480s/data/original/stray.bin", to);
        write_bytes(p, 0x33, 16);
        CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_REFUSED,
              "--check goes red on a stray file");
        unlink(p);
    }
    {
        /* Content is immutable after install now that every sector write goes to a
         * save slot, so a played-in content file is a defect rather than the
         * tolerated `played-in` note it used to be. This is a semantic flip of an
         * existing verdict, and the control that proves it can still go red is the
         * `content-drift-is-tolerated` one in tests/install_controls.py. */
        snprintf(p, sizeof p, "%.480s/data/original/mapx40", to);
        snprintf(q, sizeof q, "%.1000s/snap-mapx40-dirt", base);
        copy_file(p, q);
        write_bytes(p, 0xEE, 4224);
        CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_REFUSED,
              "--check goes red when a content sector file drifts: content is never written");
        CHECK(strstr(err, "does not match") != NULL, "  and the refusal says the tree is broken");
        copy_file(q, p);
        CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_OK,
              "  and goes green again once the content copy is restored");
    }    {
        snprintf(p, sizeof p, "%.480s/data/original/monsters", to);
        write_bytes(p, 0xEE, 4224);
        CHECK(u2_install_check(to, log, err, sizeof err) == U2_INSTALL_REFUSED,
              "--check still goes red when a file the game never rewrites changes");
    }
    {
        char foreign[1024];
        snprintf(foreign, sizeof foreign, "%.1000s/foreign", base);
        CHECK(u2_install_check(foreign, log, err, sizeof err) == U2_INSTALL_REFUSED,
              "--check refuses a tree with no manifest rather than guessing");
    }

    /* --- migration: an old flat install becomes save-scoped ------------------
     *
     * docs/save-scoped-worlds.md § 4, and the only irreversible code in this
     * change. Each case builds the old layout out of the same synthetic fixtures,
     * because the shapes that matter are the ones a real played install leaves
     * behind: a flat saves/PLAYER file where a directory now has to go, a release
     * blank beside it, and content that play already dirtied. */
    {
        char mg[1024], mgs[1024], flat[1024], lowf[1024], rec[1024];
        char map40[1024], ov40[1024], snap40[1024], dirt40[1024], blanksrc[1024];
        char snaprec[1024], snapov[1024];
        snprintf(mg,      sizeof mg,      "%.480s/game-mig", base);
        snprintf(mgs,     sizeof mgs,     "%.480s/saves", mg);
        snprintf(flat,    sizeof flat,    "%.480s/saves/PLAYER", mg);
        snprintf(lowf,    sizeof lowf,    "%.480s/saves/player", mg);
        snprintf(rec,     sizeof rec,     "%.480s/saves/PLAYER/PLAYER", mg);
        snprintf(map40,   sizeof map40,   "%.480s/data/original/mapx40", mg);
        snprintf(ov40,    sizeof ov40,    "%.480s/saves/PLAYER/mapx40", mg);
        snprintf(snap40,  sizeof snap40,  "%.1000s/mig-snap-mapx40", base);
        snprintf(dirt40,  sizeof dirt40,  "%.1000s/mig-dirt-mapx40", base);
        snprintf(snaprec, sizeof snaprec, "%.1000s/mig-snap-rec", base);
        snprintf(snapov,  sizeof snapov,  "%.1000s/mig-snap-ov", base);

        u2_install_request mrq = { .to = mg, .original_dir = rel, .updated_dir = upd };
        CHECK(u2_install_apply(&mrq, log, err, sizeof err) == U2_INSTALL_OK,
              "migration: install a tree first, then play it the old way");
        mkdir(mgs, 0755);
        write_bytes(flat, 0x77, 384);                 /* the record, flat */
        snprintf(blanksrc, sizeof blanksrc, "%.480s/data/original/player", mg);
        copy_file(blanksrc, lowf);                    /* the seed the old installer wrote */
        copy_file(map40, snap40);                     /* remember the pristine bytes */
        write_bytes(map40, 0xEE, 4224);               /* a creature stamp baked in */
        copy_file(map40, dirt40);

        u2_install_request dryrq = mrq;
        dryrq.dry_run = 1;
        entry pre[512]; const int npre = collect(mg, "", pre, 0, 512);
        CHECK(u2_install_apply(&dryrq, log, err, sizeof err) == U2_INSTALL_OK,
              "migration shows its plan under --dry-run");
        entry post[512]; const int npost = collect(mg, "", post, 0, 512);
        CHECK(npre == npost && file_exists(flat) && !file_exists(rec),
              "  and a dry run moves nothing");

        CHECK(u2_install_apply(&mrq, log, err, sizeof err) == U2_INSTALL_OK,
              "migration runs on the real tree");
        CHECK(file_exists(rec), "the flat record moved into saves/PLAYER/PLAYER");
        CHECK(!file_exists(flat), "  and the flat file is gone: moved, not copied");
        CHECK(!file_exists(lowf),
              "the release blank the old installer seeded was removed");
        CHECK(same_bytes(map40, snap40),
              "the played-in content mapx40 was restored from its recorded source");
        CHECK(file_exists(ov40) && same_bytes(ov40, dirt40),
              "  and its played-in bytes are now this save's overlay");
        CHECK(u2_install_check(mg, log, err, sizeof err) == U2_INSTALL_OK,
              "--check is green afterwards, and a slot overlay is not a stray");
        {
            /* The manifest is provenance about what the installer wrote from where.
             * A slot is player data, and a row for it would make a player's save
             * look like installer output and invite a cleanup that deletes it. */
            char man[1024], line[1024];
            snprintf(man, sizeof man, "%.480s/_INSTALL.tsv", mg);
            FILE *mf = fopen(man, "rb");
            int saves_rows = 0;
            while (mf && fgets(line, sizeof line, mf))
                if (!strncmp(line, "saves/", 6)) saves_rows++;
            if (mf) fclose(mf);
            CHECK(saves_rows == 0,
                  "the manifest records no slot overlays, so a save is not mistaken for install output");
        }

        copy_file(rec, snaprec);
        copy_file(ov40, snapov);
        const int nmid = collect(mg, "", pre, 0, 512);
        CHECK(u2_install_apply(&mrq, log, err, sizeof err) == U2_INSTALL_OK,
              "a second migration run is allowed");
        const int naft = collect(mg, "", post, 0, 512);
        CHECK(nmid == naft && same_bytes(rec, snaprec) && same_bytes(ov40, snapov),
              "  and it is a no-op: nothing moved twice");
    }

    /* A lowercase record beside a real slot record is a second game, not litter.
     * Deleting it is the failure the whole document exists to prevent, so the
     * migration promotes it to its own slot even though nothing can select a
     * second slot yet. */
    {
        char pg[1024], pgs[1024], pslot[1024], prec[1024], plow[1024], pprom[1024];
        char psnap[1024], plsnap[1024];
        snprintf(pg,     sizeof pg,     "%.480s/game-mig2", base);
        snprintf(pgs,    sizeof pgs,    "%.480s/saves", pg);
        snprintf(pslot,  sizeof pslot,  "%.480s/saves/PLAYER", pg);
        snprintf(prec,   sizeof prec,   "%.480s/saves/PLAYER/PLAYER", pg);
        snprintf(plow,   sizeof plow,   "%.480s/saves/player", pg);
        snprintf(pprom,  sizeof pprom,  "%.480s/saves/player/PLAYER", pg);
        snprintf(psnap,  sizeof psnap,  "%.1000s/mig2-snap-rec", base);
        snprintf(plsnap, sizeof plsnap, "%.1000s/mig2-snap-low", base);

        u2_install_request prq = { .to = pg, .original_dir = rel, .updated_dir = upd };
        CHECK(u2_install_apply(&prq, log, err, sizeof err) == U2_INSTALL_OK,
              "a second slot case starts from a clean install");
        mkdir(pgs, 0755);
        mkdir(pslot, 0755);
        write_bytes(prec, 0x11, 384);
        write_bytes(plow, 0x9C, 384);        /* not the release blank: someone's game */
        copy_file(prec, psnap);
        copy_file(plow, plsnap);
        CHECK(u2_install_apply(&prq, log, err, sizeof err) == U2_INSTALL_OK,
              "migration runs with two spellings of the record present");
        CHECK(file_exists(pprom) && !file_exists(plow),
              "a non-blank lowercase record is promoted to its own slot, never deleted");
        CHECK(same_bytes(prec, psnap) && same_bytes(pprom, plsnap),
              "  and neither record was overwritten: the slot kept its own bytes");
    }

    /* The other shape: only the release spelling exists, so it is not a second
     * save, it is the save, and it belongs in the default slot. */
    {
        char qg[1024], qgs[1024], qlow[1024], qrec[1024], qsnap[1024];
        snprintf(qg,    sizeof qg,    "%.480s/game-mig3", base);
        snprintf(qgs,   sizeof qgs,   "%.480s/saves", qg);
        snprintf(qlow,  sizeof qlow,  "%.480s/saves/player", qg);
        snprintf(qrec,  sizeof qrec,  "%.480s/saves/PLAYER/PLAYER", qg);
        snprintf(qsnap, sizeof qsnap, "%.1000s/mig3-snap", base);

        u2_install_request qrq = { .to = qg, .original_dir = rel, .updated_dir = upd };
        CHECK(u2_install_apply(&qrq, log, err, sizeof err) == U2_INSTALL_OK,
              "a third slot case starts from a clean install");
        mkdir(qgs, 0755);
        write_bytes(qlow, 0x5B, 384);
        copy_file(qlow, qsnap);
        CHECK(u2_install_apply(&qrq, log, err, sizeof err) == U2_INSTALL_OK,
              "migration runs with only the release spelling present");
        CHECK(file_exists(qrec) && same_bytes(qrec, qsnap) && !file_exists(qlow),
              "a lone release-spelled record becomes the default slot's record");
    }

    fclose(log);
    printf(fails ? "install check: FAIL (%d of %d)\n" : "install check: all %d checks passed\n",
           fails ? fails : checks, checks);
    return fails != 0;
}
