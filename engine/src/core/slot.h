/*
 * slot.h — where one save game lives, and what may live inside it.
 * docs/save-scoped-worlds.md is the design; this header is its § 2 and § 3.
 *
 * The rule the whole layout turns on: content is an immutable base and every
 * mutation belongs to a save. A slot is that save's directory — the record plus
 * only the sector files that save has changed — so a sector write can be deleted
 * instead of being permanent damage to the player's only copy of the release
 * data. Read order for a sector is therefore slot first, then the content roots
 * (core/roots.h), and the write target is always the slot.
 *
 * Two things this deliberately is NOT, both of them rulings rather than
 * omissions:
 *
 *   - it is not a content root. Only `mapx`/`monx` overlay (D3); `tlkx`, `pic*`
 *     and the executable stay pure content, so the slot never joins the table in
 *     core/roots.c and never appears in the installer's manifest — a slot is
 *     player data and the manifest is provenance about what the installer wrote.
 *   - it is not created by the installer (D6). A slot that exists but holds
 *     nothing is a lie about having a game, so the engine makes one lazily, the
 *     same way the tile cache makes its directory.
 *
 * One slot exists today. `--save NAME` is deferred (D5): the layout is the work,
 * the flag is then small. u2_slot_name() is the single place that knows which
 * slot is active, and it is where that flag will land.
 *
 * The path arithmetic in slot.c bounds a save directory to 400 characters, a slot
 * name to 8 and a file name inside a slot to 16, and composes them so a slot path
 * is at most 455 bytes — every caller's buffer has to be at least that. Sector and
 * record names are all far under those limits; the bounds exist so gcc's
 * format-truncation check can prove the arithmetic rather than take it on trust.
 */

#ifndef U2_CORE_SLOT_H
#define U2_CORE_SLOT_H

#include <stddef.h>

typedef struct u2_ctx u2_ctx;

/* The directory a save goes in when nothing says otherwise. Same default as the
 * old flat layout, so an existing install's --save-dir still means the same
 * thing; what changes is that a slot directory now lives inside it. */
#define U2_SAVE_DEFAULT_DIR "saves"

/* D1: the default slot is spelled exactly like the record file inside it, so a
 * slot and its record share one name. Odd to read, and still the right ruling —
 * it matches U2_SAVE_FILE_NAME, and another slot is another name in the same
 * place rather than a rename of anything. */
#define U2_SAVE_SLOT_DEFAULT "PLAYER"

/* The active slot's name. No flag selects another one yet (D5). */
const char *u2_slot_name(const u2_ctx *ctx);

/* The save directory: --save-dir, or "saves". */
void u2_save_dir(const u2_ctx *ctx, char *buf, size_t bufsz);

/* The active slot's directory: "<save_dir>/<slot>". Never created here; use
 * u2_slot_ensure() when the question is "may I write into it". */
void u2_slot_dir(const u2_ctx *ctx, char *buf, size_t bufsz);

/* Create the slot directory if it is absent. Two plain mkdirs rather than a
 * recursive one: a slot is exactly one level below the save directory, so this
 * cannot invent a path nobody asked for, and a save directory that is a file
 * fails here instead of producing a slot in the wrong place. Returns 0 on ok. */
int u2_slot_ensure(const u2_ctx *ctx);

/* Adopt a record left behind by the layout that predates slots.
 *
 * An older build wrote the record as a FILE at `<save_dir>/PLAYER`, which is
 * exactly where the slot directory has to go, so until it moves nothing can be
 * saved at all — and in the drop-in layout (`--data-dir` at a raw release
 * directory) there is no installer to run. This does the move: rename only,
 * never an overwrite, never touching content, and it creates nothing when there
 * is nothing to adopt, so D6's rule that an empty slot is a lie still holds.
 *
 * It must run BEFORE the first save load, which is why it is separate from
 * u2_slot_ensure(): adopting after the load would leave the game holding a blank
 * state and the first save would overwrite the record it just moved. The
 * installer's migration covers the same case plus the content repair, which needs
 * _INSTALL.tsv and so cannot live here. Returns 0 on ok or nothing-to-do. */
int u2_slot_adopt_legacy(const u2_ctx *ctx);

/* "<slot>/<name>" into `out`, whether or not it exists. For callers that only
 * need the destination of a write. */
void u2_slot_path(char *buf, size_t bufsz, const u2_ctx *ctx, const char *name);

/* 1 and the full path when the slot holds `name`, else 0 and `out` is empty. */
int u2_slot_find(const u2_ctx *ctx, const char *name, char *out, size_t bufsz);

/* Rotate the active slot aside so a new character starts on a clean world.
 *
 * WHY. There is one slot (D5 defers `--save NAME`), and a slot holds the sector
 * overlay as well as the record. So the previous character's world is the next
 * character's ground: the overlay is read ahead of content, and a sector the last
 * save stamped is authored terrain for whoever loads it next. That is how a brand
 * new character was found walking past monsters that are not there.
 *
 * WHEN. Called by the creation path after every validation has passed and
 * immediately before the first save of the new character, so a refused form never
 * rotates anything and a rotation always has a character to replace it.
 *
 * WHAT COUNTS AS A GAME. A slot holding a non-empty record. An absent slot, an
 * empty directory or a zero-length record rotates to nothing, because D6's rule
 * that an empty slot is a lie means there is no game to preserve and a rotation
 * per failed launch would litter the save directory.
 *
 * THE ARCHIVE IS NOT A SLOT. The rotated directory keeps the slot's name with a
 * timestamp suffix and stays inside the save directory, which is inert today only
 * because nothing enumerates slots — `u2_slot_find` takes a name, it does not
 * scan. When D5 lands and something does scan, it must skip anything whose name
 * carries the suffix, or a rotated world becomes a selectable save that is really
 * a corpse. That is the rule this comment exists to hand on.
 *
 * Writes the archive path to `out` when it moved something. Returns 1 when it
 * rotated, 0 when there was nothing to rotate, -1 on a failure the caller must
 * say out loud rather than save over. */
int u2_slot_rotate(u2_ctx *ctx, char *out, size_t outsz);

#endif
