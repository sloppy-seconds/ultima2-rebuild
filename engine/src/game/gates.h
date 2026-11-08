#ifndef U2_GATES_H
#define U2_GATES_H

/*
 * Time doors (moongates) — the overworld era transit.
 *
 * Everything in this header is a live observation from 2026-10-29, graded in
 * docs/moongates.md. Read that file before changing anything here: the mechanic was
 * refuted by an earlier document in this repo on a census that looked only at stores
 * of the byte, and it is easy to re-introduce that mistake.
 *
 * SCOPE, and it changed on 2026-11-08. The table is decoded for all five eras, and so is the
 * destination: asm/26B7 + asm/26C3 + asm/26C6 are one routine that says
 *
 *     target_era = slot if slot < era else slot + 1
 *
 * so all four slots have a destination in every era and u2_gate_partner_era() no longer reports
 * unknown for any of them. It used to carry a four-row table of the pairings ever watched live and
 * refuse the other two, which was the right call while the rule was undiscovered — a door onto a
 * guessed era reads as finished and is silently wrong, the failure mode AGENTS.md § 3 was written
 * against. The rule is not a guess: it reproduces both watched transits unchanged, and
 * tools/gate_graph.py checks it against the Codex's five general rules and four named two-way
 * connections. Two of the four slots are still derived rather than observed; docs/moongates.md
 * section 3b and needs-gameplay G73 say so.
 *
 * Also deliberately NOT modelled: stamping 0xC0 into the resident sector. Two reasons,
 * both recorded in docs/moongates.md § 7. The substitute art for that byte is under
 * cleanroom control, and writing into the resident grid marks the sector dirty — while
 * whether the original persists a stamped gate into mapxNN on a save is untested.
 */

#include "core/ctx.h"

struct u2_overworld;

/* Reload value for the countdown: 8, OBSERVED LIVE, in every era watched — 1, 2 and 3
 * through play and era 0 by forcing an expiry (captures/spice86/era0-probe).
 *
 * Do NOT attribute this to asm/2744. An earlier revision of this file cited that routine,
 * and a stop armed at 17D:2908 across observed rotations refuted the attribution the same
 * day — see docs/moongates.md § 6. The number is sound because it was measured in memory;
 * the code that produces it is still undiscovered. */
#define U2_GATE_COUNTDOWN_RELOAD 8

/* Observed at load, both bytes 0x00 (captures/spice86/moongate-cycle, first row after P).
 * The engine's zero-initialised state matches, so there is nothing to seed here — which is
 * worth recording because a countdown that reloads to 8 looks like it should start at 8.
 *
 * And before anyone waits for a rotation to happen on its own: from 0x00 the byte wraps to
 * 0xff and needs roughly 255 turns before the first expiry — about 25 minutes of standing
 * still at the idle auto-PASS rate, or two minutes of walking. A watch shorter than that
 * proves nothing. Poke DS:6F=1 to force one in seconds. */
#define U2_GATE_COUNTDOWN_AT_LOAD 0

/* Gate indices are byte offsets 0, 2, 4, 6 into an 8-byte era row. */
#define U2_GATE_SLOTS 4

/* The era row stride in the table, in bytes: asm/2708 computes DS:1F = DS:49 * 8. */
#define U2_GATE_ERA_STRIDE 8

/*
 * The gate cell for an era and a slot index (0..3), from the table at file 0x2998 —
 * the runtime displacement %cs:0x2798 plus the 0x200 file/runtime offset, which is the
 * OPPOSITE direction to the label mapping and is what hid this table for a day.
 *
 * Returns 1 and writes x and y for a known era and slot, 0 otherwise.
 */
int u2_gate_cell(int era, int slot, int *x, int *y);

/* The cell the door is standing on right now, or 0 when there is no door at all — which is
 * the state at load, and stays the state until the first rotation. Independent of the
 * partner table: in the original all four slots stamp in every era, including the two
 * whose destination is unknown, so the door is visible even where the engine will not yet
 * take you. Verified in era 0 by forcing expiries: slots 1, 2 and 3 all stamped.
 *
 * x and y are sector coordinates, not window coordinates. */
int u2_gate_current_cell(const u2_ctx *ctx, const struct u2_overworld *ow, int *x, int *y);

/* The staging byte the renderer needs for a door cell. The fill loop computes staging as
 * (map_byte >> 1) & 0xFE and the flush halves it again to reach the art record, so a map
 * byte of 0xC0 becomes staging 0x60 and art 0x30 — the moongate record in
 * docs/tile-art-spec.md, whose index is written in hex. Derived from the engine's own
 * proven mapping rather than copied out of the game, and it renders from the player's
 * extracted tiles, so the cleanroom is not in play on the normal path. */
#define U2_GATE_STAGING 0x60

/*
 * The era a door leads to, or -1 for an era/slot the table does not cover.
 * From asm/26B7+26C3+26C6: `target = slot if slot < era else slot + 1`. The slot IS the
 * destination, expressed as a position in the sorted list of the other four eras, so gate 0 always
 * leads to the earliest other era and gate 3 always to the latest. Consequence: slot k is the
 * two-way link between era k and era k+1, and nothing else round-trips.
 *   era1 slot1 (47,28) <-> era2 slot1 (48,24)      watched live
 *   era2 slot2 (42,24) <-> era3 slot2 (33,23)      watched live
 *   era2 slot0 (16,16)  -> era0 slot0 (29,56)      derived, never watched
 *   era2 slot3 (19,52)  -> era4 slot3 (8,11)       derived, never watched
 * The destination is always the SAME SLOT in the partner era; only the era changes.
 */
int u2_gate_partner_era(int era, int slot);

/* The slot index currently active, derived from the DS:6E analogue. DS:6E steps by 2
 * and is masked to 3 bits, so the slot is DS:6E / 2. */
int u2_gate_slot(uint8_t gate_index);

/*
 * One world turn of the rotation: the countdown falls by one, and on reaching zero it
 * reloads to U2_GATE_COUNTDOWN_RELOAD and the index advances by 2 mod 8. A rotation is
 * also what puts a door on the map, so this is what sets ow->gate_stamped; before the
 * first expiry there is no door anywhere, which is how the original behaves at load.
 *
 * The unit is TURNS, and that is measured rather than assumed: 12 direction keys sent one
 * at a time with a 0.55 s gap all landed, and DS:6F fell by exactly 12 — 1.00 drops per
 * landed turn — while idling 20 s moved it only 2 (captures/spice86/turn-or-second).
 * Standing still looks like a slow clock because a turn also ends on the idle auto-PASS
 * timeout (asm/08FF), measured at about one every 8-10 s. So the recalled "every 8 turns"
 * was right throughout; an earlier note here "corrected" it to time and was itself wrong.
 *
 * One refinement from captures/spice86/g73-6f-unit: **a turn also ends on a key the game
 * rejects.** 20 arrow keys sent to a run started without --patch-arrows were every one ILLEGAL
 * COMMAND, the player never moved, and DS:6F fell exactly 20. turn-or-second could not have seen
 * that, because its method verified each key by the position change and so excludes rejected keys
 * by construction. The engine ticks on an accepted turn, so it differs for rejected keys and for
 * idle auto-PASS turns — neither of which the play API produces, since the API deliberately does
 * not fire the auto-PASS (see platform/input_api.c). Named, not plumbed.
 */
void u2_gate_turn(u2_ctx *ctx, struct u2_overworld *ow);

/*
 * If the player is standing on the active gate cell, perform the transit: the era digit changes,
 * the position becomes the same slot in the partner era, and the rotation advances as it did live
 * (countdown reloaded, index +2 mod 8).
 *
 * Returns 0 with nothing done until the first rotation has stamped a door — at load the
 * index points at slot 0 but no gate exists, and transiting there would be a door the
 * original does not have.
 *
 * Returns 1 when a transit happened. There is no prompt and no key: the original
 * consumed the player within one tick of the step onto the cell, which is the opposite
 * of a town or tower gate and is why this is called from the move path and not from
 * cmd_E.
 */
int u2_gate_transit(u2_ctx *ctx, struct u2_overworld *ow);

#endif
