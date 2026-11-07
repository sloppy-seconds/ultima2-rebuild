/*
 * monsters.c — CP5 W1: the monx block <-> the creature arrays in the state block.
 *
 * No combat rules and no AI live here on purpose. Every byte movement in this file
 * is a copy between two buffers whose layout is measured, not guessed:
 *
 *   monxNN offset -> DS field   (docs/monster-spawn.md § 6, matched live on four sectors)
 *        0..31     -> mon_x   (DS:137)
 *       32..63     -> mon_y   (DS:157)
 *       64..95     -> mon_hp  (DS:177)
 *       96..127    -> mon_type(DS:197)
 *      128..159    -> mon_kind(DS:1B7)
 *      160..191    -> mon_role(DS:1D7)
 *
 * asm/22D5 reads 256 bytes (CX=0x100) for six 32-byte arrays totalling 192, so
 * the tail of the buffer is unread by anything found so far and is left alone on
 * a store round trip.
 */
#include "game/monsters.h"

#include <string.h>

#include "core/ctx.h"
#include "core/rng.h"          /* u2_rand_byte: the spawner's two coordinate rolls */

/* monx offset of each array. Kept as one table so a future re-measurement is a
 * single edit, and so the store path cannot drift from the load path. */
#define MON_OFF_X    0
#define MON_OFF_Y    32
#define MON_OFF_HP   64
#define MON_OFF_TYPE 96
#define MON_OFF_KIND 128
#define MON_OFF_ROLE 160

int u2_monsters_spawn(u2_ctx *ctx, u2_world *w)
{
    if (ctx == NULL || w == NULL || !w->mon_loaded)
        return 0;

    u2_state *s = &ctx->state;
    memcpy(s->mon_x,    w->mon + MON_OFF_X,    U2_MON_SLOTS);
    memcpy(s->mon_y,    w->mon + MON_OFF_Y,    U2_MON_SLOTS);
    memcpy(s->mon_hp,   w->mon + MON_OFF_HP,   U2_MON_SLOTS);
    memcpy(s->mon_type, w->mon + MON_OFF_TYPE, U2_MON_SLOTS);
    memcpy(s->mon_kind, w->mon + MON_OFF_KIND, U2_MON_SLOTS);
    memcpy(s->mon_role, w->mon + MON_OFF_ROLE, U2_MON_SLOTS);

    /* The role-3 wander vectors are deliberately ZERO-FILLED, not copied from the sector file.
     *
     * monx does have bytes at the offsets these map to (monx[192] and monx[224] under the established
     * monx[0] -> DS:137 mapping), and in every shipped file they are all zero. They are not data that
     * happens to be empty: the original WRITES them at runtime. asm/10AA and asm/10BF store
     * sign_of_al(rand_byte) into them, and asm/1086 reads them back into the step pair. A fresh
     * sector therefore starts with a zero vector and the creature does not move until the first re-roll
     * on the 25% branch — which is faithful behaviour, not a gap.
     *
     * Zero-filling rather than memcpy-ing is the visible signal at the call site that this is runtime
     * state with a data-shaped address. See asm/1086_creature_wander_step.asm for the whole chain.
     */
    memset(s->mon_wander_x, 0, U2_MON_SLOTS);
    memset(s->mon_wander_y, 0, U2_MON_SLOTS);

    int occupied = 0;
    for (int i = 0; i < U2_MON_SLOTS; i++) {
        if (s->mon_x[i] || s->mon_y[i])
            occupied++;
    }

    /* CP5 W2: the stamp.
     *
     * The renderer does not read the monster record. docs/art-index.md settled
     * that on the only slot in the corpus where the two fields disagree — grid
     * 0x38 versus type 0x34 — where the drawn pixels matched 0x38>>2 at 256/256
     * and 0x34>>2 at 171/256. So the original stamps the type byte into the
     * in-memory sector map at spawn and the view draws the map, which is what
     * this loop reproduces.
     *
     * Stamping the one grid rather than keeping a second is deliberate: the same
     * cell is what passability reads, so a creature blocks movement the way the
     * original does it — by not being a walkable tile — with no extra check
     * anywhere. One grid, one truth.
     *
     * The empty-slot test is the module's own (x||y), not hp, so a slot the
     * spawner left alone is never stamped. On a kill the record is cleared and
     * the terrain underneath shows again — see u2_monsters_restamp, and note
     * that the 0x80 at asm/2E3D file 0x30F4 is not this grid.
     *
     * INFERRED, and it is the line to suspect first if a creature ever draws in
     * the wrong cell: that the stamp lands at y*64+x with the sector's own stride
     * is assumed from the grid geometry, not read off the stamping site.
     *
     * The layout this stamps was re-confirmed live on 2026-10-23 against sector 31
     * with G23MAX loaded: mon_hp, mon_type and mon_role matched monx31 at 32/32
     * each, mon_kind 30/32, and x/y under-matched only because creatures walk.
     * That is a fourth independent capture agreeing with docs/monster-spawn.md § 2,
     * which is also the capture that disproved a false alarm raised against this
     * same file the same day — see § 10 there before doubting these offsets. */

    for (int i = 0; i < U2_MON_SLOTS; i++) {
        /* type 0 is empty, which is asm/0A74's own test. The x||y pair above is
         * kept because a monx file can carry coordinates on a slot the original
         * never gave a type, but a type-less slot must not stamp 0 over terrain —
         * the wandering spawner produces exactly such slots whenever its arm
         * refuses, because asm/0CC0 writes the coordinates before the arm runs. */
        if (!(s->mon_x[i] || s->mon_y[i]) || s->mon_type[i] == 0)
            continue;
        const int cell = (int)s->mon_y[i] * U2_SECTOR_EDGE + (int)s->mon_x[i];
        if (cell < 0 || cell >= U2_SECTOR_TILES)
            continue;
        w->map[cell] = s->mon_type[i];
    }
    return occupied;
}

/*
 * Re-stamp the grid after the creature arrays changed — a walk, a kill, a spawn.
 *
 * The grid is rebuilt from `w->map_base` rather than patched cell by cell, so a
 * creature leaving a cell puts the terrain back instead of leaving its own type
 * byte behind forever. asm/2233 commits a move by writing the new coordinates
 * and the kind byte, and the cell bookkeeping around it (the `and 0xf0` then
 * `or [di+0x197]` pair at asm/1151's tail) is only partly read, so the exact
 * nibble arithmetic of the original's stamp is NOT reproduced here — what is
 * reproduced is the observable property: one creature per cell, and the cell
 * under it survives its departure.
 *
 * One cell used to be exempt, and that was wrong (2026-10-28). This loop left any
 * cell carrying 0x80 alone, because asm/2E3D file 0x30F4 does write
 * `movb $0x80,(%bx,%si)` on a kill and the address pair was assumed to name the
 * map cell. Whatever it names, it is not the cell the renderer reads: 0x80 is a
 * staging-space flag, and in map space the `>>2` transform turns it into art 32,
 * the first letter glyph — so the engine drew a letter A on the grass where the
 * creature had died, permanently, because nothing would clear it.
 * captures/spice86/cp5-fight/screen3.png -> screen4.png (sector 31, a town,
 * `ATTACK--EAST--HIT!!!` then `KILLED--GOLD+1--EXP.+02`) shows the original
 * leaving plain floor. Terrain comes back here, unconditionally.
 */
void u2_monsters_restamp(u2_ctx *ctx, u2_world *w)
{
    if (ctx == NULL || w == NULL || !w->map_loaded)
        return;

    /* Inside a site the same 4096-byte buffer is NOT a 64x64 sector. It is 16 pages of
     * 16x16 indexed by a floor byte, so the y*64+x arithmetic below is nonsense there:
     * a creature at page 0 cell (5,5) lands at byte 325, which is page 1, every live
     * creature gets stamped on a floor the player is not standing on, and the cells
     * around the player get stamped by creatures that are elsewhere. A live session froze
     * on exactly this on 2026-10-29, on the first kill, because the kill path is one of
     * this function's five callers: the page was rebuilt, phantom stamps appeared in the
     * corridor, and ADVANCE refused cells nothing occupied.
     *
     * Same job, correct arithmetic. The site-kind test is the one dungeon.h documents for
     * u2_site_is_dungeon(); it is repeated rather than called because this file is linked
     * into standalone CP5 harnesses that do not carry the dungeon module, and a stamp
     * routine that cannot link is a stamp routine nobody runs.
     *
     * This is AGENTS.md section 3's dungeon/overworld quarantine being load-bearing rather
     * than tidy: the two grids share a buffer and a byte offset and nothing else. */
    const u2_state *s = &ctx->state;
    const int dungeon = (s->sector_second == 4 || s->sector_second == 5);

    for (int i = 0; i < U2_SECTOR_TILES; i++)
        w->map[i] = w->map_base[i];

    for (int i = 0; i < U2_MON_SLOTS; i++) {
        if (s->mon_type[i] == 0)
            continue;                       /* asm/0A74: an empty slot is type 0 */
        /* The same occupancy convention u2_monsters_store() uses: a slot at (0,0) is unused
         * (docs/monster-spawn.md § 2). Without this the two stamp paths disagree — the
         * initial stamp skips such a slot and the rebuild stamped it into the corner, which
         * is a phantom creature at (0,0) that no attack can ever reach. No shipped monx slot
         * has a type with no coordinates, so this is latent rather than live, but a phantom
         * is exactly the class of defect the ghost counter exists to catch, and the counter
         * found it the day it was written. */
        if (!(s->mon_x[i] || s->mon_y[i]))
            continue;
        if (dungeon) {
            /* One creature per page cell, ORed into the low nibble so a rock cell keeps
             * its 0x80. The floor is the creature's own [DI+0x1B7] byte, which in a
             * dungeon asm/147F fills with slot>>1 — floor 0..15, not a kind index. */
            const int fl = (int8_t)s->mon_kind[i];
            u2_world_dungeon_put(w, fl, s->mon_x[i], s->mon_y[i],
                                 u2_world_dungeon_at(w, fl, s->mon_x[i], s->mon_y[i])
                                     | s->mon_type[i]);
            continue;
        }
        /* Deliberately a mon_type test and NOT a mon_hp test — asm/0A74's own rule. A
         * creature at zero hp is drawn and walks in the original (measured 2026-10-26: a
         * poked-to-zero slot moved (27,48)->(28,49)->(28,50) with its type intact), so the
         * grid has to show it. The attack scan skips hp 0 anyway, which is the asymmetry
         * the original has; see u2_monsters_at(). Do not "tidy" the two rules into one. */
        const int cell = (int)s->mon_y[i] * U2_SECTOR_EDGE + (int)s->mon_x[i];
        if (cell < 0 || cell >= U2_SECTOR_TILES)
            continue;
        w->map[cell] = s->mon_type[i];
    }
}

void u2_monsters_clear_spawn_cells(u2_ctx *ctx, u2_world *w)
{
    u2_state *s = &ctx->state;

    for (int i = 0; i < 32; i++) {
        if (s->mon_type[i] == 0)
            continue;
        const int cell = (int)s->mon_y[i] * U2_SECTOR_EDGE + (int)s->mon_x[i];
        if (cell < 0 || cell >= U2_SECTOR_TILES)
            continue;
        /* Only a cell that actually carries the baked creature byte. The header's
         * premise is that mapx was authored with the creatures drawn into it, and
         * that premise holds for the sector's own monx file — but this runs after
         * u2_monsters_spawn, and the wandering spawner parks creatures on ordinary
         * ground. Paving those is not faithful and it is not invisible: art 0x1C is
         * town paving in the original CGA table, which is why nobody ever noticed
         * it there, and it is a red brick wall in the EGA fan pack and in the FM
         * Towns theme.
         *
         * Measured against the owner's own save on 2026-11-06, sector 20: 13 slots
         * with a type, and 0 of the 13 sat on a byte equal to their own type. Every
         * one of them was paved, over ground of 0x08, 0x0c and 0x00, and the result
         * was thirteen brick tiles scattered across the sector. The sector-03 seed
         * capture that established the 0x70 behaviour rewrote cells that WERE baked,
         * so this guard keeps that result and drops the one nobody reported.
         *
         * The "he left a copy of him behind" defect this function exists for is a
         * baked-cell problem, and it stays fixed.
         *
         * CORRECTED 2026-11-08: what gets written was wrong. This restored a constant
         * 0x70, but the original restores the SLOT's own terrain byte - `DS:1B7[slot]`,
         * which `asm/2233_sub_2233` writes back into the grid on departure and
         * `asm/0CEE_spawn_tile_store` fills at spawn from the rolled cell's terrain.
         * No constant 0x70 is ever stored to the grid anywhere in the corpus. The
         * shipped data agrees: across 511 live slots `monx[128..159]` is only ever
         * 0x70 or 0x08, because that is where authored creatures stand. Sector 03 is a
         * paving sector, which is exactly why the constant looked right there and why
         * § 9 could not tell the two apart from that sector alone. Right in towns, wrong
         * in the field. See docs/map-format.md § 9 and docs/needs-gameplay.md G66.
         *
         * The baked-cell guard above stays, and for an honest reason: the original needs
         * no guard because it restores on departure, whereas the engine restores here at
         * load, so it must not pave ground that was never baked. The guard reaching the
         * right outcome for the wrong reason is why the constant survived this long. */
        if (w->map_base[cell] != s->mon_type[i])
            continue;
        /* Both arrays: map_base is what restamp rebuilds from, so clearing only w->map would let
         * the baked art reappear on the creature's first step off the cell - which is the bug. */
        w->map_base[cell] = s->mon_kind[i];
        w->map[cell] = s->mon_kind[i];
    }
}

void u2_monsters_store(u2_ctx *ctx, u2_world *w)
{
    if (ctx == NULL || w == NULL || !w->mon_loaded)
        return;

    const u2_state *s = &ctx->state;
    memcpy(w->mon + MON_OFF_X,    s->mon_x,    U2_MON_SLOTS);
    memcpy(w->mon + MON_OFF_Y,    s->mon_y,    U2_MON_SLOTS);
    memcpy(w->mon + MON_OFF_HP,   s->mon_hp,   U2_MON_SLOTS);
    memcpy(w->mon + MON_OFF_TYPE, s->mon_type, U2_MON_SLOTS);
    memcpy(w->mon + MON_OFF_KIND, s->mon_kind, U2_MON_SLOTS);
    memcpy(w->mon + MON_OFF_ROLE, s->mon_role, U2_MON_SLOTS);
    w->mon_dirty = 1;
}

int u2_monsters_at(const u2_ctx *ctx, int x, int y)
{
    if (ctx == NULL)
        return -1;
    for (int i = 0; i < U2_MON_SLOTS; i++) {
        /* The attack scan skips hp 0, and the grid stamp does not. That asymmetry is the
         * original's, not an oversight — it was measured, and it survived two attempts to
         "fix" it away today.
         *
         * Evidence (2026-10-26, original under Spice86, sector 03): poke mon_hp of an
         * adjacent live slot to 0, leaving type and kind set, then swing at it. The first
         * swing MISSED — the slot was still at hp 0 plus one regen tick and its type was
         * intact. The second swing killed it (type cleared, slot emptied) from the same
         * geometry. The only difference is that the regen in the first swing's post-command
         * tail lifted hp off 0, so the second swing had a target. Same run also showed the
         * zero-hp creature WALKING: (27,48) -> (28,49) -> (28,50), type 0xFC throughout.
         *
         * So: drawn and mobile (asm/0A74 keys the sweep and the stamp on mon_type), but
         * unhittable while hp is 0 (this scan). In normal play that window is almost
         * unreachable, because the blow that zeroes hp is followed in the SAME command by
         * the sweep's regen (asm/2122/212a, +1 below 16 HP), so the player's next swing
         * meets hp 1, not hp 0. The observable effect of dmg == hp is one extra swing.
         *
         * The exception worth remembering: the regen lives in the move routine, so a
         * zero-hp creature that never attempts a step never regens and stays unhittable.
         *
         * The x||y test is the module's occupancy convention (docs/monster-spawn.md § 2: a
         * slot at (0,0) is unused), kept alongside hp rather than replaced by it.
         *
         * Ordering note for anyone re-deriving this: the scan returns the LOWEST slot index
         * at the cell, which is what the live capture showed only if the front creature
         * happened to be the lower slot. Whether the original scans in slot order or in some
         * other order is not established; slot order is the assumption, and it is the one to
         * revisit if a fight ever picks the wrong target among stacked creatures. */
        if (ctx->state.mon_hp[i] == 0)
            continue;
        if ((ctx->state.mon_x[i] || ctx->state.mon_y[i]) &&
            ctx->state.mon_x[i] == (uint8_t)x &&
            ctx->state.mon_y[i] == (uint8_t)y)
            return i;
    }
    return -1;
}

int u2_monsters_ghost_cells(const u2_ctx *ctx, const u2_world *w)
{
    if (ctx == NULL || w == NULL || !w->map_loaded)
        return 0;

    /* Which cells some creature slot is standing on. Occupied means "has a type and is not
     * the (0,0) unused convention" — deliberately NOT "hittable", because a zero-hp creature
     * is legitimately stamped and is not a ghost. See the header. */
    uint8_t occupied[U2_SECTOR_TILES];
    memset(occupied, 0, sizeof occupied);
    for (int i = 0; i < U2_MON_SLOTS; i++) {
        if (ctx->state.mon_type[i] == 0)
            continue;
        if (!(ctx->state.mon_x[i] || ctx->state.mon_y[i]))
            continue;
        const int cell = (int)ctx->state.mon_y[i] * U2_SECTOR_EDGE
                       + (int)ctx->state.mon_x[i];
        if (cell >= 0 && cell < U2_SECTOR_TILES)
            occupied[cell] = 1;
    }

    int ghosts = 0;
    for (int c = 0; c < U2_SECTOR_TILES; c++) {
        if (w->map[c] != w->map_base[c] && !occupied[c])
            ghosts++;
    }
    return ghosts;
}

/* The mirror image of u2_monsters_ghost_cells(), and it exists because the ghost counter
 * cannot see this defect at all.
 *
 * Ghosts count cells that are stamped with no creature standing there — "he left a copy of
 * him behind". The opposite failure is a creature in the state table whose cell in the grid
 * still holds terrain, which renders as an invisible monster: walkable, unhittable, and
 * completely silent until it moves and the restamp finally notices it. That is exactly what
 * happened on entering a town before site_reload learned to restamp, and the owner walked
 * into it as "the NPCs don't exist".
 *
 * Counted as slots rather than cells because a slot is the unit the renderer and the attack
 * scan both work from, and two slots on one cell is a collision worth seeing as two. */
int u2_monsters_unstamped_cells(const u2_ctx *ctx, const u2_world *w)
{
    if (ctx == NULL || w == NULL || !w->map_loaded)
        return 0;

    int missing = 0;
    for (int i = 0; i < U2_MON_SLOTS; i++) {
        if (ctx->state.mon_type[i] == 0)
            continue;
        if (!(ctx->state.mon_x[i] || ctx->state.mon_y[i]))
            continue;
        const int cell = (int)ctx->state.mon_y[i] * U2_SECTOR_EDGE
                       + (int)ctx->state.mon_x[i];
        if (cell < 0 || cell >= U2_SECTOR_TILES)
            continue;
        if (w->map[cell] != ctx->state.mon_type[i])
            missing++;
    }
    return missing;
}

int u2_monsters_nearest(const u2_ctx *ctx, int x, int y)
{
    if (ctx == NULL)
        return -1;

    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < U2_MON_SLOTS; i++) {
        if (!(ctx->state.mon_x[i] || ctx->state.mon_y[i]))
            continue;
        int dx = (int)ctx->state.mon_x[i] - x;
        int dy = (int)ctx->state.mon_y[i] - y;
        int d  = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* ------------------------------------------------------------- the wandering spawn */

/*
 * The nine spawn arms, indexed by the lowest set bit of the selector byte, with a
 * zero byte falling through to the last row. Every column is read off asm/, not
 * chosen: the type and HP immediates come from asm/0D4F (Orc), asm/0D6F (Thief at
 * 0D6F, Daemon at 0D8F), asm/0DAF (sea serpent) and asm/0DC4 (Fighter, Frigate,
 * Devil, Wizard, Balron and the shared reject tail).
 *
 * want_tile is the immediate each arm's `cmp` carries against the slot's terrain
 * byte. The two arms wanting 0x00 are the two sea creatures and the rest want
 * 0x08, which is the strongest single piece of evidence that the compared byte is
 * the terrain of the rolled cell rather than a monster kind — see
 * docs/monster-spawn.md § 15.4, where all nine rows were pinned by forcing the
 * selector and watching which arm ran.
 */
static const struct u2_spawn_arm kSpawnArms[9] = {
    { 0x30, 0x10, 0x08 },   /* bit0  Orc */
    { 0xFC, 0x20, 0x08 },   /* bit1  Thief */
    { 0x34, 0x40, 0x08 },   /* bit2  Daemon */
    { 0x2C, 0x40, 0x00 },   /* bit3  sea serpent — water */
    { 0xF0, 0x80, 0x08 },   /* bit4  Fighter */
    { 0x48, 0xA0, 0x00 },   /* bit5  Frigate — water */
    { 0x38, 0xC0, 0x08 },   /* bit6  Devil */
    { 0xF8, 0xE0, 0x08 },   /* bit7  Wizard */
    { 0x3C, 0xFF, 0x08 },   /* no bits set: Balron */
};

struct u2_spawn_arm u2_spawn_arm_for(uint8_t selector)
{
    for (int bit = 0; bit < 8; bit++)
        if (selector & (1u << bit))
            return kSpawnArms[bit];
    return kSpawnArms[8];
}

int u2_monsters_wander(u2_ctx *ctx, u2_world *w)
{
    if (ctx == NULL || w == NULL || !w->map_loaded)
        return -1;

    u2_state *s = &ctx->state;

    /* asm/0C9C..0CB0. The scan walks 31 down to 1 and takes the first slot whose
     * mon_type is zero — the same emptiness test asm/0A74 uses for the sweep, and
     * slot 0 is unreachable because `dec di ; jne` stops at 1. Taking the highest
     * free slot is what the original does and is observable: a live run filled
     * 1f, 1e, 1d ... in that order.
     *
     * The bytes are `mov ah,[di+0x197] ; or ah,al ; je found`, and that AL is
     * residue from the sweep rather than something this path sets. What it can
     * only ever mean is a second, unexplained condition on the same test; the
     * observed behaviour is the mon_type test alone, so that is what is
     * implemented, and the residue is recorded rather than smoothed over. */
    int slot = -1;
    for (int i = U2_MON_SLOTS - 1; i >= 1; i--) {
        if (s->mon_type[i] == 0) { slot = i; break; }
    }
    if (slot < 0)
        return -1;

    /* asm/0CB6..0CD4: two rolls. The first is the x and must come in under 0x3F;
     * the second is rejected on its sign bit and then masked to 0x3F for y, which
     * is where the overworld's wrap is enforced on the way in. */
    const uint8_t rx = u2_rand_byte(ctx);
    if (rx >= 0x3F)
        return -1;
    const uint8_t ry_raw = u2_rand_byte(ctx);
    if (ry_raw & 0x80u)
        return -1;
    const uint8_t ry = (uint8_t)(ry_raw & 0x3F);

    /* asm/0CC0 and asm/0CDC store the coordinates BEFORE the arm is chosen, so a
     * refused attempt still leaves its coordinates behind. That is not a leak to
     * paper over: live reads found exactly this residue, a slot with x, y and the
     * terrain byte and no type at all. */
    s->mon_x[slot] = rx;
    s->mon_y[slot] = ry;

    const int cell = (int)ry * U2_SECTOR_EDGE + (int)rx;
    if (cell < 0 || cell >= U2_SECTOR_TILES)
        return -1;

    /* asm/0CEB calls asm/50B3_map_tile_ptr, which reads the LIVE grid — so a cell
     * another creature is standing on reads as that creature's type byte, and the
     * terrain test then refuses. asm/0CEE files that byte into the slot. */
    const uint8_t tile = w->map[cell];
    s->mon_kind[slot] = tile;

    s->spawn_selector++;
    const struct u2_spawn_arm arm = u2_spawn_arm_for(s->spawn_selector);
    if (tile != arm.want_tile) {
        /* asm/0DC4's head: `decb 0x60` and out. The selector is rolled back, which
         * is the mechanism behind the measured "it creeps, it does not march". */
        s->spawn_selector--;
        return -1;
    }

    s->mon_type[slot] = arm.type;
    s->mon_hp[slot]   = arm.hp;
    w->map[cell]      = arm.type;   /* the stamp: asm/0D4F's `mov %al,(%bx,%si)` */
    return slot;
}
