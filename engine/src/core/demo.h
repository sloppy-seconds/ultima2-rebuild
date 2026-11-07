#ifndef U2_CORE_DEMO_H
#define U2_CORE_DEMO_H

/*
 * demo.h — record and playback of a playable session. docs/demo-record-playback.md
 * is the design; this header is its § 4 (format), § 6 (architecture) and § 7
 * (lifecycles). Every D-number below points at a decision in that plan.
 *
 * What a demo is: a checkpoint of the whole DS mirror, plus the ordered list of
 * keys the GAME CONSUMED, plus a digest of the state each key was delivered into
 * (D1, D3, D6). Playback restores the checkpoint, re-injects the keys through the
 * real input seam, and verifies every digest. A demo that plays back wrong is a
 * failed playback and exits non-zero, not a demo that ended differently (D6).
 *
 * The unit is the delivered key, not the turn (D12). That is what lets one file
 * cross character creation, the overworld, a site and back out with no per-mode
 * hook: every mode asks the seam for its next key, so the seam is the only place
 * that has to be tapped.
 *
 * ---------------------------------------------------------------------------
 * Two things this module must never do, both of them divergence sources:
 *
 *   1. It must never write to the game screen. The per-record digest covers the
 *      text grid as well as the DS mirror, so a notice the recorder printed and
 *      the player did not would make the first record mismatch. All demo messages
 *      go to stderr, exactly like the Ctrl-S screenshot notice in seam.c.
 *   2. It must never poll, sleep or read the clock (hazard 3). The observer that
 *      measures itself perturbs what it measures; `tools/u2_record.py` shipped
 *      that defect once already. Recording records and returns. Pacing lives in
 *      the playback input backend, which is presentation and cannot reach state.
 * ---------------------------------------------------------------------------
 *
 * The digest scope is NOT `u2_state_digest_json()`, and that is a deliberate
 * departure from the plan's D6, recorded here rather than in prose somewhere it
 * would drift. The frozen CP2 scope embeds the engine version string and the
 * platform backend names (`renderer`, `input`, `clock`), so it changes on every
 * release bump and differs between the null and SDL renderers. Using it would
 * make D13 (old demos replay on a new engine) impossible by construction and
 * would make W8 (renderer independence) unprovable rather than unproven. The demo
 * scope is behavioural: the DS mirror minus the clock-derived bytes D3 excludes,
 * the roll counter, and the text grid. It is frozen at version 1 below and
 * changing it is a baseline move needing owner approval, same rule, same reason.
 */

#include <stddef.h>
#include <stdint.h>
#include "core/ctx.h"
#include "core/world.h"   /* u2_world, for u2_demo_note_world */

/* Format identity, first line of every file. An unrecognised version refuses (D8's
 * "refuse rather than guess" rule applied to the version rather than to truncation). */
#define U2_DEMO_MAGIC   "udemo"
#define U2_DEMO_VERSION 2

/* Which digest scope the `sha` values use. Bumping this is what downgrades an old
 * demo to "playback unverified" (D13), because comparing a scope-1 digest against
 * a scope-2 digest is comparing two different functions. */
#define U2_DEMO_DIGEST_VERSION 1

/* Ctrl-R. D10: the availability is a gate, not a claim — `tools/demo_key_gate.py`
 * proves no `cmp` against 0x12 exists anywhere in asm/, which is the same
 * reasoning that let --dev claim Ctrl-Q (0x11). Handled at the seam and swallowed
 * before game logic, like Ctrl-S (0x13) in seam.c. */
#define U2_KEY_DEMO_TOGGLE 0x12

/* The DS span a checkpoint carries: the whole mirror, not --dump-ds's 0x280.
 * D3: a curated field list can forget a field and this project has been bitten by
 * exactly that; the struct IS the DS working segment, so a whole-span snapshot
 * cannot. --dump-ds stays at 0x280 (hazard 1: widening it would move an existing
 * gate's fixture shape). */
#define U2_DEMO_SPAN 0x650

/* DS offsets excluded from COMPARISON (D3), with the reason in the plan's table:
 * 0x74/0x75 is the game speed, calibrated at every startup by counting busy
 * passes in a 20-tick window (asm/06AC), and measured to differ on 16 of 16 turns
 * across two identical boots; 0x550..0x553 is the saved BIOS tick pair (asm/06B0),
 * a free-running clock by definition.
 *
 * They are excluded from the COMPARE and kept in the SNAPSHOT, which is what lets
 * playback pin them (D5). Excluding them from the snapshot would lose the ability
 * to restore a mid-session speed value.
 *
 * The plan's § 4 example line reads `exclude 74 550 551 552 553` and omits 0x75,
 * which is the low half of the same uint16 as 0x74. D3's table says DS:74..75, so
 * the table is the decision and the example was a typo; 0x75 is excluded here. */
#define U2_DEMO_EXCLUDE_COUNT 6
extern const int U2_DEMO_EXCLUDE[U2_DEMO_EXCLUDE_COUNT];

/* Exit codes. Distinct because D13 needs "unverified" to be tellable apart from
 * "diverged" without parsing prose: a divergence on the same build is a bug, a
 * divergence on an older build is the answer to "did this change?". */
#define U2_DEMO_EXIT_DIVERGED   5
#define U2_DEMO_EXIT_UNVERIFIED 6

typedef enum u2_demo_state {
    U2_DEMO_IDLE = 0,
    U2_DEMO_RECORDING,
    U2_DEMO_PLAYING
} u2_demo_state;

/* Which flow the recording started in (D12's narrowing of D2). Playback resumes
 * into this mode, which is why a demo started during character creation does not
 * have to be played from the title menu. `title` is added to the plan's five
 * because Ctrl-R is honoured there too and refusing it would be a silent gap. */
typedef enum u2_demo_mode {
    U2_DEMO_MODE_TITLE = 0,
    U2_DEMO_MODE_CREATION,
    U2_DEMO_MODE_OVERWORLD,
    U2_DEMO_MODE_SITE,
    U2_DEMO_MODE_DUNGEON,
    U2_DEMO_MODE_SPACE
} u2_demo_mode;

const char *u2_demo_mode_name(u2_demo_mode m);
/* 1 when the mode's state lives in a loaded sector, i.e. when the checkpoint has
 * to wait for the world and the restamp (hazard 2). */
int u2_demo_mode_needs_world(u2_demo_mode m);

/* The behavioural digest every record carries. Writes 64 hex chars plus a NUL.
 * See the scope note at the head of this header. */
void u2_demo_digest(const u2_ctx *ctx, char out[65]);

/* ---- lifecycle ---------------------------------------------------------- */

/* Start recording. `path` NULL means derive the default name under --demo-dir
 * from host time (§ 4, Filenames: basic ISO 8601, never ctx->clock, which is
 * virtual under a scripted run — hazard 7). Nothing is written yet: the file
 * opens at the next quiescent point, because a checkpoint taken mid-command would
 * have to restore engine scratch that is not in the DS mirror (D12). */
void u2_demo_begin_recording(u2_ctx *ctx, const char *path);

/* Parse and stage a demo for playback. Returns 0 on success.
 * Refuses: an unrecognised format version, a file with no `@end` unless
 * ctx->demo_salvage, a checkpoint whose record region disagrees with `save-sha`,
 * and a `digest-version` mismatch is NOT a refusal — it downgrades the run to
 * playback unverified (D13). */
int u2_demo_begin_playback(u2_ctx *ctx, const char *path);

/* The seam tap. Called from seam_key_poll / seam_key_wait after the uppercase
 * fold and after the Ctrl-S swallow, with the key about to be handed to game
 * logic. Returns 1 when it swallowed the key (it was the toggle) and the caller
 * must not deliver it.
 *   recording — appends this key's record, holding the digest of the state at
 *               this instant, and flushes
 *   playback  — verifies the PREVIOUS record against the live state, then lets
 *               the key through
 * Divergence is reported by u2_demo_finish and ctx->exit_code, never by refusing
 * the key: a demo that stops the game mid-turn would be a second bug. */
int u2_demo_key(u2_ctx *ctx, u2_key *k);

/* Where a pending Ctrl-R may take effect, and where a staged playback mounts its
 * checkpoint. Each mode's top-level wait calls this, and the overworld loop only
 * calls it when `ow->pending_cmd` is 0, because that is what keeps a half-typed
 * command out of the format (D12). `mode` is how the header learns where the
 * recording started — one call site, one argument, no second hook.
 *
 * Mounting here rather than at startup is what makes the two directions agree: the
 * recorder's checkpoint and the player's first delivered key straddle the same point
 * in the same loop, so resuming anywhere else measures a different instant. When a
 * world-mode checkpoint lands, `ctx->demo_restamp_pending` goes up and the overworld
 * loop re-syncs its position and rebuilds the grid before it draws (hazard 2). */
void u2_demo_quiescent(u2_ctx *ctx, u2_demo_mode mode, const u2_world *world);

/* The idle auto-PASS, from the overworld loop's idle arm only.
 *
 * The plan's § 6 table says "called by u2_overworld_pass". It is not, and the
 * reason is a divergence: cmd_P reaches u2_overworld_pass too, so a pass taken
 * from a recorded key would write an extra `kind=auto` record, and playback would
 * then synthesise a second PASS on top of the one cmd_P already performed. The
 * idle arm is the only site that corresponds to the original's asm/08FF, so it is
 * the only site that gets a record.
 *
 * Returns 1 when it wrote one. */
int u2_demo_mark_auto_pass(u2_ctx *ctx);

/* Playback's replacement for the poll-count auto-PASS (D4). Returns 1 when the
 * next record is `kind=auto`: the caller performs the PASS, which is how a demo
 * reproduces a turn the player never typed. Auto-PASS is otherwise forced off for
 * the session, because the playback loop polls and each poll would otherwise add
 * up to a PASS that was never in the recording. */
int u2_demo_auto_pass_due(u2_ctx *ctx);

/* The playback input backend's whole job, behind this call.
 *   1  *out filled with the next recorded key
 *   0  nothing deliverable right now (the next record is `kind=auto`)
 *  -1  the demo is finished
 * The previous record's digest is verified when a record's first key is taken,
 * which is the instant the plan's D12 names: after the previous effect, before
 * this one. */
int u2_demo_next_key(u2_ctx *ctx, u2_key *out);

/* Flush, write the epilogue, report. Idempotent, safe on every exit path, and the
 * only writer of `@end`. This is also where the last record gets verified — there
 * is no key N+1 to trigger it (D12's first consequence) — and where the closing
 * state digest is compared. Returns 0 on a clean run, U2_DEMO_EXIT_* on a bad one. */
int u2_demo_finish(u2_ctx *ctx);

/* ---- checkpoint application --------------------------------------------- */

/* The header's sector digits, for the one thing that has to happen before the
 * checkpoint can land: `u2_overworld_init_state` chooses which sector file to open
 * from DS:49/DS:4A, so playback has to put the demo's digits there first. The
 * checkpoint itself then mounts at the loop's first quiescent point, which is the
 * same instant the recorder took it. Returns 1 and fills the digits when a playback
 * is staged and its mode lives in a sector. */
int u2_demo_preload_sector(const u2_ctx *ctx, int *sector_hi, int *sector_lo);

/* The mode a staged playback resumes into, so main.c can enter that flow instead of
 * the title menu. A demo that cannot be entered is refused at parse time. */
u2_demo_mode u2_demo_playback_mode(const u2_ctx *ctx);

/* The `overlay` lines: the sector files whose bytes reached the world.
 *
 * Called by the game layer wherever a sector is resident — at the quiescent point
 * before a recording starts, and in overworld_resume before the checkpoint is
 * applied. Recording captures the three digests into the header; playback
 * compares them and latches a failure, which the caller reads back with
 * u2_demo_overlay_failed() and refuses on, naming the file.
 *
 * This is where the plan's § 4 correction lands: refuse on bytes that reach the
 * world, note once on bytes that only reach the screen. Tile art, theme and the
 * FM Towns art are the latter and are recorded as header fields, never as a
 * refusal, because a demo that only plays for whoever owns one legal art set is
 * not shareable and sharing is the whole request.
 *
 * The digest is of the bytes as the loader had them: `map_base` (the sector
 * before anything stamped creatures into it), `mon`, and the `tlkx` read. Not the
 * live `map`, which carries stamps and would make the expectation depend on when
 * the checkpoint was taken. */
void u2_demo_note_world(u2_ctx *ctx, const u2_world *w);

/* Replace a freshly loaded sector with the bytes the recorder had, using that
 * sector's `@world` delta. Call it after the map load and BEFORE the spawn clear and
 * the restamp, so the two pipelines agree from the same starting bytes.
 *
 * It re-reads the CONTENT layer rather than patching whatever the loader chose, so a
 * viewer holding their own overlay for the sector still gets the recorder's world
 * rather than a third world that matches neither. § 7.4's isolation is the reason.
 *
 * Returns 0 when there is nothing to apply — not playing back, no line for this
 * sector, delta=none, or the caller overrode the content check — and -1 when the
 * bytes on disk are not the bytes the delta claims to patch. */
int u2_demo_apply_world(u2_ctx *ctx, u2_world *w, u2_sector_id id, int is_map);

/* Non-zero once a checked overlay has disagreed; the caller refuses. */
int u2_demo_overlay_failed(const u2_ctx *ctx);

/* ---- observation --------------------------------------------------------- */

u2_demo_state u2_demo_state_of(const u2_ctx *ctx);
/* Records written so far (recording) or parsed (playback). */
int u2_demo_record_count(const u2_ctx *ctx);
/* The first divergent record index, or -1. */
int u2_demo_divergence(const u2_ctx *ctx);
/* 1 when the staged demo's digest-version is not this build's: playback runs and
 * verifies nothing (D13). */
int u2_demo_unverifiable(const u2_ctx *ctx);

#endif /* U2_CORE_DEMO_H */
