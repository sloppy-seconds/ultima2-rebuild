/*
 * dungeon.h — CP7: the interior sector. A first-person crawl over a paged grid.
 *
 * What a "dungeon" is, and how much of that is evidence
 * ----------------------------------------------------
 * A dungeon is not a separate mode of the game. It is an ordinary sector whose
 * filename ends in 4 or 5 — 24/34/44 are towers, 15/25/35/45/85 are caves — and
 * entering one loads mapxNN into the same 4096-byte grid buffer the overworld
 * uses and sets [DS:12] to 4. docs/sector-rulesets.md § 1 and § 1b established
 * that, and established that the split is {4,5} versus {0,1,2,3}.
 *
 * What is different inside one is everything this module owns:
 *
 *   The grid is 16 pages of 16x16 cells, not 64x64. asm/68C0 forms the cell
 *   address as map_base + floor*256 + y*16 + x with the offset truncated to one
 *   byte, and [DS:25] is the floor. Verified against captures/spice86/dungeon-35-live,
 *   whose live page 0 is byte-identical to original/mapx35's first 256 bytes.
 *
 *   A cell is two layers in one byte. The high nibble is terrain, the low nibble
 *   is whatever the loader stamped there. The four terrain bits read off the
 *   renderer's own tests and the two ladder commands:
 *       0x80 rock        — asm/6144 treats bit 7 as "the corridor ends here"
 *       0x40 chest       — GET's site arm compares the cell under the player to
 *                          '@' (0x40) and prints " CHEST!" (asm/363B, runtime 0x36C4)
 *       0x20 down ladder — cmd_D's gate is [DS:14] & 0x20
 *       0x10 up ladder   — cmd_K's gate is [DS:14] & 0x10
 *       0xC0 door        — an OPEN doorway, not a locked one. Bit 7 is set so asm/6144's
 *                          rock-ahead path fires, but the stop rule then tests `& 0x40` and
 *                          `& 0x20`, and 0xC0 passes both, so the corridor is drawn THROUGH it
 *                          and the ADVANCE gate (`c == 0x80`) lets the player walk. Renders as
 *                          an opening and behaves as an opening: shape, not obstacle. Owner
 *                          observation 2026-11-09, confirmed against asm/6144's own header
 *                          ("0x80 alone is a dead end, 0xC0 continues, and 0xE0 stops").
 *       0xE0 secret door — a FAKE WALL. Bit 7 set and `& 0x20` set, so the render stop rule
 *                          ends the corridor and it draws as solid rock -- but the walk gate
 *                          compares `== 0x80` exactly, so it is WALKABLE. A wall you can walk
 *                          into is exactly what a secret door is, and it needs no key and no
 *                          OPEN: it is found by walking into it. 45 to 259 cells per dungeon
 *                          file, and in mapx25/35 fully 79% of them sit inside a wall line
 *                          with passage beyond, which is the signature of a hidden route.
 *                          Note 0x30 is not a separate code: it is 0x10|0x20, and the two
 *                          ladder gates are bit tests, so an up/down cell satisfies both with
 *                          no extra rule. ModdingWiki lists it as its own row and our bit
 *                          masks already cover it.
 *   THE TWO HALVES ARE ONE MECHANISM, and reading only one of them looks like a contradiction.
 *   The walk gate blocks only true rock; the render stop rule decides what is *drawn* as solid.
 *   That asymmetry is the whole door system. It has one live consequence worth knowing: RETREAT
 *   tests `& 0x8f`, which is wider than ADVANCE's `== 0x80`, so backing into a fake wall
 *   refuses while walking into it passes. Transcribed as the bytes have it, not unified.
 *   The shipped mapx35 uses exactly seven values, all multiples of 0x10, and only
 *   seven of the sixteen combinations a free nibble would allow. That is a small
 *   enum, not an art index, and it is why the tile renderer has never covered this.
 *   The seven are 0x00, 0x10, 0x20, 0x40, 0x80, 0xC0, 0xE0 — the count has always
 *   included the two door codes; only their names were missing from this list.
 *
 *   DOORS ARE TWO DISJOINT SYSTEMS, and conflating them is the trap here. The unlock rule
 *   `u2_unlock_rule` tests the cell for 0xA0, which is a SITE door tile: 0xA0 occurs in 16
 *   town and castle maps (1 to 29 cells) and in ZERO dungeon or tower files. The dungeon
 *   doors are 0xC0 and 0xE0, which occur in quantity in dungeon files and almost not at all
 *   in site maps. So UNLOCK is a town-and-castle mechanic and correctly never fires underground
 *   -- dungeon doors are free passage and fake walls are found by walking, so there is nothing
 *   for a key to do there. Counts and the reasoning: docs/map-format.md § 3b.
 *
 *   The view is a wireframe. asm/6000 renders, and it renders nothing at all when
 *   the light counter [DS:26] is zero — that branch is the darkness, and it is a
 *   byte, not an inference. When there is light it samples three columns of cells
 *   out to seven depths (asm/6078), picks a shape per depth from the cell bits
 *   (asm/6144), and draws each shape as one to nine straight lines out of eight
 *   depth tables in CS (asm/6291 .. asm/66CB, asm/67B3).
 *
 *   Movement is not a grid step in four directions. It is ADVANCE, RETREAT,
 *   TURN LEFT and TURN RIGHT, and the facing is a signed byte pair at [DS:02]/
 *   [DS:03], not an enum. asm/277B holds all four and they are transcribed here
 *   instruction for instruction, including the position wrap at $0x3f and the
 *   trap roll that follows a successful ADVANCE.
 *
 * What is NOT proven, and is marked at the site
 * --------------------------------------------
 *   * Which low-nibble stamp is which creature. The dungeon creature encoding is
 *     [DI+0x1B7] = slot >> 1 and a type of 1..7 (asm/147F, validated 54/54 slots
 *     by tools/dungeon_spawn_check.py), but the stamp values 0x01..0x07 written
 *     into the grid have never been joined to those types by a capture.
 *   * The sprite pass. asm/68ed walks the per-depth object array and draws from a
 *     stroke table at [DS:474] = DS:2900 — that is T9, still undecoded, and this
 *     module draws a placeholder box and says so rather than inventing outlines.
 *   * Whether `A` attacks or advances inside a site. The two captures disagree
 *     and disagree with the overworld: dungeon-35-live shows A = ADVANCE,
 *     tower-34 shows A = ATTACK--MISS, and asm/2B9B's gate compares [DS:4A] to 4
 *     in a direction that explains neither on its own. The rule implemented
 *     below is the one that reproduces both observations, and it names itself as
 *     inferred where it is used.
 */
#ifndef U2_GAME_DUNGEON_H
#define U2_GAME_DUNGEON_H

#include <stdint.h>
#include "core/ctx.h"
#include "core/world.h"
#include "game/commands.h"

typedef struct u2_overworld u2_overworld;

/* Terrain bits of a dungeon cell's high nibble. */
#define U2_DG_ROCK   0x80u
#define U2_DG_CHEST  0x40u
#define U2_DG_DOWN   0x20u
#define U2_DG_UP     0x10u
#define U2_DG_TERRAIN 0xf0u
#define U2_DG_STAMP   0x0fu

/* Dungeon door codes. Both are already handled -- by the render stop rule and the walk gate
 * together, not by any code that names them. 0xC0 draws as an opening and is walkable; 0xE0
 * draws as solid rock and is ALSO walkable, which is what makes it a fake wall. A site door is
 * a different code (0xA0) on a different map layer and is handled by u2_unlock_rule; do not
 * reuse that constant down here, and do not add an unlock test for these two. */
#define U2_DG_DOOR     0xc0u   /* corridor continues; free passage */
#define U2_DG_SECRET   0xe0u   /* corridor stops, but the walk gate passes: a fake wall */

/* asm/6078 samples eight depths; asm/6144 stops drawing at seven. */
#define U2_DG_SAMPLE_DEPTH 8
#define U2_DG_DRAW_DEPTH   7

/* The light counter's refill value, written by both a torch and the LIGHT spell. */
#define U2_DG_LIGHT_FULL 0x96u

/* asm/6078's three-column sample, exposed so the enhanced 3D renderer (dungeon3d.c)
 * reads the same topology the wireframe does rather than re-deriving it. */
typedef struct u2_dg_scan {
    uint8_t left[U2_DG_SAMPLE_DEPTH];
    uint8_t ctr[U2_DG_SAMPLE_DEPTH];
    uint8_t obj[U2_DG_SAMPLE_DEPTH];
    uint8_t right[U2_DG_SAMPLE_DEPTH];
} u2_dg_scan;

void u2_dg_scan_build(const u2_ctx *ctx, const u2_overworld *ow, u2_dg_scan *s);

/* The enhanced-mode perspective corridor (docs/enhanced-dungeon-3d.md). Flat-shaded
 * first cut: ceiling / floor / wall quads + end cap from the scan, into the enhanced
 * map window. Sprites and texture are later steps. */
void u2_dungeon_render3d(u2_ctx *ctx, u2_overworld *ow, const u2_dg_scan *s);

/* True when the resident sector is dungeon-shaped: site kind 4 (tower) or 5. */
int u2_site_is_dungeon(const u2_ctx *ctx);

/* Non-zero when the dev inspection hall is requested (U2_DG_INSPECT set). Used
 * by the overworld boot to enter the dungeon directly for art review. */
int u2_dungeon_inspect(void);

/* The facing vector at [DS:02]/[DS:03], as the signed pair the original keeps it. */
void u2_dungeon_facing(const u2_ctx *ctx, int *dx, int *dy);

/* asm/6000's three probes, written back to [DS:14]/[DS:15]/[DS:16] exactly where
 * the original writes them: the cell under, the cell ahead, the cell behind. */
void u2_dungeon_probe(u2_ctx *ctx, const u2_overworld *ow);

/* The four movement commands (asm/277B). Each prints its own word, so the caller
 * does not print anything. TURN LEFT/RIGHT take no turn; ADVANCE/RETREAT do. */
void          u2_dungeon_turn(u2_ctx *ctx, int left);
u2_cmd_result u2_dungeon_advance(u2_ctx *ctx, u2_overworld *ow);
u2_cmd_result u2_dungeon_retreat(u2_ctx *ctx, u2_overworld *ow);

/* asm/2B8F cmd_A falling into asm/2B9B: the site-kind gate does NOT refuse the
 * swing inside a tower or a dungeon, it drops into the same arms gate and roll the
 * overworld uses. What is different is that there is no direction to ask for — the
 * facing vector already names the cell — so the whole command completes on the
 * letter. See the header note on the contradiction with docs/spice86-live-findings.md
 * section 13's "A = ADVANCE" row. */
u2_cmd_result u2_dungeon_attack(u2_ctx *ctx, u2_overworld *ow);

/* GET inside a site (asm/363B's kind>=4 arm): a chest, or " WHAT?". */
u2_cmd_result u2_dungeon_get(u2_ctx *ctx, u2_overworld *ow);

/* asm/1151's tail: IT'S DARK! at zero, else decrement and TORCH BURNED OUT! when
 * a step takes the counter to zero. Returns 1 when there is light to draw with. */
int u2_dungeon_light_step(u2_ctx *ctx);

/* One dungeon creature turn: asm/1151 runtime 0x135E..0x148E.
 *
 * This is the paged-grid twin of u2_ai_creature_turn, and the two cannot share code.
 * Four differences, each of them a byte:
 *
 *   The step vector is asm/152E, not asm/4F11. asm/152E is `stc; cmc; sbb` then
 *   asm/50D8's sign, so it is a plain sign(player - creature) with NO `add al,al` pair.
 *   asm/4F11's doubling is what makes an overworld creature take the short way round a
 *   64x64 torus; a 16x16 page has edges and no wrap, so the dungeon must not copy it.
 *
 *   The grid is indexed by the creature's OWN floor, [DI+0x1B7], not by the sector grid.
 *   In a dungeon that byte is not a kind index at all: asm/147F writes `slot >> 1` into
 *   it, which over 32 slots is exactly floor 0..15, and asm/143C refuses a move unless
 *   it equals [DS:25]. Same byte, different meaning, which is the dungeon/overworld
 *   quarantine in AGENTS.md section 3 being load-bearing rather than stylistic.
 *
 *   A creature below 15 HP has its vector NEGATED: it flees. The overworld's twin of that
 *   same threshold regenerates instead (asm/2122), so the two grids disagree about what
 *   a wounded monster does.
 *
 *   The stamp is a nibble, not a cell. Moving clears the low nibble of the old cell and
 *   ORs mon_type into the new one, leaving the terrain bits alone.
 *
 * Two asymmetries are reproduced because the bytes have them, not because they are tidy:
 * a destination holding another creature ENDS the creature's turn instead of falling
 * through to the other axis (runtime 0x13EB goes to the move path, and the move path
 * rejects at 0x1430 with `jmp` to the next slot), and rock at 0x80 does try the other
 * axis. A cell byte of exactly 0xFF is also ambiguous between "wall" and "wrong floor",
 * because asm/143C signals the floor mismatch with that same literal; the shipped maps
 * cannot produce it, since every authored value is a multiple of 0x10 and a stamp is
 * 1..7, but the collision is real and named here rather than hidden.
 *
 * What a creature reaching the PLAYER does is asm/12DB_dungeon_events runtime 0x1508, and
 * it is NOT a general attack: one roll in four, and only dungeon types 2, 5 and 7 act —
 * a blown torch, a stolen food unit, and sleep. Types 1, 3, 4 and 6 reach you and do
 * nothing. See dg_contact() for the arms and for the one branch of that routine the engine
 * still understates (the failed roll falls into an armour-gated spell block that is not
 * decoded). */
typedef struct u2_dg_ai_report {
    int examined;    /* non-empty slots the sweep looked at                      */
    int moved;       /* slots that changed cell                                 */
    int fled;        /* slots whose vector was negated for being under 15 HP     */
    int off_floor;   /* slots refused by asm/143C's floor test                  */
    int bump;        /* slots that ended their turn on another creature's cell   */
    int contacts;    /* slots whose candidate step was the player's cell         */
    int effects;     /* contacts that landed one of the three asm/12DB arms      */
} u2_dg_ai_report;

u2_dg_ai_report u2_dungeon_creatures(u2_ctx *ctx, u2_overworld *ow);

/* asm/147F: fill empty creature slots with a random dungeon creature whose HP is
 * computed rather than authored. Called on entering a site and after a kill. */
int u2_dungeon_spawn(u2_ctx *ctx, u2_overworld *ow);

/* One direction key inside a site. Returns 1 when the key belonged to the
 * dungeon and has been handled — Up is ADVANCE, Down is RETREAT, Left and Right
 * are the two turns (asm/277B, and confirmed live: captures/spice86/tower-34
 * shows the arrow keys printing TURN LEFT / TURN RIGHT). Returns 0 to let the
 * overworld's own arrow arms run. */
int u2_dungeon_key(u2_ctx *ctx, u2_overworld *ow, uint8_t ch);

/* One dungeon turn's world cost: asm/1151's tail calls asm/12DB with 0x10, which
 * subtracts 10 packed-decimal food, and then the light counter steps. */
void u2_dungeon_tick(u2_ctx *ctx, u2_overworld *ow);

/* Called when a site becomes resident: north-facing, dark until a torch, and
 * populated by asm/147F rather than by monxNN. */
void u2_dungeon_enter(u2_ctx *ctx, u2_overworld *ow);

/* The wireframe. Draws nothing when `lit` is 0, which is the darkness. */
void u2_dungeon_render(u2_ctx *ctx, u2_overworld *ow, int lit);

/* Dev-only side-by-side (U2_DG_DUAL): true when the original CGA wireframe should
 * also be drawn into the enhanced panel next to the 3D view. */
int u2_dg_dual_mode(void);
void u2_dungeon_render_classic_view(u2_ctx *ctx, u2_overworld *ow,
                                    int ox, int oy, int clear_bottom);

#endif /* U2_GAME_DUNGEON_H */
