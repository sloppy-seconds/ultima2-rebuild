/* panel.c — see panel.h. Geometry comes from core/layout.h and nowhere else. */

#include "core/panel.h"

#include <stdio.h>
#include <string.h>

#include "core/layout.h"
#include "core/font.h"
#include "core/state.h"
#include "core/pool_names.h" /* the weapon and armour name pools, same lookup as status.c */
#include "gen_content.h"        /* era and world names, from the extractor, not retyped */

#ifdef U2_T_WEAPONS_N
   /* The pools come from the player's own executable, so a build without it has
    * blank names and correct positions. Same deliberate degradation status.c
    * documents; it is not a silent one. */
#endif
#define P_NAME(tbl, i) U2_POOL_NAME(tbl, i)

/* ------------------------------------------------------------- colours */

static int luminance(const uint8_t e[3]) { return e[0] + e[1] + e[2]; }

/* Choose the palette entry that reads as a given hue. This exists because a v2
 * theme replaces the palette wholesale: the FM Towns table puts black at index 3,
 * which already made status text invisible once when an index was assumed rather
 * than looked up. Scanning the live table cannot go wrong that way. */
static int pick(const uint8_t pal[16][3], int (*score)(const uint8_t *), int want_max)
{
    int best = 0, best_score = score(pal[0]);
    for (int i = 1; i < 16; i++) {
        const int s = score(pal[i]);
        if ((want_max && s > best_score) || (!want_max && s < best_score)) {
            best_score = s;
            best = i;
        }
    }
    return best;
}

static int s_blue(const uint8_t *e)   { return e[2] - (e[0] + e[1]) / 2; }
static int s_red(const uint8_t *e)    { return e[0] - (e[1] + e[2]) / 2; }
static int s_dark(const uint8_t *e)   { return luminance(e); }
static int s_light(const uint8_t *e)  { return luminance(e); }

void u2_panel_pick_colours(u2_panel_colours *c, const uint8_t pal[16][3],
                           uint8_t text_fg)
{
    if (!c || !pal) return;
    c->backdrop = (uint8_t)pick(pal, s_blue, 1);
    c->box_bg   = (uint8_t)pick(pal, s_dark, 0);
    c->border   = text_fg ? text_fg : (uint8_t)pick(pal, s_light, 1);
    c->text     = text_fg ? text_fg : (uint8_t)pick(pal, s_light, 1);
    c->accent   = (uint8_t)pick(pal, s_red, 1);   /* ours, not the reference's yellow */
    c->bar      = (uint8_t)pick(pal, s_red, 1);
}

/* ---------------------------------------------------------- primitives */

/* Everything the panel draws goes through u2_frame_put, NOT u2_frame_pixel and
 * NOT u2_frame_put_logical.
 *
 * u2_frame_pixel masks the colour with &3 because it is the CGA 2bpp path, so a
 * 16-colour index written through it silently becomes index & 3 — and in the FM
 * Towns palette index 3 is black, which is how the box borders were being drawn
 * and then vanished. u2_frame_put_logical would multiply the COORDINATES by the
 * frame scale, and the panel geometry is already in physical pixels, so it would
 * draw everything off the right-hand edge. u2_frame_put writes one physical pixel
 * at the full index, which is what a layout expressed in physical pixels needs. */
static void pset(u2_frame *f, int x, int y, uint8_t col)
{
    u2_frame_put(f, x, y, col);
}

static void fill(u2_frame *f, int x, int y, int w, int h, uint8_t col)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            pset(f, i, j, col);
}

static void hline(u2_frame *f, int x, int y, int w, uint8_t col)
{
    for (int i = x; i < x + w; i++) pset(f, i, y, col);
}

static void vline(u2_frame *f, int x, int y, int h, uint8_t col)
{
    for (int j = y; j < y + h; j++) pset(f, x, j, col);
}

static void box(u2_frame *f, int x, int y, int w, int h, uint8_t bg, uint8_t border)
{
    fill(f, x, y, w, h, bg);
    hline(f, x, y, w, border);
    hline(f, x, y + h - 1, w, border);
    vline(f, x, y, h, border);
    vline(f, x + w - 1, y, h, border);
}

/* Text at a physical pixel position. The font API works in glyph cells, and one
 * cell is U2_ENH_TEXT_W physical pixels on the doubled enhanced frame, so this is
 * a division by the cell and not by the raw 8. Dividing by 8 was correct only for
 * the scale-1 prototype and silently halved every coordinate once the frame
 * doubled. */
static void text(u2_frame *f, int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    u2_font_draw_text(f, y / U2_ENH_TEXT_H, x / U2_ENH_TEXT_W, s, fg, bg);
}

/* Four BCD digits as the DOS status block prints them, leading zeros included: the
 * original shows GOLD=0009 rather than 9, so the padding is the original's and not
 * an invention. An earlier version stripped the zeros to match the FM Towns
 * reference's style, which was the wrong side to err on once the labels came from
 * the binary. */
static void bcd4(uint8_t hi, uint8_t lo, char *out, size_t n)
{
    snprintf(out, n, "%c%c%c%c",
             (char)('0' + ((hi >> 4) & 0x0F)), (char)('0' + (hi & 0x0F)),
             (char)('0' + ((lo >> 4) & 0x0F)), (char)('0' + (lo & 0x0F)));
}

/* Right-align a value in a field, the way the reference's leader column does. */
static void value_right(u2_frame *f, int right_x, int y, const char *s,
                        uint8_t fg, uint8_t bg)
{
    const int cols = (int)strlen(s);
    text(f, right_x - cols * U2_ENH_TEXT_W, y, s, fg, bg);
}

/* --------------------------------------------------------------- boxes */

static void draw_vitals(u2_frame *f, const u2_ctx *ctx, const u2_tiles_ega *tiles,
                        const u2_panel_colours *c, int avatar_art)
{
    const int x = U2_ENH_VIT_X, y = U2_ENH_VIT_Y, w = U2_ENH_VIT_W, h = U2_ENH_VIT_H;
    box(f, x, y, w, h, c->box_bg, c->border);

    const int cx = x + U2_ENH_BOX_PAD;          /* first text column, pixels */
    const int r0 = y + U2_ENH_BOX_PAD;
    /* The avatar owns the last two interior cells, so text is cut short of the box
     * rather than running out under the figure. */
    const int text_cols = U2_ENH_VIT_COLS - 2;

    char name[17];
    int len = 0;
    /* The name field is not guaranteed NUL-terminated in a hand-edited save, so
     * bound it by the field rather than trusting the bytes. */
    while (len < (int)sizeof ctx->state.name && ctx->state.name[len]) len++;
    if (len > text_cols) len = text_cols;
    if (len > (int)sizeof name - 1) len = (int)sizeof name - 1;
    memcpy(name, ctx->state.name, (size_t)len);
    name[len] = '\0';
    text(f, cx, r0, name, c->text, c->box_bg);

    char hp[8], xp[8], gd[8], fd[8];
    /* The byte pairs come from the one definition the classic status band also uses,
     * so the two renderers cannot disagree about which bytes a field is. FOOD is
     * the case this fixes: the panel used to print [0x54],[0x55], which exposed the
     * fractional low byte and made every step look like it burned 19 food. */
    uint8_t a = 0, b = 0;
    u2_state_bcd_pair(&ctx->state, U2_BCD_HP,   &a, &b);  bcd4(a, b, hp, sizeof hp);
    u2_state_bcd_pair(&ctx->state, U2_BCD_EXP,  &a, &b);  bcd4(a, b, xp, sizeof xp);
    u2_state_bcd_pair(&ctx->state, U2_BCD_GOLD, &a, &b);  bcd4(a, b, gd, sizeof gd);
    u2_state_bcd_pair(&ctx->state, U2_BCD_FOOD, &a, &b);  bcd4(a, b, fd, sizeof fd);

    /* The original's own four labels, taken from the status block (asm/50F9):
     * H.P.= EXP.= GOLD= FOOD=. Used verbatim rather than abbreviated, and the
     * values keep the original's zero padding, so this reads GOLD=0009 and not
     * GOLD: 9. The order down the column is the owner's: HP, EXP, GOLD, FOOD. */
    static const char *const vl[4] = {
        U2_S_STATUS_HP, U2_S_STATUS_EXP, U2_S_STATUS_GOLD, U2_S_STATUS_FOOD
    };
    const char *vv[4] = { hp, xp, gd, fd };
    char line[32];
    for (int i = 0; i < 4; i++) {
        snprintf(line, sizeof line, "%s%s", vl[i], vv[i]);
        text(f, cx, r0 + (i + 1) * U2_ENH_TEXT_H, line, c->text, c->box_bg);
    }

    /* One line at the box floor, per the owner. W and A are enough of a label once
     * the box is this narrow: "W:GREAT SWORD" is 13 cells and the box has exactly
     * 13 of text once the avatar keeps its two. The names are truncated rather
     * than wrapped, which only bites on a name longer than anything the game
     * ships. "W:"/"A:" are ours -- the binary has no such status label -- and the
     * name after them is the real extracted pool. */
    char wline[24];
    snprintf(wline, sizeof wline, "W:%s",
             P_NAME(U2_T_WEAPONS, ctx->state.readied_weapon));
    if ((int)strlen(wline) > text_cols) wline[text_cols] = '\0';
    text(f, cx, r0 + 6 * U2_ENH_TEXT_H, wline, c->text, c->box_bg);

    if (avatar_art >= 0 && tiles) {
        /* Through the theme's map, not raw: the avatar art is the live player stamp
         * shifted once, so it is 60..63 by class and the vehicle tile while mounted,
         * and pool index 60 in a v2 theme is a capital O. That mismatch was a real bug,
         * fixed 2026-11-05; the rule is that the value must come through the map, not
         * that it is ever 60. */
        const int tile = u2_tiles_ega_tile(tiles, avatar_art);
        /* A theme whose map has no row for the current art falls back to the Fighter
         * figure rather than drawing nothing. The avatar disappearing because you
         * changed class, or boarded, would be a silent blank of exactly the kind this
         * project keeps calling a defect; a slightly wrong figure is at least a
         * visible one. The built-in theme is an identity map, so this only fires for
         * a v2 theme with a partial map. */
        const int tile_ok = tile >= 0 ? tile : u2_tiles_ega_tile(tiles, 60);
        if (tile_ok >= 0)
            u2_frame_blit_tile_ega(f, tiles, tile_ok,
                                   x + w - U2_ENH_BOX_PAD - u2_tiles_ega_tile_w(tiles),
                                   y + (h - u2_tiles_ega_tile_h(tiles)) / 2);
    }
}

static void draw_stats(u2_frame *f, const u2_ctx *ctx, const u2_panel_colours *c)
{
    const int x = U2_ENH_STA_X, y = U2_ENH_STA_Y, w = U2_ENH_STA_W, h = U2_ENH_STA_H;
    box(f, x, y, w, h, c->box_bg, c->border);

    /* The original's own stat names, from the name pool (asm/4C55). It ships
     * INTELL. rather than INTELLIGENCE, and that is five cells this box does not
     * have, so using the binary's spelling is both more faithful and what makes the
     * layout fit at all. Order is the pool's order. */
    const uint8_t v[6] = {
        ctx->state.stat_str, ctx->state.stat_agi, ctx->state.stat_sta,
        ctx->state.stat_cha, ctx->state.stat_wis, ctx->state.stat_int
    };
    const int right  = x + w - U2_ENH_BOX_PAD;
    for (int i = 0; i < 6; i++) {
        const char *lab = U2_T_STATS[i];
        const int ry = y + U2_ENH_BOX_PAD + i * U2_ENH_TEXT_H;
        text(f, x + U2_ENH_BOX_PAD, ry, lab, c->text, c->box_bg);
        /* The dotted leader the reference puts between label and value, drawn
         * rather than assumed: fill the gap the label left with dots. Only if a
         * whole cell is left over, or the dots crowd the value in a narrow box. */
        const int dots_from = x + U2_ENH_BOX_PAD
                            + (int)strlen(lab) * U2_ENH_TEXT_W + U2_ENH_TEXT_W;
        const int dots_to   = right - 3 * U2_ENH_TEXT_W;
        for (int dx = dots_from; dx + U2_ENH_TEXT_W <= dots_to; dx += U2_ENH_TEXT_W)
            text(f, dx, ry, ".", c->text, c->box_bg);
        char num[3];
        /* Packed decimal, as stored. Printing these bytes with %u showed 96 for a
         * STR held as 0x60, which is BCD for 60 -- the same class of mistake as the
         * FOOD byte pair, and fixed the same way: one shared definition. */
        u2_bcd2_str(v[i], num);
        value_right(f, right, ry, num, c->text, c->box_bg);
    }

    /* A: and its name on the bottom row, same one-line rule as the weapon. "A:" is
     * ours; the name is the real extracted pool. */
    char aline[32];
    snprintf(aline, sizeof aline, "A:%s",
             P_NAME(U2_T_ARMOUR, ctx->state.worn_armour));
    if ((int)strlen(aline) > U2_ENH_STA_COLS) aline[U2_ENH_STA_COLS] = '\0';
    text(f, x + U2_ENH_BOX_PAD, y + U2_ENH_BOX_PAD + 6 * U2_ENH_TEXT_H,
         aline, c->text, c->box_bg);
}

static void draw_cmd(u2_frame *f, const u2_panel_colours *c,
                     const char *const *log, int n_log)
{
    const int x = U2_ENH_CMD_X, y = U2_ENH_CMD_Y, w = U2_ENH_CMD_W, h = U2_ENH_CMD_H;
    box(f, x, y, w, h, c->box_bg, c->border);
    if (!log) return;

    const int cols = U2_ENH_CMD_COLS;
    /* Interior rows, 0-indexed. This used to be (h - PAD) / TEXT_H, which counted
     * the bottom padding as drawing room and returned a row too many: the line
     * landed at y+160 on a 160-tall box, entirely below its own border. The owner
     * saw the last CMD sitting half outside the box, which is what it was. */
    const int last_row = (h - 2 * U2_ENH_BOX_PAD) / U2_ENH_TEXT_H - 1;
    const int rows_avail = last_row + 1;

    /* The log arrives in SCREEN order: oldest line first, the live command echo
     * last. It has to be drawn that way round, because the bottom line is the one
     * the player is typing into. The first version put log[0] on the bottom row
     * and walked up, which mirrored the message area upside down and left the
     * command echo floating three rows above the box floor with empty space under
     * it. So: measure what everything costs, drop from the OLDEST end until it
     * fits, then lay the survivors out top-to-bottom with the newest on last_row. */
    enum { MAXL = 32 };
    const int n = n_log < MAXL ? n_log : MAXL;
    int nc[MAXL], total = 0;
    for (int i = 0; i < n; i++) {
        const int len = log[i] ? (int)strlen(log[i]) : 0;
        nc[i] = (len + cols - 1) / cols;
        if (nc[i] < 1) nc[i] = 1;          /* a blank line still holds its row */
        total += nc[i];
    }
    int first_i = 0;
    while (first_i < n && total > rows_avail) { total -= nc[first_i]; first_i++; }

    int row = last_row - (total - 1);
    if (row < 0) row = 0;
    for (int i = first_i; i < n; i++, row++) {
        const char *s = log[i] ? log[i] : "";
        const int len = (int)strlen(s);
        if (len == 0) continue;            /* blank line: row reserved, nothing drawn */
        /* Lines longer than the box wrap onto the following row. That is a real
         * change from the DOS 40-column grid: the panel is narrower, so a line
         * such as CMD: ATTACK--EAST--HIT!!! occupies more rows here. The reference
         * lives with 17 columns, so this is normal rather than a defect. */
        for (int off = 0, k = 0; off < len; off += cols, k++) {
            char chunk[64];
            const int take = len - off > cols ? cols : len - off;
            memcpy(chunk, s + off, (size_t)take);
            chunk[take] = '\0';
            text(f, x + U2_ENH_BOX_PAD, y + U2_ENH_BOX_PAD + (row + k) * U2_ENH_TEXT_H,
                 chunk, c->text, c->box_bg);
        }
    }
}

/* ---------------------------------------------------------------- draw */

/* The bottom bar's first real content: where the player is, in the game's own words.
 * The era names are the ANOS: lines the original prints when a time door lands (asm/3546)
 * and the world names are the table at exe 0x072b9; both arrive through the generated
 * content header, so the bar carries no hand-maintained copy of game text.
 *
 * One label is ours. That world table ends `...PLUTO.` then `X.` and nothing more, so to
 * the bytes the ninth world is called X. "Planet X" is what everyone calls it, and this
 * is the presentation mode rather than the fidelity run, so the bar says PLANET X. */
static const char *const k_era[5] = {
    U2_S_ANOS_LEGENDS, U2_S_ANOS_9M_BC, U2_S_ANOS_1423_BC,
    U2_S_ANOS_1990_AD, U2_S_ANOS_2112_AD,
};
static const char *const k_world[10] = {
    U2_S_WORLD_EARTH, U2_S_WORLD_MERCURY, U2_S_WORLD_VENUS, U2_S_WORLD_MARS,
    U2_S_WORLD_JUPITER, U2_S_WORLD_SATURN, U2_S_WORLD_URANUS, U2_S_WORLD_NEPTUNE,
    U2_S_WORLD_PLUTO, "PLANET X",
};

/* "ANOS: 1990 A.D." -> "1990 A.D.", "URANUS." -> "URANUS". The affixes are the
 * original's, because that is how the strings are stored; the bar wants the noun. */
static void bar_word(char *out, size_t n, const char *s)
{
    if (strncmp(s, "ANOS: ", 6) == 0)
        s += 6;
    snprintf(out, n, "%s", s);
    size_t l = strlen(out);
    while (l > 0 && (out[l - 1] == '.' || out[l - 1] == '!'))
        out[--l] = '\0';
}

/* Earth is "EARTH - <era>", because on Earth the sector's tens digit IS the era. Off
 * Earth the same digit is the world, and DS:6D is what says which of the two it is —
 * docs/planet-maps-galaxy-patch.md § 3a is the reason this cannot be read off the
 * sector number alone. DS:6D = 0x0A is deep space, which the original narrates as
 * "YOU ARE ORBITING <planet>" rather than naming, so that label is ours too. */
static void bar_place(char *out, size_t n, const u2_ctx *ctx)
{
    const uint8_t w = ctx->state.world;
    if (w == 0) {
        char world_name[16], era_name[24];
        bar_word(world_name, sizeof world_name, k_world[0]);
        const uint8_t era = ctx->state.sector_first;
        bar_word(era_name, sizeof era_name, era < 5 ? k_era[era] : "???");
        snprintf(out, n, "%s - %s", world_name, era_name);
    } else if (w <= 9) {
        char name[16];
        bar_word(name, sizeof name, k_world[w]);
        snprintf(out, n, "%s", name);
    } else {
        snprintf(out, n, "DEEP SPACE");
    }
}

static void draw_bar(u2_frame *f, const u2_ctx *ctx, const u2_panel_colours *c)
{
    char place[64];
    bar_place(place, sizeof place, ctx);
    box(f, U2_ENH_BAR_X, U2_ENH_BAR_Y, U2_ENH_BAR_W, U2_ENH_BAR_H,
        c->bar, c->border);
    text(f, U2_ENH_BAR_X + U2_ENH_BOX_PAD, U2_ENH_BAR_Y, place, c->text, c->bar);
}

void u2_panel_draw(u2_frame *f, const u2_ctx *ctx, const u2_tiles_ega *tiles,
                   const u2_panel_colours *c, int avatar_art,
                   const char *const *log, int n_log)
{
    if (!f || !c) return;
    /* The backdrop is the field the boxes sit in, so it is painted before them. */
    fill(f, U2_ENH_PANEL_X, 0, U2_ENH_PANEL_W, U2_ENH_MAP_ROWS * 32, c->backdrop);
    draw_vitals(f, ctx, tiles, c, avatar_art);
    draw_stats(f, ctx, c);
    draw_cmd(f, c, log, n_log);

    /* Was constant chrome, measured as identical at H:258 and H:4922 and therefore not a
     * health bar, and ruled to stay empty until there was a use for it. The use is the
     * one the layout header named as the candidate: planet or area. */
    draw_bar(f, ctx, c);
}
