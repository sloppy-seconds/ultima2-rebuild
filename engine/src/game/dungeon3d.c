/*
 * dungeon3d.c — the enhanced-mode 3D dungeon: a DDA raycaster over the grid.
 *
 * This is a FEATURE, not a fidelity target: no shipped DOS original draws a
 * textured 3D dungeon, so nothing here is byte-proven and none of it may be
 * cited as evidence about original/ultimaii.exe. See docs/enhanced-dungeon-3d.md
 * for the design and the owner rulings.
 *
 * Ruling 2026-11-14 (owner): the FM Towns art route is retired to branch
 * archive/fmtowns-dungeon-route; the engine draws its own procedurally
 * generated art (dungeon_assets.c), movement stays instant like the original,
 * and the view renders into u2_frame like every other pixel the engine makes —
 * no SDL in this file, so the terminal backends, the demo recorder and the
 * headless tests all still see the dungeon.
 *
 * What is engine truth here (dungeon.h):
 *   * the grid: 16x16 pages of the sector buffer, floor = [DS:25], the same
 *     u2_world_dungeon_at the wireframe and the walk gates read, with the same
 *     & 0x3f wrap the scan uses;
 *   * solidity by the render-stop rule, not the walk rule: bit 7 set draws
 *     solid EXCEPT 0xC0, the open doorway, which draws as a passage. 0xE0, the
 *     fake wall, therefore draws exactly like rock — finding it by walking is
 *     the mechanic, and the renderer must not give it away;
 *   * the low nibble is the creature stamp (1..7), 0x40 is a chest, 0x10/0x20
 *     are the ladders, 0xC0 is a doorway: ladders and doors are floor texture
 *     variants because you walk over them.
 *
 * What is design, not evidence (all of the rest): the projection (per-column
 * DDA, ~74 deg FOV), the torch falloff bands, the texture set, the sprite
 * shapes, and the ray range of 8 cells (chosen to echo asm/6078's eight sample
 * depths, not because a raycast was ever observed).
 */

#include "game/dungeon.h"

#include <stdint.h>
#include <string.h>

#include "core/ctx.h"
#include "core/frame.h"
#include "core/layout.h"
#include "core/world.h"
#include "game/dungeon_assets.h"
#include "game/overworld.h"

#define DG3_W     (U2_ENH_MAP_COLS * 32)   /* 448 — the enhanced map window */
#define DG3_H     (U2_ENH_MAP_ROWS * 32)   /* 448 */
#define DG3_CX    (DG3_W / 2)
#define DG3_CY    (DG3_H / 2)

/* tan(FOV/2). Design choice: ~74 degrees, a touch wider than Wolf3D's 66 so
 * the near-square 448x448 window shows enough of the side walls to read a
 * corridor junction when you face down the corridor. */
#define DG3_PLANE 0.75f
#define DG3_PROJ  ((float)DG3_W / 2.0f / DG3_PLANE)
#define DG3_MAX_DIST 8.0f                  /* rays stop here; see header      */
#define DG3_FAR_Z  1e9f

static float f_abs(float v) { return v < 0 ? -v : v; }
static int f_floor(float v) { int i = (int)v; return i - (v < i); }

/* Solid for RENDERING — the wireframe's stop rule, not the walk gate. */
static int dg3_solid(uint8_t c)
{
    const uint8_t t = c & U2_DG_TERRAIN;
    return (t & U2_DG_ROCK) && t != U2_DG_DOOR;
}

static uint8_t dg3_cell(const u2_overworld *ow, int fl, int x, int y)
{
    return u2_world_dungeon_at(&ow->world, fl, x & 0x3f, y & 0x3f);
}

/* Which floor art a cell gets. 0x30 (both ladder bits) prefers DOWN, matching
 * the bit-test order of the two ladder gates. */
static int dg3_floor_tex(uint8_t c)
{
    const uint8_t t = c & U2_DG_TERRAIN;
    if (t == U2_DG_DOOR) return U2_DGA_FLOOR_DOOR;
    if (t & U2_DG_DOWN) return U2_DGA_FLOOR_LADDER_DOWN;
    if (t & U2_DG_UP) return U2_DGA_FLOOR_LADDER_UP;
    return U2_DGA_FLOOR_FLAG;
}

/* ---------------------------------------------------------- floor/ceiling */
/* Classic floor casting: every row below the horizon is one constant distance
 * row, walked from the leftmost to the rightmost ray; the ceiling mirrors it.
 * Texture is picked per pixel from the cell being looked at, so ladders, door
 * thresholds and flagstone appear underfoot exactly where the grid says they
 * are. */
static void dg3_floor_ceiling(u2_frame *f, const u2_overworld *ow, int fl,
                              float px, float py,
                              float dirX, float dirY,
                              float planeX, float planeY)
{
    const float ldx = dirX - planeX, ldy = dirY - planeY;   /* leftmost ray  */
    const float rdx = dirX + planeX, rdy = dirY + planeY;   /* rightmost ray */

    for (int y = DG3_CY + 1; y < DG3_H; y++) {
        const float p = (float)y + 0.5f - DG3_CY;
        const float rowDist = (0.5f * DG3_PROJ) / p;
        const int lvl = u2_dga_shade(rowDist);
        const int yTop = DG3_H - 1 - y;                    /* ceiling mirror */

        if (rowDist > DG3_MAX_DIST) {
            for (int x = 0; x < DG3_W; x++) {
                u2_frame_put(f, x, y, 0);
                u2_frame_put(f, x, yTop, 0);
            }
            continue;
        }

        const float stepX = rowDist * (rdx - ldx) / (float)DG3_W;
        const float stepY = rowDist * (rdy - ldy) / (float)DG3_W;
        float fx = px + rowDist * ldx;
        float fy = py + rowDist * ldy;

        const uint8_t *ceil = u2_dga_tex(U2_DGA_CEIL_CAVE, lvl);
        for (int x = 0; x < DG3_W; x++, fx += stepX, fy += stepY) {
            const int cx = f_floor(fx), cy = f_floor(fy);
            const uint8_t *tex = u2_dga_tex(dg3_floor_tex(dg3_cell(ow, fl, cx, cy)), lvl);
            const int tx = ((int)((fx - (float)cx) * 32.0f)) & 31;
            const int ty = ((int)((fy - (float)cy) * 32.0f)) & 31;
            u2_frame_put(f, x, y, tex[ty * 32 + tx]);
            u2_frame_put(f, x, yTop, ceil[ty * 32 + tx]);
        }
    }
}

/* ------------------------------------------------------------------ walls */
/* One DDA ray per screen column. Hits are the render-solid rule above; the
 * perpendicular distance feeds wall height, texture falloff and the z-buffer
 * the sprites depth-test against. */
static void dg3_walls(u2_frame *f, const u2_overworld *ow, int fl,
                      float px, float py,
                      float dirX, float dirY,
                      float planeX, float planeY, float *zbuf)
{
    for (int x = 0; x < DG3_W; x++) {
        const float camX = 2.0f * (float)x / (float)DG3_W - 1.0f;
        const float rdx = dirX + planeX * camX;
        const float rdy = dirY + planeY * camX;

        int mapX = f_floor(px), mapY = f_floor(py);
        const float ddx = (rdx == 0.0f) ? 1e30f : f_abs(1.0f / rdx);
        const float ddy = (rdy == 0.0f) ? 1e30f : f_abs(1.0f / rdy);
        int stepX, stepY;
        float sideX, sideY;
        if (rdx < 0.0f) { stepX = -1; sideX = (px - (float)mapX) * ddx; }
        else            { stepX =  1; sideX = ((float)mapX + 1.0f - px) * ddx; }
        if (rdy < 0.0f) { stepY = -1; sideY = (py - (float)mapY) * ddy; }
        else            { stepY =  1; sideY = ((float)mapY + 1.0f - py) * ddy; }

        int side = 0, hit = 0;
        for (;;) {
            if (sideX < sideY) { side = 0; mapX += stepX; sideX += ddx; }
            else               { side = 1; mapY += stepY; sideY += ddy; }
            if ((side ? sideY : sideX) > DG3_MAX_DIST) break;
            if (dg3_solid(dg3_cell(ow, fl, mapX, mapY))) { hit = 1; break; }
        }

        if (!hit) { zbuf[x] = DG3_FAR_Z; continue; }
        const float perp = (side == 0) ? (sideX - ddx) : (sideY - ddy);
        zbuf[x] = perp;

        const int lineH = (int)(DG3_PROJ / (perp < 0.05f ? 0.05f : perp));
        int y0 = DG3_CY - lineH / 2, y1 = DG3_CY + lineH / 2;
        if (y0 < 0) y0 = 0;
        if (y1 > DG3_H) y1 = DG3_H;

        float wallX = (side == 0) ? (py + perp * rdy) : (px + perp * rdx);
        wallX -= (float)f_floor(wallX);
        int texX = ((int)(wallX * 32.0f)) & 31;
        if ((side == 0 && rdx > 0.0f) || (side == 1 && rdy < 0.0f))
            texX = 31 - texX;                       /* keep brick handedness */

        const uint8_t *tex = u2_dga_tex(U2_DGA_WALL_STONE, u2_dga_shade(perp));
        const float stepT = 32.0f / (float)lineH;
        float tpos = (float)(y0 - (DG3_CY - lineH / 2)) * stepT;
        for (int y = y0; y < y1; y++, tpos += stepT)
            u2_frame_put(f, x, y, tex[((int)tpos & 31) * 32 + texX]);
    }
}

/* --------------------------------------------------------------- sprites */
/* Billboards for the things that live on a cell you can see: creature stamps
 * and chests. Collected from the page grid, sorted far-to-near, drawn through
 * the wall z-buffer so a creature behind a corner is occluded per column. */
#define DG3_MAX_SPR 128

typedef struct { float sx, sy, d2; int id; int tall; } dg3_spr;

static void dg3_sprites(u2_frame *f, const u2_overworld *ow, int fl,
                        float px, float py,
                        float dirX, float dirY,
                        float planeX, float planeY, const float *zbuf)
{
    dg3_spr list[DG3_MAX_SPR];
    int n = 0;

    for (int cy = 0; cy < U2_DG_PAGE_EDGE; cy++)
        for (int cx = 0; cx < U2_DG_PAGE_EDGE; cx++) {
            const uint8_t c = u2_world_dungeon_at(&ow->world, fl, cx, cy);
            const uint8_t stamp = c & 0x07u;
            int id = -1, tall = 1;
            if (stamp >= 1 && stamp <= 7) id = U2_DGA_SP_MON1 + stamp - 1;
            else if ((c & U2_DG_TERRAIN) == U2_DG_CHEST) { id = U2_DGA_SP_CHEST; tall = 0; }
            if (id < 0 || n >= DG3_MAX_SPR) continue;

            const float sx = (float)cx + 0.5f - px;
            const float sy = (float)cy + 0.5f - py;
            const float d2 = sx * sx + sy * sy;
            if (d2 > DG3_MAX_DIST * DG3_MAX_DIST) continue;
            list[n].sx = sx; list[n].sy = sy; list[n].d2 = d2;
            list[n].id = id; list[n].tall = tall;
            n++;
        }

    for (int i = 1; i < n; i++) {          /* insertion sort, far first */
        const dg3_spr k = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].d2 < k.d2) { list[j + 1] = list[j]; j--; }
        list[j + 1] = k;
    }

    const float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    for (int i = 0; i < n; i++) {
        const float tX = invDet * ( dirY * list[i].sx - dirX * list[i].sy);
        const float tY = invDet * (-planeY * list[i].sx + planeX * list[i].sy);
        if (tY <= 0.1f) continue;

        const int screenX = (int)(((float)DG3_W / 2.0f) * (1.0f + tX / tY));
        const int hFull = (int)(DG3_PROJ / tY);
        const int h = list[i].tall ? (hFull * 9 / 10) : (hFull / 2);
        const int w = list[i].tall ? h : (hFull / 2);
        const int floorY = DG3_CY + (int)(DG3_PROJ / (2.0f * tY));
        const int y0 = floorY - h, y1 = floorY;
        const int x0 = screenX - w / 2, x1 = screenX + w / 2;

        int lvl = 3;
        if (list[i].d2 < 6.25f) lvl = 0;
        else if (list[i].d2 < 16.0f) lvl = 1;
        else if (list[i].d2 < 36.0f) lvl = 2;
        const uint8_t *tex = u2_dga_sprite(list[i].id, lvl);

        for (int x = x0; x < x1; x++) {
            if (x < 0 || x >= DG3_W || tY >= zbuf[x]) continue;
            const int tx = ((x - x0) * 32) / (w > 0 ? w : 1);
            for (int y = y0; y < y1; y++) {
                if (y < 0 || y >= DG3_H) continue;
                const int ty = ((y - y0) * 32) / (h > 0 ? h : 1);
                const uint8_t c = tex[ty * 32 + tx];
                if (c == U2_DGA_TRANSPARENT) continue;
                u2_frame_put(f, x, y, c);
            }
        }
    }
}

/* ---------------------------------------------------------------- driver */
void u2_dungeon_render3d(u2_ctx *ctx, u2_overworld *ow, const u2_dg_scan *s)
{
    (void)s;   /* the scan is the wireframe's sample; the raycaster reads the
                * same grid under the same solidity rule instead, so the two
                * views agree by rule rather than by shared sample. */
    u2_frame *f = &ow->frame;
    u2_dga_init();

    int dx, dy;
    u2_dungeon_facing(ctx, &dx, &dy);
    const int fl = (int8_t)ctx->state.site_floor;
    const float px = (float)ow->x + 0.5f;
    const float py = (float)ow->y + 0.5f;
    const float dirX = (float)dx, dirY = (float)dy;
    const float planeX = (float)dy * DG3_PLANE;
    const float planeY = (float)(-dx) * DG3_PLANE;

    float zbuf[DG3_W];
    dg3_floor_ceiling(f, ow, fl, px, py, dirX, dirY, planeX, planeY);
    dg3_walls(f, ow, fl, px, py, dirX, dirY, planeX, planeY, zbuf);
    dg3_sprites(f, ow, fl, px, py, dirX, dirY, planeX, planeY, zbuf);
}
