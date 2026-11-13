/*
 * dungeon_assets.h — procedurally generated art for the enhanced 3D dungeon.
 *
 * FEATURE, not fidelity: nothing here is byte-proven and none of it may be cited
 * as evidence about original/ultimaii.exe. The art is OURS by construction —
 * generated at startup from intent ("stone wall, brick courses, mortar"), never
 * measured from the originals. The generator must never read the forbidden list
 * in docs/tile-art-spec.md § 2; that is the cleanroom wall, and it is what makes
 * this Apache-2.0-shippable where an FM Towns rip would not be.
 *
 * Owner ruling 2026-11-14: the FM Towns dungeon-art route is retired (preserved
 * on branch archive/fmtowns-dungeon-route); the engine draws its own art. This
 * is round one: procedural placeholders good enough to explore by. A later round
 * may replace the generators or take commissioned art (ComfyUI or similar) —
 * the renderer only needs 32x32 index arrays with the same IDs, so the swap is
 * contained here.
 *
 * Textures are palette indices (0..15, the EGA set), same convention as
 * u2_frame: the frame carries indices, not RGB, and the backend owns the
 * palette. Sprites use U2_DGA_TRANSPARENT for "draw nothing here".
 *
 * Every texture ships pre-shaded at U2_DGA_SHADE_LEVELS levels (level 0 = full
 * torchlight, higher = darker) so distance falloff is an index swap, not a
 * per-pixel multiply in the draw loop.
 */
#ifndef U2_GAME_DUNGEON_ASSETS_H
#define U2_GAME_DUNGEON_ASSETS_H

#include <stdint.h>

#define U2_DGA_TEX_EDGE   32
#define U2_DGA_TEX_PX     (U2_DGA_TEX_EDGE * U2_DGA_TEX_EDGE)
#define U2_DGA_SHADE_LEVELS 4
#define U2_DGA_TRANSPARENT  0xFFu

/* Wall / floor / ceiling texture ids. Floor ids are picked per cell by the
 * renderer: ladder and door cells are FLOOR variants because all three are
 * walkable ground features, not obstacles. */
typedef enum {
    U2_DGA_WALL_STONE = 0,   /* brick courses with mortar — rock and fake wall */
    U2_DGA_FLOOR_FLAG,       /* flagstone */
    U2_DGA_FLOOR_DOOR,       /* plank threshold under a 0xC0 doorway */
    U2_DGA_FLOOR_LADDER_UP,  /* flagstone + rungs */
    U2_DGA_FLOOR_LADDER_DOWN,/* flagstone + dark shaft */
    U2_DGA_CEIL_CAVE,        /* dark rocky ceiling */
    U2_DGA_TEX_COUNT
} u2_dga_tex_id;

/* Sprite ids. The chest is terrain 0x40. The seven monster slots are keyed by
 * the grid stamp nibble 1..7 — which stamp is WHICH creature has never been
 * joined by a capture (dungeon.h header note), so these are deliberately
 * generic types, not named monsters. Names come from the bestiary only when a
 * capture joins them; the shapes are invented here either way. */
typedef enum {
    U2_DGA_SP_CHEST = 0,
    U2_DGA_SP_MON1 = 1, U2_DGA_SP_MON2, U2_DGA_SP_MON3, U2_DGA_SP_MON4,
    U2_DGA_SP_MON5, U2_DGA_SP_MON6, U2_DGA_SP_MON7,
    U2_DGA_SP_COUNT
} u2_dga_sprite_id;

/* Generate everything. Idempotent; call before the getters. Deterministic:
 * fixed seed, integer math, identical output on every platform. */
void u2_dga_init(void);

/* 32*32 palette indices (or 0xFF-transparent for sprites). level is the
 * torchlight shade band, clamped to U2_DGA_SHADE_LEVELS-1. */
const uint8_t *u2_dga_tex(int id, int level);
const uint8_t *u2_dga_sprite(int id, int level);

/* Shade band for a distance in cells. Design choice, not a decoded value: the
 * torch is drawn as fully lit to ~2.5 cells and fades to the darkest band
 * past 6. */
int u2_dga_shade(float dist_cells);

#endif /* U2_GAME_DUNGEON_ASSETS_H */
