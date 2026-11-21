#ifndef U2_RESIDENCY_H
#define U2_RESIDENCY_H
/*
 * Making a sector resident.
 *
 * One sector is resident at a time (core/world.h), and there are exactly two ways
 * that happens: the world is opened, or the player crosses an edge, enters a site,
 * or dives from orbit. Both need the same sequence in the same order, and the order
 * is not arbitrary — each step closes a failure somebody documented, and running
 * them out of sequence reproduces it. Until 2026-11-21 the sequence existed twice,
 * in overworld_open() and site_reload(), and the copies had already drifted: the
 * restamp was added to one and missed in the other, and the project made that
 * mistake twice before extracting the sequence instead of patching both copies.
 *
 * So the sequence lives here. What a caller needs around it — the DS:12 view mode,
 * the dungeon's own spawner, the status band, its own wording of the note — stays
 * with the caller, because those really are different rules and not one rule
 * written twice.
 */
#include "core/world.h"      /* u2_sector_id */
#include "game/overworld.h"  /* u2_overworld */

/* What making a sector resident produced, so a caller can word its own note
 * without re-deriving anything. */
typedef struct u2_residency {
    int creatures;   /* how many the sector authored */
    int no_monx;     /* monx absent: normal for 17 of 33 sectors, and the note says so */
} u2_residency;

/*
 * Make sector `id` the live world: map layer, the recorded delta for each layer,
 * creature layer, spawn, spawn-cell clear, dialogue, grid stamps, and the demo
 * digest. Returns -1 with ow->note naming the sector and the reason; callers print
 * that note verbatim, so it has to describe the sector the player is standing in.
 *
 * Two things it deliberately does not do:
 *
 *   It does not clear the creature table. The reload path clears first (see
 *   u2_sector_leave); the open path must not, because a save may have put bytes in
 *   the table that the spawn does not overwrite.
 *
 *   It does not set ctx->state.view_mode. The reload path derives it from the
 *   sector's ones digit and the open path from the class stamp; same field, two
 *   rules, and folding them together is how DS:12 got latched at 4 for 56 records.
 */
int u2_sector_load(u2_ctx *ctx, u2_overworld *ow, u2_sector_id id, u2_residency *out);

/*
 * Clear the six creature arrays before a load, so a sector that authors no monsters
 * cannot inherit the last sector's table. Only the reload path owes this: at world
 * open the state is whatever the player's save said, and wiping it here would erase
 * creature bytes the spawn never overwrites.
 */
void u2_sector_leave(u2_ctx *ctx);

/*
 * Publish the overworld's working position into the DS mirror (DS:00/01).
 *
 * The mirror is the captured field, so anything that reads position — the turn log,
 * a demo record, --dump-state — has to see the same value the frame was drawn from.
 * That is why this is called from the draw path and not from each movement site:
 * u2_turnlog runs inside the draw, so mirroring beside it means the log and the
 * frame cannot disagree. A movement site that mirrored as well would be a second
 * copy of this rule, and victory_flight had exactly that duplicate until 2026-11-21.
 *
 * One direction only. When something writes the mirror from the other side — a demo
 * checkpoint landing at a quiescent point — that is u2_sector_absorb.
 */
void u2_sector_publish(u2_ctx *ctx, const u2_overworld *ow);

/*
 * Absorb a checkpoint that just landed in the DS mirror: pull DS:00/01 back into the
 * overworld's working position and restamp the grid, because the grid was stamped
 * from the freshly loaded monx rather than from the resumed creature table.
 *
 * Returns non-zero when it did something, which today means ctx->demo_restamp_pending
 * was up; it clears the latch. The caller is the command loop at its quiescent point,
 * and the reason this lives here rather than inline there is the same reason the load
 * sequence does: the re-sync is three steps with an ordering, and an ordering written
 * twice is a bug waiting to be half-fixed.
 */
int u2_sector_absorb(u2_ctx *ctx, u2_overworld *ow);

#endif /* U2_RESIDENCY_H */
