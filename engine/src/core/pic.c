/*
 * pic.c — unpack one of the original's PIC screens into the engine's frame.
 *
 * See pic.h for the format evidence and the copyright posture. The short version: the files
 * are literal images of CGA mode-4 video memory, and asm/5351 points DS at 0xB800 (that
 * framebuffer segment) in what is clearly the picture load, so there is no codec here and no
 * header to skip. Which instruction moves the bytes is not established; see pic.h. The engine's
 * u2_frame already stores one CGA index per pixel, so this is a planar-to-linear unpack of
 * 16,000 bytes into 64,000.
 */

#include <stdio.h>
#include <string.h>

#include "core/pic.h"

#include "core/ctx.h"
#include "core/pic_ega.h"
#include "core/roots.h"
#include "core/frame.h"
#include "debug/trace.h"

/* 320x200 at 2bpp = 16000 bytes of pixel data. The files are padded up to a sector
 * boundary (16384, or 16512 for picdra), and the padding is trailing, so reading
 * exactly this many bytes from offset 0 is the whole of the read. */
#define PIC_PIXEL_BYTES (U2_SCREEN_H * U2_SCREEN_ROW)     /* 200 * 80 = 16000 */
#define PIC_ODD_FIELD   (U2_SCREEN_H / 2 * U2_SCREEN_ROW) /* 100 * 80 = 8000   */

/* The pack ships these lowercase. The tile loader carries the same three-way case list
 * for ultimaii.exe for the same reason: a copy that came off a FAT volume can arrive with
 * the names uppercased, and refusing to look is indistinguishable from the file not
 * existing, which is the failure mode worth avoiding.
 *
 * Both spellings are searched across the content roots rather than only beside the
 * binary, for the reason core/roots.h gives: under the explicit-layer layout the
 * pictures live in the release layer. */
static FILE *pic_open(const u2_ctx *ctx, const char *name)
{
    char path[512];
    char upper[32];
    size_t i = 0;

    if (u2_root_find(ctx, name, 1, path, sizeof path)) {
        FILE *f = fopen(path, "rb");
        if (f)
            return f;
    }

    for (; name[i] && i + 1 < sizeof upper; i++) {
        const char c = name[i];
        upper[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    upper[i] = '\0';
    if (!u2_root_find(ctx, upper, 1, path, sizeof path))
        return NULL;
    return fopen(path, "rb");
}

static int pic_read(const u2_ctx *ctx, const char *name, uint8_t *out, size_t cap)
{
    if (cap < PIC_PIXEL_BYTES)
        return 1;
    FILE *f = pic_open(ctx, name);
    if (!f)
        return 1;
    const size_t got = fread(out, 1, PIC_PIXEL_BYTES, f);
    fclose(f);
    /* Short is refused rather than zero-padded: a truncated picture would draw a
     * plausible image with a band of black across it, which is worse than not drawing
     * one at all, because it looks like the game rather than like a missing file. */
    return got == PIC_PIXEL_BYTES ? 0 : 1;
}

int u2_pic_show(u2_ctx *ctx, const char *base, struct u2_frame *f)
{
    if (!u2_pic_backend_has_frames(ctx))
        return U2_PIC_ERR_FRAMES;
    /* EGA first when the art axis asks for it: the fan pack's PIC*-E is a full
     * 16-colour image and the richer art wins. If it is absent the CGA pic is
     * used unchanged, so --ega never blanks a splash that CGA could show.
     */
    if (ctx->art_mode == U2_ART_EGA && u2_pic_can_show_ega(ctx, base)) {
        if (u2_pic_blit_ega(ctx, base, f) == 0)
            return U2_PIC_SHOWN_EGA;
    }
    if (!u2_pic_can_show(ctx, base))
        return U2_PIC_ERR_NOFILE;
    if (u2_pic_blit(ctx, base, f) != 0)
        return U2_PIC_ERR_BLIT;
    return U2_PIC_SHOWN_CGA;
}

int u2_pic_backend_has_frames(const u2_ctx *ctx)
{
    return (ctx && ctx->renderer && ctx->renderer->present_frame) ? 1 : 0;
}

int u2_pic_can_show(u2_ctx *ctx, const char *name)
{
    if (!u2_pic_backend_has_frames(ctx))
        return 0;
    FILE *f = pic_open(ctx, name);
    if (!f)
        return 0;
    const long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    fclose(f);
    return size >= PIC_PIXEL_BYTES;
}

int u2_pic_blit(u2_ctx *ctx, const char *name, struct u2_frame *f)
{
    if (!ctx || !name || !f)
        return 1;

    static uint8_t raw[PIC_PIXEL_BYTES];
    if (pic_read(ctx, name, raw, sizeof raw) != 0) {
        u2_trace(ctx, "pic", "535A", "name=%s loaded=0", name);
        return 1;
    }

    /* Each scanline pair comes from ONE 80-byte row of the file: line 2n and line
     * 2n+1 both come from byte (line/2)*80. The art is therefore 320x100, displayed
     * doubled to fill the 320x200 screen, and only the first 8,000 bytes of the
     * file are pixels.
     *
     * This used to read line 2n+1 from offset 8000 on the theory that the files are
     * literal mode-4 framebuffer dumps, even field then odd field. That theory was
     * resting on a colour census, and a census cannot tell these two layouts apart:
     * both produce exactly 11,567 inked pixels for picdra. What decides it is
     * position, and the original was captured and measured on 2026-11-21
     * (captures/spice86/cga-splash-live/). Against that capture, per scanline:
     *
     *   displayed row 2n == file row n        187 of 187 rows EXACT (two pictures)
     *   displayed row 2n+1 ~= file row n      mean 308/320 and 276/320
     *   displayed row 2n+1 == file row 100+n  mean 223/320 and 196/320
     *
     * The old mapping put the second half on the odd scanlines, which is why every
     * picture came out horizontally doubled and combed: the left half of a
     * displayed row was one file row and the right half was a different one. The
     * owner reported exactly that on 2026-11-20.
     *
     * What is NOT established, stated plainly because the old comment overstated it:
     * the routine that moves these bytes has still not been decoded. asm/5351 points
     * DS at 0xB800 and asm/535A reads ONE record into DS:0x245, which is not 16,000
     * bytes, so neither of them is the picture loader. The mapping above is an
     * empirical fit to a live capture, and it reproduces the original at 98.2%
     * (dragon) and 93.1% (town) over the picture rows. The second half of each file
     * is not pixels, but it is not junk either: it is a horizontally shifted copy of
     * the first half (picout by 22 px, pictwn by 36 px), which is unexplained and
     * belongs to whoever decodes the loader.
     *
     * Within a byte the leftmost pixel is still the top two bits, the same
     * convention the tile art uses (frame.h), so the two paths agree.
     */
    uint8_t rowbuf[U2_SCREEN_W];
    for (int line = 0; line < U2_SCREEN_H; line++) {
        const uint8_t *src = raw + (line >> 1) * U2_SCREEN_ROW;
        for (int b = 0; b < U2_SCREEN_ROW; b++) {
            const uint8_t v = src[b];
            rowbuf[b * 4 + 0] = (uint8_t)((v >> 6) & 3);
            rowbuf[b * 4 + 1] = (uint8_t)((v >> 4) & 3);
            rowbuf[b * 4 + 2] = (uint8_t)((v >> 2) & 3);
            rowbuf[b * 4 + 3] = (uint8_t)(v & 3);
        }
        /* The pictures are 320x200 art whatever the theme, so at a larger frame
         * scale they are block-upscaled rather than left as an island in the
         * corner. Nearest-neighbour is the only honest choice for 4-colour art. */
        for (int x = 0; x < U2_SCREEN_W; x++)
            /* Through u2_frame_cga_idx, not straight in. This is 4-colour CGA art,
             * and the frame it lands on may be presenting through the 16-colour EGA
             * table (EGA mode with no EGA copy of this picture, or the enhanced
             * layout). CGA's black/cyan/magenta/white already exist in the EGA table
             * at 0/3/5/15, and the frame flag says when to make that jump; it is
             * identity on the CGA path, so classic output is unchanged. Without it a
             * CGA picture on an EGA palette renders white as cyan and magenta as
             * green, which is the noise the owner reported on 2026-11-20. */
            u2_frame_put_logical(f, x, line, u2_frame_cga_idx(f, rowbuf[x]));
    }
    u2_trace(ctx, "pic", "535A", "name=%s loaded=1 bytes=%d", name, PIC_PIXEL_BYTES);
    return 0;
}
