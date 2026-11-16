#ifndef U2_SEAM_H
#define U2_SEAM_H

/*
 * Seam layer — the engine's display / input / audio / time entry points, one
 * per named seam in the asm tree. Game logic calls these and nothing else
 * that touches the outside world. Every call is traced with the asm function
 * it stands in for, which is what makes a --trace log auditable against asm/.
 * One exception: char_out has no located asm counterpart and traces asm=-.
 */

#include <stdint.h>
#include "core/ctx.h"

/* One glyph at the cursor: no CR handling, no margin policy of its own.
 * Engine primitive — the asm/4A1C it used to cite is INT 10h AH=00 AL=01, a video-mode
 * set, and the real INT 10h AH=0E teletype site has never been located in this binary. */
void seam_char_out(u2_ctx *ctx, uint8_t ch);
/* seam: char_print (asm/5003_char_print.asm) — teletype with CR handling. */
void seam_char_print(u2_ctx *ctx, uint8_t ch);
/* seam: char_print_wrap (asm/53F4_char_print_wrap.asm) — wrap at 80 columns. */
void seam_str_out(u2_ctx *ctx, const char *s);
/* seam: str_out_at (asm/51EB_str_out_at.asm) — place cursor, then print. */
void seam_str_out_at(u2_ctx *ctx, int col, int row, const char *s);
/* cursor placement alone (INT 10h AH=02 half of asm/51FD_str_out_core.asm). */
void seam_goto(u2_ctx *ctx, int col, int row);
/* blank one text row (asm/53A0_sub_53a0.asm: 40 spaces at column 0). */
void seam_clear_row(u2_ctx *ctx, int row);
/* clear the whole screen and home the cursor (asm/4C1C). */
void seam_clear_screen(u2_ctx *ctx);
/* seam: key_poll (asm/4F4D_key_poll.asm) — non-blocking key check. */
int  seam_key_poll(u2_ctx *ctx, u2_key *out);
/* blocking key read (INT 16h AH=00 sites). */
int  seam_key_wait(u2_ctx *ctx, u2_key *out);
/*
 * Present whatever has been printed, THEN block for a key. Use this instead of
 * seam_key_wait at any site that asks the player a question.
 *
 * The original never needs this: it writes to 0xB8000 and the CRT is always displaying it.
 * A port with a real back buffer does. A handler that prints "1-DA, 2-MA, ... WHICH?" and
 * then blocks has, without this call, put the menu into a frame nobody has shown yet -- the
 * presenting loop sits above the handler, so the menu reaches the screen only when the
 * handler returns, which is after the player has already typed blind. Owner-reported
 * 2026-11-02: buying a mace in Linda and pressing R looked like a crash for exactly this
 * reason, and W did the same. cmd_M had it too and nobody had noticed.
 *
 * Deliberately NOT folded into seam_key_wait. That was tried and it broke CP4: presenting
 * from the shared wait path also runs at the title screen, where --dump-screen sets its
 * done-flag during the flush and the title flow then loops on a no-key result forever. So
 * this is opt-in per site, and creation.c's waits stay on the plain seam for that reason.
 *
 * It composes a whole frame rather than flushing the text layer, because a bare text flush
 * presents the text surface alone and the map goes black behind the question.
 */
int  seam_key_wait_shown(u2_ctx *ctx, u2_key *out);
/* seam: rand_byte (asm/5217_rand_byte.asm). */
uint8_t seam_rand_byte(u2_ctx *ctx);
/* seam: wait_ms — the engine's own pacing seam, with NO counterpart at asm/52CA.
 * That routine is the DOS FCB call wrapper; see platform.h's seam table. */
void seam_wait_ms(u2_ctx *ctx, uint32_t ms);
/* seam: speaker_freq (asm/5CD2_speaker_freq.asm) — 8253 latch divisor. */
void seam_tone(u2_ctx *ctx, uint16_t divisor, uint32_t ms);
/* The gap between two tones: the speaker stays silent for ms. Backends without a
 * silence seam answer NULL and the caller plays its notes back to back. */
void seam_silence(u2_ctx *ctx, uint32_t ms);
/* paint the grid to the active renderer (call at frame boundaries). */
void seam_present(u2_ctx *ctx);
/* Present the pixel framebuffer. Returns non-zero when the display asked to
 * quit (window closed). A no-op returning 0 on cell-only backends, so callers
 * do not need to know which kind of renderer they were given. */
int  seam_present_frame(u2_ctx *ctx);

#endif /* U2_SEAM_H */
