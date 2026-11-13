/*
 * dungeon3d.c — the enhanced-mode perspective corridor.
 *
 * This is a FEATURE, not a fidelity target: no shipped DOS original draws a textured
 * 3D dungeon, so nothing here is byte-proven and none of it may be cited as evidence
 * about original/ultimaii.exe. See docs/enhanced-dungeon-3d.md for the whole design
 * and the owner rulings (disc-supplied FM Towns art, open-archway doors, pinhole
 * convergence).
 *
 * This first cut is FLAT-SHADED: it establishes the projection and the dispatch, not
 * the art. The corridor is drawn as nested depth frames converging to the map-window
 * centre, with the regions between consecutive frames filled as ceiling / floor /
 * wall trapezoids and an end cap where the corridor stops. Texture from the FM Towns
 * banks is a later step (§ 7a step 3); the flat palette colours here are placeholders
 * chosen so the geometry is legible and the gate can tell a filled corridor from the
 * sparse CGA wireframe it replaces in enhanced mode.
 *
 * The topology is not re-derived: the caller passes the same u2_dg_scan the wireframe
 * reads (asm/6078's three columns), so the 3D view and the wireframe agree on which
 * faces are solid by construction.
 */

#include "game/dungeon.h"

#include <stdint.h>
#include <string.h>

#include "core/ctx.h"
#include "core/frame.h"
#include "core/layout.h"
#include "game/overworld.h"

/* The map window in the enhanced layout: 14x14 cells of 32 px at (0,0). */
#define DG3_W  (U2_ENH_MAP_COLS * 32)   /* 448 */
#define DG3_H  (U2_ENH_MAP_ROWS * 32)   /* 448 */
#define DG3_CX (DG3_W / 2)              /* 224 — the vanishing point is the window centre */
#define DG3_CY (DG3_H / 2)

/* Flat placeholder colours, CGA-compatible indices so the corridor is legible with no
 * theme loaded. Ceiling is deliberately non-black so the whole window reads as filled
 * ink (the gate's discriminator against the sparse wireframe); a real ceiling is dark
 * and arrives with the texture step. */
#define DG3_CEIL   U2_CGA_CYAN      /* 1 */
#define DG3_FLOOR  U2_CGA_MAGENTA   /* 2 */
#define DG3_WALL   U2_CGA_WHITE     /* 3 */
#define DG3_ENDCAP U2_CGA_WHITE     /* 3 */
#define DG3_SPRITE U2_CGA_BLACK     /* 0 — a silhouette punched into the corridor */

/* Wall texture: a 32x32 array of palette indices, sampled across the wall quads. When
 * NULL the walls are the flat placeholder colour (the geometry-only path the CP7 gate
 * pins). The FM Towns DUNG1 flat-brick tile is supplied here by the disc loader once it
 * is wired (docs/enhanced-dungeon-3d.md § 7a step 2); until then a caller may set any
 * 32x32 indexed tile. The renderer is palette-agnostic: it puts whatever index the tile
 * holds, so the disc loader owns the k_pal -> engine palette remap. */
static const uint8_t *g_wall_tex;

void u2_dungeon3d_set_wall_tex(const uint8_t *tex32)
{
    g_wall_tex = tex32;
}

typedef struct { int x0, y0, x1, y1; } dg3_rect;

/* Depth frame d, d = 0 (nearest, fills the window) .. U2_DG_DRAW_DEPTH (far).
 * Pinhole convergence s(d) = 1/(d+1): the half-extent shrinks as 1/(d+1) toward the
 * vanishing point. This is our design choice (docs/enhanced-dungeon-3d.md § 1b), not
 * a value read from the original. */
static dg3_rect dg3_frame(int d)
{
    const int hw = (DG3_W / 2) / (d + 1);
    const int hh = (DG3_H / 2) / (d + 1);
    dg3_rect r = { DG3_CX - hw, DG3_CY - hh, DG3_CX + hw, DG3_CY + hh };
    return r;
}

static void dg3_fill_rect(u2_frame *f, dg3_rect r, uint8_t c)
{
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++)
            u2_frame_put(f, x, y, c);   /* put clips out-of-window pixels */
}

/* The four faces of the segment between a (near, larger) and b (far, smaller). Each
 * interpolates the frame width/height linearly across the band, which is exact for a
 * pinhole corridor viewed head-on. u2_frame_put clips, so a band that would spill past
 * the window edge is simply not drawn there. */
static void dg3_fill_ceiling(u2_frame *f, dg3_rect a, dg3_rect b, uint8_t c)
{
    const int y0 = a.y0, y1 = b.y0;          /* near top is higher (smaller y) */
    if (y1 <= y0) return;
    const int den = y1 - y0;
    for (int y = y0; y < y1; y++) {
        const int t = y - y0;
        const int xl = a.x0 + (b.x0 - a.x0) * t / den;
        const int xr = a.x1 + (b.x1 - a.x1) * t / den;
        for (int x = xl; x < xr; x++) u2_frame_put(f, x, y, c);
    }
}

static void dg3_fill_floor(u2_frame *f, dg3_rect a, dg3_rect b, uint8_t c)
{
    const int y0 = b.y1, y1 = a.y1;          /* near bottom is lower (larger y) */
    if (y1 <= y0) return;
    const int den = y1 - y0;
    for (int y = y0; y < y1; y++) {
        const int t = y1 - y;                /* t=0 at the near edge */
        const int xl = a.x0 + (b.x0 - a.x0) * t / den;
        const int xr = a.x1 + (b.x1 - a.x1) * t / den;
        for (int x = xl; x < xr; x++) u2_frame_put(f, x, y, c);
    }
}

static void dg3_fill_left(u2_frame *f, dg3_rect a, dg3_rect b, uint8_t c, const uint8_t *tex)
{
    const int x0 = a.x0, x1 = b.x0;          /* near left is further left */
    if (x1 <= x0) return;
    const int den = x1 - x0;
    for (int x = x0; x < x1; x++) {
        const int t = x - x0;
        const int yt = a.y0 + (b.y0 - a.y0) * t / den;
        const int yb = a.y1 + (b.y1 - a.y1) * t / den;
        const int u = tex ? (t * 32 / den) & 31 : 0;
        for (int y = yt; y < yb; y++) {
            if (!tex) { u2_frame_put(f, x, y, c); continue; }
            const int v = ((y - yt) * 32 / (yb - yt)) & 31;
            u2_frame_put(f, x, y, tex[v * 32 + u]);
        }
    }
}

static void dg3_fill_right(u2_frame *f, dg3_rect a, dg3_rect b, uint8_t c, const uint8_t *tex)
{
    const int x0 = b.x1, x1 = a.x1;          /* near right is further right */
    if (x1 <= x0) return;
    const int den = x1 - x0;
    for (int x = x0; x < x1; x++) {
        const int t = x1 - x;                /* t=0 at the near edge */
        const int yt = a.y0 + (b.y0 - a.y0) * t / den;
        const int yb = a.y1 + (b.y1 - a.y1) * t / den;
        const int u = tex ? (t * 32 / den) & 31 : 0;
        for (int y = yt; y < yb; y++) {
            if (!tex) { u2_frame_put(f, x, y, c); continue; }
            const int v = ((y - yt) * 32 / (yb - yt)) & 31;
            u2_frame_put(f, x, y, tex[v * 32 + u]);
        }
    }
}

void u2_dungeon_render3d(u2_ctx *ctx, u2_overworld *ow, const u2_dg_scan *s)
{
    (void)ctx;
    u2_frame *f = &ow->frame;

    /* Background: the distance is ceiling-coloured dark, so an open corridor that
     * runs past the draw depth fades to the ceiling tone rather than a hard edge. */
    for (int y = 0; y < DG3_H; y++)
        for (int x = 0; x < DG3_W; x++)
            u2_frame_put(f, x, y, DG3_CEIL);

    /* Painter's algorithm, far to near: nearer segments overwrite the parts of the
     * farther ones they occlude. A rock-ahead cell is an end cap that fills its frame
     * and occludes everything beyond it, so that depth draws the cap and skips its
     * own ring (the corridor does not continue past a wall). */
    for (int d = U2_DG_DRAW_DEPTH - 1; d >= 0; d--) {
        const dg3_rect a = dg3_frame(d);
        const dg3_rect b = dg3_frame(d + 1);

        if (s->ctr[d] & U2_DG_ROCK) {
            dg3_fill_rect(f, a, DG3_ENDCAP);
            continue;
        }
        dg3_fill_ceiling(f, a, b, DG3_CEIL);
        dg3_fill_floor(f, a, b, DG3_FLOOR);
        if (s->left[d] & U2_DG_ROCK)  dg3_fill_left(f, a, b, DG3_WALL, g_wall_tex);
        if (s->right[d] & U2_DG_ROCK) dg3_fill_right(f, a, b, DG3_WALL, g_wall_tex);
    }

    /* Placeholder creature: a black silhouette at the nearest depth whose centre cell
     * carries a stamp. The FM Towns monster sprites (DUNMON banks) replace this in the
     * texture step; until then a filled block marks that a creature is there. */
    for (int d = 0; d < U2_DG_DRAW_DEPTH; d++) {
        if (s->obj[d] == 0) continue;
        const dg3_rect fr = dg3_frame(d);
        const int cx = (fr.x0 + fr.x1) / 2, cy = (fr.y0 + fr.y1) / 2;
        const int hw = (fr.x1 - fr.x0) / 4, hh = (fr.y1 - fr.y0) / 4;
        dg3_rect sp = { cx - hw, cy - hh, cx + hw, cy + hh };
        dg3_fill_rect(f, sp, DG3_SPRITE);
        break;
    }
}
