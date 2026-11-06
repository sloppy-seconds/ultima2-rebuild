/*
 * monsters.h — the creature table: load it, look things up in it, write it back.
 *
 * CP5 W1. The six parallel arrays live in `u2_state` at their proven DS offsets
 * (mon_x 0x137, mon_y 0x157, mon_hp 0x177, mon_type 0x197, mon_kind 0x1B7,
 * mon_role 0x1D7), each pinned by a _Static_assert in state.h. This module owns
 * only the movement of those bytes between the sector buffer and the state
 * block, plus the lookups the combat and movement code needs. It deliberately
 * contains no combat rules and no monster AI.
 */
#ifndef U2_MONSTERS_H
#define U2_MONSTERS_H

#include "core/state.h"
#include "core/world.h"

#define U2_MON_SLOTS 32

/*
 * Place the sector's creatures into the state block.
 *
 * `w->mon` is the 256-byte monxNN image already read by u2_world_load(). asm/22D5
 * reads it with CX=0x100, and the six arrays occupy the first 192 bytes in the
 * order below — measured against four live sectors in docs/monster-spawn.md § 6,
 * where `type` matched 32/32 in every sector and `hp` in 31-32 of 32.
 *
 * Slots with x == 0 && y == 0 are treated as empty, matching asm/102B, which
 * tests the role byte for zero rather than the coordinates; both readings agree
 * on every capture in the corpus.
 *
 * Returns the number of occupied slots, for callers that want to say so.
 *
 * CP5 W2: this also STAMPS each creature's type byte into the sector grid, so it
 * takes the world non-const on purpose. The renderer draws the grid, not the
 * monster record — docs/art-index.md proved that on the one slot in the corpus
 * where the two disagree — and the original's spawner mutates the in-memory map
 * the same way. The same stamped cell is what passability reads, which is why a
 * creature blocks movement without any extra check.
 */
int u2_monsters_spawn(u2_ctx *ctx, u2_world *w);

/*
 * The wandering spawn: one attempt, exactly as asm/0C9C..0D05 does it.
 *
 * Called once per completed command, and only in a sector whose second decimal
 * digit is 0 (the world map), because that is the gate asm/0A38 puts on the whole
 * post-command sweep and the spawner is that sweep's tail. Returns the slot it
 * filled, or -1 for any of the four ways an attempt can come to nothing: no free
 * slot, either coordinate roll failing, or the selector's arm refusing the terrain
 * it landed on. The last of those rolls the selector byte back, which is the whole
 * reason `spawn_selector` creeps instead of marching.
 *
 * docs/monster-spawn.md § 15.4 is the evidence; every constant here is read off
 * asm/, not chosen.
 */
int u2_monsters_wander(u2_ctx *ctx, u2_world *w);

/* The arm the selector picks: the lowest set bit of `selector`, and a zero byte
 * falls through to the Balron arm. Exposed for the test, which asserts the table
 * rather than trusting the loop that walks it. */
struct u2_spawn_arm {
    uint8_t type;      /* what goes into mon_type and is stamped into the grid */
    uint8_t hp;        /* what goes into mon_hp */
    uint8_t want_tile; /* the terrain byte the cell must carry, else the arm refuses */
};
struct u2_spawn_arm u2_spawn_arm_for(uint8_t selector);

/*
 * Copy the live creature arrays back into the sector buffer and mark it dirty,
 * which is the engine's side of the original's 256-byte write from DS:137 on
 * save. Only call it when the creatures actually changed; the sector store is
 * dirty-gated by design (see world.h).
 */
void u2_monsters_store(u2_ctx *ctx, u2_world *w);

/*
 * Re-stamp the sector grid from the creature arrays, restoring the terrain under
 * any cell a creature has left. Call it whenever the arrays move (a creature
 * turn, a kill) and before anything reads the grid for passability or drawing.
 * Rebuilds from `world.map_base`, so it is idempotent, and nothing is exempt:
 * a cell carrying 0x80 is terrain like any other, which is what keeps a killed
 * creature from staying on screen as a letter (see the comment in monsters.c).
 */
void u2_monsters_restamp(u2_ctx *ctx, u2_world *w);

/* The slot occupying sector cell (x,y), or -1 for none. A creature occupies a
 * cell the same way a mountain does: the step is rejected, not fatal. Observed
 * live 2026-10-23 — stepping onto an adjacent creature printed
 * `EAST--INVALID MOVE!` over a tile the player was standing on the value of. */
int u2_monsters_at(const u2_ctx *ctx, int x, int y);

/* Count cells the grid claims are occupied but no creature slot actually sits on.
 *
 * This is the "he left a copy of him behind" invariant, stated so a machine can check it: the grid is
 * stamped from the creature arrays, so if a cell differs from the pristine terrain and no slot is
 * standing there, something drew a creature that is not there — a sprite whose owner walked off and
 * whose cell was never put back.
 *
 * Note what this deliberately does NOT count. A zero-hp creature is stamped (asm/0A74 keys on type)
 * and is skipped by the attack scan, and that asymmetry is the original's own — measured 2026-10-26.
 * It is a wounded monster, not a ghost, so occupancy here is "some slot with a type is at this cell",
 * not "some hittable creature is". Using u2_monsters_at() for the second half would report every
 * wounded creature as a ghost and teach everyone to ignore the number.
 */
/* Clear the creature art that mapx bakes into every spawn cell.
 *
 * mapxNN is not pure ground: at each cell where monxNN places a creature, the map byte IS that
 * creature's own art byte. Measured across the whole pack: 483 of 511 live spawn cells have
 * mapx[x,y] == monx type. The map was authored by drawing the creatures into the terrain.
 *
 * The original does not treat that byte as ground. Once the cell is no longer under a creature it
 * reads 0x70 (art 0x1C, paving, walkable) - and it reads 0x70 even when the file says otherwise.
 * Proven by seeding: spice86_start.sh --seed-map with every sector-03 spawn cell rewritten to 0x0C
 * forest, then a live recording; 13 of 14 vacated spawn cells came back 0x70, not the 0x0C the
 * file was given. So 0x70 is written by the game, not derived from mapx.
 *
 * Why the engine has to do this explicitly: u2_world_load reads mapx verbatim into map_base, and
 * u2_monsters_restamp rebuilds w->map from map_base. Without this clear, a creature walking off its
 * spawn cell has its own sprite restored underneath it - the owner's report "he left a copy of him
 * behind" - and the spawn cell is a wall, because every art index seen at sector-03 spawn cells
 * (0x0d-0x0f, 0x1a, 0x3c-0x3f) is impassable in u2_tile_passable's default-deny table while the
 * original has walkable paving there.
 *
 * Whether the original clears at load or on departure is still open (docs/map-format.md § 9); both
 * produce this terrain, so both are served by clearing map_base once here. Call after the monx
 * load, while the arrays still hold spawn positions.
 *
 * Amended 2026-11-06 after the owner reported brick tiles in open country. The clear is now
 * restricted to cells whose map byte actually equals the creature's own type, which is what the
 * paragraph above is about. It used to fire for every slot carrying a type, and because this runs
 * after the wandering spawner that paved ordinary terrain wherever a spawned creature stopped.
 * Sector 20 in the owner's own save had 13 creatures and 0 baked cells, so all 13 got paved. Art
 * 0x1C is subtle paving in the original CGA table and a red brick wall in the EGA fan pack and the
 * FM Towns theme, which is why the defect was invisible in the DOS original and is not here.
 */
void u2_monsters_clear_spawn_cells(u2_ctx *ctx, u2_world *w);

int u2_monsters_ghost_cells(const u2_ctx *ctx, const u2_world *w);

/*
 * How many creature slots are in the state table but NOT stamped into the grid — the mirror
 * image of u2_monsters_ghost_cells(), and the counter that catches an invisible monster.
 *
 * A ghost is a stamp with no creature. This is a creature with no stamp: walkable ground that
 * is secretly occupied, which is what entering a site used to produce, because site_reload
 * loaded the creatures and cleared their baked art but nothing restamped until the first
 * creature move. Zero means every creature in the table is visible on the grid.
 */
int u2_monsters_unstamped_cells(const u2_ctx *ctx, const u2_world *w);

/* The nearest occupied slot to (x,y) in manhattan distance, or -1 if the sector
 * holds nothing. Used by the attack command's "nothing there" path and by tests. */
int u2_monsters_nearest(const u2_ctx *ctx, int x, int y);

#endif /* U2_MONSTERS_H */
