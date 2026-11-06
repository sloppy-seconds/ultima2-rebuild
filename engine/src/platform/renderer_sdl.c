/*
 * renderer_sdl.c — SDL2 bitmap backend.
 *
 * Compiled only in the `make sdl` build (-DU2_HAVE_SDL). The default build globs
 * every file under src/platform and must keep working with nothing but a C
 * compiler, so the whole file is empty unless SDL was asked for.
 *
 * This is a presentation shell and nothing more: it uploads the u2_frame the
 * engine already composed, and for cell-only flows (title, menu, creation)
 * renders the vram grid through core/font into its own text layer. All
 * rendering semantics — geometry, palette, the do-not-draw rule, clipping —
 * live in core/frame.c and core/font.c, so a backend cannot get the game
 * wrong. That is the point of the split, and it is what makes a hi-res or
 * alternative renderer a *backend* rather than a fork.
 */

#ifdef U2_HAVE_SDL

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#  include <direct.h>
#  define U2_MKDIR(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  define U2_MKDIR(p) mkdir(p, 0755)
#endif

#include "core/ctx.h"
#include "core/font.h"
#include "core/frame.h"
#include "core/layout.h"   /* U2_ENH_W/H for the enhanced window size */
#include "core/scale.h"

#define SDL_SCALE 2      /* default window size: 320x200 -> 640x400. Only the
                          * starting size; the player can resize and the frame
                          * refits by integer scale from then on. */

typedef struct sdl_video {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    uint32_t     * staging;   /* w*h RGBA, filled from the indexed frame */
    int           tex_w, tex_h;   /* the size tex and staging are actually built for.
                                   * They MUST be the frame's size, not a constant:
                                   * the classic frame is 640x400 and the enhanced
                                   * one is 1024x512, and filling or uploading a
                                   * 1024x512 frame into a 640x400 buffer is a heap
                                   * overflow that only shows up when a frame is
                                   * really presented, so every headless path passes
                                   * and the windowed build segfaults. */
    Uint32        pal[16];    /* CGA fills 0..3 (rest black); EGA fills all 16.
                               * The upload indexes with & 15, so a CGA frame
                               * (values 0..3) reads the same four entries. */
    uint8_t       pal_src[16][3];  /* the RGB table v->pal was built from, so a
                                    * Ctrl-T cycle into a themed palette is
                                    * noticed and the mapping is rebuilt */
    int           pal_from_theme;
    unsigned      frames;
    const struct u2_frame *last;  /* last frame uploaded, so a window resize can
                                   * repaint without the game drawing a new one */
    /* Text layer for the cell-only screens (title, menu, creation). The
     * overworld composites its own text rows into the game frame, so this
     * one belongs to the renderer and only serves flows that draw through
     * renderer->cell. The grid copy exists so the cursor can be painted
     * inverse at flush time and cleanly restored when it moves. */
    u2_frame      text;
    uint8_t       grid[U2_ROWS][U2_COLS];
    int           cur_row, cur_col, cur_vis;
    int           dumped;     /* --dump-screen already written */
} sdl_video;

static sdl_video *sv(u2_ctx *ctx) { return (sdl_video *)ctx->be_render; }

/* The window and texture follow the theme: a 16x16-tile theme is 320x200 and a
 * 32x32-tile theme is 640x400, because the map window is the same 20x10 cells
 * either way and shrinking the view to keep the window size would change what
 * the player can see, which is a rules change wearing an art flag. */
static int sdl_frame_scale(const u2_ctx *ctx)
{
    return ctx && ctx->frame_scale > 1 ? ctx->frame_scale : 1;
}

/* One driver name per line, for --video-drivers. Does not initialise video, so it
 * works on a machine with no display at all — which is exactly where the release
 * gate needs to run it. */int u2_sdl_list_video_drivers(void)
{
    const int n = SDL_GetNumVideoDrivers();
    for (int i = 0; i < n; i++)
        printf("%s\n", SDL_GetVideoDriver(i));
    return n;
}

/* The version of the SDL actually linked, asked at runtime rather than stamped at
 * compile time, because the two can disagree: sdl2-compat reports an SDL2 version
 * number while being SDL3 underneath, and that gap is the thing
 * docs/sdl2-vs-sdl3.md is about. No SDL_Init here, so it is safe to call before
 * any subsystem exists. */
void u2_sdl_runtime_info(char *out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    /* SDL3 returns one packed integer instead of filling a struct. The packing is
     * major*1e6 + minor*1e3 + patch, so 3004018 is 3.4.18; decoding it by hand
     * rather than with SDL_VERSIONNUM keeps this readable without pulling in the
     * version header's macro name. */
    const int v = SDL_GetVersion();
    snprintf(out, n, "%d.%d.%d", v / 1000000, (v / 1000) % 1000, v % 1000);
}

/* Why a window could not be created is usually a one-word answer ("No available
 * video device" means this binary has no display backend compiled in), and SDL
 * does not say which backends it does have. Printing the list turns "nothing
 * appeared" into something a player can act on, and it is the same line the
 * release gate greps. */
static void sdl_note_available_drivers(void)
{
    const int n = SDL_GetNumVideoDrivers();
    const char *hint = getenv("SDL_VIDEODRIVER");

    fprintf(stderr, "u2: video drivers in this binary (%d):", n);
    for (int i = 0; i < n; i++)
        fprintf(stderr, " %s", SDL_GetVideoDriver(i));
    fputc('\n', stderr);
    fprintf(stderr, "u2: SDL_VIDEODRIVER=%s\n", hint && *hint ? hint : "(unset)");
    if (n <= 2)
        fprintf(stderr, "u2: no display backend was compiled in — this binary was built"
                        " from an SDL3 configured without X11 or Wayland dev packages."
                        " It can only render headlessly.\n");
}

static int sdl_init(u2_ctx *ctx)
{
    sdl_video *v = calloc(1, sizeof *v);
    if (!v) return -1;
    const int fs = sdl_frame_scale(ctx);

    /* SDL3 returns bool, where SDL2 returned 0 for success. Inverting the test
     * rather than the call is the whole change, but it is the kind of change that
     * compiles either way and silently inverts the meaning: `!= 0` on a bool would
     * have made every successful init look like a failure and every failed one
     * look fine. */
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "sdl: %s\n", SDL_GetError());
        free(v);
        return -1;
    }

    /* SDL3 dropped SDL_HINT_RENDER_SCALE_QUALITY and made the default LINEAR
     * filtering, so nearest-neighbour is now per-texture and must be asked for
     * explicitly after the texture exists. This is the one change on this list
     * that fails silently and beautifully: a bilinear upscale of 2bpp CGA still
     * draws, it just smears every pixel into a gradient, and a screenshot taken
     * in a hurry looks like a plausible retro blur rather than a bug. */
    /* The enhanced layout is a native 640x480 canvas, not a doubled 320x200, so
     * the window cannot be derived from U2_SCREEN_* times a scale there. Deriving
     * it anyway would open a 640x400 window and clip the bottom bar and the last
     * two rows of the command box, which reads as a drawing bug rather than a
     * sizing one. */
    const int win_w = ctx->enhanced_layout ? U2_ENH_W : U2_SCREEN_W * fs * SDL_SCALE;
    const int win_h = ctx->enhanced_layout ? U2_ENH_H : U2_SCREEN_H * fs * SDL_SCALE;
    v->win = SDL_CreateWindow("Ultima II (engine reconstruction)",
                              win_w, win_h,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!v->win) {
        fprintf(stderr, "sdl: window: %s\n", SDL_GetError());
        sdl_note_available_drivers();
        free(v);
        return -1;
    }
    fprintf(stderr, "u2: video: %s\n",
            SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(unknown)");

    /* SDL3 dropped the renderer flags: NULL means "you choose", which is what the
     * accelerated-then-software ladder used to spell out. The fallback is kept
     * because a machine with a broken GPU driver is exactly where a reconstruction
     * project still wants to run, and SDL_SOFTWARE_RENDERER is now the name rather
     * than a flag. */
    v->ren = SDL_CreateRenderer(v->win, NULL);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, SDL_SOFTWARE_RENDERER);
    if (!v->ren) {
        fprintf(stderr, "sdl: renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(v->win);
        free(v);
        return -1;
    }
    v->tex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_STREAMING,
                               U2_SCREEN_W * fs, U2_SCREEN_H * fs);
    if (!v->tex) {
        fprintf(stderr, "sdl: texture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(v->ren);
        SDL_DestroyWindow(v->win);
        free(v);
        return -1;
    }
    if (!SDL_SetTextureScaleMode(v->tex, SDL_SCALEMODE_NEAREST))
        fprintf(stderr, "u2: WARNING: nearest-neighbour scaling refused (%s);"
                        " 2bpp art will be smeared\n", SDL_GetError());
    v->staging = malloc(sizeof(uint32_t) * (size_t)(U2_SCREEN_W * fs) *
                        (size_t)(U2_SCREEN_H * fs));
    v->tex_w = U2_SCREEN_W * fs;
    v->tex_h = U2_SCREEN_H * fs;
    if (!v->staging) {
        SDL_DestroyTexture(v->tex);
        SDL_DestroyRenderer(v->ren);
        SDL_DestroyWindow(v->win);
        free(v);
        return -1;
    }

    uint8_t pal[16][3];
    const int ega = (ctx->art_mode == U2_ART_EGA);
    if (ega) {
        u2_frame_palette_ega(pal);
    } else {
        u2_frame_palette(pal);          /* fills pal[0..3] */
        for (int i = 4; i < 16; i++)    /* CGA never indexes past 3; keep the
                                         * rest defined so & 15 is well-formed */
            { pal[i][0] = pal[i][1] = pal[i][2] = 0; }
    }
    /* A v2 theme may bring its own RGB table (docs/handoff-themes-builder.md
     * § 4.1). The FM Towns palette is not an EGA palette — index 3 is black there
     * — and drawing that art through the EGA table makes it read as noise while
     * every byte-level check passes, which is the failure this whole thread
     * started from. The theme's table wins whenever it exists. */
    if (ctx->theme_palette_set) {
        memcpy(pal, ctx->theme_palette, sizeof pal);
        v->pal_from_theme = 1;
    }
    memcpy(v->pal_src, pal, sizeof v->pal_src);
    /* SDL3 replaced SDL_AllocFormat with a read-only details struct, and MapRGBA
     * now takes that plus an explicit palette (NULL: RGBA8888 has none). The
     * details pointer is owned by SDL and must not be freed. */
    {
        const SDL_PixelFormatDetails *f = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);
        if (!f) {
            free(v->staging); SDL_DestroyTexture(v->tex);
            SDL_DestroyRenderer(v->ren); SDL_DestroyWindow(v->win); free(v);
            return -1;
        }
        for (int i = 0; i < 16; i++)
            v->pal[i] = SDL_MapRGBA(f, NULL, pal[i][0], pal[i][1], pal[i][2], 255);
    }

    ctx->be_render = v;
    if (u2_frame_init(&v->text, fs) != 0) {
        free(v->staging); SDL_DestroyTexture(v->tex);
        SDL_DestroyRenderer(v->ren); SDL_DestroyWindow(v->win); free(v);
        ctx->be_render = NULL;
        return -1;
    }
    u2_frame_clear(&v->text, U2_CGA_BLACK);
    u2_frame_set_text_fg(&v->text, ctx->text_fg);
    for (int r = 0; r < U2_ROWS; r++)
        for (int c = 0; c < U2_COLS; c++)
            v->grid[r][c] = ' ';
    return 0;
}

/* Upload one composed frame and present it. Shared by the game-frame path
 * (present_frame) and the text-layer path (flush), so both screens show the
 * same palette and the same scaling and neither can drift from the other. */
/* Build (or rebuild) the texture and staging buffer for this frame's size.
 *
 * They used to be allocated once at init from U2_SCREEN_W/H * scale, which was
 * true only while every frame was a scaled 320x200. The enhanced layout is
 * 1024x512, so the fill loop wrote 1024*512 pixels into a 640*400 buffer and
 * SDL_UpdateTexture read past the end of it at a row pitch the texture did not
 * have. That is a heap overflow, and it only happens on a real present, so every
 * headless and scripted path passed and the windowed build segfaulted the moment a
 * frame reached the screen. Deriving the buffers from the frame that is about to
 * be uploaded removes the whole class: any future frame size is correct by
 * construction instead of by remembering to update a constant. */
static int sdl_ensure_surface(sdl_video *v, const struct u2_frame *f)
{
    if (v->tex && v->staging && v->tex_w == f->w && v->tex_h == f->h)
        return 0;
    if (f->w <= 0 || f->h <= 0) return -1;

    SDL_Texture *tex = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_STREAMING, f->w, f->h);
    if (!tex) {
        fprintf(stderr, "u2: texture %dx%d: %s\n", f->w, f->h, SDL_GetError());
        return -1;
    }
    uint32_t *st = malloc(sizeof(uint32_t) * (size_t)f->w * (size_t)f->h);
    if (!st) { SDL_DestroyTexture(tex); return -1; }

    if (v->tex) SDL_DestroyTexture(v->tex);
    free(v->staging);
    v->tex = tex;
    v->staging = st;
    v->tex_w = f->w;
    v->tex_h = f->h;
    if (!SDL_SetTextureScaleMode(v->tex, SDL_SCALEMODE_NEAREST))
        fprintf(stderr, "u2: WARNING: nearest-neighbour scaling refused (%s)\n",
                SDL_GetError());
    return 0;
}

static void sdl_upload(u2_ctx *ctx, const struct u2_frame *f)
{
    sdl_video *v = sv(ctx);
    const u2_frame *fr = (const u2_frame *)f;
    if (!v || !fr || !fr->px) return;

    /* Re-map when the theme's palette changed under us. A Ctrl-T cycle can land
     * on a v2 theme from a v1 one mid-run, and a stale mapping would draw FM Towns
     * indices through the EGA table: legal, silent, and wrong in exactly the way a
     * screenshot catches and a byte test does not. */
    uint8_t want[16][3];
    if (ctx->theme_palette_set) memcpy(want, ctx->theme_palette, sizeof want);
    else if (ctx->art_mode == U2_ART_EGA) u2_frame_palette_ega(want);
    else { u2_frame_palette(want);
           for (int i = 4; i < 16; i++) { want[i][0]=want[i][1]=want[i][2]=0; } }
    if (memcmp(want, v->pal_src, sizeof want) != 0) {
        const SDL_PixelFormatDetails *fd = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);
        if (fd) {
            for (int i = 0; i < 16; i++)
                v->pal[i] = SDL_MapRGBA(fd, NULL, want[i][0], want[i][1], want[i][2], 255);
            memcpy(v->pal_src, want, sizeof v->pal_src);
            v->pal_from_theme = ctx->theme_palette_set;
        }
    }

    if (sdl_ensure_surface(v, f) != 0) return;

    for (int y = 0; y < fr->h; y++) {
        const uint8_t *src = fr->px[y];
        uint32_t *dst = v->staging + (size_t)y * (size_t)fr->w;
        for (int x = 0; x < fr->w; x++) dst[x] = v->pal[src[x] & 15];
    }
    /* Destination is computed, not NULL. NULL means "fill the output rect", which
     * does scale, but it stretches 320x200 to whatever aspect the window happens
     * to be and makes source pixels rectangular. The fit is integer and centred,
     * so blocks stay square and the remainder becomes letterbox bars. Queried every
     * frame rather than cached from a resize event, because the drawable size is
     * also what changes under a HiDPI scale factor with no resize at all. */
    int ow = 0, oh = 0;
    /* Renamed in SDL3, and it returns bool: a failure here leaves ow/oh at zero,
     * the fit comes back empty, and the frame is cleared to black. Checked rather
     * than ignored, because "the window went black" is otherwise undiagnosable. */
    if (!SDL_GetRenderOutputSize(v->ren, &ow, &oh)) {
        fprintf(stderr, "u2: render output size: %s\n", SDL_GetError());
        return;
    }
    u2_fit fit = u2_fit_integer(ow, oh, fr->w, fr->h);

    if (!SDL_UpdateTexture(v->tex, NULL, v->staging, fr->w * (int)sizeof(uint32_t))) {
        fprintf(stderr, "u2: texture upload failed: %s\n", SDL_GetError());
        return;
    }
    (void)SDL_RenderClear(v->ren);
    if (fit.w > 0 && fit.h > 0) {
        /* SDL_RenderCopy is SDL_RenderTexture in SDL3, and its rectangles are
         * floats. The fit is computed in integers in core/scale.c and widened here
         * rather than the other way round: the integer arithmetic is what the gate
         * in tests/scale_check.c can assert on, and an exact int -> float widening
         * of these magnitudes loses nothing. */
        const SDL_FRect dst_f = { (float)fit.x, (float)fit.y,
                                  (float)fit.w, (float)fit.h };
        (void)SDL_RenderTexture(v->ren, v->tex, NULL, &dst_f);
    }
    (void)SDL_RenderPresent(v->ren);
    v->frames++;
    v->last = fr;

    /* --dump-screen: the frame that was just presented is the window's
     * content, so writing it out proves the cell->font->upload path without
     * a display (SDL_VIDEODRIVER=dummy). The input backend ends the run once
     * this flag is set. */
    if (ctx->dump_screen && !v->dumped) {
        int wrc;
        if (ctx->art_mode == U2_ART_EGA) {
            uint8_t pal[16][3];
            if (ctx->theme_palette_set) memcpy(pal, ctx->theme_palette, sizeof pal);
            else u2_frame_palette_ega(pal);
            wrc = u2_frame_write_ppm16(fr, ctx->dump_screen, pal);
        } else {
            uint8_t pal[4][3];
            u2_frame_palette(pal);
            wrc = u2_frame_write_ppm(fr, ctx->dump_screen, pal);
        }
        if (wrc == 0)
            ctx->dump_screen_done = 1;
        v->dumped = 1;
    }
}

static int sdl_present_frame(u2_ctx *ctx, const struct u2_frame *f)
{
    sdl_upload(ctx, f);
    return 0;
}

/* Ctrl-S. The point is a bug the player can see but a log cannot show, so this
 * captures the frame that was actually presented, not the grid the game thinks it
 * drew -- the same distinction that made the invisible-cursor bug invisible to
 * every text assertion. BMP because the writer already exists in core/frame.c and
 * needs no image library on any of the three build targets; a PNG would mean
 * linking zlib into the static and mingw builds as well.
 *
 * The path is deliberately not configurable: screenshots are for filing a bug, and
 * `./screenshots/` next to the game is where a person looks first. */
static int sdl_screenshot(u2_ctx *ctx)
{
    sdl_video *v = sv(ctx);
    if (!v || !v->last) {
        fprintf(stderr, "u2: no frame has been presented yet, nothing to capture\n");
        return 1;
    }
    if (U2_MKDIR("screenshots") != 0 && errno != EEXIST) {
        fprintf(stderr, "u2: cannot create screenshots/: %s\n", strerror(errno));
        return 1;
    }
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char path[64];
    if (lt)
        strftime(path, sizeof path, "screenshots/u2-%Y%m%d-%H%M%S.bmp", lt);
    else
        snprintf(path, sizeof path, "screenshots/u2-%ld.bmp", (long)t);

    int rc;
    if (ctx->art_mode == U2_ART_EGA) {
        uint8_t pal[16][3];
        if (ctx->theme_palette_set) memcpy(pal, ctx->theme_palette, sizeof pal);
        else u2_frame_palette_ega(pal);
        rc = u2_frame_write_bmp16(v->last, path, pal);
    } else {
        uint8_t pal[4][3];
        u2_frame_palette(pal);
        rc = u2_frame_write_bmp(v->last, path, pal);
    }
    if (rc != 0) {
        fprintf(stderr, "u2: could not write %s\n", path);
        return 1;
    }
    /* stderr, not the game screen: a message drawn into the frame would be in the
     * next screenshot, which is the opposite of the point. */
    fprintf(stderr, "u2: saved %s\n", path);
    fflush(stderr);
    return 0;
}

/* Re-present the last frame. The window was resized and nothing else will paint
 * it: the game presents when it has something new to say, and a resize is not
 * something the game says. Called from the input backend's event pump, which is
 * the only place SDL_WINDOWEVENT is seen. No-op before the first frame exists. */
void u2_sdl_repaint(u2_ctx *ctx)
{
    sdl_video *v = ctx ? sv(ctx) : NULL;
    if (v && v->last) sdl_upload(ctx, v->last);
}

static void sdl_cell(u2_ctx *ctx, int row, int col, uint8_t ch, uint8_t attr)
{
    sdl_video *v = sv(ctx);
    if (!v || row < 0 || row >= U2_ROWS || col < 0 || col >= U2_COLS) return;
    v->grid[row][col] = ch;
    /* The engine's highlight bit swaps the pair this cell would otherwise be drawn
     * with rather than naming a colour: a theme that ships an RGB table can make
     * any index anything, which is exactly the trap that made the cursor invisible
     * in the FM Towns theme. Everything else keeps the pair it has always used, so
     * honouring the attribute is a no-op on every screen that does not ask for it. */
    if (attr & U2_ATTR_HILITE)
        u2_font_draw_cell(&v->text, row, col, ch, U2_CGA_BLACK, v->text.text_fg);
    else
        u2_font_draw_cell(&v->text, row, col, ch, v->text.text_fg, U2_CGA_BLACK);
}

static void sdl_cursor(u2_ctx *ctx, int row, int col, int visible)
{
    sdl_video *v = sv(ctx);
    if (!v) return;
    v->cur_row = row; v->cur_col = col; v->cur_vis = visible;
}

static void sdl_flush(u2_ctx *ctx)
{
    sdl_video *v = sv(ctx);
    if (!v) return;
    /* The grid is the truth; the cursor is painted on top of it, inverse video of
     * the pair the grid is drawn with, and the previous cell is restored first so
     * a moving cursor cannot leave a stale block behind.
     *
     * That pair is the FRAME's, not the CGA constants, for the reason core/font.c
     * already spells out one file over: a theme that brings an RGB table can make
     * index 3 black, and FM Towns does. U2_CGA_WHITE is 3, so a hardcoded white
     * painted the cursor black-on-black and repainted the character underneath it
     * in black too -- the block vanished and took the glyph with it. That is the
     * report "in the creation screen with the fmtowns theme the cursor is
     * missing", and it is invisible to every text assertion because the grid bytes
     * are correct; only the pixels were wrong. */
    const uint8_t ink = v->text.text_fg;
    const uint8_t bg  = v->text.text_bg;
    const uint8_t ch  = v->grid[v->cur_row][v->cur_col];
    u2_font_draw_cell(&v->text, v->cur_row, v->cur_col, ch, ink, bg);
    if (v->cur_vis)
        u2_font_draw_cell(&v->text, v->cur_row, v->cur_col, ch, bg, ink);
    sdl_upload(ctx, &v->text);
}

static void sdl_shutdown(u2_ctx *ctx)
{
    sdl_video *v = sv(ctx);
    if (!v) return;
    SDL_DestroyTexture(v->tex);
    SDL_DestroyRenderer(v->ren);
    SDL_DestroyWindow(v->win);
    free(v->staging);
    u2_frame_free(&v->text);
    free(v);
    ctx->be_render = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

const u2_renderer u2_renderer_sdl = {
    .name = "sdl",
    .init = sdl_init,
    .cell = sdl_cell,
    .cursor = sdl_cursor,
    .flush = sdl_flush,
    .shutdown = sdl_shutdown,
    .present_frame = sdl_present_frame,
    .screenshot = sdl_screenshot,
};

#endif /* U2_HAVE_SDL */
