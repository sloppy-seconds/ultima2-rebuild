#ifndef U2_PANEL_H
#define U2_PANEL_H
/*
 * Enhanced-mode side panel: the three boxes to the right of the map.
 *
 * The arrangement is the FM Towns version's, as measured in
 * docs/enhanced-layout-plan.md § 1-2. The CONTENT is ours: the owner ruled that
 * enhanced mode is a reimplementation of a presentation rather than a byte
 * matching target, so the labels here are ours and the stat order is ours, not the
 * reference's (which runs STR AGI INT WIS CHA STA and abbreviates equipment as
 * WEA:/ARM:). Every invented string is marked as invented at its site.
 *
 * Colours are never hardcoded palette indices. A v2 theme brings its own palette
 * and index 3 can be black there — the exact trap that made status text invisible
 * in the FM Towns theme — so the backdrop, border and bar are chosen by scanning
 * the palette that is actually in use.
 */
#include <stdint.h>
#include "core/frame.h"
#include "core/ctx.h"
#include "core/tiles_ega.h"

typedef struct {
    uint8_t backdrop;   /* the field behind the boxes */
    uint8_t box_bg;     /* box interior */
    uint8_t border;     /* box outline */
    uint8_t text;       /* ordinary text */
    uint8_t accent;     /* what the world said to the player */
    uint8_t bar;        /* the bottom bar, which now carries the planet and era */
} u2_panel_colours;

/* Pick the panel's colours from the palette actually in force. `text_fg` is the
 * index the loader already chose for readable text, so the panel cannot drift from
 * the text layer the way it did when index 3 was assumed to be black. */
void u2_panel_pick_colours(u2_panel_colours *c, const uint8_t pal[16][3],
                           uint8_t text_fg);

/* Draw the whole panel: backdrop, three boxes, and the bottom bar.
 * `avatar_art` is the player's art index (drawn through the theme's map, so a v2
 * theme puts the right figure in box 1); pass -1 for none. `log` holds the message
 * lines newest-last, already plain text; they are re-wrapped to the box width,
 * which is narrower than the 40-column DOS grid the log was written for. */
void u2_panel_draw(u2_frame *f, const u2_ctx *ctx, const u2_tiles_ega *tiles,
                   const u2_panel_colours *c, int avatar_art,
                   const char *const *log, int n_log);

#endif /* U2_PANEL_H */
