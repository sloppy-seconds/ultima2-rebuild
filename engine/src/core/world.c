/*
 * world.c — read and write the player's own sector files.
 *
 * The contract this file exists to honour (docs/phase4-plan.md § CP4 design,
 * docs/phase4-implementation.md § CP4):
 *
 *   - the map is runtime data, never extracted content, so nothing here embeds
 *     a tile byte or a map string;
 *   - the original rewrites its own sector files (asm/2305, AH=28h), and the
 *     owner chose the faithful behaviour for `original` mode, so a write-back
 *     has to survive a crash: a half-written world file has no pristine copy the
 *     way the PLAYER record does, which is why every write here is temp+rename;
 *   - and it has to land in the save slot, never in the content directory, so a
 *     player's release maps stay an immutable base and a stamped sector is a
 *     deletable overlay. docs/save-scoped-worlds.md;
 *   - a content directory that cannot be used is reported, never worked around.
 *     A silently empty map would be worse than the original, which has no way to
 *     lose a map change.
 */

#define _POSIX_C_SOURCE 200809L   /* access(), rename() under -std=c11 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/world.h"
#include "core/compat.h"
#include "core/roots.h"
#include "core/slot.h"

/* Spelling candidates for the sector name. DOS names are case-insensitive, the
 * shipped pack is lowercase, and the Galaxy patch in the fan pack renames
 * mapx -> mapg and monx -> mong (RENGAL.BAT; `updated/` carries MAPG10.. and
 * MONG10.. beside the lowercase originals), so both prefixes and both cases are
 * tried before declaring a sector missing. Order is the common case first.
 *
 * The two lists are separate on purpose. An earlier version shared one map-only
 * list, so `is_map == 0` read a *map* file into the monster buffer and the
 * creature table was silently wrong rather than missing — caught by
 * engine/tests/monsters_check.c, which could not load monx31 at all. */
static const char *const k_map_prefixes[] = { "mapx", "MAPX", "mapg", "MAPG" };
static const char *const k_mon_prefixes[] = { "monx", "MONX", "mong", "MONG" };

/* Defined below alongside u2_world_path; forward-declared because the stroke loader
 * sits above it and both resolve the content directory the same way. */
static const char *world_dir(const u2_ctx *ctx);

static const char *const *prefix_list(int is_map, size_t *count)
{
    if (is_map) {
        *count = sizeof k_map_prefixes / sizeof k_map_prefixes[0];
        return k_map_prefixes;
    }
    *count = sizeof k_mon_prefixes / sizeof k_mon_prefixes[0];
    return k_mon_prefixes;
}

/* The content roots live in core/roots.c, which is their only owner. world.c, the
 * tile loader and the picture loader all ask the same list, because two copies of
 * "where does content live" is the drift this project keeps naming: the engine
 * would find a sector in a layer the tile atlas never looked at, and the failure
 * would be a blank screen rather than an error.
 *
 * `.patched` marks a patch layer. The patch layer is where the Galaxy spellings
 * are looked for, and it is never consulted for the release spellings: that is the
 * historical behaviour kept intact, since `updated/mapx40` has never been a
 * candidate and still is not.
 */
#define N_ROOTS u2_root_count

/* The Galaxy-patch spellings per kind. The release lists above already carry
 * both cases and both spellings, so only the patched side needs its own. */
static const char *const k_map_patch[] = { "MAPG", "mapg" };
static const char *const k_mon_patch[] = { "MONG", "mong" };

/* One (root, prefix) pair to try, plus the bare prefix recorded for write-back.
 * `root` points into u2_roots, so it is static and safe to keep on the world. */
struct cand { const char *root; const char *prefix; const char *label; };
#define N_CAND 20

/* Fill `out` with the candidates for one sector kind, in try order, and return
 * the count. On a planet the sector filename is shared with an Earth era --
 * sector 40 is both Earth-2112 and Jupiter -- and the fan pack ships the true
 * planet content under the patched spelling, either in a patch layer awaiting
 * RENGAL.BAT or already sitting in the release layer where it ran (docs/planet-
 * maps-galaxy-patch.md). So off Earth the patched spellings go first, across
 * every root, patch layer before release layer; the unpatched bytes are the
 * fallback when no patched copy exists. On Earth (world 0) the release spelling
 * IS the era content and the patched spellings are not tried at all, which is
 * the historical order. The `label` is the bare prefix recorded for store, so a
 * map read from a patch layer still writes back under a legal prefix rather than
 * the illegal "updated/MAPG". */
static int build_cands(int off_earth, const char *const *patch_pfx, size_t n_patch,
                       const char *const *rel_pfx, size_t n_rel,
                       struct cand *out, int max)
{
    int n = 0;

    if (off_earth)
        for (size_t r = 0; r < N_ROOTS && n < max; r++)
            for (size_t p = 0; p < n_patch && n < max; p++)
                out[n++] = (struct cand){ u2_roots[r].sub, patch_pfx[p], patch_pfx[p] };

    for (size_t r = 0; r < N_ROOTS && n < max; r++) {
        if (u2_roots[r].patched)
            continue;
        for (size_t p = 0; p < n_rel && n < max; p++)
            out[n++] = (struct cand){ u2_roots[r].sub, rel_pfx[p], rel_pfx[p] };
    }
    return n;
}

/* The slot's filename spellings for one kind, rotated so the patched spelling
 * comes first off Earth and the release spelling first on Earth — the same order
 * the content layers are tried in, because the slot holds whatever spelling the
 * sector was read from when it was written back. The four spellings of every
 * release list are mapx MAPX mapg MAPG (and monx MONX mong MONG), so the patched
 * pair is the trailing two and a rotation is the whole rule. */
static size_t slot_prefixes(int off_earth, int is_map, const char **out, int max)
{
    size_t nall = 0;
    const char *const *all = prefix_list(is_map, &nall);
    const size_t first = off_earth ? nall - 2 : 0;
    size_t n = 0;
    for (size_t i = 0; i < nall && n < (size_t)max; i++)
        out[n++] = all[(first + i) % nall];
    return n;
}

/* The directory a root resolves to is core/roots.c's job; this stays so the call
 * sites below read the way they always did. */
static void root_path(char *buf, size_t bufsz, const u2_ctx *ctx, const char *root)
{
    u2_root_path(buf, bufsz, ctx, root);
}

static void sector_path(char *buf, size_t bufsz, const u2_ctx *ctx,
                        const char *root, const char *prefix, u2_sector_id id)
{
    char dir[448];   /* smaller than every caller's path buffer, so the append
                     * below cannot truncate: gcc proves it and -Werror enforces. */
    root_path(dir, sizeof dir, ctx, root);
    snprintf(buf, bufsz, "%s/%s%u%u", dir, prefix,
             (unsigned)(id.hi & 0x0F), (unsigned)(id.lo & 0x0F));
}

u2_world_result u2_world_load_strokes(u2_ctx *ctx, u2_world *w)
{
    char path[544];
    FILE *f = NULL;

    /* The name is not templated: no sector digits, no case variants, no extension spelling
     * list. asm/07F3 opens it by name and reads one 0x800 block, which is why the read
     * length is U2_STROKE_BYTES and not the file's 2176 — the last 128 bytes are never
     * transferred by the original and the engine does not pretend otherwise.
     *
     * It is looked for across the roots like any other content, because under the
     * explicit-layer layout it lives in the release layer and not beside the binary. */
    for (size_t r = 0; r < N_ROOTS && !f; r++) {
        char dir[448];
        root_path(dir, sizeof dir, ctx, u2_roots[r].sub);
        snprintf(path, sizeof path, "%s/monsters", dir);
        f = fopen(path, "rb");
    }
    if (!f)
        return U2_WORLD_MISSING;

    const size_t got = fread(w->strokes, 1, U2_STROKE_BYTES, f);
    fclose(f);
    if (got != U2_STROKE_BYTES)
        return U2_WORLD_SHORT;

    w->strokes_loaded = 1;
    return U2_WORLD_OK;
}

const char *u2_world_result_text(u2_world_result r)
{
    switch (r) {
    case U2_WORLD_OK:        return "ok";
    case U2_WORLD_NOT_READY: return "content directory is missing or not writable";
    case U2_WORLD_MISSING:   return "no such sector file";
    case U2_WORLD_SHORT:     return "sector file is shorter than the original's read";
    case U2_WORLD_ERROR:     return "I/O error";
    }
    return "unknown";
}

static const char *world_dir(const u2_ctx *ctx)
{
    return u2_content_root(ctx);
}

void u2_world_path(char *buf, size_t bufsz, const u2_ctx *ctx,
                   const char *prefix, u2_sector_id id)
{
    snprintf(buf, bufsz, "%s/%s%u%u", world_dir(ctx),
             prefix ? prefix : k_map_prefixes[0],
             (unsigned)(id.hi & 0x0F), (unsigned)(id.lo & 0x0F));
}

/* The original's FCB drive byte is 0 — the default drive — so the game plays in
 * whatever directory it was launched from. --data-dir is that directory. It is
 * never created here: an empty world directory would look like success and show
 * an empty map, which is the failure mode this refuses to produce. */
u2_world_result u2_world_open(const u2_ctx *ctx)
{
    struct stat st;
    const char *dir = world_dir(ctx);

    if (stat(dir, &st) != 0)
        return U2_WORLD_NOT_READY;
    if (!S_ISDIR(st.st_mode))
        return U2_WORLD_NOT_READY;
    if (access(dir, R_OK | X_OK) != 0)
        return U2_WORLD_NOT_READY;
    return U2_WORLD_OK;
}

/* Writability is checked at store time, not at open time: a read-only content
 * directory is perfectly playable right up until the game needs to write a
 * sector, and that is exactly when the original would have shown its INSERT
 * prompt. Refusing to start would be a deviation nobody asked for. Under the
 * save-slot layout the directory that has to be writable is the slot, not the
 * content: content is read-only by design and a game can be played to the end
 * without ever writing to it. */
static int dir_writable(const char *dir)
{
    return access(dir, W_OK | X_OK) == 0;
}

static size_t want_bytes(int is_map)
{
    return is_map ? (size_t)U2_SECTOR_TILES : (size_t)U2_MON_TILES;
}

static int id_of(const u2_world *w, int is_map, u2_sector_id *out)
{
    if (is_map) { *out = w->map_id; return w->map_loaded; }
    *out = w->mon_id;
    return w->mon_loaded;
}

/* Which layer a sector read may draw from. Three shapes, because the live game and the
 * demo recorder need different questions answered: the game wants "what is this player's
 * world", which is the slot if it has one and content otherwise. The demo wants "what did
 * this save change", which is only meaningful as slot-against-content, so it needs each
 * layer on its own. */
#define LAYER_SLOT_THEN_CONTENT 0
#define LAYER_CONTENT_ONLY      1
#define LAYER_SLOT_ONLY         2

/* Walk the candidate list into a caller-owned buffer, without touching u2_world.
 *
 * That last clause is the whole reason this is a separate function and not a flag on
 * u2_world_load(): the demo path has to look at the content bytes of a sector while a
 * DIFFERENT set of bytes is live in the world, and reading them through the normal load
 * would clobber the running game out from under it.
 *
 * Reports the filename spelling and the layer it resolved. A name that is a guess must
 * never be checked against a digest — the absent-tlkx defect in core/demo.c is the
 * cautionary tale, and it is why out_name is filled only on success. */
static u2_world_result read_layer(const u2_ctx *ctx, int is_map, u2_sector_id id,
                                  int mode, uint8_t *buf, size_t bufsz,
                                  const char **out_label, const char **out_root,
                                  char *out_name, size_t out_name_sz)
{
    char path[512];
    const size_t want = want_bytes(is_map);
    size_t got = 0;
    int found = 0;
    const char *used = NULL;
    const char *used_root = "";

    if (!buf || bufsz < want)
        return U2_WORLD_SHORT;

    const char *const *prefixes;
    size_t nprefixes;
    prefixes = prefix_list(is_map, &nprefixes);

    /* Candidate (root, prefix) pairs, tried in order; the reasoning is on
     * build_cands. Monster layouts collide with Earth eras exactly as maps do
     * (monx/mong mirror mapx/mapg), so the same patched-first order applies to
     * both kinds. */
    struct cand list[N_CAND];
    const int off_earth = ctx && ctx->state.world != 0 && ctx->state.world != 0x0A;
    int n = 0;
    if (mode != LAYER_SLOT_ONLY)
        n = build_cands(off_earth,
                        is_map ? k_map_patch : k_mon_patch,
                        is_map ? (int)(sizeof k_map_patch / sizeof k_map_patch[0])
                               : (int)(sizeof k_mon_patch / sizeof k_mon_patch[0]),
                        prefixes, nprefixes, list, N_CAND);

    /* The save slot's overlay is tried before any content root. This is the read
     * half of docs/save-scoped-worlds.md: a sector this save has changed lives in
     * the slot, and it outranks the release copy because it *is* that player's
     * world. A slot with nothing in it — every fresh game, and every install that
     * has never saved — falls straight through to the content roots and behaves
     * exactly as it did before the layer existed.
     *
     * mapx/monx only, and that is a ruling rather than an oversight: tlkx, pic*
     * and the executable are never written by anything, so overlaying them would
     * buy a second place for content to disagree with itself (D3). */
    if (mode != LAYER_CONTENT_ONLY) {
        char sdir[448];
        const char *spfx[4];
        const size_t ns = slot_prefixes(off_earth, is_map, spfx, 4);
        u2_slot_dir(ctx, sdir, sizeof sdir);
        for (size_t i = 0; i < ns && !found; i++) {
            /* %.4s because a prefix is four characters at most, and gcc's
             * format-truncation check only accepts the arithmetic when it can
             * bound every field: 447 + 1 + 4 + 2 digits < the 512-byte path. */
            snprintf(path, sizeof path, "%.447s/%.4s%u%u", sdir, spfx[i],
                     (unsigned)(id.hi & 0x0F), (unsigned)(id.lo & 0x0F));
            FILE *f = fopen(path, "rb");
            if (!f)
                continue;
            got = fread(buf, 1, want, f);
            fclose(f);
            found = 1;
            used = spfx[i];
            used_root = U2_WORLD_SLOT_LAYER;
        }
    }

    if (mode != LAYER_SLOT_ONLY) {
        for (int i = 0; i < n && !found; i++) {
            FILE *f;
            sector_path(path, sizeof path, ctx, list[i].root, list[i].prefix, id);
            f = fopen(path, "rb");
            if (!f)
                continue;
            got = fread(buf, 1, want, f);
            fclose(f);
            found = 1;
            used = list[i].label;
            used_root = list[i].root;
        }
    }

    if (!found)
        return U2_WORLD_MISSING;
    if (got != want)
        return U2_WORLD_SHORT;

    if (out_label) *out_label = used;
    if (out_root)  *out_root  = used_root;
    if (out_name && out_name_sz)
        snprintf(out_name, out_name_sz, "%.4s%u%u", used ? used : "",
                 (unsigned)(id.hi & 0x0F), (unsigned)(id.lo & 0x0F));
    return U2_WORLD_OK;
}

u2_world_result u2_world_load(u2_ctx *ctx, u2_world *w, int is_map, u2_sector_id id)
{
    const size_t want = want_bytes(is_map);
    const char *used = NULL;
    const char *used_root = "";

    const u2_world_result r = read_layer(ctx, is_map, id, LAYER_SLOT_THEN_CONTENT,
                                         is_map ? w->map : w->mon, want,
                                         &used, &used_root, NULL, 0);
    if (r != U2_WORLD_OK)
        return r;

    w->bytes_read = want;
    /* Per kind, never one shared field: see the note on the struct. The overworld loads
     * map then monsters into the same world, so a single field would always say "monx"
     * by the time anything is stored. */
    if (is_map) { w->map_prefix = used; w->map_root = used_root; }
    else        { w->mon_prefix = used; w->mon_root = used_root; }
    if (is_map) {
        w->map_id = id;
        w->map_loaded = 1;
        w->map_dirty = 0;
        /* The pristine copy the creature stamp is drawn over. Set here and only
         * here, so a stamped grid can always be rebuilt (see world.h). */
        memcpy(w->map_base, w->map, U2_SECTOR_TILES);
    } else {
        w->mon_id = id;
        w->mon_loaded = 1;
        w->mon_dirty = 0;
    }
    return U2_WORLD_OK;
}

u2_world_result u2_world_read_content(const u2_ctx *ctx, int is_map, u2_sector_id id,
                                      uint8_t *buf, size_t bufsz, char *out_name,
                                      size_t out_name_sz)
{
    return read_layer(ctx, is_map, id, LAYER_CONTENT_ONLY, buf, bufsz,
                      NULL, NULL, out_name, out_name_sz);
}

u2_world_result u2_world_read_slot(const u2_ctx *ctx, int is_map, u2_sector_id id,
                                   uint8_t *buf, size_t bufsz, char *out_name,
                                   size_t out_name_sz)
{
    return read_layer(ctx, is_map, id, LAYER_SLOT_ONLY, buf, bufsz,
                      NULL, NULL, out_name, out_name_sz);
}

/* asm/22D5's dialogue read. Prefixes mirror the map/monster lists because the shipped pack
 * carries both spellings, and a missing tlkx is normal rather than an error: several sectors
 * ship no dialogue file at all, and the caller must then fall back rather than print noise. */
static const char *const k_tlk_prefixes[] = { "tlkx", "TLKX", "tlkg", "TLKG" };

u2_world_result u2_world_load_tlkx(u2_ctx *ctx, u2_world *w, u2_sector_id id)
{
    char path[512];
    size_t got = 0;
    const char *used = NULL;

    /* Same world-aware preference as u2_world_load, across the same roots: on a
     * planet the sector's dialogue collides with an Earth era (tlkx/tlkg mirror
     * mapx/mapg), so the patched tlkg is preferred wherever it lives before the
     * unpatched tlkx. On Earth the tlkx spelling is the era dialogue and the
     * order is unchanged. tlkx is read-only, so nothing records a root for it.
     * docs/planet-maps-galaxy-patch.md. */
    static const char *const k_tlk_patch[] = { "TLKG", "tlkg" };
    struct cand list[N_CAND];
    int off_earth = ctx && ctx->state.world != 0 && ctx->state.world != 0x0A;
    int n = build_cands(off_earth, k_tlk_patch,
                        (int)(sizeof k_tlk_patch / sizeof k_tlk_patch[0]),
                        k_tlk_prefixes,
                        (int)(sizeof k_tlk_prefixes / sizeof k_tlk_prefixes[0]),
                        list, N_CAND);

    for (int i = 0; i < n; i++) {
        FILE *f;
        sector_path(path, sizeof path, ctx, list[i].root, list[i].prefix, id);
        f = fopen(path, "rb");
        if (!f)
            continue;
        got = fread(w->tlkx, 1, U2_TLKX_BYTES, f);
        fclose(f);
        used = list[i].label;
        break;
    }

    if (!used)
        return U2_WORLD_MISSING;
    if (got != U2_TLKX_BYTES)
        return U2_WORLD_SHORT;

    w->tlkx_prefix = used;
    w->tlkx_id = id;
    w->tlkx_loaded = 1;
    return U2_WORLD_OK;
}

/* asm/154E, in address order:
 *
 *   si = AL ; di = [DS:476]
 *   1564  al = es:[di] ; cmp 0 ; je 156E          scan forward to the next NUL
 *   156B  inc di ; jmp 1564
 *   156E  dec si ; je 1573 ; else jmp 156B        count boundaries until si reaches 0
 *   1573  inc di ; al = es:[di] ; cmp 0 ; je 1580 step onto the record; emit to its NUL
 *   157B  call emit_char ; jmp 1573
 *
 * Because byte 0 of the buffer is itself a NUL, the first `scan to NUL` succeeds without
 * moving, so index 1 selects the first real record. That is the whole reason the role byte
 * maps as role - 0x80 and no further offset is needed.
 *
 * The scan is bounded by the buffer on purpose. The original walks a 256-byte buffer with no
 * bound check and stops at whatever NUL it finds; a role byte pointing past the end would run
 * off into adjacent memory. Here it returns -1, which the caller renders as no line rather
 * than as garbage. */
int u2_world_tlkx_record(const u2_world *w, int index, char *out, size_t outsz)
{
    if (!w || !out || outsz == 0)
        return -1;
    out[0] = '\0';
    if (!w->tlkx_loaded || index < 1)
        return -1;

    size_t i = 0;
    for (int n = 0; n < index; n++) {
        while (i < U2_TLKX_BYTES && w->tlkx[i] != 0)
            i++;
        if (i >= U2_TLKX_BYTES)
            return -1;
        i++;                       /* step past the boundary NUL */
    }
    if (i >= U2_TLKX_BYTES)
        return -1;

    size_t len = 0;
    while (i < U2_TLKX_BYTES && w->tlkx[i] != 0) {
        if (len + 1 >= outsz)
            break;                 /* truncate rather than overrun the caller's buffer */
        out[len++] = (char)(w->tlkx[i] & 0x7Fu);
        i++;
    }
    out[len] = '\0';
    return (int)len;
}

/*
 * Atomic write: temp file in the same directory, then rename. Same directory
 * matters — rename(2) is atomic only within one filesystem, and a cross-device
 * rename would fail or degrade into a copy.
 *
 * The file is laid down as [live sector][whatever the file had past the read
 * length]. A shipped 4224-byte map keeps its 128-byte tail because the engine
 * replaces only the 4096 bytes the original reads and copies the rest through
 * verbatim — the same rule the save path follows for the PLAYER record's tail.
 * A file that is exactly 4096 bytes (the Galaxy content) stays 4096.
 *
 * The tail comes from the overlay when one already exists, and otherwise from the
 * content file the sector was read from. Without that second source the first
 * write of a fresh save would produce a 4096-byte overlay out of a 4224-byte
 * release file, and the tail — which the original never reads but always keeps —
 * would quietly disappear the first time anyone played.
 */
u2_world_result u2_world_store(u2_ctx *ctx, u2_world *w, int is_map)
{
    u2_sector_id id;
    char path[544], tmp[576];
    size_t want = want_bytes(is_map);
    size_t nprefixes = 0;
    const uint8_t *buf = is_map ? w->map : w->mon;
    FILE *src, *dst;

    if (!id_of(w, is_map, &id))
        return U2_WORLD_ERROR;

    /* The write target is the save slot, always, and never a content directory.
     * This is the whole point of docs/save-scoped-worlds.md: the original does
     * bake creature bytes into its sector files (asm/2305 writes the stamped
     * grid), so a write-back has to land somewhere — the only question is whose
     * copy it damages. In the content directory it was the player's only copy of
     * the release data, and it was sticky, because map_base is loaded from that
     * same file and a baked stamp becomes terrain. In the slot it is this save's
     * overlay: read back ahead of content, and deletable.
     *
     * The slot is created here rather than by the installer (D6): a slot that
     * exists and holds nothing would be a claim about a game that does not exist,
     * and a sector write is the first moment one genuinely does. */
    char slot[448];
    u2_slot_dir(ctx, slot, sizeof slot);
    if (u2_slot_ensure(ctx) != 0)
        return U2_WORLD_NOT_READY;
    if (!dir_writable(slot))
        return U2_WORLD_NOT_READY;

    /* The prefix must belong to the kind being written. This is the loud version of the
     * invariant the shared field used to violate: a map buffer reaching a monx filename
     * (or the reverse) is not a naming detail, it silently destroys the other file and
     * reports success. Refusing costs a save; writing the wrong file costs the player's
     * creature data, and there is no backup in the content directory.
     *
     * want_bytes() is checked the same way for the same reason: the sizes differ (4096 vs
     * 256), so a mismatch here is the other half of the same class of mistake. */
    const char *const *ok_prefixes = prefix_list(is_map, &nprefixes);
    const char *want_prefix = is_map ? w->map_prefix : w->mon_prefix;
    const char *used_root = is_map ? w->map_root : w->mon_root;
    if (want_prefix) {
        int legal = 0;
        for (size_t i = 0; i < nprefixes; i++)
            if (strcmp(want_prefix, ok_prefixes[i]) == 0) { legal = 1; break; }
        if (!legal)
            return U2_WORLD_ERROR;
    }

    /* The overlay keeps the spelling the sector was read under, so a planet map
     * that came from a patch layer is stored as MAPGnn and the read side finds it
     * again with the same rotation, ahead of every content layer. */
    snprintf(path, sizeof path, "%.437s/%.4s%u%u", slot, want_prefix ? want_prefix : ok_prefixes[0],
             (unsigned)(id.hi & 0x0F), (unsigned)(id.lo & 0x0F));
    snprintf(tmp, sizeof tmp, "%.575s.u2tmp", path);

    dst = fopen(tmp, "wb");
    if (!dst)
        return U2_WORLD_ERROR;
    if (fwrite(buf, 1, want, dst) != want) {
        fclose(dst); unlink(tmp);
        return U2_WORLD_ERROR;
    }

    src = fopen(path, "rb");
    if (!src && used_root && strcmp(used_root, U2_WORLD_SLOT_LAYER) != 0) {
        /* First write of this sector: the overlay does not exist yet, so the tail
         * comes from the content file the bytes were read from. */
        char under[544];
        sector_path(under, sizeof under, ctx, used_root, want_prefix, id);
        src = fopen(under, "rb");
    }
    if (src) {
        if (fseek(src, (long)want, SEEK_SET) == 0) {
            uint8_t chunk[512];
            size_t n;
            while ((n = fread(chunk, 1, sizeof chunk, src)) > 0) {
                if (fwrite(chunk, 1, n, dst) != n) {
                    fclose(src); fclose(dst); unlink(tmp);
                    return U2_WORLD_ERROR;
                }
            }
        }
        fclose(src);
    }

    if (fflush(dst) != 0 || fsync(fileno(dst)) != 0) {
        fclose(dst); unlink(tmp);
        return U2_WORLD_ERROR;
    }
    if (fclose(dst) != 0) { unlink(tmp); return U2_WORLD_ERROR; }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return U2_WORLD_ERROR;
    }

    if (is_map) w->map_dirty = 0; else w->mon_dirty = 0;
    return U2_WORLD_OK;
}
