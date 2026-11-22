/* u2_scenario.h — one way to arm the engine inside a C check.
 *
 * The problem this exists for: a check that wants to run a command in-process has to arm
 * a u2_ctx and a u2_overworld by hand, and the arming is not one line. It is a memset of
 * both, u2_vram_init, u2_state_init, a frame init, an input backend (because the game
 * waits for a key and a check that never supplies one hangs instead of failing), and a
 * clock backend (because the seams go through it and the check links the game core, not
 * the platform layer). Five checks had independently written all of that, and two of them
 * found the hard way that `.wait` is not optional — seam_key_wait calls input->wait, and
 * a backend that only fills .poll is a call through a NULL pointer. commerce_check
 * failed at address 0x0 before somebody added it.
 *
 * So the arming lives here. It is a header of static-inline functions rather than a
 * compiled unit because a check is a single translation unit compiled against libu2.a,
 * and a fixture that needed its own object file would make every check link two sources
 * for no gain.
 *
 * What deliberately stays out of it: a backend whose *behaviour is the subject*.
 * dirkey_check and keywait_check exist to pin the input seam's answer alphabet, and
 * their fake answers -1 and writes nothing to *out on 0 and -1 on purpose. Folding that
 * into a shared fixture would put the thing under test inside the harness, which is the
 * same mistake as a gate that supplies the dependency it claims is absent. Those two
 * keep their own backends, and say so.
 */
#ifndef U2_SCENARIO_H
#define U2_SCENARIO_H

#include <string.h>

#include "core/ctx.h"
#include "core/frame.h"
#include "core/state.h"
#include "core/vram.h"
#include "core/world.h"
#include "game/overworld.h"

/* The scripted input. `keys` is a NUL-terminated string consumed one byte per poll;
 * when it is spent the backend answers -1, which is what a closed window looks like
 * and what makes a check fail fast instead of waiting forever. */
typedef struct u2_sc_input {
    const char *keys;
    int at;
    /* Optional: called just before each key is delivered, so a check can snapshot the
     * screen at the instant the game is parked waiting for input. That instant is the
     * only place some prompts can be observed, and death_check needs it. */
    void (*on_poll)(void *user);
    void *user;
} u2_sc_input;

static u2_sc_input u2_sc_in;   /* one per translation unit; checks are single-TU */

static inline int u2_sc_poll(u2_ctx *ctx, u2_key *out)
{
    (void)ctx;
    if (u2_sc_in.keys && u2_sc_in.keys[u2_sc_in.at]) {
        if (u2_sc_in.on_poll) u2_sc_in.on_poll(u2_sc_in.user);
        memset(out, 0, sizeof *out);
        out->ch = (unsigned char)u2_sc_in.keys[u2_sc_in.at++];
        out->scan = 0x1E;          /* a real letter scancode; the seam may replace it */
        return 1;
    }
    return -1;
}

/* Not optional, and see the comment above: seam_key_wait calls this. */
static inline int u2_sc_wait(u2_ctx *ctx, u2_key *out) { return u2_sc_poll(ctx, out); }
static inline int u2_sc_init(u2_ctx *ctx) { (void)ctx; return 0; }
static inline void u2_sc_shutdown(u2_ctx *ctx) { (void)ctx; }

static const u2_input u2_sc_input_backend = {
    .name = "scenario-script", .init = u2_sc_init, .poll = u2_sc_poll,
    .wait = u2_sc_wait, .shutdown = u2_sc_shutdown,
};

static inline uint64_t u2_sc_tick(u2_ctx *ctx) { (void)ctx; return 0; }
static inline void u2_sc_sleep(u2_ctx *ctx, uint32_t ms) { (void)ctx; (void)ms; }

/* The clock the game core needs and the platform layer normally supplies. Doing nothing
 * is correct here: the key is already in the queue, so a real delay would only slow the
 * check down, and a check that sleeps is a check somebody will be tempted to skip. */
static const u2_clock u2_sc_clock_backend = {
    .name = "scenario-null-clock", .tick = u2_sc_tick, .sleep_ms = u2_sc_sleep,
};

/* Arm a context only, for a check whose subject takes a u2_ctx and never touches the
 * overworld — u2_death is the case that exists. u2_sc_arm is this plus a frame. */
static inline void u2_sc_arm_ctx(u2_ctx *ctx, const char *keys)
{
    memset(ctx, 0, sizeof *ctx);
    u2_vram_init(&ctx->vram);
    u2_state_init(&ctx->state);
    ctx->input = &u2_sc_input_backend;
    ctx->clock = &u2_sc_clock_backend;
    u2_sc_in.keys = keys;
    u2_sc_in.at = 0;
    u2_sc_in.on_poll = NULL;
    u2_sc_in.user = NULL;
}

/* Arm a context and an overworld for a check. `keys` is the whole script; pass "" for a
 * check that drives nothing through the seam and only calls a function directly.
 *
 * The frame is initialised because a command that draws would otherwise write into a
 * zeroed u2_frame whose dimensions are also zero, and the draw would silently do
 * nothing — a check that asserts on pixels would then fail for the wrong reason. */
static inline void u2_sc_arm(u2_ctx *ctx, u2_overworld *ow, const char *keys)
{
    u2_sc_arm_ctx(ctx, keys);
    memset(ow, 0, sizeof *ow);
    u2_frame_init(&ow->frame, 1);
}

/* Refill the script partway through a run, e.g. when one command consumes two keys and
 * the next leg needs a different answer. */
static inline void u2_sc_replay(u2_ctx *ctx, const char *keys)
{
    (void)ctx;
    u2_sc_in.keys = keys;
    u2_sc_in.at = 0;
}

/* Is the needle anywhere on the character grid? Row and column are deliberately not
 * returned: where a print lands is frequently a low-confidence decode, and a helper
 * that hands back coordinates invites a check to assert on them. */
static inline int u2_sc_grid_has(u2_ctx *ctx, const char *needle)
{
    char row[U2_COLS + 1];
    for (int r = 0; r < U2_ROWS; r++) {
        for (int c = 0; c < U2_COLS; c++) row[c] = (char)ctx->vram.cell[r][c].ch;
        row[U2_COLS] = '\0';
        if (strstr(row, needle)) return 1;
    }
    return 0;
}

/* One row of the screen, trimmed of leading and trailing spaces, over [c0, c1]. */
static inline const char *u2_sc_row(u2_ctx *ctx, int row, char *buf, int c0, int c1)
{
    int n = 0;
    for (int c = c0; c <= c1 && n < U2_COLS; c++) buf[n++] = (char)ctx->vram.cell[row][c].ch;
    while (n > 0 && buf[n - 1] == ' ') n--;
    buf[n] = '\0';
    char *p = buf;
    while (*p == ' ') p++;
    return p;
}

/* A sector that is loaded and filled with one tile, so "the world is there" is true
 * without any check needing a game file. `tile` 0x0C is land; a check that wants a
 * feature (a door, a chest, a signpost) stamps the one cell it cares about after this. */
static inline void u2_sc_flat_map(u2_overworld *ow, uint8_t tile)
{
    ow->world.map_loaded = 1;
    for (int i = 0; i < U2_SECTOR_EDGE * U2_SECTOR_EDGE; i++)
        ow->world.map[i] = tile;
}

#endif /* U2_SCENARIO_H */
