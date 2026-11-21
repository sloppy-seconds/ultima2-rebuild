/*
 * Time doors (moongates) — see gates.h for the scope ruling and docs/moongates.md
 * for the evidence behind every number in this file.
 *
 * Byte provenance for the table: original/ultimaii.exe file offset 0x2998, which is the
 * runtime displacement %cs:0x2798 read by asm/2708 plus the 0x200 file/runtime offset.
 * Row = era * 8, slot = 2 bytes. Verified 12/12 against gate cells watched live in
 * eras 1, 2 and 3 (captures/spice86/moongate-cycle).
 */

#include "game/gates.h"

#include <stddef.h>

#include "core/state.h"
#include "core/world.h"
#include "game/monsters.h"    /* u2_monsters_store: sync creatures before the transit save */
#include "game/overworld.h"

struct gate_cell { uint8_t x, y; };

/* Four doors per era, five eras. The era digit is DS:49 / sector_first, which is the
 * sector's TENS digit and, on Earth, the time period — settled 2026-10-29 by transiting
 * and reading DS:49 on both sides (02 -> 03 -> 02) while DS:6D, the world byte, did not
 * move. See docs/moongates.md § 4 and docs/needs-gameplay.md G45. */
static const struct gate_cell k_gates[5][U2_GATE_SLOTS] = {
    { { 29, 56 }, { 31, 56 }, { 33, 56 }, { 35, 56 } },   /* era 0 */
    { { 34,  8 }, { 47, 28 }, { 36, 56 }, { 20, 37 } },   /* era 1 */
    { { 16, 16 }, { 48, 24 }, { 42, 24 }, { 19, 52 } },   /* era 2 */
    { { 56, 52 }, { 28, 12 }, { 33, 23 }, { 20, 52 } },   /* era 3 */
    { { 18, 23 }, { 50, 34 }, { 52, 24 }, {  8, 11 } },   /* era 4 */
};

/* The destination era, by gate index. Read out of asm/26B7 + asm/26C3 + asm/26C6, which are one
 * contiguous routine (26B7..26CB) that the Phase 2 map cut into three fragments:
 *
 *     k = DS:6E >> 1
 *     target = k if k < era else k + 1
 *     DS:49 = target
 *
 * The gate index IS the destination, as a position in the sorted list of the other four eras: gate
 * 0 always leads to the earliest other era, gate 3 always to the latest. Consequences, all of them
 * checked in gate_check.c rather than asserted in prose: every era reaches the other four exactly
 * once; gate k is the two-way link between era k and era k+1 and nothing else round-trips.
 *
 * This replaced a four-row table of observed pairings. Two of the four slots had never been watched
 * transiting and the engine refused to offer a door there, which was the right call while the rule
 * was unknown and is now a gap the bytes close. The rule reproduces both watched transits unchanged
 * (era 2 slot 1 -> 1, era 2 slot 2 -> 3), and tools/gate_graph.py checks it against the Codex's
 * five general rules and four named two-way connections. docs/moongates.md section 3b. */
int u2_gate_partner_era(int era, int slot)
{
    if (era < 0 || era >= 5 || slot < 0 || slot >= U2_GATE_SLOTS)
        return -1;
    return slot < era ? slot : slot + 1;
}

int u2_gate_cell(int era, int slot, int *x, int *y)
{
    if (era < 0 || era >= 5 || slot < 0 || slot >= U2_GATE_SLOTS)
        return 0;
    if (x) *x = k_gates[era][slot].x;
    if (y) *y = k_gates[era][slot].y;
    return 1;
}

int u2_gate_slot(uint8_t gate_index)
{
    return (gate_index & 0x7) / 2;
}

/* The doors belong to Earth's world map and to nothing else.
 *
 * Two independent refusals, and the second one is newer than this file:
 *
 *   - **not off-Earth.** `asm/26F9` is the only caller of the countdown decrement and it opens
 *     `mov 0x6d,%al ; cmp $0x0,%al ; je L_002901`. The clock never ticks while the world byte is
 *     nonzero, so a planet has no rotating door and no door at all. This matters beyond the clock:
 *     off-Earth the sector's tens digit IS the world index (`asm/7030` writes DS:49 from DS:6D on
 *     landing), and the era row this file indexes with that digit is a different axis. Mercury is
 *     not the Pangea of 9,000,000 BC; it is a planet that happens to arrive in sector 10. Before
 *     this check existed the engine rotated era-1 doors on Mercury, and nothing in the suite could
 *     see it because every leg ran on Earth.
 *   - **not inside a settlement.** Measured 2026-11-02 inside Port Boniface (sector 31, ones digit
 *     1), entered the honest way through its wilderness mouth so a return cell was on record:
 *     with the rotation armed by a poke to DS:6F and the live slot at 2, the whole 64x64 town grid
 *     held no 0xC0 at all, and DS:6F sat unchanged across six moves where the wilderness advanced
 *     the slot within four. Two mechanisms failing independently is why both are gated here rather
 *     than one standing in for the other: a stamp test alone would still let the index rotate
 *     invisibly inside a town and come back out of step. captures/spice86/s31-south-edge/.
 */
static int gate_is_on_earth(const u2_ctx *ctx)
{
    return ctx->state.world == 0;
}

static int gate_is_on_world_map(const u2_ctx *ctx)
{
    return gate_is_on_earth(ctx) && ctx->state.sector_second == 0;
}

void u2_gate_turn(u2_ctx *ctx, struct u2_overworld *ow)
{
    if (!gate_is_on_world_map(ctx))
        return;
    /* Mirrors asm/2701 directly: `decb 0x6f ; je L_002908`. Decrement the byte, and rotate only
     * when the DECREMENTED value is zero. That form is chosen over the observation it came from
     * ("the reload happens ON the 1") because it gets both live measurements right at once, and
     * the previous `countdown <= 1` form got only one:
     *
     *   - from 8 the sequence is 7,6,...,1,0 -> reload 8, so eight turns per rotation and a
     *     watcher never samples the zero. That is what docs/moongates.md recorded live.
     *   - at load the byte is 0 (U2_GATE_COUNTDOWN_AT_LOAD, observed twice). The first tick makes
     *     it 0xff and nothing rotates until it comes all the way back round, about 256 turns.
     *     Seen directly in captures/spice86/g73-6f-unit: DS:6F read 0x00 at load, 0xfe moments
     *     later, and DS:6E sat at 00 for the whole session.
     *
     * Under the old form a cold load rotated on turn 1 and put a door on the map that the
     * original would not produce for roughly 256 turns. A bot built on that engine would go
     * looking for a door the real game has not made yet.
     *
     * The unit IS turns, and a turn ends on any consumed keypress -- including one the game
     * rejects -- or on the idle auto-PASS at asm/08FF, roughly every 8-10 s. That is
     * captures/spice86/turn-or-second's conclusion with captures/spice86/g73-6f-unit's rejected-
     * key arm added; an earlier note here claimed the countdown was not turn-driven at all, which
     * was an overcorrection. This function is called on an accepted turn, so it agrees for the
     * API case (one command, one key) and differs for rejected keys and for idle auto-PASS turns,
     * neither of which the API produces. Named rather than plumbed.
     *
     * What is still NOT attributed: which code reloads the byte to 8. gates.h is explicit that a
     * stop at 17D:2908 refuted asm/2744 as the source, and the number is sound because it was
     * measured in memory. This mirrors the observable, not a mechanism nobody has found. */
    ctx->state.gate_countdown = (uint8_t)(ctx->state.gate_countdown - 1u);
    if (ctx->state.gate_countdown != 0)
        return;
    ctx->state.gate_countdown = U2_GATE_COUNTDOWN_RELOAD;
    ctx->state.gate_index = (uint8_t)((ctx->state.gate_index + 2) & 0x7);
    /* The rotation is what puts a door on the map. Before the first one there is
     * nothing to step on, even though the index already points at slot 0. */
    if (ow)
        ow->gate_stamped = 1;
}

int u2_gate_current_cell(const u2_ctx *ctx, const struct u2_overworld *ow, int *x, int *y)
{
    /* No door exists until the first rotation stamps one. At load the index is 0 and the
     * slot-0 cell is ordinary ground; the original stamps nothing until the countdown
     * first expires (era 0, forced expiry, captures/spice86/era0-probe). */
    if (!ow->gate_stamped)
        return 0;
    /* And no door exists inside a settlement, however long the player stands there.
     * See gate_is_on_world_map. */
    if (!gate_is_on_world_map(ctx))
        return 0;
    return u2_gate_cell(ctx->state.sector_first,
                        u2_gate_slot(ctx->state.gate_index), x, y);
}

int u2_gate_transit(u2_ctx *ctx, struct u2_overworld *ow)
{
    const int slot = u2_gate_slot(ctx->state.gate_index);
    int gx = 0, gy = 0;
    if (!u2_gate_current_cell(ctx, ow, &gx, &gy))
        return 0;
    if (ow->x != gx || ow->y != gy)
        return 0;

    const int era = ctx->state.sector_first;

    const int partner = u2_gate_partner_era(era, slot);
    if (partner < 0)
        return 0;                       /* not an era/slot the table covers */
    int px = 0, py = 0;
    if (!u2_gate_cell(partner, slot, &px, &py))
        return 0;

    /* The original flushes the sector being left on a time-door transit: the player
     * record plus the outgoing sector's mapx and monx. Measured 2026-11-13
     * (captures/spice86/g64-timedoor-autosave): an isolated three-point md5 showed the
     * gate rotation writes nothing and the transit step writes all three. A site edge
     * writes only the player record (site_state.py); a transit writes the sector too,
     * so this is NOT gated by keep_site_state -- the original does it in original
     * fidelity. The write lands in the save-slot overlay (u2_world_store), never the
     * content dir (docs/save-scoped-worlds.md). Sync the creature table first so the
     * monx write carries the current creatures, then autosave (player + dirty sector).
     * This must run BEFORE the era digit moves, or it checkpoints the destination.
     * Gate: tests/gate_transit_save.py. */
    u2_monsters_store(ctx, &ow->world);
    ctx->store_reason = "time-door transit";
    (void)u2_overworld_autosave(ctx, ow);

    /* The era digit moves and the world byte does not — that is the whole content of
     * G45, and getting it the other way round would put the player on another planet. */
    ctx->state.sector_first = (uint8_t)partner;
    ow->sector_hi = partner;

    const u2_sector_id id = { (uint8_t)partner, (uint8_t)ow->sector_lo };
    (void)u2_world_load(ctx, &ow->world, 1, id);

    /* The creature table belongs to the sector, not to the player, and it has to be
     * reloaded with the map or the era you left rides through the door with you. This
     * path loaded only the map, so every creature from the outgoing sector stayed in the
     * live table, got stamped into the destination's grid, and was then written into the
     * destination's monx by the next save — which spreads a population across eras rather
     * than merely showing it. Reported by the owner 2026-11-18: crossing into Time of
     * Legends carried the previous era's monsters with them.
     *
     * This is the creature half of what u2_sector_load() does when a sector becomes
     * resident (game/residency.c), and it needs no clear first: u2_monsters_spawn()
     * memcpy's all 32 slots from the file it was given, so the old table cannot
     * survive it. The
     * dialogue is per-sector for the same reason and was also left behind. A time door
     * is a third way a sector becomes resident and does not go through the residency
     * module: it has no map layer to reload here, and its demo-delta story is its own. */
    if (u2_world_load(ctx, &ow->world, 0, id) == U2_WORLD_OK) {
        (void)u2_monsters_spawn(ctx, &ow->world);
        u2_monsters_clear_spawn_cells(ctx, &ow->world);
        u2_monsters_restamp(ctx, &ow->world);
    }

    ow->world.tlkx_loaded = 0;
    (void)u2_world_load_tlkx(ctx, &ow->world, id);

    ow->x = px;
    ow->y = py;

    /* Observed on arrival: the countdown is reloaded and the index has already advanced,
     * so the door you came through is not the one now standing open. */
    ctx->state.gate_countdown = U2_GATE_COUNTDOWN_RELOAD;
    ctx->state.gate_index = (uint8_t)((ctx->state.gate_index + 2) & 0x7);
    return 1;
}
