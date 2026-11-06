#ifndef U2_VRAM_H
#define U2_VRAM_H

/*
 * Character-cell screen model.
 *
 * The grid is 40x25, not the 80x25 the plan assumed. Evidence: the row-clear
 * loop prints 0x28 = 40 spaces (asm/53A0_sub_53a0.asm), the printable right
 * margin is 0x26/0x28 (asm/456B_sub_456b.asm, asm/48A7_sub_48a7.asm), and the
 * scroll window spans columns 0..right margin (asm/5025_sub_5025.asm).
 *
 * The original drives the screen two ways and the engine models both, plus one
 * primitive that is the engine's own:
 *   1. its own text printer — char_print (asm/5003_char_print.asm) writes with
 *      INT 10h AH=09h at the cursor in DS:0x2E (column) / DS:0x2F (row), using
 *      the attribute in DS:0x23E, wrapping at DS:0x31 and scrolling the
 *      message window when the row reaches 24;
 *   2. direct video-memory pokes — routines that point DS at 0xB800 and read
 *      raw disk sectors into VRAM (asm/5351_sub_5351.asm + asm/535A_sub_535a.asm)
 *      are the picture path; the engine's bitmap side of it is CP9;
 *   3. a bare one-glyph write at the cursor — the engine's u2_vram_put, reached by
 *      seam_char_out. It used to be mapped to char_out (asm/4A1C), but that routine
 *      is INT 10h AH=00 AL=01, a video-mode set, and the INT 10h AH=0E teletype
 *      site has never been located in this binary.
 *
 * Cursor position is applied with INT 10h AH=02h in str_out_core
 * (asm/51FD_str_out_core.asm). NB: docs/phase4-plan.md §1 lists DS:0x2E/0x2F
 * as "cursor page/col"; the bytes load DL from 0x2E and DH from 0x2F with
 * BH=0, i.e. 0x2E = column, 0x2F = row.
 */

#include <stdint.h>
#include "platform/platform.h"

/* The message window the original scrolls, from asm/5025_sub_5025.asm:
 * INT 10h AH=06h with CH=0x14, DH=0x17. */
#define U2_MSG_TOP     20
#define U2_MSG_BOTTOM  23
/* char_print scrolls when the row counter reaches this (cmpb $0x18,0x2f). */
#define U2_SCROLL_ROW  24

/* Engine-side cell flag: draw this cell inverse, swapping whatever text pair the
 * frame is using. The original's own attribute byte at DS:0x23E only ever holds
 * 0x0F normal, 0x70 dim or 0x02 red (state.h, asm/53D9, asm/53E6, asm/53EE), so
 * bit 7 -- the CGA blink bit -- is never set by it and the engine borrows it for
 * its own widgets. A renderer that cannot do reverse video must ignore the bit
 * rather than drop the cell: the text has to survive a plain terminal even when
 * the highlight does not. */
#define U2_ATTR_HILITE 0x80

typedef struct u2_cell {
    uint8_t ch;
    uint8_t attr;
} u2_cell;

typedef struct u2_vram {
    u2_cell cell[U2_ROWS][U2_COLS];
    int row;        /* cursor row    <- DS:0x2F */
    int col;        /* cursor column <- DS:0x2E */
    int visible;    /* cursor drawn */
    int dirty;      /* cells changed since last flush */
    uint64_t writes;/* total cells written (trace/diagnostics) */
} u2_vram;

void u2_vram_init(u2_vram *v);
void u2_vram_clear(u2_vram *v, uint8_t attr);
void u2_vram_goto(u2_vram *v, int row, int col);
/* Write one cell at the cursor and advance it (teletype semantics). */
void u2_vram_put(u2_vram *v, uint8_t ch, uint8_t attr);
/* Write one cell anywhere (direct VRAM poke semantics, no cursor move). */
void u2_vram_poke(u2_vram *v, int row, int col, uint8_t ch, uint8_t attr);
/* Scroll a rectangle up one line, blanking the bottom row (INT 10h AH=06h). */
void u2_vram_scroll_window(u2_vram *v, int top, int bottom, int left, int right,
                           uint8_t attr);
/* Render the grid through the active renderer and clear the dirty flag. */
void u2_vram_flush(u2_vram *v, u2_ctx *ctx);
/* Plain-text row, NUL-terminated, trailing spaces trimmed. Returns length. */
int  u2_vram_row_text(const u2_vram *v, int row, char *out, int outsz);

#endif /* U2_VRAM_H */
