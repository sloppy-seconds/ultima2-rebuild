#ifndef U2_OVERWORLD_H
#define U2_OVERWORLD_H

/*
 * Overworld — the CP4 screen: a sector, a position, and the map window.
 *
 * This module is the first consumer of all four CP4 pieces together: the sector
 * loader (core/world), the tile cache (core/tiles), the viewport mapping
 * (core/view) and the framebuffer (core/frame). Nothing here knows what a
 * terminal or an SDL window is; it produces a u2_frame and a backend presents
 * it, which is what lets the same code drive a file dump, a window, or a test.
 */

#include "core/ctx.h"
#include "core/frame.h"
#include "core/tiles.h"
#include "core/tiles_ega.h"
#include "core/view.h"
#include "core/world.h"

/* The probe start state. These are the values read out of the live data-segment
 * capture (captures/spice86/live-ds-4f4d.bin: sector digits 3 and 0, position
 * x=33 y=23), so the dump shows a real sector the original was actually
 * standing in. They are a probe's starting point, NOT an assertion about where
 * a new game begins — that is CP5+ territory and comes from the player record. */
#define U2_OVERWORLD_PROBE_SECTOR_HI 3
#define U2_OVERWORLD_PROBE_SECTOR_LO 0
#define U2_OVERWORLD_PROBE_X 33
#define U2_OVERWORLD_PROBE_Y 23

/* Art record the original draws for the player, derived from the live DS
 * capture: the staging byte at the player's cell is 0x78, and 0x78 >> 1 == 60,
 * a humanoid figure. The player is stamped in after the terrain fill, which is
 * why that cell holds a value no map byte can produce. */
#define U2_OVERWORLD_PLAYER_ART 60

typedef struct u2_overworld {
    u2_world world;
    u2_tiles tiles;
    /* The EGA atlas, loaded only when --ega is set and the files are present.
     * `use_ega` is the resolved decision: 1 means draw the map window from
     * tiles_ega in 16 colours, 0 means the CGA `tiles` above. It is 0 whenever
     * art_mode is CGA, and also 0 when EGA was asked for but the art was absent
     * (the fallback is silent in state, announced once at load). */
    u2_tiles_ega tiles_ega;
    int          use_ega;
    char         ega_theme[32];   /* the theme name currently loaded, so Ctrl-T
                                   * knows where in the alphabetical cycle it is */
    u2_view  view;
    u2_frame frame;
    int      x, y;                 /* position inside the sector, DS:00/01 */
    /* A command that printed a prompt and is waiting for a direction key several
     * turns later: 'A' (ATTACK--) or 'U' (UNLOCK DIRECTION-), 0 when nothing is
     * pending. It lives here rather than as a local in u2_overworld_loop so the
     * handlers in commands.c can set it -- the original consumes the direction inside
     * the handler chain, so the state is the command's, not the loop's. */
    uint8_t  pending_cmd;
    uint8_t  view_mode;            /* the DS:12 analogue: 0xFF in-world, 4 in a site */
    /* Set when a sector switch could not rebuild the world — a missing map file, or a
     * demo whose recorded delta refused. Both mean the grid on screen is the PREVIOUS
     * sector's while the position, the mode byte and the creature table are the new one's,
     * which is a state no player should be left standing in. `site_reload` already wrote
     * the reason into `note`; this is the flag that says somebody has to act on it. Before
     * this existed the only callers were `(void)`, so a playback carried on from a
     * half-loaded sector and the owner's report of it was "it died after coming back from
     * the town" with nothing on screen to say why. */
    int      reload_failed;
    int      sector_hi, sector_lo;
    /* Stands in for "a gate is actually standing on the map". In the original that is
     * literally true — the door is a 0xC0 stamped into the resident sector — and the
     * engine deliberately does not stamp (gates.h gives the two reasons). This is the
     * proxy for the part of that fact that changes behaviour: at load NOTHING is stamped,
     * and the first door appears only when the rotation first fires. Measured 2026-10-29
     * in era 0: DS:6E=0 at load with no 0xC0 anywhere in the grid, and the gate appeared
     * only after a forced expiry. Without this the engine would offer a door at slot 0
     * from turn one, which the original does not. docs/moongates.md § 2. */
    int      gate_stamped;
    /* Turn counter for --debug cli. `turns` advances once per resolved player
     * turn (it is bumped inside creature_turn, the one place every turn passes);
     * `logged_turn` remembers which turn was last written, so the dump happens
     * once per turn from the draw path and not once per frame at 60 Hz. */
    unsigned turns;
    unsigned logged_turn;
    char     note[160];            /* what happened on load, for the caller */
} u2_overworld;

/*
 * Load the tiles and a sector, then composite one frame.
 *
 * Returns 0 on success. On failure `ow->note` explains why in terms the player
 * can act on — a missing content directory and a corrupt tile cache need
 * different advice, and the engine must not draw an invented world either way.
 *
 * The status band and the CMD prompt are printed into the vram grid here, once,
 * exactly as the original prints them once on entering the world: the status
 * block through the proven status sites, then "CMD: " (asm/087D) at row 23
 * col 0, leaving the cursor at col 5 where the next echo lands.
 */
int u2_overworld_init(u2_ctx *ctx, u2_overworld *ow);

/*
 * Spawn from the loaded player record instead of the probe: sector digits
 * [DS:49][DS:4A] and entry pair [DS:5A]/[DS:5B], the 'P' contract proven at
 * file 0x09E6..0x0A7F. Call after u2_save_load succeeded.
 */
int u2_overworld_init_state(u2_ctx *ctx, u2_overworld *ow);

/* The present/poll/move loop for an already-opened overworld. Returns
 * non-zero when the display asked to quit. */
int u2_overworld_loop(u2_ctx *ctx, u2_overworld *ow);

/*
 * Save-gate policy — the single decision point for Q's location checks.
 *
 * The faithful defaults reproduce the original's three gates (asm/3DC7 with
 * the guards asm/3DE4/asm/3DFA): save only outdoors (DS:4A == 0), only on
 * Earth (DS:6D == 0), only on foot (DS:13 >= 0x78). A common community patch
 * lets the player save anywhere; CP11's enhanced rules surface will override
 * this struct rather than edit the loop, so the loop must consult only the
 * policy and never the raw gates.
 */
typedef struct u2_save_policy {
    int allow_indoors;    /* skip the outdoors gate (DS:4A) */
    int allow_off_earth;  /* skip the Earth gate (DS:6D)   */
    int allow_in_vehicle; /* skip the on-foot gate (DS:13) */
} u2_save_policy;

extern const u2_save_policy U2_SAVE_POLICY_ORIGINAL;

/* Enhanced mode: every guard dropped, so the player saves anywhere. Selected by fidelity at
 * the call site; u2_overworld_save() itself never looks at the mode. */
extern const u2_save_policy U2_SAVE_POLICY_ENHANCED;

/*
 * The Q handler. Echoes the prompt and any gate refusal into the CMD window,
 * then performs the save the original performs at file 0x24FD: entry pair
 * [DS:5A]/[DS:5B] := live position, write the sector back, write the player
 * record. Returns 0 when saved, 1 when a gate refused (the refusal message
 * is already echoed). The monsters write (256 B from DS:137) is not lifted:
 * the engine does not load monsters until CP5.
 */
int u2_overworld_save(u2_ctx *ctx, u2_overworld *ow, const u2_save_policy *pol);

/*
 * The save the original performs on its own initiative, with no prompt echoed and no
 * CMD-window message. Two triggers are proven and they are not gated like Q:
 *
 *   entering a site   asm/341E, asm/343F (-TOWN, -TOWER), asm/34B5/asm/34CF (-CASTLE)
 *                     and asm/34E4 (-DUNGEON) all record the return cell and then call
 *                     the save body. Callers must invoke this BEFORE switching the
 *                     sector digit, because the original's file records the sector the
 *                     player came FROM — that is what makes dying in a town put you
 *                     back outside.
 *   rocket launch     asm/38B0, behind [DS:6D]==0 AND [DS:4A]==0. That guard belongs to
 *                     the caller, not here: this function performs the writes and
 *                     nothing else, so a caller that calls it unconditionally is
 *                     claiming a trigger the bytes do not show.
 *
 * Both are silent in the original, and this stays silent to match. Returns 0 on a
 * successful write, non-zero if the record could not be stored.
 */
int u2_overworld_autosave(u2_ctx *ctx, u2_overworld *ow);

/* Recomposite the frame from the current position: map window (pixel rows
 * 0..159) then the text rows 20..24 of the vram grid — status block, CMD
 * prompt and message history, one composited framebuffer. */
void u2_overworld_draw(u2_ctx *ctx, u2_overworld *ow);

/*
 * Render one frame of the probe sector and write it to `path` as PPM. This is
 * the zero-dependency way to see the real extracted tiles, and it is also what
 * the screenshot test drives, since a headless CI has no display for SDL.
 * Returns 0 on success.
 */
int u2_overworld_dump(u2_ctx *ctx, const char *path);

/*
 * Interactive play: draw, present, read a key, move. Returns 0 on a clean exit.
 *
 * Movement is in-sector only, and that limit is deliberate rather than an
 * oversight. Crossing a sector edge is handled by asm/242D / asm/2434 / asm/243E,
 * and what those routines set on reload — in particular which sector digit moves
 * and in which direction — is not yet established by the asm. Walking the player
 * into a neighbouring sector on a guess would produce a map that is subtly wrong
 * in a way that looks like a rendering bug, so the edge is reported and the step
 * refused until the trace is done.
 */
int u2_overworld_run(u2_ctx *ctx, u2_overworld *ow);

/*
 * The tile -> site table from asm/33C3..33FF, exposed so it can be tested against
 * real sector files. Returns the site digit for [DS:4A] (0 = not a gate), writes
 * the announcement string to *label (NULL for the village, which has none) and the
 * arrival cell to ax and ay.
 *
 * The gate is tile >> 2 because cmd_E dispatches on [DS:14] >> 1 and DS:14 is
 * map_byte >> 1 (CP5 ticket C7).
 */
int u2_site_for_tile(uint8_t tile, const char **label, int *ax, int *ay);

/* ---- travel modes (DS:13) -------------------------------------------------
 *
 * DS:13 is one byte doing two jobs: it is the sprite stamped into the player's map
 * cell, and it is the travel mode that asm/253B_food_mode_dispatch branches on. The
 * four vehicle values are the vehicle map byte shifted right once, which is the same
 * quantity as [DS:14] and the same one the passability ladder tests.
 *
 * On foot the byte is NOT a constant: asm/080D computes it as `mov 0x47,%al ; add
 * %al,%al ; clc ; adc $0x78,%al`, i.e. 0x78 + 2*pclass, so the four classes sit at
 * 0x78/0x7A/0x7C/0x7E. Every capture has read 0x78 because every capture used a
 * Fighter, and the mounted test everywhere in the binary is `cmp $0x78,%al` with the
 * mounted arm being the `jb` side — so anything below 0x78 is a vehicle and anything
 * at or above it is a class. That test is what the engine uses; the class-dependent
 * value itself is recorded and NOT applied, because no caller of asm/080D has been
 * found and where it runs is therefore unproven.
 */
#define U2_MODE_HORSE   0x22u   /* asm/2FD6, tile 0x44 */
#define U2_MODE_SHIP    0x24u   /* asm/3038 (the FRIGATE arm), tile 0x48 */
#define U2_MODE_PLANE   0x26u   /* asm/3048, tile 0x4C */
#define U2_MODE_ROCKET  0x28u   /* asm/3048 tail, tile 0x50 */
#define U2_MODE_FOOT_MIN 0x78u   /* `cmp $0x78,%al ; jb` = mounted (asm/33A6, asm/44D4) */

/* The four vehicle map bytes. [DS:14] = map_byte >> 1, so each mode value doubled is
 * the tile that produces it — corroborated three ways in asm/2FAD's header. */
#define U2_TILE_HORSE   0x44u
#define U2_TILE_SHIP    0x48u
#define U2_TILE_PLANE   0x4Cu
#define U2_TILE_ROCKET  0x50u

/* Which tile a boarding leaves behind. asm/2FD6, asm/3048 and asm/3048's rocket tail
 * each write 0x08 (grass) through the cell pointer at DS:06; asm/3038 writes 0x00
 * (water). The live sample is captures/spice86/ship-port-boniface: [DS:14] read 0x24
 * standing on the ship tile and 0x00 immediately after BOARD, with the position
 * unchanged. So boarding consumes the vehicle tile — it does not move the player.
 */
#define U2_TILE_AFTER_BOARD_LAND  0x08u
#define U2_TILE_AFTER_BOARD_WATER 0x00u

/* The items that gate boarding and launching, as indices into state.items_held
 * (DS:0xD6..DS:E5, 16 slots). The gate bytes are DS:DD, DS:DF, DS:E1, DS:E2 and DS:E5,
 * which are slots 7, 9, 11, 12 and 15 of that array, and the slot names come from
 * docs/spice86-live-findings.md § 11 — ANKH, SKULL KEY, BRASS BUTTON, BLUE TASSLE and
 * TRI-LITHIUM. That section is the source; what the asm adds is only that these five
 * bytes are the ones the vehicle code tests. The refusal messages agree with it — "YOU
 * MUST HAVE AN ANKH!" behind DS:DD, "STRANGE YOU CAN'T GET IN!" behind DS:DF, "MISSING A
 * BRASS BUTTON!" behind DS:E1, "THE CREW OF THIS SHIP WILL NOT LET YOU BOARD!" behind
 * DS:E2, "SHIP INCAPABLE OF LAUNCH!" behind DS:E5 — which corroborates the pairing rather
 * than establishing it. Sites: asm/3048 (DS:DF, DS:DD), asm/2FF6 (DS:E2),
 * asm/38B0 (DS:E1, DS:E5).
 *
 * The horse has no gate: asm/2FD6 sets the mode unconditionally.
 */
#define U2_ITEM_ANKH         7u   /* DS:DD — gates ROCKET boarding   (asm/3048) */
#define U2_ITEM_SKULL_KEY    9u   /* DS:DF — gates PLANE boarding    (asm/3048) */
#define U2_ITEM_BRASS_BUTTON 11u  /* DS:E1 — gates PLANE launch      (asm/38B0) */
#define U2_ITEM_BLUE_TASSLE  12u  /* DS:E2 — gates SHIP boarding     (asm/2FF6) */
#define U2_ITEM_TRI_LITHIUM  15u  /* DS:E5 — gates ROCKET launch     (asm/38B0) */

/*
 * The passability verdict for a travel mode — asm/2423's mode dispatch at
 * asm/253B, which is the first thing the routine does after storing its AL argument
 * at DS:29. `stamp` is DS:13; anything that is not one of the four vehicle modes
 * takes the on-foot ladder.
 *
 * Returns 1 to accept the step and 0 to reject it. Charges no food: the charge is a
 * separate call (u2_overworld_step_food), because in the original the charge sits
 * inside the same routine but ahead of the verdict, and the engine's walk loop needs
 * to charge on refused steps too.
 */
int u2_mode_passable(const u2_world *w, int x, int y, uint8_t stamp);

/*
 * The per-step food charge for the mode the player is in, and the death check that
 * goes with it. asm/2599 subtracts 19 once on foot; asm/2626 (horse and plane) subtracts
 * 19 and then calls the foot tail, which subtracts 19 again — two charges, not one of 38,
 * and the engine does two for the same reason asm/2626's own header gives: the first
 * underflow goes to the starvation tail and the second never runs. The ship arm
 * (asm/2619) and the rocket arm (asm/2542) reach no drain at all.
 *
 * Returns 1 when a subtract underflowed, having already run u2_death.
 */
int u2_overworld_step_food(u2_ctx *ctx);

/*
 * Spend `bcd_sub` food from the packed-decimal triple DS:53/54/55 and return 1
 * if the subtraction underflowed, which is the original's death condition
 * (file 0x0B2E jumps to runtime 0x0E62 on a borrow). On that borrow it runs
 * u2_death, because the borrow-out is a jump into the death tail, not a flag.
 * `bcd_sub` is the raw immediate the original hands `sbb` — 0x0A for a PASS, 0x19
 * for a step — not a decimal count.
 * Exposed for tests; the PASS and move arms of the loop are its in-game callers.
 */
int u2_overworld_spend_food(u2_ctx *ctx, unsigned bcd_sub);

/*
 * One PASS: echo the word, charge the 10 food, advance the creatures. Shared by the
 * walk loop's space arm (asm/0936's `cmp $0x20`), by cmd_P in commands.c (asm/3DBC,
 * a jump-table letter) and by the idle auto-PASS (asm/08FF) — three sites in the
 * original, only one of which is a command letter.
 */
void u2_overworld_pass(u2_ctx *ctx, u2_overworld *ow);

/*
 * The ATTACK once its direction is known: print `word` on the current line, then
 * the arms gate, the roll, and --HIT/--MISS. Public because inside a site the
 * direction is the facing vector and arrives with the letter rather than turns
 * later, so the dungeon's ATTACK has no pending_cmd stage to wait in.
 */
void u2_overworld_attack_dir(u2_ctx *ctx, u2_overworld *ow, const char *word,
                             int dx, int dy);

/* asm/2C2D -> asm/2C38: Minax's teleport on a surviving hit. Swaps mon_x/mon_y of the
 * given slot, sets mon_role to 0x84, restamps the grid, and prints "SHE'S GONE!!!".
 * Called from the melee and cannon survive paths when the hit creature is type 0x40.
 * Proven live 2026-11-12 (sector 03: one hit moved slot 31 from (60,3) to (3,60)). */
void u2_minax_teleport(u2_ctx *ctx, u2_world *w, int slot);

/*
 * asm/3389 cmd_E: enter the site on the cell the player stands on, or refuse with
 * " WHAT?" / ONLY ON FOOT!. Owned here rather than in commands.c because it needs the
 * sector store and the tile table, which are this file's; cmd_P is the same arrangement.
 */
void u2_overworld_enter(u2_ctx *ctx, u2_overworld *ow);

/*
 * Leave the site we are inside and go back to the sector we entered from: restore the
 * entry cell, zero the site kind, and re-read the sector. asm/242D/2434/243E reach this
 * from the sector-edge test, and asm/3823 reaches it from the far end of KLIMB when the
 * floor counter underflows past zero. Exported for cmd_D and cmd_K, which are the other
 * two routes out of a site and which the original routes through the same block.
 */
void u2_overworld_site_leave(u2_ctx *ctx, u2_overworld *ow);


/*
 * The death tail: asm/0C62..0C99 as one span. Zeroes DS:51..DS:59, repaints the
 * status band, scrolls the log and prints `<NAME> IS DEAD!` with no `CMD: `
 * prompt after it. Every kill site in the binary jumps here — the food
 * borrow-outs, the force field, Minax, dungeon events and cmd_L — so anything
 * new that can kill calls this rather than zeroing HP on its own.
 */
void u2_death(u2_ctx *ctx);

/* The death handoff: `(PRESS ANY KEY)`, wait for one key. Owner ruling 2026-11-11,
 * because the original's death tail never returns and the engine was simply ending.
 * It lives at the end of u2_overworld_loop so every entry path gets it; the flag stays
 * set afterwards as the record of how the session ended, and the callers reload the
 * last save on it (title.c's 'P' loop, game.c's enhanced-mode entry). */
void u2_death_handoff(u2_ctx *ctx);

/* Bitmask over tile ids 0..63 that the engine stepped onto without a measured
 * passability verdict. Zero means every tile the player entered had evidence
 * behind it; non-zero names exactly which ids still need a live sample.
 * Tests assert on this so a permissive default can never pass unnoticed. */
uint64_t u2_overworld_unmeasured_tiles(void);

/* Passability verdict for a raw map byte, exposed for tests only. See the comment
 * at its definition: the gate rule regressed once precisely because nothing could
 * assert on it. */
int u2_tile_walkable_byte(uint8_t map_byte);

/* Public test surface for the sector-edge rule, same reasoning as
 * u2_tile_walkable_byte above: the rule lives in the walk loop and was argued in a
 * comment for as long as it was wrong. Says whether a step to the UNMASKED
 * (raw_x, raw_y) leaves the sector. It leaves only from a site (second digit non-zero)
 * and on any edge, not just the high one -- measured east, west and south 2026-10-28.
 * See the comment on site_leave() in overworld.c. */
int u2_site_edge_crossed(int sector_second, int raw_x, int raw_y);

/* The UNLOCK rule (asm/4309..435E), split out so a test can drive every branch.
 * Returns one of the verdicts below. */
#define U2_UNLOCK_NO_AIM   0   /* no sideways aim: asm/4309 falls through to NO DOOR */
#define U2_UNLOCK_NO_DOOR  1   /* target cell is not map byte 0xA0 */
#define U2_UNLOCK_NO_KEYS  2   /* door present, DS:65 empty */
#define U2_UNLOCK_OPENED   3   /* key spent, cell paved */
int u2_unlock_rule(int dx, uint8_t target_tile, int keys);
/* The byte written over the door: the ground under the player, low bit cleared. */
uint8_t u2_unlock_replacement(uint8_t ground_tile);
/* One packed-decimal key spent (asm/435E's stc/mov/cmc/sbb/das/cmc). */
uint8_t u2_unlock_spend_key(uint8_t keys);

/* asm/44D4 (X-IT, the dismount) — the guard chain, in the order the bytes test it.
 * `mode_a` is [DS:14], the tile under the player shifted right one. */
#define U2_DISMOUNT_ON_FOOT  0   /* [DS:13] >= 0x78: nothing to destroy -> " WHAT?" */
#define U2_DISMOUNT_NOT_HERE 1   /* neither grass nor water-above-a-ship -> "-NOT HERE!" */
#define U2_DISMOUNT_OK       2
int u2_dismount_rule(uint8_t stamp, uint8_t mode_a);
/* The tile X-IT puts back under the player: 2*[DS:13], the exact inverse of BOARD's shift. */
uint8_t u2_dismount_tile(uint8_t stamp);
/* The on-foot stamp X-IT restores: 0x78 + 2*pclass. Class-dependent, not the constant 0x78
 * every capture shows, because every capture so far was a fighter. Same expression as
 * asm/080D; see asm/44D4_cmd_X.asm and docs/needs-gameplay.md G41. */
uint8_t u2_foot_stamp(uint8_t pclass);

/* Make the sector named by ow->sector_hi / ow->sector_lo resident: map layer, creature
 * layer, spawn and restamp, tlkx, the DS:12 view-mode derivation from the sector's ones
 * digit, and the demo delta. Returns -1 with ow->note naming what failed.
 *
 * Public because the rocket dive is a third caller (commands.c, the asm/7030 dive arm):
 * changing the sector digits without reloading leaves ow->world holding the LAUNCH
 * sector, so the descent draws the wrong map and u2_space_dive_survives tests a tile
 * from the wrong world. Defined in overworld.c beside its two existing callers, which
 * are the site entry and exit paths. */
int site_reload(u2_ctx *ctx, u2_overworld *ow);

#endif /* U2_OVERWORLD_H */
