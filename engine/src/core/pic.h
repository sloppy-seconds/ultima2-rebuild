#ifndef U2_PIC_H
#define U2_PIC_H

/*
 * The original's full-screen pictures (PICDRA and its five siblings).
 *
 * There is no image format here, and that is the finding. The files are literal images of
 * CGA mode-4 video memory, so the "decoder" is an unpack, not a parser.
 *
 * What is proven: decoding a pic file as 320x200 at 2bpp with even scanlines at offset 0 and
 * odd scanlines at 8000 reproduces a live captured frame of the original's title splash at
 * 100% colour-class agreement over 4000 sampled pixels (captures/spice86/splash-look, and
 * engine/tests/pic_check.c re-checks the census on any machine that has the game). The engine's
 * u2_frame stores one CGA index per pixel, so the unpack lands straight into it.
 *
 * What is not proven is which instruction moves the bytes. asm/5351_sub_5351.asm does
 * `mov $0xb800,%ax ; mov %ax,%ds`, and 0xB800 is the mode-4 framebuffer, so the game certainly
 * aims a read at video; but asm/535A_sub_535a.asm, which follows it, restores DS and asks for
 * ONE FCB record into a fixed DS:0x245, which is not 16,384 bytes. Either the picture arrives
 * by a path not yet identified or it arrives in many one-record reads with the buffer advanced
 * by a caller nobody has found. Both readings are consistent with the bytes, neither is
 * established, and the two asm headers say so rather than naming a mechanism half read. The
 * engine does not need the answer: it reads the file itself.
 *
 * Layout of the pixel data, measured rather than assumed:
 *   - 320x200, 2 bits per pixel, 80 bytes per scanline
 *   - even scanlines occupy offset 0..7999 and odd scanlines 8000..15999, which is the
 *     CGA mode-4 interleave (B8000 even field, BA000 odd field)
 *   - within a byte the leftmost pixel is in the top two bits
 *   - palette index 0 black, 1 cyan, 2 magenta, 3 white
 *   - five files are 16384 bytes (32 sectors, so 384 bytes of trailing padding) and
 *     picdra is 16512; the pixel data starts at offset 0 in both, which is what a
 *     header would have broken
 *
 * Proof of the layout: see the 100% agreement figure above. picdra is 16512 bytes and the
 * other five are 16384 (32 sectors), and the pixel data starts at offset 0 in both, which is
 * what a header would have broken.
 *
 * Which picture goes where, and the grade on each:
 *   - the intro shows PICDRA (the Ultima II dragon logo) after the second presentation
 *     screen and before the title menu, held about five seconds. Observed: five
 *     consecutive one-second frames all matched picdra at 100%.
 *   - the 'D' demonstration writes the story poem over the bottom rows, one couplet per
 *     picture, each held about eight seconds. Observed 2026-11-13 across ~130 frames of
 *     several loops (captures/spice86/g58-demonstration): couplet 1 over PICOUT (outdoor
 *     map), 2 over PICTWN (town), 4 over PICDNG (dungeon wireframe), 5 over PICSPA
 *     (starfield), 6 (MINAX) over a field of figures over a cross (PICMIN? -- not a
 *     pyramid, so the PICMIN label below is unverified). Couplet 3 (PLEAD WITH MEDIEVAL
 *     KINGS) was never observed -- the town picture was directly followed by the dungeon
 *     picture. The earlier "PICSPA then PICMIN, split inferred" note was a one-second-
 *     sampling guess and is refuted; present_poem() in game/title.c still implements that
 *     guess and needs the per-couplet pictures (flagged follow-up, cosmetic).
 *   - PICDNG, PICTWN and PICOUT are referenced by name in the executable (file offsets
 *     0x5c3, 0x63d, 0x72a) and ARE used: they are the demonstration pictures under
 *     couplets 4, 2 and 1 respectively (see the demonstration note above).
 *   - PICCAS is referenced at file offset 0x6ba and has NO file in the pack. The original
 *     has a picture load that can never find its data, and this engine inherits that.
 *
 * Copyright posture, which is the same as the tile art's and is not new: these files are
 * the player's own, read at runtime from --data-dir. Nothing here is committed, nothing
 * is substituted, and with the file absent the text screens stand exactly as they did
 * before this module existed.
 */

#include "core/ctx.h"

struct u2_frame;

/* The picture names, as the executable spells them. */
#define U2_PIC_TITLE   "picdra"   /* the Ultima II dragon logo, the intro splash */
#define U2_PIC_STARS   "picspa"   /* starfield, under the opening couplet */
#define U2_PIC_PYRAMID "picmin"   /* Minax's pyramid, under the rest of the poem */

/* Non-zero only when BOTH halves could work: a renderer that accepts a frame, and a
 * readable picture file. Callers branch on this so a terminal player keeps the text
 * screen instead of getting a black rectangle where a picture was meant to be. */
int u2_pic_can_show(u2_ctx *ctx, const char *name);

/* Unpack the picture into f, filling all 320x200 pixels. Returns 0 on success and
 * non-zero if the file is missing or too short, in which case f is left untouched. */
int u2_pic_blit(u2_ctx *ctx, const char *name, struct u2_frame *f);

/* Does the attached renderer take frames at all? Exposed because title.c wants to
 * decide between a picture and a text screen before it has a file to test. */
int u2_pic_backend_has_frames(const u2_ctx *ctx);

#endif /* U2_PIC_H */
