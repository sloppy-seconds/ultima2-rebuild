#ifndef U2_PIC_H
#define U2_PIC_H

/*
 * The original's full-screen pictures (PICDRA and its five siblings).
 *
 * There is no image format here, and that is the finding. The files are literal images of
 * CGA mode-4 video memory, so the "decoder" is an unpack, not a parser.
 *
 * What is proven: the pixel layout, established positionally on 2026-11-21 against a live
 * capture of the original (captures/spice86/cga-splash-live/). Displayed row 2n equals file
 * row n on 187 of 187 sampled rows across two pictures, so the art is 320x100 displayed
 * doubled and only the first 8,000 bytes of a pic file are pixels. engine/tests/pic_check.c
 * pins this by asserting scanline pairs are identical, which the earlier reading fails on 97
 * of 200 rows.
 *
 * What used to be claimed here, and why it survived: "even scanlines at offset 0 and odd at
 * 8000 reproduces a live captured frame at 100% colour-class agreement". A colour-class
 * agreement is a census, and a census cannot separate these two layouts -- both yield exactly
 * 11,567 inked pixels for picdra. The wrong reading therefore passed every check aimed at it
 * and produced pictures that were horizontally doubled and combed. Do not re-prove this with
 * a census.
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
 *   - the screen is 320x200 at 2 bits per pixel, 80 bytes per displayed scanline pair
 *   - displayed rows 2n and 2n+1 both come from file bytes (n)*80, i.e. the art is 320x100
 *     and the original doubles it vertically; the bytes at 8000..15999 are not the odd field
 *     and are not drawn. They are a horizontally shifted variant of the first half (picout by
 *     22 px, pictwn by 36 px) and remain unexplained.
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
 *     sampling guess and is refuted. **present_poem() now implements the observed mapping**
 *     (2026-11-15), with couplet 3 keeping the town picture because it is the last one seen
 *     before the gap rather than an invented fifth.
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
#define U2_PIC_STARS   "picspa"   /* starfield, under couplet 5 of the poem */
#define U2_PIC_PYRAMID "picmin"   /* the couplet-6 field of figures over a cross; the pack
                                     calls it picmin and it is NOT a pyramid, so the name is
                                     the file's and not a description -- see the G58 note */
#define U2_PIC_OUTDOOR "picout"   /* outdoor map, under couplet 1 */
#define U2_PIC_TOWN    "pictwn"   /* town, under couplets 2 and 3 */
#define U2_PIC_DUNGEON "picdng"   /* dungeon wireframe, under couplet 4 */

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

/* Which art a picture came from, and why it did not.
 *
 * u2_pic_show is the ONE place that decides between the EGA variant and the CGA
 * original. It exists so the caller's fallback, the dev dump and a gate all look at
 * what a run actually draws rather than each re-deriving the rule and drifting.
 */
#define U2_PIC_SHOWN_EGA   0
#define U2_PIC_SHOWN_CGA   1
#define U2_PIC_ERR_FRAMES (-1)   /* this renderer takes no frames (terminal build) */
#define U2_PIC_ERR_NOFILE (-2)   /* neither spelling is readable at the right size */
#define U2_PIC_ERR_BLIT   (-3)   /* found, and the unpack failed */
int u2_pic_show(u2_ctx *ctx, const char *base, struct u2_frame *f);

#endif /* U2_PIC_H */
