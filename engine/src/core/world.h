#ifndef U2_WORLD_H
#define U2_WORLD_H

/*
 * Sector model — the map is runtime data, read from the player's own files.
 *
 * Everything here is proven in docs/phase4-implementation.md § CP4. The short
 * version, with the asm citations:
 *
 *   - The sector file name is not a string literal anywhere in the binary. It is
 *     assembled by self-patching two ASCII digits over the `F` placeholders of
 *     an inline FCB name: asm/2290 (runtime 0x2290, file 0x2490) reads DS:0x49
 *     and DS:0x4A, adds 0x30 to each, and stores them at cs:0x22bb / cs:0x22bc,
 *     which are the fifth and sixth bytes of the `MAPXFF  ` name at file
 *     0x24b7. So the name is MAPX + two decimal digits, no extension.
 *   - The read is FCB random block read through the generic DOS wrapper:
 *     AH=27h, CX=0x1000 (4096 records, record size 1), DX=[DS:0x472]. asm/22D5
 *     is the same shape for MONX with CX=0x100 into [DS:0x476].
 *   - asm/2305 writes the sector back out (AH=28h, CX=0x1000, same name), so the
 *     original mutates its own map files. Owner decision 2026-10-27: the engine
 *     does the same in `original` mode, which makes --data-dir writable content
 *     and makes an atomic write non-negotiable.
 *   - A tile is ONE BYTE, indexed y*64 + x (asm/50B3_map_tile_ptr: shl $6 of
 *     DS:0x24, add DS:0x23, add [DS:0x472]). A sector is therefore 64x64 tiles
 *     = 4096 bytes, which is exactly the CX of the read.
 *   - The shipped files are 4224 bytes: 4096 tiles plus a 128-byte tail that is
 *     never read. Do NOT assert 4224 — the Galaxy content in the fan pack ships
 *     sectors of exactly 4096 with no tail, and the read is 4096 either way.
 *   - The second digit is the SITE KIND, not a coordinate: asm/343F prints
 *     "-TOWN" before selecting 2 and "-TOWER" before 4, asm/243E selects 0 for
 *     the overworld, and asm/2423 branches on [DS:4A]==0 for overworld-vs-site.
 *     The fan pack's own file list agrees (10 Mercury overworld, 15 Mercury
 *     dungeon, 32 Mars town, 41 Jupiter village).
 *
 * No game bytes live in this file or in world.c. The engine ships zero content;
 * the tiles come from the player's directory at run time.
 */

#include <stddef.h>
#include <stdint.h>
#include "core/ctx.h"

#define U2_SECTOR_EDGE  64                       /* asm/50B3: shl $6 */
#define U2_SECTOR_TILES (U2_SECTOR_EDGE * U2_SECTOR_EDGE) /* 4096 = CX at asm/2290 */
#define U2_MON_TILES    256                      /* CX at asm/22D5 */
/* The dialogue read is ALSO 0x100 bytes, and that limit is load-bearing rather than
 * incidental. tlkxNN files ship at 384 bytes; the last ~128 are the sector's code and
 * graphics tail, and record 6 of tlkx22 decoded from that tail comes out as
 * `ARM   P\x01T2\r\tK\x01PRBYTE...`. asm/22D5 reads CX=0x100, so the original never sees it.
 * Reading the whole file would put garbage on screen and look like a decode bug. */
#define U2_TLKX_BYTES   256
#define U2_MAP_PREFIXES 4                        /* mapx / MAPX / mapg / MAPG */

/* What `map_root` / `mon_root` hold when the bytes came from the save slot's
 * overlay instead of a content layer. A distinct value rather than "" because
 * "where did this sector come from" is a question a report and a test both ask,
 * and a slot is a different kind of place from a content root: it is the
 * player's, it is deletable, and it is where every write lands.
 * docs/save-scoped-worlds.md. */
#define U2_WORLD_SLOT_LAYER "slot"

/* Site kinds, i.e. the second digit. Proven for 0, 2 and 4 by the strings at
 * asm/243E and asm/343F; 1, 3 and 5 come from the fan pack's file list and are
 * marked inferred until the asm says so directly. */
typedef enum u2_site_kind {
    U2_SITE_OVERWORLD = 0,  /* proven: asm/243E, asm/2423 */
    U2_SITE_VILLAGE   = 1,  /* inferred: asm/342A selects 1 */
    U2_SITE_TOWN      = 2,  /* proven: asm/343F prints "-TOWN" */
    U2_SITE_CASTLE    = 3,  /* inferred: mapx03 exists, TLKX03 is "Legends Castle" */
    U2_SITE_TOWER     = 4,  /* proven: asm/343F prints "-TOWER" */
    U2_SITE_DUNGEON   = 5   /* inferred: fan pack names MAPG15/45 as dungeons */
} u2_site_kind;

typedef enum u2_world_result {
    U2_WORLD_OK = 0,        /* sector read / written */
    U2_WORLD_NOT_READY,     /* content dir missing or unwritable: the "insert disk" analogue */
    U2_WORLD_MISSING,       /* dir is fine, this sector file is not there */
    U2_WORLD_SHORT,         /* file exists but is smaller than the bytes the original reads */
    U2_WORLD_ERROR          /* unexpected I/O failure */
} u2_world_result;

/* A sector is named by two decimal digits: high = world / planet, low = site kind. */
typedef struct u2_sector_id {
    uint8_t hi;
    uint8_t lo;
} u2_sector_id;

/*
 * One resident sector at a time. The viewport never straddles a file: crossing
 * x or y >= 0x40 reloads the sector (asm/242D, asm/2434 -> asm/243E), so there
 * is no second buffer and no seam between adjacent files to get wrong.
 */
/* The dungeon creature stroke table, `original/monsters`.
 *
 * asm/07F3 is a DOS AH=27h random-block read with CX = 0x800 and DS:474 as the
 * transfer address, and the live DS at 0x2900 is byte-identical to the shipped
 * file's first 2048 bytes — so this is runtime data the game loads, in exactly the
 * same category as mapxNN, and it is read from the player's own content directory
 * and never committed. It is ART: 8 blocks of 256 bytes of 2-byte stroke records,
 * and the cleanroom rule applies to it just as it applies to the tile atlas.
 *
 * 2048 and not the file's full 2176: the read is one block of CX bytes, and the
 * trailing 128 are never transferred. Eight blocks of 256, one per dungeon type
 * 1..7 with block 0 empty, which is what asm/68ED's `add ax,[DS:474]` over the
 * stamp byte scales.
 */
#define U2_STROKE_BYTES 2048
#define U2_STROKE_BLOCKS 8
#define U2_STROKE_BLOCK  256

typedef struct u2_world {
    uint8_t      map[U2_SECTOR_TILES];
    /*
     * The sector as the file had it, before anything stamped creatures into
     * `map`. CP5 W2 stamps each creature's type byte into the grid because that
     * is what the renderer draws (docs/art-index.md), and once creatures can
     * walk, the cell they leave has to become terrain again — so the terrain has
     * to be kept somewhere. This is that somewhere, and it is never written by
     * anything except the loader. It is not a second truth about the world: it
     * is the one truth, and `map` is the truth plus the creatures on it.
     */
    uint8_t      map_base[U2_SECTOR_TILES];
    uint8_t      mon[U2_MON_TILES];
    u2_sector_id map_id;
    u2_sector_id mon_id;
    int          map_loaded;
    int          mon_loaded;
    int          map_dirty;   /* set by a tile write; only dirty sectors are stored */
    int          mon_dirty;
    /* Which filename spelling each kind was actually found under, kept PER KIND on
     * purpose. There used to be one field for both, and because the overworld always
     * loads the map (asm/2290) and then the monsters (asm/22D5), it read "monx" by save
     * time — so storing a dirty map wrote 4096 bytes of map data over monxNN and returned
     * U2_WORLD_OK. See docs/phase4-state.md, OPEN DEFECT, and the both-kind case in
     * tests/world_check.c, which is the only shape that can see it.
     * Legal values are the entries of prefix_list() for that kind: mapx/MAPX/mapg/MAPG,
     * monx/MONX/mong/MONG. u2_world_store refuses a prefix that is not one of them. */
    const char  *map_prefix;  /* spelling the map sector was found under */
    const char  *mon_prefix;  /* spelling the creature table was found under */
    /* Which layer each kind was found in, as the subdirectory of the content root
     * ("" is the root itself), or U2_WORLD_SLOT_LAYER for the save slot. Recorded
     * for the same reason the prefixes are and per kind for the same trap: a
     * shared field read "monx" by save time and the store wrote map bytes over
     * the creature table.
     *
     * It no longer chooses the write target — sector writes always go to the slot
     * (docs/save-scoped-worlds.md § 3) — but it is still load-bearing: it names
     * the file whose bytes past the read length have to be carried through into
     * the overlay, and it is the provenance a report answers to. Points into a
     * static table or a string literal, never allocated. */
    const char  *map_root;    /* layer the map sector was found in */
    const char  *mon_root;    /* layer the creature table was found in */
    /* The sector's scripted dialogue, asm/22D5's second read into [DS:476]. Read-only and
     * never stored: the game writes mapx back (asm/2305) but nothing ever writes dialogue
     * home, so there is no dirty flag here and adding one would invite a write-back that the
     * original does not perform. */
    uint8_t      tlkx[U2_TLKX_BYTES];
    u2_sector_id tlkx_id;
    int          tlkx_loaded;
    const char  *tlkx_prefix;   /* spelling the dialogue file was found under */
    /* The dungeon sprite strokes, loaded once per run rather than per sector: the
     * filename has no sector digits, so there is no id to track and nothing to
     * write back. Read-only, like tlkx. */
    uint8_t      strokes[U2_STROKE_BYTES];
    int          strokes_loaded;
    size_t       bytes_read;  /* diagnostics and tests */
} u2_world;

/* Tile addressing, exactly as asm/50B3 computes it. Coordinates are masked to
 * the sector by the caller, which is what the original does at the edges. */
static inline int u2_world_tile_index(int x, int y)
{
    return ((y & (U2_SECTOR_EDGE - 1)) * U2_SECTOR_EDGE) + (x & (U2_SECTOR_EDGE - 1));
}

static inline uint8_t u2_world_tile_at(const u2_world *w, int x, int y)
{
    return w->map[u2_world_tile_index(x, y)];
}

/* The GROUND byte, as opposed to whatever is standing on it.
 *
 * `map` is the stamped grid: a creature's type byte, or 0x78 for the player, sits
 * in the cell and the terrain byte is gone from it until u2_monsters_restamp()
 * rebuilds the grid from map_base. That is the right model for the DOS renderer,
 * which draws exactly one art index per cell, but it means a renderer that wants to
 * draw ground and then a sprite over it cannot ask `map` what the ground is.
 * map_base is that answer, and it already exists for the restamp; this only exposes
 * it. Writes that change the world rather than its occupants go through
 * u2_world_terrain_put(), which updates both, so a boarded ship or an opened door
 * is ground here and not a phantom. */
static inline uint8_t u2_world_ground_at(const u2_world *w, int x, int y)
{
    return w->map_base[u2_world_tile_index(x, y)];
}

/* Marking dirty here is deliberate: the only way to change a tile is through
 * this function, so a sector cannot be mutated without the write-back knowing. */
static inline void u2_world_tile_put(u2_world *w, int x, int y, uint8_t v)
{
    w->map[u2_world_tile_index(x, y)] = v;
    w->map_dirty = 1;
}

/* A terrain write: the cell AND the pristine copy, for a tile the game changes for
 * good rather than covers temporarily.
 *
 * u2_world_tile_put() alone is not enough, but the reason changed on 2026-11-08 and the
 * old wording had outlived itself. It used to read "the creature stamp is rebuilt from
 * map_base on every creature turn, so a write that lands only in `map` is erased" —
 * true then, and the reason this function exists. The move-path rebuild is now gone
 * (step 2c-ii), so that justification no longer holds.
 *
 * What does hold: `map_base` is the ground record, and `u2_world_ground_at()` serves it
 * to the EGA compositor (`frame.c:308`), which draws terrain and then a sprite over it.
 * A terrain edit that missed `map_base` would survive on the CGA path and show as stale
 * ground under a sprite on the EGA one — a wrong answer that only one renderer can see,
 * which is the worst shape for this. So both arrays, still.
 *
 * The vehicle arms of asm/2FAD are the site that forced the distinction: the live
 * sample in captures/spice86/ship-port-boniface read [DS:14] go 0x24 -> 0x00 on BOARD
 * and it stayed water for every step after, which is only possible if the write
 * outlives the redraws. NOTE, not fixed here: the door-paving write in the UNLOCK arm
 * (asm/435E, called from the walk loop) now uses this for the same reason — changed
 * 2026-10-29 after tests/site_state.py case C showed the tile_put paving being erased by the
 * creature turn that immediately follows an unlock. It is shipped behaviour, so it got its own
 * gate rather than riding along with the vehicle work.
 */
static inline void u2_world_terrain_put(u2_world *w, int x, int y, uint8_t v)
{
    const int i = u2_world_tile_index(x, y);
    w->map[i] = v;
    w->map_base[i] = v;
    w->map_dirty = 1;
}

/* ---- the same buffer read as a dungeon --------------------------------------
 *
 * asm/68C0_dungeon_grid_byte computes the cell address as
 * word[DS:472] + [DS:25]*256 + [DS:24]*16 + [DS:23], where word[DS:472] is the
 * map buffer (0x1800 in every live capture) and [DS:25] is the floor. So a
 * tower or dungeon mapxNN is not a 64x64 sector at all: it is 16 pages of
 * 16x16 cells, and the floor index picks the page. Measured against
 * captures/spice86/dungeon-35-live, whose live page 0 is byte-identical to
 * original/mapx35's first 256 bytes.
 *
 * The `(y*16 + x) & 0xFF` is not tidiness: the original forms the offset in one
 * 8-bit register, so a coordinate outside 0..15 wraps inside the page rather
 * than walking into the next one. ADVANCE masks the running position with
 * $0x3f before it gets here, so the wrap is only ever seen by the depth probe.
 */
#define U2_DG_PAGE_BYTES 256
#define U2_DG_PAGE_EDGE   16

static inline int u2_world_dungeon_index(int floor, int x, int y)
{
    const int off = ((y & 0xFF) * U2_DG_PAGE_EDGE + (x & 0xFF)) & 0xFF;
    return (((floor & 0xFF) * U2_DG_PAGE_BYTES) + off) & (U2_SECTOR_TILES - 1);
}

static inline uint8_t u2_world_dungeon_at(const u2_world *w, int floor, int x, int y)
{
    return w->map[u2_world_dungeon_index(floor, x, y)];
}

/* Dirty for the same reason u2_world_tile_put is: the original rewrites its own
 * sector file, and a chest you emptied has to stay empty. */
static inline void u2_world_dungeon_put(u2_world *w, int floor, int x, int y, uint8_t v)
{
    w->map[u2_world_dungeon_index(floor, x, y)] = v;
    w->map_dirty = 1;
}

/* The paged twin of u2_world_terrain_put: a permanent edit of the dungeon, not a
 * stamp. asm/363B runtime 0x36F1 clears an emptied chest through the cell pointer
 * and it stays cleared, so the write has to outlive whatever restamps later. */
static inline void u2_world_dungeon_terrain_put(u2_world *w, int floor, int x, int y, uint8_t v)
{
    const int i = u2_world_dungeon_index(floor, x, y);
    w->map[i] = v;
    w->map_base[i] = v;
    w->map_dirty = 1;
}

/* "<dir>/<prefix><hi><lo>" — the caller supplies the prefix spelling. */
void u2_world_path(char *buf, size_t bufsz, const u2_ctx *ctx,
                   const char *prefix, u2_sector_id id);

/* Is the content directory usable? Never creates it: unlike the save directory
 * there is nothing to seed, and inventing an empty world directory would hide a
 * wrong --data-dir behind a silent empty map. */
u2_world_result u2_world_open(const u2_ctx *ctx);

/* Read a sector. Tries the mapx/MAPX/mapg/MAPG spellings in order, because DOS
 * names are case-insensitive and the Galaxy patch renames mapx -> mapg. */
u2_world_result u2_world_load(u2_ctx *ctx, u2_world *w, int is_map, u2_sector_id id);

/* Load the sector's tlkxNN dialogue buffer, 256 bytes, into w->tlkx.
 * Separate from u2_world_load rather than a third value of its is_map flag: is_map is a
 * boolean that tests and the save path already reason about, and dialogue is never stored,
 * so sharing the parameter would buy nothing and risk the exact kind-confusion that made
 * map_prefix and mon_prefix separate fields in the first place. */
u2_world_result u2_world_load_tlkx(u2_ctx *ctx, u2_world *w, u2_sector_id id);

/* asm/154E_tlkx_record_printer, as a copy into a NUL-terminated buffer.
 *
 * The walk is the original's, byte for byte in effect: the buffer begins with a NUL, so
 * skipping `index` NUL boundaries lands on the byte after the index-th one, and the record
 * runs to the next NUL. `index` is what asm/4209 hands over as [slot role] - 0x80, so
 * index 1 is the first real record. Observed live: index 2 in sector 22 is
 * `JOHN MAYER ORDERS:\rTAKE OUT THE TRASH!!!`.
 *
 * High-bit-set ASCII is masked to 0x7F here, matching tools/tlkx_decode.py, so callers and
 * tests see the readable form instead of raw Apple II bytes. Returns the length written, or
 * -1 if the index runs off the end of the buffer (which is what a role byte pointing past
 * the 256-byte read does). */
int u2_world_tlkx_record(const u2_world *w, int index, char *out, size_t outsz);

/* Write a sector back into the save slot, atomically (temp file + rename in the
 * same directory). Callers must only store sectors that are dirty.
 *
 * The target is always `saves/<slot>/<prefix><NN>`, whatever layer the sector was
 * read from, and the slot directory is created on demand. That is the property
 * docs/save-scoped-worlds.md exists for: the content directory is an immutable
 * base, so a write can never damage the player's only copy of the release maps.
 * The bytes themselves are faithful to asm/2305, which writes the stamped grid —
 * what changed is where they land, not what they are. */
u2_world_result u2_world_store(u2_ctx *ctx, u2_world *w, int is_map);

/* Human-readable reason for the last failure, for the INSERT-style message. */
/* Read the dungeon stroke table. `dir/monsters`, no sector digits and no extension
 * spelling variants: unlike mapx/monx/tlkx the name is not templated. Returns
 * U2_WORLD_MISSING when the file is absent, which is normal for a content directory
 * that only carries the fan-pack maps, and the caller draws no sprites in that case
 * rather than failing the run. */
u2_world_result u2_world_load_strokes(u2_ctx *ctx, u2_world *w);

const char *u2_world_result_text(u2_world_result r);

#endif /* U2_WORLD_H */
