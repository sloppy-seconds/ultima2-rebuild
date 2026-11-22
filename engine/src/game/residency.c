/*
 * Making a sector resident — see residency.h.
 *
 * Every step below is in this order for a reason, and the reasons are recorded
 * beside the steps rather than in a document that will drift from them.
 */
#include <stdio.h>
#include <string.h>

#include "game/residency.h"
#include "core/ctx.h"
#include "core/demo.h"
#include "game/monsters.h"

void u2_sector_leave(u2_ctx *ctx)
{
    u2_state *s = &ctx->state;
    memset(s->mon_x,    0, sizeof s->mon_x);
    memset(s->mon_y,    0, sizeof s->mon_y);
    memset(s->mon_hp,   0, sizeof s->mon_hp);
    memset(s->mon_type, 0, sizeof s->mon_type);
    memset(s->mon_kind, 0, sizeof s->mon_kind);
    memset(s->mon_role, 0, sizeof s->mon_role);
}

int u2_sector_absorb(u2_ctx *ctx, u2_overworld *ow)
{
    if (!ctx->demo_restamp_pending)
        return 0;
    /* The checkpoint just landed. The grid was stamped from the freshly loaded monx
     * rather than from the resumed creature table, so it has to be re-stamped before
     * anything draws. Position needs no second step: DS:00/01 IS the position, not a
     * copy of one, so a checkpoint that wrote the mirror wrote the position. */
    ctx->demo_restamp_pending = 0;
    u2_monsters_restamp(ctx, &ow->world);
    return 1;
}

int u2_sector_load(u2_ctx *ctx, u2_overworld *ow, u2_sector_id id, u2_residency *out)
{
    const u2_world_result r = u2_world_load(ctx, &ow->world, 1, id);
    if (r != U2_WORLD_OK) {
        snprintf(ow->note, sizeof ow->note, "sector %d%d: %s",
                 id.hi, id.lo, u2_world_result_text(r));
        return -1;
    }

    /* The recorded delta, applied here and nowhere later: it has to land before the
     * spawn clear and the restamp, or those two run over bytes the recording never
     * had and the pipelines diverge for a reason that has nothing to do with the
     * player. The refusal text has already been printed by the time this returns -1. */
    if (u2_demo_apply_world(ctx, &ow->world, id, 1) != 0) {
        snprintf(ow->note, sizeof ow->note, "sector %d%d: demo world does not match",
                 id.hi, id.lo);
        return -1;
    }

    const u2_world_result mr = u2_world_load(ctx, &ow->world, 0, id);
    /* The creature file carries a delta too, and it has to land before the spawn: the
     * spawn copies monx into the table, and clear_spawn_cells then restores terrain into
     * the grid from mon_kind. Do that from the content creature file and the grid ends up
     * with a creature the recorder never had -- which is exactly the 13-byte divergence
     * measured at record 20 of the owner's demo, five of them the last element of each
     * creature array. */
    if (mr == U2_WORLD_OK && u2_demo_apply_world(ctx, &ow->world, id, 0) != 0) {
        snprintf(ow->note, sizeof ow->note, "sector %d%d: demo creatures do not match",
                 id.hi, id.lo);
        return -1;
    }
    const int creatures = (mr == U2_WORLD_OK) ? u2_monsters_spawn(ctx, &ow->world) : 0;
    if (mr == U2_WORLD_OK)
        u2_monsters_clear_spawn_cells(ctx, &ow->world);

    /* asm/22D5 reads the sector's dialogue into the same scratch the creature table used.
     * A sector with no tlkx file is normal, not a failure -- but the buffer must not keep the
     * PREVIOUS sector's lines, or a role byte in a dialogue-less town would speak someone
     * else's line. Clear first, load second. */
    ow->world.tlkx_loaded = 0;
    (void)u2_world_load_tlkx(ctx, &ow->world, id);

    /* Restamp, or every creature in the sector is invisible until it moves.
     *
     * u2_monsters_spawn loads the creature table into state and clear_spawn_cells blanks the
     * baked art the map file carries at those cells, but neither writes the sprites back into
     * the grid. The only other caller of restamp is the creature turn, gated on rep.moved, so
     * on entry the grid held paving where a monster stood: walkable, unhittable, and silent
     * until that monster's first step, which is when it suddenly appeared. The owner walked
     * into this as "when I first go into a town, the NPCs don't exist... after the first move
     * they get rendered in that spot".
     *
     * This is also why the ghost counter never caught it: ghosts count stamps with no
     * creature, and this is creatures with no stamp. u2_monsters_unstamped_cells() exists now.
     */
    if (mr == U2_WORLD_OK)
        u2_monsters_restamp(ctx, &ow->world);
    /* Latched AFTER the load path has finished, so it measures what a player would actually
     * see on the first frame of the new sector: with the restamp in place it is 0, and with
     * it removed it is the whole creature count, because nothing else stamps until a
     * creature happens to move. */
    ctx->diag_unstamped_at_load = u2_monsters_unstamped_cells(ctx, &ow->world);

    /* The digest-and-delta capture, at the one point where the loaded bytes are both
     * available and still pristine — after the load, before anything has stamped into
     * them. Refuse on the bytes that reach the world, note on the bytes that only reach
     * the screen. Without this a recording only ever carried the sector it STARTED in:
     * walk into a neighbour your save had dirtied and the demo had nothing to say about
     * it, so a viewer rebuilt the original ground and the run diverged somewhere that
     * looked exactly like an engine bug. That is the § 13.7 per-sector item; the cap was
     * the smaller half of it. */
    u2_demo_note_world(ctx, &ow->world);
    if (u2_demo_overlay_failed(ctx)) {
        /* The note has to name THIS sector. The latch is run-wide and sticky, so a
         * load that fails here long after an earlier refusal would otherwise leave
         * whatever note the first failure wrote, and the message at the top level would
         * blame a sector that was never involved. That is not hypothetical: it is what
         * this path did, and it named sector 70 for a failure in sector 71. */
        snprintf(ow->note, sizeof ow->note, "sector %d%d: demo world does not match",
                 id.hi, id.lo);
        return -1;
    }

    if (out) {
        out->creatures = creatures;
        out->no_monx = (mr != U2_WORLD_OK);
    }
    return 0;
}
