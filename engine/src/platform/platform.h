#ifndef U2_PLATFORM_H
#define U2_PLATFORM_H

/*
 * Platform abstraction — the only surface game logic may touch.
 *
 * This is the Phase 3 seam discipline carried over: every routine in the
 * byte-proven asm tree that reaches the hardware goes through one of these
 * four vtables, and nothing in src/game/ may call a terminal escape, an SDL
 * call, clock(), or read(2) directly.
 *
 * Seam map (asm -> API):
 *   char_out        engine primitive, no located asm site -> seam_char_out -> renderer->cell
 *                   (it used to cite asm/4A1C, which is INT 10h AH=00 AL=01 -- a video-mode
 *                   set, not a teletype. The AH=0E teletype site has never been found.)
 *   char_print      asm/5003_char_print.asm -> seam_char_print
 *   str_out         asm/4FDF_str_out.asm    -> seam_str_out   -> renderer->cell
 *   str_out_at      asm/51EB_str_out_at.asm -> seam_goto      -> renderer->cursor
 *   str_out_core    asm/51FD_str_out_core.asm (INT 10h AH=02 cursor set)
 *   key_poll        asm/4F4D_key_poll.asm   -> seam_key_poll  -> input->poll
 *   rand_byte       asm/5217_rand_byte.asm  -> seam_rand_byte -> core/rng.c
 *   timed_wait      asm/52CA_timed_wait.asm -> seam_wait_ms   -> clock->sleep_ms
 *   speaker_freq    asm/5CD2_speaker_freq.asm -> seam_tone    -> audio->tone
 *   video_int10 / key_int16 / clock_int1a / dos_int21 / speaker_tone
 *                   — the raw INT seams labelled throughout asm/; each one
 *                     lands on exactly one of the four vtables below.
 */

#include <stdint.h>
#include <stddef.h>   /* size_t, for u2_sdl_runtime_info */

/*
 * The text grid is 40 columns x 25 rows, not 80x25: the row-clear loop prints
 * 0x28 = 40 spaces (asm/53A0_sub_53a0.asm) and the printable right margin is
 * 0x26/0x28 (asm/456B_sub_456b.asm, asm/48A7_sub_48a7.asm). The original sets
 * video mode 4 (asm/4A3E_sub_4a3e.asm: INT 10h AH=00h AL=04h), i.e. 320x200,
 * which is exactly 40x25 cells of 8x8 pixels.
 */
#define U2_COLS 40
#define U2_ROWS 25

typedef struct u2_ctx u2_ctx;

/* One keyboard event. The original reads the BIOS key queue (INT 16h):
 * AL = character, AH = scancode. Scancode is carried so arrow-key-era
 * additions stay possible without changing the API. */
typedef struct u2_key {
    uint8_t ch;
    uint8_t scan;
} u2_key;

/* Pseudo characters for keys the original reached through INT 16h scan codes.
 * The original only ever used printable keys plus ESC, so these exist for the
 * engine's own interaction improvements (docs/phase4-plan.md § 4a) and are
 * deliberately outside the ASCII range a game string could contain. */
#define U2_KEY_UP    0xF0
#define U2_KEY_DOWN  0xF1
#define U2_KEY_LEFT  0xF2
#define U2_KEY_RIGHT 0xF3

/* Defined in core/frame.h. Forward-declared here so the display seam can refer
 * to a framebuffer without pulling core/frame.h into every translation unit
 * that includes platform.h — frame.h reaches ctx.h through view.h and world.h,
 * so including it here would close a cycle. */
struct u2_frame;

/* Character-cell display. The engine owns the 80x25 grid (core/vram.h);
 * renderers only paint it. */
typedef struct u2_renderer {
    const char *name;
    int  (*init)(u2_ctx *ctx);
    void (*cell)(u2_ctx *ctx, int row, int col, uint8_t ch, uint8_t attr);
    void (*cursor)(u2_ctx *ctx, int row, int col, int visible);
    void (*flush)(u2_ctx *ctx);
    void (*shutdown)(u2_ctx *ctx);
    /* Optional pixel seam, present only on bitmap backends. The original draws
     * everything in mode 4 graphics including its text, so a cell-only renderer
     * is an approximation of the original rather than a model of it; this is the
     * seam that lets a backend show real pixels without the game logic knowing
     * which backend exists. Returns non-zero when the display asked to quit. */
    int  (*present_frame)(u2_ctx *ctx, const struct u2_frame *f);
    /* Optional: save the last presented frame to a file the backend chooses.
     * Present only on bitmap backends, because a cell renderer has no pixels to
     * capture. Returns zero on success and non-zero with the reason already on
     * stderr; the caller does not print again. */
    int  (*screenshot)(u2_ctx *ctx);
} u2_renderer;

typedef struct u2_input {
    const char *name;
    int  (*init)(u2_ctx *ctx);
    /* poll: non-blocking, mirrors key_poll (which at asm/4F4D checks the BIOS
     * queue with AH=01h and only then consumes with AH=00h).
     *  1  = key available, *out filled
     *  0  = no key right now
     * -1  = input is finished for good (script exhausted, stdin closed) */
    int  (*poll)(u2_ctx *ctx, u2_key *out);
    /* wait: blocking read (INT 16h AH=00). Same alphabet as poll above, and it
     * matters: 1 means *out was written and nothing else does. The SDL backend
     * answers -1 for a quit request (window closed, SIGINT/SIGTERM), so a caller
     * that tests only for 0 reads a key that never arrived. seam_key_wait collapses
     * the three states to 1/0 for exactly that reason; see its comment. */
    int  (*wait)(u2_ctx *ctx, u2_key *out);
    void (*shutdown)(u2_ctx *ctx);
} u2_input;

/* PC speaker. divisor is the 8253 latch value the asm wrote with
 * out 0x42 (asm/5CD2_speaker_freq.asm); ms is how long to hold it. */
typedef struct u2_audio {
    const char *name;
    int  (*init)(u2_ctx *ctx);          /* optional */
    void (*tone)(u2_ctx *ctx, uint16_t divisor, uint32_t ms);
    void (*shutdown)(u2_ctx *ctx);      /* optional */
} u2_audio;

/* The original keeps its PRNG state in the DS segment (DS:0x23F..0x244, seeded
 * at asm/0100_sub_0100.asm) — see core/rng.h. The clock is only what
 * timed_wait waits on; no randomness is derived from it. */
typedef struct u2_clock {
    const char *name;
    int      (*init)(u2_ctx *ctx);      /* optional */
    uint64_t (*tick)(u2_ctx *ctx);
    void     (*sleep_ms)(u2_ctx *ctx, uint32_t ms);
    void     (*shutdown)(u2_ctx *ctx);  /* optional */
} u2_clock;

/* Backends (selected in main.c by mode; never by game logic). */
extern const u2_renderer u2_renderer_term;
extern const u2_renderer u2_renderer_null;
extern const u2_input    u2_input_term;
extern const u2_input    u2_input_script;
extern const u2_audio    u2_audio_beep;
extern const u2_audio    u2_audio_null;
extern const u2_clock    u2_clock_posix;
extern const u2_clock    u2_clock_script;

/* The wall clock is the one backend chosen by the host OS rather than by the
 * mode, and it is compiled per platform: the Makefile's win target builds
 * time_win32.c instead of time_posix.c, because mingw links neither
 * clock_gettime nor nanosleep. */
extern const u2_clock    u2_clock_win32;

/* The SDL backends exist only in the `make sdl` build, so the default cc-only
 * build never references them and needs no third-party headers. */
#ifdef U2_HAVE_SDL
extern const u2_renderer u2_renderer_sdl;
extern const u2_input    u2_input_sdl;
extern const u2_audio    u2_audio_sdl;
/* Print the video drivers compiled into this SDL2 and return how many there
 * were. Backs --video-drivers, and the release gate that refuses to ship a
 * player binary with no display backend. */
int u2_sdl_list_video_drivers(void);
/* Re-present the last frame after the window changed size. The game presents when
 * it has something new to draw and a resize is not that, so the input backend's
 * event pump has to ask for it. Defined in renderer_sdl.c; a no-op before the
 * first frame. */
void u2_sdl_repaint(u2_ctx *ctx);
/* Write the version of the SDL implementation actually linked (e.g. "2.32.74")
 * into out. Called by core/version.c so a binary can report what it is holding:
 * a stamp that says sdl2 with a runtime that disagrees means a compatibility
 * layer, which is the failure docs/sdl2-vs-sdl3.md is about. */
void u2_sdl_runtime_info(char *out, size_t n);
#endif

#endif /* U2_PLATFORM_H */
