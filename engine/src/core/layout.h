#ifndef U2_LAYOUT_H
#define U2_LAYOUT_H
/*
 * Enhanced-mode screen layout.
 *
 * The shape comes from the FM Towns version as photographed in
 * references/fmtowns-screens, and it is a reimplementation of a presentation, not
 * a byte-matching target: the FM Towns binary has never been disassembled here, so
 * every number below is measured from photographs of a CRT and carries that grade.
 * docs/enhanced-layout-plan.md § 1 has the measurements and § 9 the owner rulings.
 *
 * The one number that is NOT from the reference is the map size. The reference map
 * is 11 x 12 because FM Towns draws its panel text at about 16 px, and a readable
 * panel at that size costs 276 px of the 640. Our font is 8x8 and the owner ruled
 * the panel at 8 px, which buys back the columns: 14 x 14 = 196 cells, against the
 * DOS window's 200. That is the whole reason the enhanced map is square-ish and the
 * reference is not.
 */

#include "core/font.h"   /* U2_FONT_CELL_W, so text columns are whole glyphs */

/* The frame the enhanced layout is designed for. Classic mode stays at 320x200
 * (or 640x400 doubled) and is untouched by any of this.
 *
 * 1024x512 rather than a full 4:3: the width is what lets a 14-column map and a
 * readable 16px panel coexist, and the height is only what the map plus the bar
 * need, so there is no dead band under the map. Fits a 1024x768 display with room
 * to spare, which is the smallest display the owner was willing to assume. */
#define U2_ENH_W 1024
#define U2_ENH_H 512

/* The enhanced frame is a DOUBLED canvas, and that is load-bearing twice over.
 * Text drawn through the font API comes out at 16px rather than the unreadable 8px
 * of a scale-1 frame, and a v1 16px theme presents at 32px via
 * u2_tiles_ega_present(), so every theme fills the same map cell instead of a v1
 * theme occupying a quarter of the space. The cost is width: 768 rather than the
 * reference's 640, because 14 map columns and a readable panel do not both fit in
 * 640 (docs/enhanced-layout-plan.md § 9a). */
#define U2_ENH_SCALE 2
/* One glyph, in physical pixels. Panel geometry is in these, not raw pixels, so
 * text can never land half on a glyph cell. */
#define U2_ENH_TEXT_W (U2_FONT_CELL_W * U2_ENH_SCALE)
#define U2_ENH_TEXT_H (U2_FONT_CELL_H * U2_ENH_SCALE)

/* Map window: 14 x 14 tiles. The tile side comes from the theme, so a v1 16 px
 * theme draws 14 x 14 of those and simply leaves room at the right; the geometry
 * below is in pixels and does not care. */
#define U2_ENH_MAP_COLS 14
#define U2_ENH_MAP_ROWS 14
#define U2_ENH_MAP_X    0
#define U2_ENH_MAP_Y    0

/* Panel column, and the three boxes inside it. Widths are in whole text cells so
 * the text never clips mid-glyph. */
#define U2_ENH_PANEL_X     (U2_ENH_MAP_COLS * 32)          /* 448 */
#define U2_ENH_PANEL_W     (U2_ENH_W - U2_ENH_PANEL_X)     /* 576 = 36 text cells */

/* Padding is a WHOLE text cell, not 4px. The font API snaps to the glyph grid, so
 * a sub-cell pad collapses to 0 and the first glyph lands on the box border —
 * which is what made the borders look missing on the first prototype. */
#define U2_ENH_BOX_PAD     U2_ENH_TEXT_W
#define U2_ENH_BOX_BORDER  1
#define U2_ENH_BOX_GAP     4

/* ---- the panel, as ruled 2026-11-05 -------------------------------------
 *
 * The first shape stacked three full-width boxes, and the owner's verdict was that
 * the top two were far too wide for what they hold. They now sit side by side, the
 * vitals as a single column of H E G F rather than a 2x2 grid, with the command log
 * pushed to the bottom and a deliberate empty band between them.
 *
 * That band is not unfinished work. The owner asked for the room specifically to
 * spend on ideas later, so nothing here should feel obliged to fill it.
 *
 * The panel is 36 text cells wide (576 px / 16) and divides as 1 + 17 + 1 + 16 + 1:
 * a one-cell gutter on each side, then the two boxes. Every width below is a whole
 * number of cells because the font API snaps to that grid: a box edge landing
 * between cells is how borders went missing once already. The outer gutters are not
 * decoration — with the boxes flush to the frame edge their right border was the
 * last pixel column and read as an open box.
 *
 * The widths are set by what the original's own strings need, not by taste. Using
 * the binary's own stat names is what makes the two-box shape fit at all: the
 * original ships INTELL. (asm/4C55 pool[67]), not INTELLIGENCE, and that
 * difference alone is five cells. Equipment is labelled W: and A: for the same
 * reason -- the full words plus the longest name do not fit beside a stats column.
 */
#define U2_ENH_TOP_Y     0
#define U2_ENH_TOP_H     (9 * U2_ENH_TEXT_H)    /* 144: seven text rows plus pads */
#define U2_ENH_GUT       U2_ENH_TEXT_W          /* 16: one cell, either side */

/* Vitals: name, then H.P.= EXP.= GOLD= FOOD= down one column, then W: and its
 * name on the last row, with the avatar owning the right two cells. */
#define U2_ENH_VIT_X     (U2_ENH_PANEL_X + U2_ENH_GUT)
#define U2_ENH_VIT_W     (17 * U2_ENH_TEXT_W)                        /* 272 */
#define U2_ENH_VIT_Y     U2_ENH_TOP_Y
#define U2_ENH_VIT_H     U2_ENH_TOP_H
#define U2_ENH_VIT_COLS  ((U2_ENH_VIT_W - 2 * U2_ENH_BOX_PAD) / U2_ENH_TEXT_W) /* 15 */

/* Stats: six with dotted leaders, then A: and its name on the bottom row. */
#define U2_ENH_STA_X     (U2_ENH_VIT_X + U2_ENH_VIT_W + U2_ENH_TEXT_W)  /* 1-cell gap */
#define U2_ENH_STA_W     (U2_ENH_PANEL_W - U2_ENH_VIT_W - 3 * U2_ENH_TEXT_W) /* 256 */
#define U2_ENH_STA_Y     U2_ENH_TOP_Y
#define U2_ENH_STA_H     U2_ENH_TOP_H
#define U2_ENH_STA_COLS  ((U2_ENH_STA_W - 2 * U2_ENH_BOX_PAD) / U2_ENH_TEXT_W) /* 14 */

/* Command log, bottom-aligned with the map so the two bottoms read as one line.
 * Everything between U2_ENH_TOP_H and this is the reserved band. */
#define U2_ENH_CMD_X     (U2_ENH_PANEL_X + U2_ENH_GUT)
#define U2_ENH_CMD_W     (U2_ENH_PANEL_W - 2 * U2_ENH_GUT) /* 544 */
#define U2_ENH_CMD_H     (10 * U2_ENH_TEXT_H)   /* 160 */
#define U2_ENH_CMD_Y     (U2_ENH_MAP_Y + U2_ENH_MAP_ROWS * 32 - U2_ENH_CMD_H)
#define U2_ENH_CMD_COLS  ((U2_ENH_CMD_W - 2 * U2_ENH_BOX_PAD) / U2_ENH_TEXT_W) /* 34 */

#define U2_ENH_TEXT_COLS U2_ENH_CMD_COLS

/* The bottom bar. Measured as constant full-width chrome in the reference (it does
 * not track HP: identical in frames at H:258 and H:4922). It was ruled to stay empty
 * until there was a use, and the use it now carries is the one named here as the
 * candidate: where the player is, planet and era on Earth, planet off Earth.
 * Positioned from the bottom of the frame rather than a literal, so it survives a
 * change of frame height without silently overlapping the map. */
#define U2_ENH_BAR_H       16
/* Must land on the text grid: the panel's text() places by y / U2_ENH_TEXT_H, so a
 * y that is not a whole cell silently loses the remainder and the label draws above
 * its own bar. The old +8 put the place line 9 px above the red it sits on — owner
 * screenshot 2026-11-20. 16 is one cell, so the bar and its text now agree exactly. */
#define U2_ENH_BAR_Y       (U2_ENH_MAP_Y + U2_ENH_MAP_ROWS * 32 + U2_ENH_TEXT_H)
#define U2_ENH_BAR_X       8
#define U2_ENH_BAR_W       (U2_ENH_W - 2 * U2_ENH_BAR_X)

/* Where the player sits in the enhanced window. The reference centres the figure;
 * with an even count there is no exact centre, so this is the one that leaves the
 * extra row below, which is where you want the warning space. */
#define U2_ENH_PLAYER_COL  (U2_ENH_MAP_COLS / 2)
#define U2_ENH_PLAYER_ROW  (U2_ENH_MAP_ROWS / 2)

#endif /* U2_LAYOUT_H */
