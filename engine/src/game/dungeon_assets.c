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

/* Ladder up: flagstone with a rung ladder standing in the cell. */
static void gen_floor_ladder_up(uint8_t *t)
{
    gen_floor_flag(t);
    for (int y = 0; y < 32; y++)
        for (int x = 6; x <= 25; x++) {
            if (x == 6 || x == 25) put(t, x, y, 7);        /* rails      */
            else if (y % 6 == 2) put(t, x, y, 15);         /* rungs      */
        }
}

/* Ladder down: flagstone with a dark square shaft and a lit rim. */
static void gen_floor_ladder_down(uint8_t *t)
{
    gen_floor_flag(t);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            const int d = (x < 16 ? 16 - x : x - 16) + (y < 16 ? 16 - y : y - 16);
            if (d < 9) put(t, x, y, 0);
            else if (d < 11) put(t, x, y, 7);
        }
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
void u2_dga_init(void)
{
    if (s_ready) return;

    /* Fresh base textures all derive from the same seed sequence, so the whole
     * set is reproducible from one constant. */
    xs_state = 0x5EEDD15Bu;

    static void (*const texgen[U2_DGA_TEX_COUNT])(uint8_t *) = {
        gen_wall_stone, gen_floor_flag, gen_floor_door,
        gen_floor_ladder_up, gen_floor_ladder_down, gen_ceil_cave
    };
    for (int id = 0; id < U2_DGA_TEX_COUNT; id++) {
        texgen[id](s_tex[id][0]);
        for (int lv = 1; lv < U2_DGA_SHADE_LEVELS; lv++)
            for (int i = 0; i < U2_DGA_TEX_PX; i++)
                s_tex[id][lv][i] = k_shade[s_tex[id][lv - 1][i]];
    }

    xs_state = 0x0D1CEA57u;   /* sprites get their own stream */
    for (int id = 0; id < U2_DGA_SP_COUNT; id++)
        for (int i = 0; i < U2_DGA_TEX_PX; i++)
            s_spr[id][0][i] = U2_DGA_TRANSPARENT;   /* static zero is black, not clear */
    gen_spr_chest(s_spr[U2_DGA_SP_CHEST][0]);
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
    return s_spr[id][level];
}

int u2_dga_shade(float d)
{
    if (d < 2.5f) return 0;
    if (d < 4.0f) return 1;
    if (d < 6.0f) return 2;
    return 3;
}
