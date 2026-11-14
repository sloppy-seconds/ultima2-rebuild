/*
 * dungeon_assets.c — procedural 32x32 textures and sprites for the 3D dungeon.
 *
 * Cleanroom note (docs/tile-art-spec.md § 2, owner ruling 2026-11-14): this
 * file's author must not have the originals in view. Every shape here is
 * described by intent in a comment and produced by the code under it; nothing
 * was copied, traced, or measured from original/, references/, bestiary/cache/,
 * screenshots, or the FM Towns banks. The FM Towns dungeon route lives on
 * branch archive/fmtowns-dungeon-route and is not an input to this file.
 *
 * Determinism is a test requirement: fixed seed, xorshift32, integer math.
 * Two runs must produce byte-identical tables (dungeon3d_check asserts it).
 */
#include "game/dungeon_assets.h"

#include <stddef.h>

#include "game/dungeon_sprites_gen.h"

/* ---------------------------------------------------------------- rng ---- */
static uint32_t xs_state = 0x5EEDD15Bu;
static uint32_t rnd_u32(void)
{
    xs_state ^= xs_state << 13;
    xs_state ^= xs_state >> 17;
    xs_state ^= xs_state << 5;
    return xs_state;
}
static unsigned rnd_mod(unsigned m) { return rnd_u32() % m; }

/* ------------------------------------------------------------- shading --- */
/* One darkening step over the EGA 16 set: each index maps to the next darker
 * stop on its ramp (white->lt grey->dk grey->black; yellow->brown->black;
 * light variants->their dark mates). Applied N times for shade level N. */
static const uint8_t k_shade[16] = {
    0,  /* 0 black   */ 1,  /* 1 blue    */ 2,  /* 2 green   */ 3,  /* 3 cyan    */
    4,  /* 4 red     */ 5,  /* 5 magenta */ 0,  /* 6 brown   */ 8,  /* 7 lt grey */
    0,  /* 8 dk grey */ 1,  /* 9 lt blue */ 2,  /* 10 lt grn */ 3,  /* 11 lt cyn */
    4,  /* 12 lt red */ 5,  /* 13 lt mag */ 6,  /* 14 yellow */ 7   /* 15 white  */
};

/* ------------------------------------------------------------- storage --- */
static uint8_t s_tex[U2_DGA_TEX_COUNT][U2_DGA_SHADE_LEVELS][U2_DGA_TEX_PX];
static uint8_t s_spr[U2_DGA_SP_COUNT][U2_DGA_SHADE_LEVELS][U2_DGA_TEX_PX];
static int s_ready;
/* High-resolution (concept-derived) sprites, expanded from the generated header
 * at init. s_spr_edge[id] is 32 for the procedural art and DG_SPR_EDGE for a
 * sprite that has concept art loaded; the renderer samples at that edge. */
static uint8_t s_hi[U2_DGA_SP_COUNT][U2_DGA_SHADE_LEVELS][DG_SPR_EDGE * DG_SPR_EDGE];
static int s_spr_edge[U2_DGA_SP_COUNT];
static int s_pal_ready;

static void put(uint8_t *t, int x, int y, uint8_t c)
{
    if ((unsigned)x < U2_DGA_TEX_EDGE && (unsigned)y < U2_DGA_TEX_EDGE)
        t[y * U2_DGA_TEX_EDGE + x] = c;
}

/* ------------------------------------------------------------ textures -- */

/* Stone wall: brick courses 8 px tall, alternate courses offset half a brick,
 * mortar in the darker grey, a highlight line under each top edge and a shadow
 * line above each bottom edge. Base stone is light grey with sparse speckles. */
static void gen_wall_stone(uint8_t *t)
{
    for (int y = 0; y < 32; y++) {
        const int row = y / 8;
        const int off = (row & 1) ? 16 : 0;
        for (int x = 0; x < 32; x++) {
            uint8_t c = 7;
            const unsigned r = rnd_mod(48);
            if (r == 0) c = 8;
            else if (r == 1) c = 15;
            const int bx = (x + off) % 32;
            if (y % 8 == 0 || bx % 16 == 0) c = 8;            /* mortar       */
            else if (y % 8 == 1 && (rnd_mod(6) == 0)) c = 15; /* top highlight*/
            else if (y % 8 == 7) c = 8;                       /* base shadow  */
            put(t, x, y, c);
        }
    }
}

static void gen_wall_door(uint8_t *t)
{
    gen_wall_stone(t);
    const int jl0 = 7, jl1 = 8;     /* left jamb  */
    const int jr0 = 23, jr1 = 24;    /* right jamb */
    const int top = 5;               /* lintel top row */
    for (int y = top; y < 32; y++)
        for (int x = jl0; x <= jr1; x++) {
            uint8_t c = U2_DGA_TRANSPARENT;
            if ((x <= jl1 || x >= jr0) || y <= top + 1) c = 6;   /* wood frame */
            put(t, x, y, c);
        }
    /* dark outline on the frame's outer edge so it reads against the stone */
    for (int y = top; y < 32; y++) { put(t, jl0 - 1, y, 0); put(t, jr1 + 1, y, 0); }
    for (int x = jl0 - 1; x <= jr1 + 1; x++) put(t, x, top - 1, 0);
}

/* Flagstone floor: 16 px slabs, dark seams, light-grey faces with sparse
 * dark and pale chips. Light base on purpose: the shade levels darken it, so
 * a dark base would crush to black one band out. */
static void gen_floor_flag(uint8_t *t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t c = 7;
            if (rnd_mod(10) == 0) c = 8;
            if (rnd_mod(48) == 0) c = 15;
            if (x % 16 == 0 || y % 16 == 0) c = 0;
            put(t, x, y, c);
        }
}

/* Plank threshold for a doorway: vertical boards, dark gaps, sparse grain. */
static void gen_floor_door(uint8_t *t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t c = 6;
            if (rnd_mod(7) == 0) c = 4;
            if (x % 8 == 0) c = 0;
            if (x % 8 == 1 && rnd_mod(3) == 0) c = 14;
            put(t, x, y, c);
        }
}

/* Down-ladder floor: flagstone with a black square hole in the centre, the
 * shaft you descend through. The hole is centred so it lands under the ladder
 * billboard, which the renderer anchors to the same cell centre. */
static void gen_floor_hole(uint8_t *t)
{
    gen_floor_flag(t);
    for (int y = 10; y <= 21; y++)
        for (int x = 10; x <= 21; x++)
            put(t, x, y, 0);                               /* black shaft */
    for (int x = 10; x <= 21; x++) put(t, x, 22, 7);       /* lit near rim */
}

/* Cavern ceiling: rocky grey, darker than the floor, with dark pits and a
 * rare pale chip. Light-grey base for the same shading reason as the floor. */
static void gen_ceil_cave(uint8_t *t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t c = 7;
            const unsigned r = rnd_mod(12);
            if (r == 0) c = 8;
            else if (r == 1) c = 0;
            put(t, x, y, c);
        }
}

/* Up-ladder ceiling: the cave ceiling with a black square hole in the centre,
 * the shaft you climb out through. Centred to line up with the ladder sprite. */
static void gen_ceil_hole(uint8_t *t)
{
    gen_ceil_cave(t);
    for (int y = 10; y <= 21; y++)
        for (int x = 10; x <= 21; x++)
            put(t, x, y, 0);                               /* black shaft */
}

/* ------------------------------------------------------------- sprites --- */

/* Treasure chest: a banded wooden coffer on the ground line. */
static void gen_spr_chest(uint8_t *t)
{
    for (int y = 12; y <= 27; y++)
        for (int x = 5; x <= 26; x++) {
            uint8_t c = 6;
            if (y == 12 || y == 27 || x == 5 || x == 26) c = 0;   /* outline */
            else if (y == 16) c = 8;                              /* lid band*/
            else if (x == 10 || x == 21) c = 8;                   /* straps  */
            else if (x >= 14 && x <= 17 && y >= 17 && y <= 20) c = 15; /* lock*/
            put(t, x, y, c);
        }
    for (int x = 5; x <= 26; x++) put(t, x, 28, 0);               /* shadow  */
}

/* A wooden ladder hung vertically: two brown rails with pale rungs. This is the
 * FM Towns dungeon look (a wooden ladder on the far wall, not a rope), drawn
 * from primitives here rather than copied from its pixels. Both the up- and
 * down-ladder use it; the entry-vs-descent distinction is the cell's bit, not
 * the art. */
static void gen_spr_ladder(uint8_t *t)
{
    const int railL = 9, railR = 20;
    for (int y = 0; y < 32; y++) {
        for (int x = railL; x <= railL + 2; x++) put(t, x, y, 6);   /* left rail  */
        for (int x = railR; x <= railR + 2; x++) put(t, x, y, 6);   /* right rail */
    }
    for (int ry = 2; ry < 32; ry += 6) {                           /* rungs      */
        for (int x = railL; x <= railR + 2; x++) {
            put(t, x, ry, 14);                                     /* pale wood  */
            if (ry + 1 < 32) put(t, x, ry + 1, 6);                 /* shadow     */
        }
    }
}

/* A generic dungeon creature: head, torso, two legs, two eyes. The per-type
 * knobs are body colour, accent colour and bulk, so the seven stamps read as
 * seven different silhouettes at a glance. Which stamp is which creature is
 * NOT proven (dungeon.h), so no type here carries a bestiary name. */
static void gen_spr_mon(uint8_t *t, uint8_t body, uint8_t accent, int bulk)
{
    const int cx = 16;
    /* head */
    for (int y = 4; y <= 10; y++)
        for (int x = cx - 3; x <= cx + 3; x++)
            put(t, x, y, (y == 4 || x == cx - 3 || x == cx + 3) ? accent : body);
    put(t, cx - 2, 7, accent);                    /* eyes */
    put(t, cx + 2, 7, accent);
    /* shoulders + torso, widening with bulk */
    for (int y = 11; y <= 22; y++) {
        const int half = 4 + bulk + (y - 11) / 6;
        for (int x = cx - half; x <= cx + half; x++) {
            uint8_t c = body;
            if (y == 11 || x == cx - half || x == cx + half) c = accent;
            put(t, x, y, c);
        }
    }
    /* legs */
    for (int y = 23; y <= 29; y++)
        for (int x = cx - 4; x <= cx + 4; x++)
            if (x < cx - 1 || x > cx + 1)
                put(t, x, y, (y == 29) ? 0 : body);
    /* ground shadow */
    for (int x = cx - 6; x <= cx + 6; x++) put(t, x, 30, 0);
}

/* --------------------------------------------------------------- init ---- */
/* Copy a generated concept sprite (base frame indices 16..255 or transparent)
 * into a hi-res slot, expanding the four falloff levels through the generated
 * shade map so distance darkening stays an index swap in the draw loop. */
static void load_gen(int slot, const uint8_t *base)
{
    for (int lv = 0; lv < U2_DGA_SHADE_LEVELS; lv++)
        for (int i = 0; i < DG_SPR_EDGE * DG_SPR_EDGE; i++) {
            const uint8_t b = base[i];
            s_hi[slot][lv][i] = (b == DG_SPR_TRANSPARENT)
                ? U2_DGA_TRANSPARENT
                : (uint8_t)(16 + dg_sprite_shade[lv][b - 16]);
        }
    s_spr_edge[slot] = DG_SPR_EDGE;
}

void u2_dga_init(void)
{
    if (s_ready) return;

    /* Fresh base textures all derive from the same seed sequence, so the whole
     * set is reproducible from one constant. */
    xs_state = 0x5EEDD15Bu;

    static void (*const texgen[U2_DGA_TEX_COUNT])(uint8_t *) = {
        gen_wall_stone, gen_wall_door, gen_floor_flag, gen_floor_door,
        gen_floor_hole, gen_ceil_cave, gen_ceil_hole
    };
    for (int id = 0; id < U2_DGA_TEX_COUNT; id++) {
        texgen[id](s_tex[id][0]);
        for (int lv = 1; lv < U2_DGA_SHADE_LEVELS; lv++)
            for (int i = 0; i < U2_DGA_TEX_PX; i++) {
                const uint8_t p = s_tex[id][lv - 1][i];
                s_tex[id][lv][i] = (p == U2_DGA_TRANSPARENT)
                                 ? U2_DGA_TRANSPARENT : k_shade[p];
            }
    }

    xs_state = 0x0D1CEA57u;   /* sprites get their own stream */
    for (int id = 0; id < U2_DGA_SP_COUNT; id++)
        for (int i = 0; i < U2_DGA_TEX_PX; i++)
            s_spr[id][0][i] = U2_DGA_TRANSPARENT;   /* static zero is black, not clear */
    gen_spr_chest(s_spr[U2_DGA_SP_CHEST][0]);
    gen_spr_ladder(s_spr[U2_DGA_SP_LADDER_UP][0]);
    gen_spr_ladder(s_spr[U2_DGA_SP_LADDER_DOWN][0]);
    /* stamp 1..7 -> (body, accent, bulk). Colours are chosen so no monster
     * reuses the grey/brown set of the static geometry: a test can prove a
     * creature actually drew by looking for its colour alone. */
    struct mon { uint8_t body, accent; int bulk; } mons[7] = {
        { 15, 7, 0 },   /* 1: pale, thin        */
        { 10, 2, 1 },   /* 2: green, stocky     */
        { 9, 1, 0 },    /* 3: blue, thin        */
        { 14, 4, 0 },   /* 4: yellow, thin      */
        { 12, 4, 1 },   /* 5: red, stocky       */
        { 11, 3, 0 },   /* 6: cyan, thin        */
        { 13, 5, 2 },   /* 7: magenta, heavy    */
    };
    for (int i = 0; i < 7; i++)
        gen_spr_mon(s_spr[U2_DGA_SP_MON1 + i][0], mons[i].body, mons[i].accent,
                    mons[i].bulk);
    for (int id = 0; id < U2_DGA_SP_COUNT; id++)
        for (int lv = 1; lv < U2_DGA_SHADE_LEVELS; lv++)
            for (int i = 0; i < U2_DGA_TEX_PX; i++) {
                const uint8_t c = s_spr[id][lv - 1][i];
                s_spr[id][lv][i] = (c == U2_DGA_TRANSPARENT)
                                 ? U2_DGA_TRANSPARENT : k_shade[c];
            }

    /* Concept-derived high-resolution sprites (dungeon_sprites_gen.h). The base
     * image carries frame indices 16..255 (or transparent); the four falloff
     * levels are the base run through the generated shade map, so falloff stays
     * an index swap in the draw loop. The seven creature stamps and the chest
     * come from concept art; the ladders stay procedural (no concept). */
    for (int i = 0; i < U2_DGA_SP_COUNT; i++) s_spr_edge[i] = U2_DGA_TEX_EDGE;
    load_gen(U2_DGA_SP_MON1, dg_spr_orc);
    load_gen(U2_DGA_SP_MON2, dg_spr_ghost);
    load_gen(U2_DGA_SP_MON3, dg_spr_carrion_creeper);
    load_gen(U2_DGA_SP_MON4, dg_spr_viper_emerald);
    load_gen(U2_DGA_SP_MON5, dg_spr_gremlin);
    load_gen(U2_DGA_SP_MON6, dg_spr_daemon_minotaur);
    load_gen(U2_DGA_SP_MON7, dg_spr_balron);
    /* The chest stays procedural: the chest concept is a multi-chest sheet, not
     * a single billboard, so it is not a valid sprite source until a single-chest
     * shot exists. */
    s_pal_ready = 1;

    s_ready = 1;
}

const uint8_t *u2_dga_tex(int id, int level)
{
    if (id < 0 || id >= U2_DGA_TEX_COUNT) return s_tex[0][0];
    if (level < 0) level = 0;
    if (level >= U2_DGA_SHADE_LEVELS) level = U2_DGA_SHADE_LEVELS - 1;
    return s_tex[id][level];
}

const uint8_t *u2_dga_sprite(int id, int level)
{
    if (id < 0 || id >= U2_DGA_SP_COUNT) return s_spr[0][0];
    if (level < 0) level = 0;
    if (level >= U2_DGA_SHADE_LEVELS) level = U2_DGA_SHADE_LEVELS - 1;
    if (s_spr_edge[id] == DG_SPR_EDGE) return s_hi[id][level];
    return s_spr[id][level];
}

int u2_dga_sprite_edge(int id)
{
    if (id < 0 || id >= U2_DGA_SP_COUNT) return U2_DGA_TEX_EDGE;
    return s_spr_edge[id] ? s_spr_edge[id] : U2_DGA_TEX_EDGE;
}

int u2_dga_palette_ready(void) { return s_pal_ready; }

void u2_dga_palette(uint8_t out[240][3])
{
    const int n = DG_SPR_PAL_N < 240 ? DG_SPR_PAL_N : 240;
    for (int i = 0; i < n; i++) {
        out[i][0] = dg_sprite_pal[i][0];
        out[i][1] = dg_sprite_pal[i][1];
        out[i][2] = dg_sprite_pal[i][2];
    }
}

int u2_dga_shade(float d)
{
    if (d < 2.5f) return 0;
    if (d < 4.0f) return 1;
    if (d < 6.0f) return 2;
    return 3;
}
