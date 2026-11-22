/*
 * dungeon.c — CP7: the interior sector. See dungeon.h for what is proven and
 * what is not; every claim below carries its asm file, and the places where the
 * bytes run out say so rather than filling the gap.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/dungeon.h"
#include "platform/platform.h"
#include "game/overworld.h"
#include "game/combat.h"
#include "core/frame.h"
#include "core/layout.h"
#include "core/seam.h"
#include "core/state.h"
#include "core/world.h"
#include "gen_content.h"

/* ------------------------------------------------------------------ state -- */

int u2_site_is_dungeon(const u2_ctx *ctx)
{
    /* Delegates to u2_state_is_dungeon() so the branch the game logic takes and the mode
     * the API reports cannot answer the same question differently. */
    return u2_state_is_dungeon(&ctx->state);
}

void u2_dungeon_facing(const u2_ctx *ctx, int *dx, int *dy)
{
    *dx = (int8_t)ctx->state.face_dx;
    *dy = (int8_t)ctx->state.face_dy;
}

static uint8_t cell(const u2_overworld *ow, int floor, int x, int y)
{
    return u2_world_dungeon_at(&ow->world, floor, x, y);
}

/* asm/6000 writes these three probes to [DS:14]/[DS:15]/[DS:16] on every render,
 * and cmd_D, cmd_K, cmd_G and the ADVANCE/RETREAT commands all read them back.
 * The empirical anchor is captures/spice86/dungeon-35-live: with the player at
 * (5,5) facing (1,0) the live bytes read 0x10 / 0x00 / 0x80, which are the cells
 * at (5,5), (6,5) and (4,5) of live grid page 0 and nothing else. */
void u2_dungeon_probe(u2_ctx *ctx, const u2_overworld *ow)
{
    int dx, dy;
    u2_dungeon_facing(ctx, &dx, &dy);
    const int fl = (int8_t)ctx->state.site_floor;
    const int px = ctx->state.sector_x, py = ctx->state.sector_y;

    ctx->state.mode_a    = cell(ow, fl, px, py);
    ctx->state.mode_walk = cell(ow, fl, px + dx, py + dy);
    ctx->state.mode_b    = cell(ow, fl, px - dx, py - dy);
}

/* ------------------------------------------------------- the four commands -- */

/* asm/277B runtime 0x28C3 and 0x28F5. Both are a quarter-turn on the signed
 * facing pair and neither passes a turn:
 *   LEFT : if (dy) { dx = dy; dy = 0 } else { dy = -dx; dx = 0 }
 *   RIGHT: if (dx) { dy = dx; dy = 0 } else { dx = -dy; dy = 0 }
 * North (0,-1) turns LEFT to west (-1,0), which is the counter-clockwise the
 * words mean. */
void u2_dungeon_turn(u2_ctx *ctx, int left)
{
    int dx = (int8_t)ctx->state.face_dx;
    int dy = (int8_t)ctx->state.face_dy;

    if (left) {
        if (dy != 0) { dx = dy; dy = 0; }
        else         { dy = -dx; dx = 0; }
        seam_str_out(ctx, U2_S_TURN_LEFT);
    } else {
        if (dx != 0) { dy = dx; dx = 0; }
        else         { dx = -dy; dy = 0; }
        seam_str_out(ctx, U2_S_TURN_RIGHT);
    }
    ctx->state.face_dx = (uint8_t)(int8_t)dx;
    ctx->state.face_dy = (uint8_t)(int8_t)dy;
}

/* The ADVANCE gate, asm/277B runtime 0x286F, read instruction by instruction:
 *
 *   al = [DS:15]                       the cell ahead, from asm/6000's probe
 *   if (al & 0x0f) -> "-BLOCKED!"      a stamp is in the way
 *   if (al == 0x80) -> "-BLOCKED!"     solid rock
 *   x = (x + dx) & 0x3f ; y = (y + dy) & 0x3f
 *   if (rand_byte() >= [DS:25]) -> done
 *   ... trap
 *
 * So a chest cell (0x40) is walkable and a rock cell is not, which is what makes
 * GET-after-walking-onto-it the only shape the chest command can have.
 *
 * The trap roll compares against the FLOOR, so floor 0 has no traps at all and
 * the deeper you go the more often one fires. That is the whole of the rule the
 * bytes give; how often it fires in practice is unobserved.
 */
static int dg_step(u2_ctx *ctx, u2_overworld *ow, int forward)
{
    int dx, dy;
    u2_dungeon_facing(ctx, &dx, &dy);
    if (!forward) { dx = -dx; dy = -dy; }

    /* RETREAT tests [DS:16] with `and $0x8f` instead: rock OR a stamp both
     * refuse, and so does anything with bit 7 set, which is a wider net than
     * ADVANCE casts. Transcribed as the bytes have it. */
    const uint8_t c = forward ? ctx->state.mode_walk : ctx->state.mode_b;
    if (forward) {
        if ((c & U2_DG_STAMP) != 0 || c == U2_DG_ROCK) return 0;
    } else {
        if ((c & 0x8f) != 0) return 0;
    }

    ctx->state.sector_x = (uint8_t)((ctx->state.sector_x + dx) & 0x3f);
    ctx->state.sector_y = (uint8_t)((ctx->state.sector_y + dy) & 0x3f);
    u2_dungeon_probe(ctx, ow);
    return 1;
}

u2_cmd_result u2_dungeon_advance(u2_ctx *ctx, u2_overworld *ow)
{
    u2_dungeon_probe(ctx, ow);
    seam_str_out(ctx, U2_S_ADVANCE);

    if (!dg_step(ctx, ow, 1)) {
        seam_str_out(ctx, U2_S_BLOCKED);
        u2_cmd_done(ctx);
        return U2_CMD_DONE;
    }

    /* asm/277B runtime 0x28A5: one roll against the floor, and on `jb` the trap. */
    const uint8_t roll = (uint8_t)seam_rand_byte(ctx);
    if (roll >= ctx->state.site_floor) {
        u2_cmd_done(ctx);
        return U2_CMD_DONE;
    }

    /* The trap. asm/277B zeroes [DS:26] BEFORE it prints, so a trap puts out the
     * light as its first effect and the next render is dark. */
    ctx->state.moved_flag = 0;
    seam_char_print(ctx, '\r');           /* the leading 0x8d on asm/277B's db line */
    seam_str_out(ctx, U2_S_TRAP);

    if (ctx->state.tools == 0) {
        /* asm/277B runtime 0x28E3: `jmp L_000e62`, which is the DEATH handler
         * (asm/0C62, sub_0c62 — runtime 0x0e62 = file 0x0c62). A trap with no tools
         * is INSTANT DEATH, not a damage roll: the handler zeroes HP/food/exp/gold
         * and prints ' IS DEAD!'. Corrected 2026-11-13 — the earlier "damage tail,
         * amount undecoded" reading misidentified the jump target; there is no amount.
         * The death handler is screen-proven in captures/spice86/cp5-weak-fight. */
        u2_death(ctx);
        u2_cmd_done(ctx);
        return U2_CMD_DONE;
    }
    seam_str_out(ctx, U2_S_TOOLS_ESCAPED);
    /* asm/277B runtime 0x2904: `stc ; cmc ; sbb $1,%al ; das ; cmc` — one packed
     * decimal step off the TOOLS counter at [DS:66]. */
    ctx->state.tools = u2_bcd_dec1(ctx->state.tools);
    u2_cmd_done(ctx);
    return U2_CMD_DONE;
}

u2_cmd_result u2_dungeon_retreat(u2_ctx *ctx, u2_overworld *ow)
{
    u2_dungeon_probe(ctx, ow);
    seam_str_out(ctx, U2_S_RETREAT);
    if (!dg_step(ctx, ow, 0))
        seam_str_out(ctx, U2_S_BLOCKED);
    u2_cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/2B8F + asm/2B9B. asm/2B9B is three instructions — `mov [DS:4A],al ;
 * cmp $4,al ; jb L_002da5` — and BOTH destinations land on asm/2BA5, the arms
 * paralysis gate: the `jb` targets it directly and the fall-through reaches it
 * three bytes later. So the site-kind gate refuses nothing; what a site changes
 * is that the direction is already known. The cell attacked is the one the facing
 * names, which is [DS:15], the same probe ADVANCE uses.
 *
 * CONTRADICTION RESOLVED 2026-11-13: docs/spice86-live-findings.md § 13 had tabulated `A` as
 * ADVANCE in a dungeon (captures/spice86/dungeon-35-live), while captures/spice86/tower-34
 * showed `A` printing ATTACK--MISS and the bytes above show no arm that could print ADVANCE
 * from the A jump-table entry. captures/spice86/g51-dungeon-A-attack observed it directly
 * (cmd_E on the sector-20 door, torch with I): `A` printed ATTACK--MISS and the corridor did
 * not move; `Up` printed CMD: ADVANCE. So A = ATTACK, Up = ADVANCE, as the engine has it.
 */
u2_cmd_result u2_dungeon_attack(u2_ctx *ctx, u2_overworld *ow)
{
    int dx, dy;
    u2_dungeon_facing(ctx, &dx, &dy);
    u2_dungeon_probe(ctx, ow);
    u2_overworld_attack_dir(ctx, ow, U2_S_ATTACK, dx, dy);
    u2_dungeon_tick(ctx, ow);
    return U2_CMD_DONE;
}

/* asm/363B's site arm, runtime 0x36C3. The gate is the cell UNDER the player
 * compared to '@' (0x40) — the same 0x40 the renderer uses — and the loot roll
 * is one rand against 0x40, gold on `jb` and TRI-LITHIUM otherwise. The gold
 * amount is `(rand & 0x1f) + 2*floor`, formed with two `adc [DS:25]`, and the
 * `and $0x77` after it keeps it in packed decimal. */
u2_cmd_result u2_dungeon_get(u2_ctx *ctx, u2_overworld *ow)
{
    u2_dungeon_probe(ctx, ow);
    seam_str_out(ctx, U2_S_GET);

    const uint8_t under = ctx->state.mode_a;
    if (under != U2_DG_CHEST) {
        seam_str_out(ctx, U2_S_ENTER_WHAT);
        u2_cmd_done(ctx);
        return U2_CMD_DONE;
    }

    seam_str_out(ctx, U2_S_CHEST);
    seam_char_print(ctx, '\r');           /* the 0x8d between " CHEST!" and the next run */
    seam_str_out(ctx, U2_S_IT_CONTAINS);

    /* asm/363B runtime 0x36F1 clears the cell through the cell pointer, so the
     * chest is gone for good. Both copies, or the creature restamp would put it
     * back: see u2_world_terrain_put's header for why a map-only write is erased.
     *
     * Do NOT "fix" this to the map-only helper on the theory that chests should not
     * persist across re-entry. G81 asked exactly that and the answer is that the
     * both-copies write is right and the reload is what makes the chest come back:
     * asm/34E4 stores the sector you are LEAVING (the store gate runs before DS:4A
     * changes) and then re-reads 0x1000 bytes over DS:472 for the sector you are
     * entering, and asm/3823 on the way out writes only the player record. A site's
     * own mapx is therefore never written and is re-read on every entry. Pinned by
     * two checks in tests/site_check.c ("G81: ..."). */
    u2_world_dungeon_terrain_put(&ow->world, (int8_t)ctx->state.site_floor,
                                 ctx->state.sector_x, ctx->state.sector_y, 0);

    /* asm/363B_cmd_G.asm L_00396a..L_0038d8. The loot is NOT a flat roll.
       TRI-LITHIUM is gated on the site floor being 0xf — the deepest floor of a
       dungeon — and that gate is the ONLY way to obtain it, so it must never come
       out of a random roll on a shallow floor. Otherwise a roll >= 0x40 is gold
       (75%) and a roll < 0x40 is an item (25%), and the item is a weapon on an
       even floor or armour on an odd one. */
    const uint8_t floor = ctx->state.site_floor;
    if (floor == 0xf) {
        /* asm/363B L_00396a: "TRI-LITHIUM!" and items_held[15] (DS:e5) += 1. */
        seam_str_out(ctx, U2_S_TRI_LITHIUM);
        ctx->state.items_held[15] =
            u2_bcd_inc_sat(ctx->state.items_held[15], U2_SATURATE(ctx));
    } else if (seam_rand_byte(ctx) >= 0x40) {
        /* asm/363B L_003922: "GOLD!". amount = ((rand & 0x1f) + 2*floor) & 0x77,
           added to the gold odometer with the same adc/daa pair asm/2DC1 uses. */
        seam_str_out(ctx, U2_S_GOLD_EXCL);
        const uint8_t amount =
            (uint8_t)((((uint8_t)seam_rand_byte(ctx) & 0x1fu) + 2u * floor) & 0x77u);
        u2_bcd_adc2(&ctx->state.gold_lo, &ctx->state.gold_hi, amount,
                    U2_SATURATE(ctx));
    } else if ((floor & 1u) == 0) {
        /* asm/363B L_003951: even floor -> "A  WEAPON". A second roll picks the
           slot: weapons_owned[rand & 7] (DS:76+idx); slot 0 is "EMPTY!". */
        seam_char_print(ctx, 'A');
        seam_char_print(ctx, ' ');
        seam_str_out(ctx, U2_S_GET_WEAPON);
        const uint8_t idx = (uint8_t)seam_rand_byte(ctx) & 0x7u;
        if (idx == 0) {
            seam_str_out(ctx, U2_S_GET_EMPTY);
            seam_char_print(ctx, '!');
        } else {
            ctx->state.weapons_owned[idx] =
                u2_bcd_inc_sat(ctx->state.weapons_owned[idx], U2_SATURATE(ctx));
        }
    } else {
        /* asm/363B L_00389b: odd floor -> " ARMOUR". armour_owned[(rand & 3) - 1]
           (DS:96+idx, armour_owned starts at DS:97); slot 0 is "EMPTY!". */
        seam_str_out(ctx, U2_S_GET_ARMOUR);
        const uint8_t idx = (uint8_t)seam_rand_byte(ctx) & 0x3u;
        if (idx == 0) {
            seam_str_out(ctx, U2_S_GET_EMPTY);
            seam_char_print(ctx, '!');
        } else {
            ctx->state.armour_owned[idx - 1] =
                u2_bcd_inc_sat(ctx->state.armour_owned[idx - 1], U2_SATURATE(ctx));
        }
    }
    u2_cmd_done(ctx);
    return U2_CMD_DONE;
}

/* ------------------------------------------------------------- the light ---- */

/* asm/1151 runtime 0x1491. Zero -> IT'S DARK! and no decrement; otherwise
 * decrement, and if the decrement reached zero, TORCH BURNED OUT!. Both strings
 * carry a TRAILING 0x8d in the binary, so the CR follows the text. */
int u2_dungeon_light_step(u2_ctx *ctx)
{
    if (ctx->state.moved_flag == 0) {
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_ITS_DARK);
        return 0;
    }
    ctx->state.moved_flag--;
    if (ctx->state.moved_flag == 0) {
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_TORCH_BURNED_OUT);
    }
    return 1;
}

/* ------------------------------------------------------------ the renderer -- */

/*
 * asm/4B76 plots a pixel at (x + [DS:4BF], y + [DS:4C1]) and asm/67B3 is the
 * line. The live capture has both origin bytes at 16, so the view's own
 * coordinate space — x 0..0xFF from the XL/XR tables, y 0..0x7F from YT/YB —
 * lands on a 256x128 rectangle inset 16 pixels from the top-left of a 320x200
 * screen. That is a 1:1 mapping with no scaling, and it is the only reading that
 * makes the tables' own ranges come out inside the frame.
 *
 * asm/4B76 ORs both plane bits, so every line it draws is colour 3 (white) and
 * asm/4BAA, which inverts the mask, is how the original erases.
 */
#define DG_ORIGIN_X 16
#define DG_ORIGIN_Y 16

typedef struct dg_view {
    u2_frame *f;
    /* The dungeon geometry is authored in 320x200 CGA space. The transform maps a
     * logical point to physical pixels. Block mode (CGA, classic EGA) multiplies by
     * `scale` and stamps a scale x scale block. Fit mode (enhanced) maps by num/den
     * with a thick stamp, because the enhanced map window is 448 px wide against the
     * dungeon's 320 px logical width — no integer block scale fills it without
     * spilling into the side panel. */
    int fit;                  /* 0 = block mode, 1 = fit mode */
    int scale;                /* block mode: 1 in CGA, 2 in classic EGA */
    int ox, oy, num, den, thick;  /* fit mode: physical = o + logical*num/den */
    int d;                    /* the depth index, [DS:482] in the original */
    int x1, y1, x2, y2;       /* the line drawer's four registers, DS:479..47C.
                               * x1/y1 are the PEN, not a per-call start point: asm/67B3
                               * steps [DS:479]/[DS:47A] until they equal [DS:47B]/[DS:47C],
                               * so every line starts where the previous one ended unless the
                               * caller overwrites them. See dg_line. */
    const char *who;          /* diagnostic: which shape routine set the registers */
} dg_view;

/* Fill v's transform for the current art mode.
 *
 * The dungeon geometry is authored in the 320x200 CGA layout. In classic EGA the
 * frame is that layout doubled (640x400, scale 2), so block scale 2 fills the map
 * window exactly. Enhanced is NOT that: its map window is 448 px wide (14x32),
 * 1.4x the dungeon's 320 px logical width, so block scale 2 would spill the view
 * into the side panel and scale 1 leaves it CGA-sized. Enhanced therefore uses the
 * rational fit map (448/320) with a 2x2 stamp to match the doubled line weight,
 * vertically centred in the square window. This is our design, not a byte-faithful
 * EGA dungeon (the real EGA dungeon is uncaptured; see docs/enhanced-layout-plan.md
 * section 10 for the deferred real-3D route). */
static void dg_set_transform(dg_view *v, const u2_ctx *ctx, const u2_frame *f)
{
    if (ctx->enhanced_layout) {
        v->fit = 1;
        v->num = U2_ENH_MAP_COLS * 32;   /* 448 */
        v->den = U2_SCREEN_W;            /* 320 */
        v->thick = 2;
        v->ox = 0;
        /* Centre the 200-tall logical box (scaled to 280) in the 448-tall window. */
        v->oy = (U2_ENH_MAP_ROWS * 32 - U2_SCREEN_H * v->num / v->den) / 2;
    } else {
        v->fit = 0;
        v->scale = f->scale > 1 ? f->scale : 1;
    }
}

/* Diagnostic seam. U2_DG_LINES=<path> makes every line the dungeon renderer draws land in
 * a file, with the routine that drew it and the depth it was drawn at. It exists because
 * comparing two wireframes by eye kept producing confident wrong answers - the missing
 * bottom and right edges were measured at 495 px and localized to a band, but "which
 * segment is absent" cannot be answered from a picture, and a raster diff cannot tell "not
 * drawn" apart from "drawn somewhere else". It is what found the pen bug: the census is how
 * many lines are slanted, and on a correct level-0 frame only asm/66CB's chest lid is.
 * The pixel side needs no tool, just the longest ink runs in the window: a rectangle frame
 * is two full-width horizontals and two full-height verticals, and text is not.
 *
 * Env-gated and off by default so no normal run pays for it and no test sees it. */
static FILE *g_dg_lines;

static void dg_line(dg_view *v)
{
    if (g_dg_lines)
    {
        fprintf(g_dg_lines, "%s d=%d (%d,%d)->(%d,%d)\n",
                v->who ? v->who : "?", v->d,
                DG_ORIGIN_X + v->x1, DG_ORIGIN_Y + v->y1,
                DG_ORIGIN_X + v->x2, DG_ORIGIN_Y + v->y2);
        fflush(g_dg_lines);   /* a killed run must still leave its evidence on disk */
    }
    /* The degenerate case is not drawn, and must not move the pen either: asm/67B3's
     * opening guard at 0x69B4 returns before the stepper when x1==x2 && y1==y2. */
    if (v->x1 != v->x2 || v->y1 != v->y2) {
        if (v->fit)
            u2_frame_line_fit(v->f, DG_ORIGIN_X + v->x1, DG_ORIGIN_Y + v->y1,
                              DG_ORIGIN_X + v->x2, DG_ORIGIN_Y + v->y2,
                              U2_CGA_WHITE, v->ox, v->oy, v->num, v->den, v->thick);
        else
            u2_frame_line_scaled(v->f, DG_ORIGIN_X + v->x1, DG_ORIGIN_Y + v->y1,
                                 DG_ORIGIN_X + v->x2, DG_ORIGIN_Y + v->y2,
                                 U2_CGA_WHITE, v->scale);
    }

    /* asm/67B3 is a stepper, not a reader: L_006a4b does `adc %al,0x479` and L_006a8f
     * does `adc %al,0x47a` once per pixel of the major axis, so on return [DS:479] and
     * [DS:47A] hold the ENDPOINT. The four registers are a pen and a target, not two
     * independent points, and a shape routine that loads only 0x47B/0x47C (asm/6425 does
     * it three times running, asm/6291 twice) is drawing from wherever the last line
     * finished. Transcribing those loads as "carry x1/y1 unchanged" instead of "carry the
     * pen" is what turned asm/6425's right edge into a top-left-to-bottom-right diagonal
     * and its bottom edge into a left edge - the residual 495 px of missing ink, and the
     * crossing diagonal Paul saw, were one defect and not two. */
    v->x1 = v->x2;
    v->y1 = v->y2;
}

/* Tag each shape call with the routine that made the lines, for the U2_DG_LINES seam. */
#define DG_SH(v, fn) do { (v)->who = #fn; fn(v); } while (0)

/* The twelve shape routines and the eight depth tables, transcribed from the
 * asm tree by tools/gen_dungeon_draw.py. Regenerate that file; do not hand-edit. */
static void dg_6291(dg_view *); static void dg_6304(dg_view *);
static void dg_6377(dg_view *); static void dg_63ce(dg_view *);
static void dg_6425(dg_view *); static void dg_648d(dg_view *);
static void dg_64e2(dg_view *); static void dg_653f(dg_view *);
static void dg_6594(dg_view *); static void dg_660c(dg_view *);
static void dg_6684(dg_view *); static void dg_66cb(dg_view *);
#include "game/dungeon_draw.inc"

/* asm/6078. Three columns of cells walked out from the player: the centre
 * column starts on the player's own cell, the side columns start one cell to
 * either hand, and all three advance by the facing vector once per depth. The
 * centre cell is read twice, once masked to the terrain nibble and once to the
 * stamp nibble, which is the two-layers-in-one-byte fact made visible in the
 * renderer itself. The type lives in dungeon.h so dungeon3d.c shares the scan. */
void u2_dg_scan_build(const u2_ctx *ctx, const u2_overworld *ow, u2_dg_scan *s)
{
    int dx, dy;
    u2_dungeon_facing(ctx, &dx, &dy);
    const int fl = (int8_t)ctx->state.site_floor;
    const int px = ctx->state.sector_x, py = ctx->state.sector_y;

    int lx = px + dy, ly = py - dx;      /* DS:479 / DS:47A */
    int cx = px,      cy = py;           /* DS:47B / DS:47C */
    int rx = px - dy, ry = py + dx;      /* DS:47D / DS:47E */

    for (int d = 0; d < U2_DG_SAMPLE_DEPTH; d++) {
        const int a = lx & 0x3f, b = ly & 0x3f;
        s->left[d] = (uint8_t)(cell(ow, fl, a, b) & U2_DG_TERRAIN);
        lx = a + dx; ly = b + dy;

        const int e = cx & 0x3f, f = cy & 0x3f;
        const uint8_t c = cell(ow, fl, e, f);
        s->ctr[d] = (uint8_t)(c & U2_DG_TERRAIN);
        s->obj[d] = (uint8_t)(c & 0x07u);
        cx = e + dx; cy = f + dy;

        const int g = rx & 0x3f, h = ry & 0x3f;
        s->right[d] = (uint8_t)(cell(ow, fl, g, h) & U2_DG_TERRAIN);
        rx = g + dx; ry = h + dy;
    }
}

/* asm/68ED, asm/68A3 and asm/4BE0, transcribed.
 *
 * The record framing is the part that was not obvious, and it is one instruction:
 * asm/68ED reads the SECOND byte of each record twice from the same pointer and only
 * then advances, so a record is two bytes, not three —
 *     b0 = x, and x == 0 ends the list
 *     b1 = y in the low five bits, pattern index in the top three
 * asm/68A3 then scales both by four and passes the pattern index as bp, and asm/4BE0
 * looks the pattern up in cs:0x48D4 = ff 11 88 cc 33 00 00 00, which selects which of
 * the four pixels inside one video byte get lit. So one record paints a run of one to
 * four pixels, and a creature is a list of those runs.
 *
 * The base pointer is where the design shows. asm/68ED has two arms:
 *   di == 1 (nearest):  base = [DS:474] + (stamp << 8)   -> the type's own 256-byte block
 *   di >= 2 (further):  base = [DS:474] + cs:[0x697E+di] -> block 0's tail, no type term
 * That second arm looks like a dropped operand and is not one. Blocks 1..7 each hold one
 * full-detail creature; block 0's tail holds the ladder the far arm indexes — 24 records
 * at 0x080, 6 at 0x0C0, 4 at 0x0E0, 2 at 0x0F0, 1 at 0x0F8, which plot as a 6x4 blob, then
 * 2x3, 2x2, 1x2, 1x1. A deliberately typeless simplification series: the nearest creature
 * gets its own art and everything further out gets a generic silhouette, because at that
 * size the type would not be legible. Plotting the shapes is what established this, and it
 * is stronger evidence here than a screenshot would have been.
 */
static const uint8_t DG_PATTERN[8] = { 0xff, 0x11, 0x88, 0xcc, 0x33, 0x00, 0x00, 0x00 };
static const uint8_t DG_DEPTH_LADDER[8] = {
    0x00, 0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc
};

/* asm/4BE0 stores cs:[bp+0x48D4] VERBATIM into CGA 2bpp memory, twice: at di and at
 * di+0x50, and 0x50 = 80 = one row of 320/4, so the second store is the row below.
 * The byte is therefore not a sub-pixel mask and the pattern index is not a shape:
 * it is four 2-bit pixels, and every stroke is a 4x2 block.
 *
 * The table is self-modified at startup. The static bytes in the exe are
 * e8 08 0b 58 2d 49 54 00 and mean nothing; the runtime bytes, identical across six
 * independent captures, are ff 11 88 cc 33 00 00 00, which decode as
 *   ff -> white x4      11 -> black,cyan,black,cyan   88 -> magenta,black,magenta,black
 *   cc -> white,black,white,black   33 -> black,white,black,white   00 -> erase
 * So the pattern field is a COLOUR selector: that is why a creature is a cyan body
 * with a magenta mouth, and why three of the eight entries are black — drawing a
 * sprite clears pixels as well as setting them, and the store is unconditional.
 *
 * The colour index is the 2-bit value itself, because U2_CGA_BLACK/CYAN/MAGENTA/WHITE
 * are 0..3 in CGA order. */
static void dg_sprite_page(const dg_view *v, const uint8_t *strokes, int base)
{
    int p = 0;

    for (;;) {
        const int x = strokes[base + p];
        if (x == 0)
            return;
        const int b1 = strokes[base + p + 1];
        const int y = b1 & 0x1f;
        const int pat = DG_PATTERN[(b1 >> 5) & 7];
        p += 2;

        /* asm/68A3: ax = x*4, bx = y*4. asm/4BE0: di = (ax + origin_x) >> 2, so the
         * leftmost pixel of the byte is origin_x + x*4 and the table's high bit
         * (bits 7-6) is that leftmost pixel. */
        const int sx = DG_ORIGIN_X + x * 4;
        const int sy = DG_ORIGIN_Y + y * 4;
        for (int k = 0; k < 4; k++) {
            const uint8_t c = (uint8_t)((pat >> (6 - 2 * k)) & 3);
            for (int row = 0; row < 2; row++) {   /* the di+0x50 store draws 2 rows */
                if (v->fit)
                    u2_frame_pixel_fit(v->f, sx + k, sy + row, c,
                                       v->ox, v->oy, v->num, v->den, v->thick);
                else
                    u2_frame_pixel_scaled(v->f, sx + k, sy + row, c, v->scale);
            }
        }
    }
}

static void u2_dungeon_sprites(u2_ctx *ctx, u2_overworld *ow, const u2_dg_scan *s)
{
    if (!ow->world.strokes_loaded)
        return;                 /* no `monsters` in the content dir: draw no sprites */
    dg_view v = { .f = &ow->frame };
    dg_set_transform(&v, ctx, &ow->frame);

    /* asm/68ED starts by incrementing [DS:482], which asm/6144 left at the depth it
     * stopped on, and then walks di from 1 while di < that value. */
    const int lim = U2_DG_DRAW_DEPTH;
    for (int di = 1; di < lim; di++) {
        const int stamp = s->obj[di];
        if (stamp == 0)
            continue;
        if (di == 1) {
            const int blk = (stamp & (U2_STROKE_BLOCKS - 1)) * U2_STROKE_BLOCK;
            dg_sprite_page(&v, ow->world.strokes, blk);
        } else {
            dg_sprite_page(&v, ow->world.strokes, DG_DEPTH_LADDER[di & 7]);
        }
    }
}

/* asm/6144. One depth per iteration, [DS:482] is the loop variable and the same
 * value the shape routines index their tables with. */
static void dg_draw(dg_view *v, const u2_dg_scan *s)
{
    /* asm/6144 calls asm/6425 (runtime L_006625) in exactly two places: once at
     * runtime 0x634C before the loop is entered, and once at 0x635F inside the rock
     * branch. The open branch at 0x6392 never calls it. The first transcription called
     * it at the top of every iteration as well, which drew a depth frame for every open
     * cell — that is the nested-box over-draw, measured against the original at (8,5)
     * facing east as roughly twice the line crossings per row (13 vs 6 at y=30).
     * Same bounding box, so the tables and the origin were never the problem.
     * asm/6425 loads [DS:479] and [DS:47A] before its first call, so it is self-contained
     * and the pen it inherits does not matter — which is not true of the wall shapes. */
    DG_SH(v, dg_6425);

    for (;;) {
        const int d = v->d;
        const uint8_t c = s->ctr[d];

        if ((c & U2_DG_ROCK) == 0) {
            /* Open ahead: draw whatever the cell holds, then the side walls. */
            if (c & U2_DG_DOWN) DG_SH(v, dg_6594);
            if (c & U2_DG_UP)   DG_SH(v, dg_660c);
            else if (c & U2_DG_CHEST) DG_SH(v, dg_66cb);

            if (s->left[d] & U2_DG_ROCK) {
                DG_SH(v, dg_6377);
                if ((s->left[d] & U2_DG_CHEST) && !(s->left[d] & U2_DG_DOWN))
                    DG_SH(v, dg_64e2);
            } else {
                DG_SH(v, dg_6291);
            }

            if (s->right[d] & U2_DG_ROCK) {
                DG_SH(v, dg_63ce);
                if ((s->right[d] & U2_DG_CHEST) && !(s->right[d] & U2_DG_DOWN))
                    DG_SH(v, dg_653f);
            } else {
                DG_SH(v, dg_6304);
            }

            v->d = d + 1;
            if (v->d >= U2_DG_DRAW_DEPTH) return;
            continue;
        }

        /* Rock ahead. asm/6144 runtime 0x635F: the depth frame is drawn again here,
         * then three tests decide whether the corridor ends or the far face is drawn
         * and the walk continues. */
        DG_SH(v, dg_6425);
        if ((c & U2_DG_CHEST) == 0) return;
        if (c & U2_DG_DOWN) return;
        DG_SH(v, dg_648d);
        if (d != 0) return;
        v->d = d + 1;
        if (v->d >= U2_DG_DRAW_DEPTH) return;
    }
}

#undef DG_SH

void u2_dungeon_render(u2_ctx *ctx, u2_overworld *ow, int lit)
{
    if (!lit) return;         /* asm/6000's `cmpb $0x0,0x26 ; je` — the darkness */

    u2_dg_scan s;
    u2_dg_scan_build(ctx, ow, &s);

    /* Enhanced mode draws the perspective corridor instead of the CGA wireframe
     * (docs/enhanced-dungeon-3d.md). CGA and classic EGA keep the wireframe. */
    if (ctx->enhanced_layout) {
        u2_dungeon_render3d(ctx, ow, &s);
        return;
    }

    dg_view v;
    memset(&v, 0, sizeof v);
    v.f = &ow->frame;
    dg_set_transform(&v, ctx, &ow->frame);
    v.d = 0;
    /* Diagnostic: U2_DG_LINES=<path> records every drawn line with its routine and depth. */
    {
        const char *lp = getenv("U2_DG_LINES");
        if (lp && lp[0] && !g_dg_lines) g_dg_lines = fopen(lp, "w");
    }
    dg_draw(&v, &s);

    /* asm/68ED: the creature sprites. Transcribed rather than invented — see
     * u2_dungeon_sprites for the record framing and for the one reading a capture
     * still owes us. */
    u2_dungeon_sprites(ctx, ow, &s);
}

/* Dev-only side-by-side comparison. When U2_DG_DUAL is set, the enhanced dungeon
 * screen also shows the original CGA wireframe (the classic path: wireframe plus
 * creature sprites, at native 320x200) blitted into the right-hand panel, so the
 * new 3D renderer and the original can be watched live from the same game state.
 * It runs after the text layer and clears the stats boxes out of the way; the CMD
 * box below is left intact. This is a development aid, not a shipped layout. */
int u2_dg_dual_mode(void)
{
    const char *e = getenv("U2_DG_DUAL");
    return e && e[0] && e[0] != '0';
}

void u2_dungeon_render_classic_view(u2_ctx *ctx, u2_overworld *ow,
                                    int ox, int oy, int clear_bottom)
{
    if (!ctx->state.moved_flag) return;   /* the darkness, same gate as the render */
    u2_dg_scan s;
    u2_dg_scan_build(ctx, ow, &s);

    u2_frame tf;
    if (u2_frame_init(&tf, 1) != 0) return;
    u2_frame_clear(&tf, U2_CGA_BLACK);

    /* Run the classic path verbatim into the temp frame: point the overworld frame
     * at it and clear enhanced_layout so dg_set_transform picks block scale 1. */
    u2_frame saved = ow->frame;
    ow->frame = tf;
    int enh = ctx->enhanced_layout;
    ctx->enhanced_layout = 0;
    dg_view v;
    memset(&v, 0, sizeof v);
    v.f = &ow->frame;
    dg_set_transform(&v, ctx, &ow->frame);
    v.d = 0;
    dg_draw(&v, &s);
    u2_dungeon_sprites(ctx, ow, &s);
    ctx->enhanced_layout = enh;
    ow->frame = saved;

    /* Block the stats boxes, then blit the CGA view into the panel. The wireframe
     * is CGA index 3; cga_idx maps it to EGA white when the frame is CGA-on-EGA. */
    for (int y = oy; y < clear_bottom && y < ow->frame.h; y++)
        for (int x = ox; x < ow->frame.w; x++)
            u2_frame_put(&ow->frame, x, y, U2_CGA_BLACK);
    for (int y = 0; y < tf.h; y++)
        for (int x = 0; x < tf.w; x++)
            u2_frame_put(&ow->frame, ox + x, oy + y,
                         u2_frame_cga_idx(&ow->frame, tf.px[y][x]));
    u2_frame_free(&tf);
}

int u2_dungeon_key(u2_ctx *ctx, u2_overworld *ow, uint8_t ch)
{
    switch (ch) {
    case U2_KEY_UP:    u2_dungeon_advance(ctx, ow); break;
    case U2_KEY_DOWN:  u2_dungeon_retreat(ctx, ow); break;
    case U2_KEY_LEFT:  u2_dungeon_turn(ctx, 1); u2_cmd_done(ctx); break;
    case U2_KEY_RIGHT: u2_dungeon_turn(ctx, 0); u2_cmd_done(ctx); break;
    default: return 0;
    }
    u2_dungeon_tick(ctx, ow);
    return 1;
}

/* asm/1151's tail, in order: the light counter, then asm/12DB with 0x10.
 * asm/12DB's whole body is `mov [DS:20],al` and a packed-decimal subtract of that
 * from the food triple DS:53/54/55, so a dungeon turn costs ten food and a
 * starvation borrow falls into the same death tail the overworld uses. */
static int dg_inspect(void);

void u2_dungeon_tick(u2_ctx *ctx, u2_overworld *ow)
{
    /* asm/1151 runs the spawner at runtime 0x1360 and the sweep immediately after it,
     * both before the light tail at 0x1491, so an emptied slot is refilled on the
     * following turn rather than only on entry. That is what the bytes do and it is
     * why a dungeon cannot be cleared permanently by fighting. */
    if (!dg_inspect()) u2_dungeon_creatures(ctx, ow);
    u2_dungeon_light_step(ctx);
    (void)u2_overworld_spend_food(ctx, 0x10u);
    u2_dungeon_probe(ctx, ow);
}

/* Dev affordance (U2_DG_INSPECT=1): build a big open hall with one creature of
 * each stamp 1..7 lined up across the view, plus a chest and both ladders, so
 * every piece of dungeon art can be eyeballed in a single frame. It overwrites
 * the loaded floor in memory only — nothing is saved — and the creature sweep is
 * suppressed while it is on so the subjects hold still. Not game behaviour.
 *
 * The value decides, not the presence, and that is a fix rather than a tidiness.
 * This tested `getenv(...) ? 1 : 0` from 2026-11-14 to 2026-11-23, and deploy/novnc/
 * up.sh passes `-e U2_DG_INSPECT=0` unconditionally, so every noVNC session booted
 * into the hall and `--inspect` was indistinguishable from not asking for it. A
 * deploy layer that always sets a switch makes presence-based truthiness always
 * true; u2_dg_dual_mode below was already reading the value.
 *
 * No cache. A `static int cached` here made the switch readable exactly once per
 * process, so no in-process test could present unset / "" / "0" / "1" and see four
 * answers -- which is how the presence bug stayed untested for nine days. getenv is
 * called per turn and per boot, never per pixel, so the cache bought nothing. */
static int dg_inspect(void)
{
    const char *e = getenv("U2_DG_INSPECT");
    return e && e[0] && e[0] != '0';
}

int u2_dungeon_inspect(void) { return dg_inspect(); }

static void dg_inspect_setup(u2_ctx *ctx, u2_overworld *ow)
{
    const int fl = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            u2_world_dungeon_put(&ow->world, fl, x, y,
                                 (x == 0 || x == 15 || y == 0 || y == 15) ? 0x80 : 0x00);
    /* The orc one cell dead ahead for a hero shot; stamps 2..7 in a back row. */
    u2_world_dungeon_put(&ow->world, fl, 3, 5, 1);
    for (int s = 2; s <= 7; s++)
        u2_world_dungeon_put(&ow->world, fl, 8, s, (uint8_t)s);
    u2_world_dungeon_put(&ow->world, fl, 10, 5, 0x40);   /* chest   */
    u2_world_dungeon_put(&ow->world, fl, 13, 3, 0x10);   /* up ladder   */
    u2_world_dungeon_put(&ow->world, fl, 13, 7, 0x20);   /* down ladder */
    ctx->state.sector_x = 2; ctx->state.sector_y = 5;
    ctx->state.site_floor = 0;
    ctx->state.face_dx = 1; ctx->state.face_dy = 0;
    ctx->state.moved_flag = 0x96;   /* a full torch (the aliased light counter), so the
                                     * entry light_step decrement leaves it lit */
}

void u2_dungeon_enter(u2_ctx *ctx, u2_overworld *ow)
{
    /* The capture is unambiguous about the light: entering a dungeon zeroes
     * [DS:26] and the screen is dark, and the value SURVIVES leaving — the first
     * draft of that finding read the surviving value as persistence and was wrong.
     * Entry is what clears it. */
    ctx->state.moved_flag = 0;
    ctx->state.site_floor = 0;
    /* The original enters a dungeon FACING EAST.
     *
     * Two independent captures agree. captures/spice86/dungeon-35-live/live-ds.bin reads
     * DS:00..03 = 05 05 01 00, and a 2026-10-29 capture driven the way a player goes in -
     * wilderness sector 30, gate cell (25,5), press E - reads the same 05 05 01 00, with
     * DS:483..484 copying it out as 01 00. The north value this line used to carry was an
     * assumption that no capture had ever checked.
     *
     * It is not cosmetic. asm/6078 walks its three depth columns along the facing, so a
     * wrong entry facing renders a different corridor from the one the player is standing
     * in, and that is the best current explanation for the residual wireframe difference
     * against the original.
     *
     * Caveat kept honest: both observations are sector 35 caves. If a tower capture ever
     * shows a different entry facing, this is the line to revisit, with the bytes.
     */
    ctx->state.face_dx = (uint8_t)1;                      /* east */
    ctx->state.face_dy = 0;
    u2_dungeon_probe(ctx, ow);
    /* Stamp everything the loader authored BEFORE the spawner runs, because
     * asm/68ED reads the grid stamps and not the creature arrays: an authored
     * creature that is never stamped is invisible even though it will happily hit
     * you. asm/147F then fills the remaining empty slots and stamps its own. */
    for (int i = 0; i < 32; i++) {
        if (ctx->state.mon_type[i] == 0) continue;
        u2_world_dungeon_put(&ow->world, (int8_t)ctx->state.site_floor,
                             ctx->state.mon_x[i], ctx->state.mon_y[i],
                             ctx->state.mon_type[i]);
    }
    u2_dungeon_spawn(ctx, ow);
    if (dg_inspect()) dg_inspect_setup(ctx, ow);
    /* The entry frame is dark, and asm/1151 says so on the turn that follows the
     * load: captures/spice86/dungeon-35-live and tower-34 both record IT'S DARK!
     * on entry with [DS:26] at zero. */
    u2_dungeon_light_step(ctx);
}

/* ------------------------------------------------------------- creatures ---- */

/* asm/50D8_sign_of_al. The value reaching the store is a unit step, never a distance. */
static int dg_sign(uint8_t al)
{
    if (al == 0) return 0;
    return ((int8_t)al < 0) ? -1 : 1;
}

/* asm/143C -> asm/144C -> asm/1451. The Phase 2 map cut one function into three
 * fragments, which is the same cut-in-half failure that hid cmd_I, cmd_D and cmd_K;
 * read alone, asm/143C looks like a floor test that returns a magic 0xFF and asm/144C
 * like a one-instruction stub.
 *
 * Together they are: if the creature's floor byte is not the player's floor, return
 * 0xFF; otherwise return the cell byte at (nx, ny) ON THE CREATURE'S FLOOR, with ZF
 * set iff it is zero. asm/1451's own header has the addressing: page base + floor*256
 * + y*16 + x, formed in one 8-bit register so an out-of-range coordinate wraps inside
 * its page instead of walking into the next one.
 *
 * The 0xFF return is indistinguishable from a cell that literally holds 0xFF. Named in
 * the header rather than patched, because patching it would be inventing a third answer
 * the original does not have. */
static int dg_probe_cell(const u2_ctx *ctx, const u2_overworld *ow,
                         int di, int nx, int ny)
{
    const int fl = (int8_t)ctx->state.mon_kind[di];
    if (fl != (int8_t)ctx->state.site_floor) return 0xFF;
    return u2_world_dungeon_at(&ow->world, fl, nx, ny);
}

/* asm/12DB_dungeon_events runtime 0x1508: a creature whose candidate step is the
 * PLAYER's cell.
 *
 * This is the answer to "should they attack me on contact", and the answer is no, not in
 * the general case. The bytes roll once and only act on a roll under 0x40 — one attempt in
 * four — then switch on the creature's dungeon type, and only three of the seven types do
 * anything at all:
 *
 *   type 2  runtime 0x13AB  YOUR TORCH IS BLOWN OUT!   zeroes the light counter, but ONLY
 *                           if it is already non-zero; in the dark the arm prints nothing
 *                           and changes nothing, so a gremlin in a pitch-black corridor
 *                           is silent and that silence is the original's.
 *   type 5  runtime 0x13DA  A GREMLIN STOLE SOME FOOD!  one BCD unit off the food triple.
 *   type 7  runtime 0x1410  YOU FEEL A STRONG MAGIC!    paral_sleep := rand & 7.
 *   else                    nothing.
 *
 * So the dungeon's danger is your light, your food and your consciousness, not your hit
 * points. Melee damage in a dungeon comes from your own ATTACK exchanges, exactly as the
 * overworld has no attack-on-contact (see ai.h). Every effect prints, which is what keeps
 * this project's "no silent mutation" rule satisfied by construction here.
 *
 * NOT modelled, and deliberately: the roll >= 0x40 branch does not go to the next slot, it
 * falls into runtime 0x153E, a second event block that re-checks the floor, calls
 * asm/60AC, rolls, masks to 0..7 and compares against [DS:62] — which state.h names
 * worn_armour. That is an armour-gated creature spell and it is a separate decode with
 * three unnamed helpers behind it (asm/60AC, asm/60F2, asm/4D26). Treating a failed roll
 * as "nothing happened" is a known understatement, named here rather than hidden.
 *
 * asm/5C95, called after each of the three prints, is a seven-byte gate on [DS:54D] and is
 * not an effect; the engine's print seam already owns message display. */
static void dg_contact(u2_ctx *ctx, u2_dg_ai_report *rep, int di)
{
    u2_state *st = &ctx->state;

    /* runtime 0x1509. */
    if (seam_rand_byte(ctx) >= 0x40) { rep->contacts++; return; }

    /* runtime 0x1518: the creature has to be on the floor the player is standing on. */
    if ((int8_t)st->mon_kind[di] != (int8_t)st->site_floor) return;

    switch (st->mon_type[di]) {
    case 2:
        /* runtime 0x13AB. The zero test comes FIRST: already dark means no message. */
        if (st->moved_flag == 0) break;
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_TORCH_BLOWN_OUT);
        st->moved_flag = 0;
        rep->effects++;
        break;
    case 5:
        /* runtime 0x13DA: `stc; cmc` clears CF, then `sbb $0x1 / das / cmc` is the usual
         * BCD subtract-by-one, on the low digit of the food triple. */
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_GREMLIN_STOLE);
        (void)u2_overworld_spend_food(ctx, 0x01u);
        rep->effects++;
        break;
    case 7:
        /* runtime 0x1410: DS:26D is paral_sleep, the sleep-turns counter state.h already
         * carries for the spell that writes it. */
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_STRONG_MAGIC);
        st->paral_sleep = (uint8_t)(seam_rand_byte(ctx) & 0x07u);
        rep->effects++;
        break;
    default:
        break;
    }
    rep->contacts++;
}

u2_dg_ai_report u2_dungeon_creatures(u2_ctx *ctx, u2_overworld *ow)
{
    u2_state *st = &ctx->state;
    u2_dg_ai_report rep = { 0 };
    const int px = ctx->state.sector_x, py = ctx->state.sector_y;

    /* asm/1151 runtime 0x1360. */
    u2_dungeon_spawn(ctx, ow);

    /* Slots 31 down to 1; slot 0 is never examined, the same blind spot as the
     * overworld sweep at asm/0A6D and asm/23E6. */
    for (int di = 0x1f; di >= 1; di--) {
        if (st->mon_type[di] == 0) continue;          /* runtime 0x1370 */
        rep.examined++;

        int sx = dg_sign((uint8_t)(px - st->mon_x[di]));
        int sy = dg_sign((uint8_t)(py - st->mon_y[di]));

        /* runtime 0x1397: `cmp $0xf,%al ; jae` past the negation. */
        if (st->mon_hp[di] < 0x0f) {
            sx = -sx;
            sy = -sy;
            rep.fled++;
        }

        int done = 0;
        for (int axis = 0; axis < 2 && !done; axis++) {
            const int step = (axis == 0) ? sx : sy;
            if (step == 0) continue;                  /* 0x13BB / 0x13F5 */

            const uint8_t nx = (uint8_t)(st->mon_x[di] + (axis == 0 ? step : 0));
            const uint8_t ny = (uint8_t)(st->mon_y[di] + (axis == 1 ? step : 0));

            /* runtime 0x13CF: `cmp 0x0,%al` is a MEMORY operand against the player's
             * cell, not a literal 0 — this tree writes immediates with `$`. Reaching
             * the player is asm/12DB's problem and is not decoded, so the creature
             * does not move. Same outcome the overworld sweep reaches on contact. */
            if (nx == px && ny == py) { dg_contact(ctx, &rep, di); done = 1; break; }

            const int r = dg_probe_cell(ctx, ow, di, nx, ny);
            if (r == 0) {
                /* fallthrough to the move */
            } else if (r == 0xFF) {
                rep.off_floor++;
                continue;                             /* 0x13E9 -> try the other axis */
            } else if (r & 0x7f) {
                /* The original goes to the move path here, and the move path is what
                 * rejects on the low three bits — so a cell occupied by another
                 * creature ends this creature's turn rather than trying the other
                 * axis. Rock (0x80) has a zero low nibble after masking and falls
                 * through to try the other axis instead. Asymmetric, and faithful. */
                if (r & 0x07) { rep.bump++; done = 1; break; }
            } else {
                continue;                             /* 0x80 rock: other axis */
            }

            /* runtime 0x1437. The nibble dance, in the original's order: stash the
             * destination in the prev_x/prev_y scratch arrays, point the cell cursor at
             * the OLD cell and clear its low nibble, install the destination, then point
             * at the NEW cell and OR the type in. DS:1F7/DS:217 are pure scratch here and
             * the engine has no fields for them, which is the one part of this span the
             * transcription does not mirror byte for byte. */
            const int fl = (int8_t)st->mon_kind[di];
            const int ox = st->mon_x[di], oy = st->mon_y[di];
            u2_world_dungeon_put(&ow->world, fl, ox, oy,
                                 u2_world_dungeon_at(&ow->world, fl, ox, oy) & 0xF0);
            st->mon_x[di] = nx;
            st->mon_y[di] = ny;
            u2_world_dungeon_put(&ow->world, fl, nx, ny,
                                 u2_world_dungeon_at(&ow->world, fl, nx, ny)
                                     | st->mon_type[di]);
            rep.moved++;
            done = 1;
        }
        (void)done;
    }
    return rep;
}

/* asm/147F, validated 54/54 slots by tools/dungeon_spawn_check.py against
 * captures/spice86/dungeon-35-live and airport32.
 *
 * Rewritten 2026-10-29 after the owner reported monsters spawning too close. Three
 * mistakes in the first version, all of them visible in play:
 *
 *   THE STAMP WENT ON THE WRONG FLOOR. The original's cell pointer comes from asm/144C,
 *   which takes the floor from [DI+0x1B7], and that byte was just set to slot>>1 at
 *   runtime 0x16D6. So a dungeon populates all sixteen pages, roughly two creatures per
 *   page. The engine stamped everything on the PLAYER's floor instead, which is what
 *   "they spawn too close" actually was: creatures belonging to floors 1..15 were being
 *   drawn into your corridor.
 *
 *   THERE WAS NO TERRAIN GATE. Runtime 0x16DA calls asm/144C and takes `jne` to the next
 *   slot when the cell is non-zero, so a candidate must be EMPTY — not rock, not a chest,
 *   not another creature's stamp.
 *
 *   THE STAMP OVERWROTE THE CELL INSTEAD OF ORING INTO IT. Runtime 0x1721 is
 *   `or [DI+0x197],%al`. With the gate in place the two are equivalent for a legal spawn,
 *   but without it a write over 0x80 turned solid rock into a walkable stamped cell and
 *   could open phantom corridors.
 *
 * Also: one coordinate draw per slot, and a rejected slot is SKIPPED rather than retried.
 * The first version retried up to 32 times, spending RNG the original never spends and
 * diverging the whole dungeon's stream. Roll order is x, y, then the type.
 *
 * The formulas are unchanged and still agree with both the captures and the bytes: type
 * = (rand & 7) + 1 with 7 rejected and accepted iff 2*(type-1) <= slot>>1, and
 * HP = ((2*type + (slot>>1)) * 8) | 0x10. */
int u2_dungeon_spawn(u2_ctx *ctx, u2_overworld *ow)
{
    u2_state *st = &ctx->state;
    int filled = 0;

    for (int slot = 0x1f; slot >= 1; slot--) {
        if (st->mon_type[slot] != 0) continue;      /* runtime 0x168C */

        const unsigned floor = (unsigned)(slot >> 1);

        /* runtime 0x16A1 and 0x16B9: odd coordinates in 1..15. */
        const unsigned x = ((seam_rand_byte(ctx) & 7u) * 2u) | 1u;
        const unsigned y = ((seam_rand_byte(ctx) & 7u) * 2u) | 1u;

        /* runtime 0x16DA: the candidate cell must be empty on THIS creature's floor. */
        if (u2_world_dungeon_at(&ow->world, (int)floor, (int)x, (int)y) != 0) continue;

        /* runtime 0x16DF: the type roll, and the floor is the difficulty ladder. */
        const unsigned v = seam_rand_byte(ctx) & 7u;
        if (v == 7u) continue;
        if (2u * v > floor) continue;

        const unsigned type = v + 1u;

        st->mon_x[slot]    = (uint8_t)x;
        st->mon_y[slot]    = (uint8_t)y;
        st->mon_kind[slot] = (uint8_t)floor;
        st->mon_type[slot] = (uint8_t)type;
        st->mon_hp[slot]   = (uint8_t)((((2u * type + floor) * 8u) | 0x10u) & 0xffu);

        /* runtime 0x1718..0x1729: re-point the cell cursor at the destination and OR the
         * type into the low nibble, on the creature's own page. */
        u2_world_dungeon_put(&ow->world, (int)floor, (int)x, (int)y,
                             u2_world_dungeon_at(&ow->world, (int)floor, (int)x, (int)y)
                                 | (uint8_t)type);
        filled++;
    }
    return filled;
}
