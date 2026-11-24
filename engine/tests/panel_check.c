/*
 * panel_check.c — the enhanced panel's geometry and no-silent-mutation gate.
 *
 * Why this exists: the enhanced panel is drawn on screen and had no assertion
 * anywhere. S1..S5 of docs/enhanced-layout-plan.md landed as code, and the plan's
 * own slice table names the missing gates. Every defect this file would have caught
 * is one the layout header already records as having happened once:
 *
 *   - a box edge landing between text cells, so the border "went missing"
 *   - a y that is not a whole text cell, so the label drew above its own bar
 *   - text drawn through the wrong frame API, landing off the right-hand edge
 *   - a palette index assumed rather than looked up, drawing ink the same colour
 *     as its background
 *   - a border sharing a row with the glyph drawn over it, so each silently ate
 *     part of the other (the bottom bar, found and ruled 2026-11-25)
 *
 * All four are invisible at runtime — the frame clips silently and the wrong colour
 * is simply not there — so none of them can be found by looking at the screen.
 *
 * Two groups:
 *   A. geometry arithmetic, evaluated from the macros alone. Catches drift between
 *      what core/layout.h documents and what it now computes.
 *   B. a live u2_panel_draw into a real frame, with every colour distinct so each
 *      painted pixel is attributable to the thing that painted it.
 *
 * Exit codes: 0 pass, 1 a real failure, 77 cannot run (never here; kept for
 * symmetry with the other checks).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/frame.h"
#include "core/layout.h"
#include "core/panel.h"
#include "core/ctx.h"
#include "core/state.h"

static int checks, fails;

#define CK(cond, label)                                                       \
    do {                                                                      \
        checks++;                                                             \
        if (!(cond)) { fails++; printf("FAIL  %s\n", (label)); }              \
    } while (0)

/* ------------------------------------------------------- geometry model */

typedef struct {
    const char *name;
    int x, y, w, h;
} boxrec;

/* The four boxes the panel owns, read from the same macros the renderer uses. */
static boxrec b_vitals(void) { return (boxrec){"vitals", U2_ENH_VIT_X, U2_ENH_VIT_Y, U2_ENH_VIT_W, U2_ENH_VIT_H}; }
static boxrec b_stats(void)  { return (boxrec){"stats",  U2_ENH_STA_X, U2_ENH_STA_Y, U2_ENH_STA_W, U2_ENH_STA_H}; }
static boxrec b_cmd(void)    { return (boxrec){"cmd",    U2_ENH_CMD_X, U2_ENH_CMD_Y, U2_ENH_CMD_W, U2_ENH_CMD_H}; }
static boxrec b_bar(void)    { return (boxrec){"bar",    U2_ENH_BAR_X, U2_ENH_BAR_Y, U2_ENH_BAR_W, U2_ENH_BAR_H}; }
static boxrec b_map(void)    { return (boxrec){"map",    U2_ENH_MAP_X, U2_ENH_MAP_Y, U2_ENH_MAP_COLS * 32, U2_ENH_MAP_ROWS * 32}; }

/* Predicates are functions rather than inline assertions so --selftest can feed
 * them values that must be rejected. A gate whose predicates have never returned
 * false has not been shown to be a gate. */

static int box_fits(const boxrec *b, int W, int H)
{
    return b->w > 0 && b->h > 0 &&
           b->x >= 0 && b->y >= 0 &&
           b->x + b->w <= W && b->y + b->h <= H;
}

static int boxes_overlap(const boxrec *a, const boxrec *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w &&
           a->y < b->y + b->h && b->y < a->y + a->h;
}

static int on_grid(int v, int cell) { return cell > 0 && (v % cell) == 0; }

/* Text inside a box gets `w` minus a pad on each side, divided into whole cells.
 * Anything wider would clip mid-glyph, which is the border-vanishing defect. */
static int cols_fit(int w, int cols, int pad, int cell)
{
    return cols > 0 && 2 * pad + cols * cell <= w;
}

/* Strictly inside the border ring. Text may legitimately reach the pad, so this
 * is the bound an overflow has to break to count as escaped. */
static int box_interior(const boxrec *b, int x, int y)
{
    return x > b->x && y > b->y && x < b->x + b->w - 1 && y < b->y + b->h - 1;
}

/* Whole box, border included. Used for the bar, which is full-width chrome outside
 * the three panel boxes and so is not held to the "text only inside a box interior"
 * rule the three boxes are. */
static int box_contains(const boxrec *b, int x, int y)
{
    return x >= b->x && y >= b->y && x < b->x + b->w && y < b->y + b->h;
}

/* --------------------------------------------------- A: geometry only */

static void group_a(void)
{
    const boxrec vit = b_vitals(), sta = b_stats(), cmd = b_cmd(), bar = b_bar();
    const boxrec map = b_map();
    const int W = U2_ENH_W, H = U2_ENH_H;

    CK(box_fits(&vit, W, H), "A1 vitals box inside frame");
    CK(box_fits(&sta, W, H), "A1 stats box inside frame");
    CK(box_fits(&cmd, W, H), "A1 cmd box inside frame");
    CK(box_fits(&bar, W, H), "A1 bar inside frame");
    CK(box_fits(&map, W, H), "A1 map region inside frame");

    /* The bar is full-width chrome and deliberately starts left of the panel, so
     * it is checked against the map vertically, not horizontally. */
    CK(!boxes_overlap(&vit, &sta), "A2 vitals and stats do not overlap");
    CK(!boxes_overlap(&vit, &cmd), "A2 vitals and cmd do not overlap");
    CK(!boxes_overlap(&sta, &cmd), "A2 stats and cmd do not overlap");
    CK(!boxes_overlap(&cmd, &bar), "A2 cmd and bar do not overlap");
    CK(!boxes_overlap(&vit, &map), "A2 vitals does not overlap the map");
    CK(!boxes_overlap(&sta, &map), "A2 stats does not overlap the map");
    CK(!boxes_overlap(&cmd, &map), "A2 cmd does not overlap the map");
    CK(bar.y >= map.y + map.h, "A2 bar starts at or below the map bottom");

    CK(vit.x >= U2_ENH_PANEL_X, "A3 vitals starts at or right of the panel column");
    CK(sta.x >= U2_ENH_PANEL_X, "A3 stats starts at or right of the panel column");
    CK(cmd.x >= U2_ENH_PANEL_X, "A3 cmd starts at or right of the panel column");
    CK(U2_ENH_PANEL_X == map.x + map.w,
       "A3 panel column begins exactly where the map ends");

    CK(cmd.y + cmd.h == map.y + map.h,
       "A5 cmd bottom is flush with the map bottom, as layout.h states");

    CK(on_grid(vit.x, U2_ENH_TEXT_W) && on_grid(vit.w, U2_ENH_TEXT_W),
       "A6 vitals x and w are whole text cells");
    CK(on_grid(sta.x, U2_ENH_TEXT_W) && on_grid(sta.w, U2_ENH_TEXT_W),
       "A6 stats x and w are whole text cells");
    CK(on_grid(cmd.x, U2_ENH_TEXT_W) && on_grid(cmd.w, U2_ENH_TEXT_W),
       "A6 cmd x and w are whole text cells");
    CK(on_grid(bar.y, U2_ENH_TEXT_H) && on_grid(bar.h, U2_ENH_TEXT_H),
       "A6 bar y and h are whole text cells (the off-grid bar put a label 9px off)");

    CK(cols_fit(vit.w, U2_ENH_VIT_COLS, U2_ENH_BOX_PAD, U2_ENH_TEXT_W),
       "A7 vitals interior holds its declared text columns");
    CK(cols_fit(sta.w, U2_ENH_STA_COLS, U2_ENH_BOX_PAD, U2_ENH_TEXT_W),
       "A7 stats interior holds its declared text columns");
    CK(cols_fit(cmd.w, U2_ENH_CMD_COLS, U2_ENH_BOX_PAD, U2_ENH_TEXT_W),
       "A7 cmd interior holds its declared text columns");

    /* The cell counts layout.h documents in prose. If the font or the frame
     * changes, these move, and the prose has to be corrected in the same commit. */
    CK(U2_ENH_PANEL_W / U2_ENH_TEXT_W == 36, "A8 panel is 36 text cells wide");
    CK(U2_ENH_VIT_COLS == 15, "A8 vitals interior is 15 columns");
    CK(U2_ENH_STA_COLS == 14, "A8 stats interior is 14 columns");
    CK(U2_ENH_CMD_COLS == 32, "A8 cmd interior is 32 columns");

    CK(cmd.y >= U2_ENH_TOP_H, "A9 the reserved band between the top boxes and the log is real");
    CK(U2_ENH_BAR_Y + U2_ENH_BAR_H <= H, "A10 the bar leaves room inside the frame");

    CK(U2_ENH_PLAYER_COL < U2_ENH_MAP_COLS && U2_ENH_PLAYER_ROW < U2_ENH_MAP_ROWS,
       "A11 the player centre is inside the map grid");

    /* A macro nobody reads is a number that will be believed anyway. */
    CK(U2_ENH_BOX_BORDER == 1, "A12 declared border thickness matches what box() draws");
}

/* ------------------------------------------------------ B: live drawing */

/* Every colour distinct, so a painted pixel names its author. The renderer picks
 * colours by scanning a palette in production; here they are set by hand precisely
 * so that text and border are NOT the same index, which is the case the production
 * picker collapses and the case where an overflow becomes visible at all. */
enum {
    C_BACKDROP = 1, C_BOXBG = 0, C_BORDER = 15,
    C_TEXT = 14, C_ACCENT = 5, C_BAR = 2, C_SENTINEL = 3
};

static void paint(u2_frame *f, int x0, int y0, int x1, int y1, uint8_t c)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            u2_frame_put(f, x, y, c);
}

static int count_colour(const u2_frame *f, const boxrec *b, int interior, uint8_t col)
{
    int n = 0;
    for (int y = b->y; y < b->y + b->h; y++)
        for (int x = b->x; x < b->x + b->w; x++) {
            const int in = interior ? box_interior(b, x, y) : 1;
            if (in && f->px[y][x] == col) n++;
        }
    return n;
}

/* Every pixel of the four border lines must still be the border colour. Text
 * paints its own background across the cells it occupies, so a glyph that runs
 * over a border erases it silently and this is the only thing that notices. */
static int border_intact(const u2_frame *f, const boxrec *b, uint8_t border)
{
    for (int x = b->x; x < b->x + b->w; x++)
        if (f->px[b->y][x] != border || f->px[b->y + b->h - 1][x] != border)
            return 0;
    for (int y = b->y; y < b->y + b->h; y++)
        if (f->px[y][b->x] != border || f->px[y][b->x + b->w - 1] != border)
            return 0;
    return 1;
}

static int group_b(void)
{
    u2_frame f;
    memset(&f, 0, sizeof f);
    if (u2_frame_init_size(&f, U2_ENH_W, U2_ENH_H, U2_ENH_SCALE) != 0) {
        printf("cannot allocate the enhanced frame\n");
        return 77;
    }

    u2_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    /* A character with something in every field the panel reads, so a field that
     * silently stopped being drawn would show as missing ink rather than as a
     * zero that happens to be invisible. */
    memcpy(ctx.state.name, "GABRIEL", 8);
    ctx.state.world = 0;
    ctx.state.sector_first = 3;
    ctx.state.readied_weapon = 1;

    u2_panel_colours c;
    c.backdrop = C_BACKDROP; c.box_bg = C_BOXBG; c.border = C_BORDER;
    c.text = C_TEXT; c.accent = C_ACCENT; c.bar = C_BAR;

    const boxrec vit = b_vitals(), sta = b_stats(), cmd = b_cmd(), bar = b_bar(), map = b_map();

    /* Guard the map: the panel owns the column right of it and nothing else. */
    paint(&f, 0, 0, map.w, map.h, C_SENTINEL);
    const int w0 = f.w, h0 = f.h, s0 = f.scale;
    const uint8_t fg0 = f.text_fg, bg0 = f.text_bg;

    const char *log[] = { "CMD: ATTACK--EAST--HIT!!!", "A GREMLIN STOLE SOME FOOD!",
                          "NO DOOR THERE!" };
    u2_panel_draw(&f, &ctx, NULL, &c, -1, log, 3);

    CK(border_intact(&f, &vit, C_BORDER), "B1 vitals border intact after drawing");
    CK(border_intact(&f, &sta, C_BORDER), "B1 stats border intact after drawing");
    CK(border_intact(&f, &cmd, C_BORDER), "B1 cmd border intact after drawing");
    /* The bar is a filled rule, not a box (owner ruling 2026-11-25), so it must carry
     * no border at all. This is the assertion that replaces the two NOTEs this gate
     * first raised: a border inside a one-cell band is a border its own label
     * overwrites, and on the shipped build the label ate 42 px of the top border row
     * and 112 px of the bottom. Re-boxing the bar goes red here. */
    CK(count_colour(&f, &bar, 0, C_BORDER) == 0,
       "B1 bar draws no border (a one-cell band cannot hold a border and its label)");

    int touched = 0;
    for (int y = 0; y < map.h; y++)
        for (int x = 0; x < map.w; x++)
            if (f.px[y][x] != C_SENTINEL) touched++;
    CK(touched == 0, "B3 panel did not touch the map region");

    CK(f.w == w0 && f.h == h0 && f.scale == s0 &&
       f.text_fg == fg0 && f.text_bg == bg0,
       "B4 panel did not mutate the frame's own geometry or text colours");

    CK(count_colour(&f, &vit, 1, C_TEXT) > 0, "B5 vitals interior has text");
    CK(count_colour(&f, &sta, 1, C_TEXT) > 0, "B5 stats interior has text");
    CK(count_colour(&f, &cmd, 1, C_TEXT) > 0, "B5 cmd interior has text");
    CK(count_colour(&f, &bar, 0, C_BACKDROP) > 0, "B5 bar is filled with its rule colour");
    /* The rule is the backdrop, mapped once and used for both the fill and the label's
     * background so the two cannot disagree -- see draw_bar. panel_check leaves
     * cga_on_ega clear, so the map is identity here and the rule is C_BACKDROP itself.
     * The control that proves this assertion means something: setting cga_on_ega and
     * filling with the unmapped value puts the label on a different colour, and the
     * uniformity check below catches it. */
    CK(count_colour(&f, &bar, 0, C_BOXBG) > 0, "B5 bar has the place label drawn in it");
    /* Uniformity: every pixel of the rule that is not label ink is the rule colour.
     * This is the assertion that fails if the fill and the glyph background ever store
     * two different indices again. */
    int rule_px = count_colour(&f, &bar, 0, C_BACKDROP);
    int ink_px = count_colour(&f, &bar, 0, C_BOXBG);
    CK(rule_px + ink_px == bar.w * bar.h,
       "B5 bar is uniform: rule colour plus label ink, nothing else");

    /* The bar's label x is U2_ENH_BAR_TEXT_X, which is already on the text grid. It
     * used to be BAR_X + U2_ENH_BOX_PAD = 24, which text() truncated to 16 in silence.
     * The property is that the ink begins inside the grid cell the constant names, not
     * that it begins on its left edge: a glyph's first column can be blank sidebearing,
     * and asserting the exact column would fail for a letter with a left gap. If the
     * label ever lands off the grid the ink moves a whole cell and this goes red. */
    int label_col = -1;
    for (int x = bar.x; x < bar.x + bar.w; x++) {
        int ink = 0;
        for (int y = bar.y; y < bar.y + bar.h; y++)
            if (f.px[y][x] == C_BOXBG) ink++;
        if (ink > 0) { label_col = x; break; }
    }
    CK(label_col >= U2_ENH_BAR_TEXT_X &&
       label_col < U2_ENH_BAR_TEXT_X + U2_ENH_TEXT_W,
       "B5 bar label starts in the grid cell at U2_ENH_BAR_TEXT_X");

    /* The strongest single assertion here: any pixel in the text colour must be
     * inside a box interior. Overflow off the right edge, or text painted into the
     * gutter or over a border, fails here rather than being a screenshot someone
     * hopes somebody takes. The bar is excluded because it is full-width chrome that
     * sits outside the three panel boxes by design. */
    int stray = 0;
    for (int y = 0; y < f.h; y++) {
        for (int x = 0; x < f.w; x++) {
            if (f.px[y][x] != C_TEXT) continue;
            if (box_interior(&vit, x, y) || box_interior(&sta, x, y) ||
                box_interior(&cmd, x, y) || box_contains(&bar, x, y))
                continue;
            if (!stray) printf("  first stray text pixel at %d,%d\n", x, y);
            stray++;
        }
    }
    CK(stray == 0, "B7 no text pixel anywhere outside a box");

    /* An empty log must leave the log box empty, not half-filled with the previous
     * frame's lines: the panel redraws rather than composites, so a stale line here
     * would be a message the player never received. */
    u2_frame g;
    memset(&g, 0, sizeof g);
    if (u2_frame_init_size(&g, U2_ENH_W, U2_ENH_H, U2_ENH_SCALE) != 0) {
        u2_frame_free(&f);
        return 77;
    }
    u2_panel_draw(&g, &ctx, NULL, &c, -1, NULL, 0);
    CK(count_colour(&g, &cmd, 1, C_TEXT) == 0, "B6 empty log draws no text in the log box");
    CK(border_intact(&g, &cmd, C_BORDER), "B6 cmd border intact with an empty log");
    u2_frame_free(&g);

    /* A very long line has to wrap, not run off. Same scan as B7, one long line. */
    u2_frame h;
    memset(&h, 0, sizeof h);
    if (u2_frame_init_size(&h, U2_ENH_W, U2_ENH_H, U2_ENH_SCALE) != 0) {
        u2_frame_free(&f);
        return 77;
    }
    static char big[512];
    memset(big, 'X', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    const char *one[] = { big };
    u2_panel_draw(&h, &ctx, NULL, &c, -1, one, 1);
    int stray2 = 0;
    for (int y = 0; y < h.h && !stray2; y++)
        for (int x = 0; x < h.w; x++)
            if (h.px[y][x] == C_TEXT &&
                !(box_interior(&vit, x, y) || box_interior(&sta, x, y) ||
                  box_interior(&cmd, x, y) || box_contains(&bar, x, y))) {
                if (!stray2) printf("  long line escapes at %d,%d\n", x, y);
                stray2++;
            }
    CK(stray2 == 0, "B7b a 511-character log line wraps inside its box");
    u2_frame_free(&h);

    /* The configuration that actually broke, and the only one in which the bar's
     * uniformity can fail. With cga_on_ega clear the text layer's index map is identity,
     * so the bar is uniform however it is painted and the assertion above cannot go red
     * no matter what it is given -- a gate that can only fail in a configuration it never
     * exercises is a decoration. CGA art on an EGA palette is the case that produced a
     * label sitting on a block of the wrong colour, so it is drawn here and checked. */
    u2_frame k;
    memset(&k, 0, sizeof k);
    if (u2_frame_init_size(&k, U2_ENH_W, U2_ENH_H, U2_ENH_SCALE) != 0) {
        u2_frame_free(&f);
        return 77;
    }
    k.cga_on_ega = 1;
    u2_panel_draw(&k, &ctx, NULL, &c, -1, NULL, 0);
    /* The control inside the gate: if the map did not move the backdrop index, B8 below
     * would pass for the wrong reason and this frame would be decoration. */
    CK(u2_frame_cga_idx(&k, C_BACKDROP) != C_BACKDROP,
       "B8 control: the CGA-on-EGA map really does move the backdrop index here");
    /* The rule is the backdrop and the ink is dark, and with a glyph-only label that
     * stays true on both sides of the map: the fill is stored raw and the ink index is a
     * fixed point. Before the overlay primitive this failed here, which is the whole
     * reason the assertion exists. */
    CK(count_colour(&k, &bar, 0, C_BACKDROP) + count_colour(&k, &bar, 0, C_BOXBG)
       == bar.w * bar.h,
       "B8 bar stays uniform with CGA art on an EGA palette (the case that broke)");
    u2_frame_free(&k);

    u2_frame_free(&f);
    return 0;
}

/* --------------------------------------------------------- red controls */

static void selftest(void)
{
    const int W = U2_ENH_W, H = U2_ENH_H;
    int bad = 0;

    /* Each control asserts the predicate REJECTS a value that must not pass. */
    boxrec over = { "over", W - 10, 0, 100, 10 };
    if (box_fits(&over, W, H)) { printf("CONTROL FAILED: overflow accepted\n"); bad++; }
    boxrec neg = { "neg", -1, 0, 10, 10 };
    if (box_fits(&neg, W, H)) { printf("CONTROL FAILED: negative origin accepted\n"); bad++; }
    boxrec zero = { "zero", 0, 0, 0, 10 };
    if (box_fits(&zero, W, H)) { printf("CONTROL FAILED: zero width accepted\n"); bad++; }

    boxrec a = { "a", 10, 10, 100, 100 }, b = { "b", 50, 50, 10, 10 },
             c = { "c", 200, 10, 10, 100 };
    if (!boxes_overlap(&a, &b)) { printf("CONTROL FAILED: overlap missed\n"); bad++; }
    if (boxes_overlap(&a, &c)) { printf("CONTROL FAILED: disjoint pair reported\n"); bad++; }

    if (on_grid(U2_ENH_TEXT_W + 1, U2_ENH_TEXT_W)) { printf("CONTROL FAILED: off-grid accepted\n"); bad++; }
    if (cols_fit(100, 100, 16, 16)) { printf("CONTROL FAILED: too-wide columns accepted\n"); bad++; }
    if (!cols_fit(100, 4, 16, 16)) { printf("CONTROL FAILED: fitting columns rejected\n"); bad++; }

    /* A predicate that cannot fail is the defect this project has now caught in
     * four different gates, so say plainly whether anything above went wrong. */
    printf("selftest: %d control(s) failed\n", bad);
    fails += bad;
    checks += 8;
}

int main(int argc, char **argv)
{
    (void)argc;
    if (argc > 1 && !strcmp(argv[1], "--selftest")) {
        selftest();
        printf("panel_check selftest: %s\n", fails ? "FAIL" : "PASS");
        return fails ? 1 : 0;
    }
    group_a();
    const int rc = group_b();
    if (rc) return rc;
    printf("panel_check: %d checks, %d failure(s)\n", checks, fails);
    if (fails) { printf("PANEL CHECK: FAIL\n"); return 1; }
    printf("PANEL CHECK: PASS\n");
    return 0;
}
