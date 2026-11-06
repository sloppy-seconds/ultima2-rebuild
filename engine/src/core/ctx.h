#ifndef U2_CTX_H
#define U2_CTX_H

/*
 * Engine context — one struct, passed everywhere. No globals in the engine:
 * a headless playthrough must be able to run several contexts side by side
 * (the MCP play-API at CP2 depends on that).
 */

#include <stdio.h>
#include <stdint.h>
#include "platform/platform.h"
#include "core/vram.h"
#include "core/state.h"

typedef enum u2_mode {
    U2_MODE_TERM = 0,   /* interactive terminal */
    U2_MODE_HEADLESS = 1/* null renderer, scripted or piped input */
} u2_mode;

typedef enum u2_fidelity {
    U2_FIDELITY_ORIGINAL = 0,   /* byte-comparison baseline behaviour */
    U2_FIDELITY_ENHANCED = 1    /* same rules; dead ends removed */
} u2_fidelity;

/* Which art set the display draws from — an independent axis from `fidelity`
 * (which is a rules axis) and from `mode` (a renderer axis). It changes no game
 * rules, so it is available in both fidelity modes.
 *   U2_ART_CGA  the original 4-colour path: CGA tiles extracted from the
 *               executable (or the cleanroom substitutes) and the CGA pics.
 *               The shipped default; byte-for-byte what the engine drew before
 *               EGA existed.
 *   U2_ART_EGA  the fan pack's 16-colour EGATILES atlas and PIC*-E pictures,
 *               read from the resolved themes dir. Falls back to CGA whenever
 *               the EGA art is absent, so it is never a launch failure.
 * Chosen at launch with --ega; see docs/ega-graphics-plan.md. */
typedef enum u2_art_mode {
    U2_ART_CGA = 0,
    U2_ART_EGA = 1
} u2_art_mode;

/* Which front end to wire, chosen by --renderer (or the legacy --sdl alias).
 * AUTO is resolved in main() from how the binary was *built*: an SDL build opens
 * the window, a cc-only build uses the text grid. That is what lets a downloaded
 * player binary need no flags at all while every test script keeps driving the
 * terminal build it has always driven. Asking for a renderer the binary was not
 * compiled with is an error, never a silent fall back -- a flag whose whole job
 * is "show me a window" must not be the one thing that fails quietly. */
typedef enum u2_renderer_choice {
    U2_RENDERER_AUTO = 0,
    U2_RENDERER_SDL = 1,
    U2_RENDERER_TERMINAL = 2
} u2_renderer_choice;

typedef struct u2_ctx {
    u2_mode mode;
    /* Fidelity axis — independent of `mode`, which is a renderer axis.
     *   U2_FIDELITY_ORIGINAL  reproduce the original's behaviour, including its
     *                         dead ends and its bugs (default; what the CP2
     *                         byte-comparison baseline is measured against)
     *   U2_FIDELITY_ENHANCED  same rules and state, but dead ends the original
     *                         could not avoid may be removed — the CP/M disk-swap
     *                         restart after character creation is the first one.
     * Chosen at launch with --enhanced; never switched mid-run. */
    u2_fidelity fidelity;
    /* Art axis — independent of `fidelity` and `mode`. U2_ART_CGA is the 4-colour
     * path; U2_ART_EGA selects the fan pack's 16-colour atlas and pictures.
     * Read once at launch. It is no longer always a named choice: when nothing
     * forced it, main() infers EGA from an installed themes tree (see
     * u2_tiles_ega_autodetect in core/tiles_ega.h, which also documents why that
     * inference deliberately skips the dev-checkout fallbacks). The theme and
     * themes-dir refine where the EGA art is found. */
    u2_art_mode art_mode;
    const char *ega_theme;       /* --ega-theme NAME; NULL means "default" */
    const char *ega_theme_dir;   /* --ega-theme-dir PATH; NULL means use the
                                  * resolution order in core/tiles_ega.h */
    /* What the active theme's tile size implies for the frame: 1 for CGA and
     * every v1 EGA theme (16x16 tiles, 320x200), 2 for a 32x32 theme such as the
     * FM Towns one, which needs a 640x400 window to show the same 20x10 cells at
     * the art's own resolution. Set by the theme loader (validate and open both
     * record it) before any frame is allocated; a frame built at the wrong scale
     * draws a theme at half size or overruns the window, so it is read once at
     * startup and never changed under a running frame. */
    int frame_scale;
    /* The enhanced screen layout: map on the left, three status boxes on the
     * right, at a native 640x480. Turned on by --enhanced, which also implies the
     * EGA art axis because a 640x480 canvas has room for a 32px theme by
     * definition — the owner ruled --ega redundant under --enhanced rather than
     * orthogonal to it. Read once at launch, like frame_scale: a layout that
     * changed under a running frame would resize the window mid-play, which is the
     * exact failure the frame-scale rule exists to prevent.
     * core/layout.h holds the geometry; docs/enhanced-layout-plan.md the reasoning. */
    int enhanced_layout;
    /* Where the player's own cell sits inside the view window, in view cells.
     * The classic layout's (9,5) is asm/5AE1's, and the enhanced layout's comes
     * from core/layout.h. It lives on ctx rather than being read off the view so
     * that game logic far from the renderer — the swamp flash, a melee contact —
     * can say "flash the player" without threading the overworld struct through
     * every call. One source: set where the view shape is set. */
    int screen_player_col, screen_player_row;
    /* The active theme's own RGB table, when it ships one (a v2 PALETTE.RGB).
     * The renderer uses this instead of the built-in EGA table whenever
     * theme_palette_set is true, and re-reads it on each upload so a Ctrl-T cycle
     * into or out of a themed palette takes effect without a restart. Zero means
     * "no theme palette", which is every v1 theme: what EGATHEME.10 and .C64 ship
     * as EGACOLOR is an index remap for the fan driver, not RGB
     * (docs/ega-graphics.md § 3.1). */
    int theme_palette_set;
    uint8_t theme_palette[16][3];
    /* The palette index the text layer should use for ink under the active theme.
     * 0 means "unset, use CGA white (3)". A theme that ships an RGB table gets the
     * brightest entry of it, because the FM Towns palette has index 3 as black and
     * the CGA pair then draws status text in the background colour. */
    uint8_t text_fg;
    const u2_renderer *renderer;
    const u2_input    *input;
    const u2_audio    *audio;
    const u2_clock    *clock;

    u2_vram  vram;
    u2_state state;

    /* The framebuffer a bitmap backend presents, owned by whoever allocates it
     * (the overworld screen today) and NULL for cell-only flows. Kept as a
     * pointer so a text-only run pays nothing for pixel support. */
    struct u2_frame *frame;

    struct {
        int   enabled;      /* --trace */
        FILE *out;          /* stderr by default */
    } trace;

    int dump_state;         /* --dump-state */
    /* --dump-frame FILE: write the last composed framebuffer to FILE as a PPM when the
     * overworld loop ends. --dump-map renders the OPENING frame and returns; this is the
     * one that can show what a scripted run ended looking at, which is what CP7's
     * wireframe gate needs — a dungeon frame only exists after ENTER and IGNITE. */
    const char *frame_dump_path;
    /* Checkpoint file for --load-ds: whitespace-separated hex bytes of the DS mirror,
     * applied after the save AND after the world/monx load, because both of those overwrite
     * fields a mid-session checkpoint carries. NULL means no checkpoint. */
    const char *load_ds_path;
    /* --trace-rolls FILE: one line per rand_byte() call, tagged with the calling function.
     * NULL when not tracing. This exists because roll COUNT alone was not the diagnosis: the
     * engine and the original consumed the same number of rolls on a turn and still ended up with
     * different creature positions, which means the rolls were spent on different decisions. Only
     * a per-site trace shows that. */
    FILE *roll_trace;
    /* Diagnostic only, never read by game logic: the count of grid cells that look occupied
     * but hold no creature, refreshed each composed frame where the world is at hand.
     * "He left a copy of him behind", made checkable. See u2_monsters_ghost_cells(). */
    int diag_ghosts;
    /* Diagnostic only: wound flashes rendered since the run began (each on+off
     * pair is one). The flash leaves the frame bit-identical to how it found it,
     * so without a counter no gate can see it happened at all — the same
     * announce-yourself rule as g_unmeasured_tiles. Wired by E1;
     * captures/spice86/wound-flash-20261102 is what it counts against. */
    unsigned diag_flashes;
    /* The mirror diagnostic: creature slots that are NOT stamped into the grid, i.e. invisible
     * monsters. diag_ghosts == 0 says nothing about this direction, which is how a town entry
     * could report a clean frame while every NPC in it was unrenderable. */
    int diag_unstamped;
    /* Latched high-water mark of diag_unstamped measured at the moment a sector becomes
     * resident, BEFORE any restamp and before the post-command creature sweep can repair
     * it. Without the latch a gate cannot distinguish "stamped on load" from "stamped by the
     * first sweep", which is exactly how a town-entry defect survived a passing test. */
    int diag_unstamped_at_load;

    /* Set by the overworld loop to a function that composites and presents a whole frame.
     * A handler that prints a prompt and then blocks reaches it through
     * seam_key_wait_shown, because the presenting loop sits ABOVE the handler: without that
     * call the question lands on screen only when the handler returns, which is after the
     * player typed blind. Null outside the overworld, where the text-layer flush alone is the
     * whole screen. See seam.h for why this is not folded into seam_key_wait itself. */
    void (*present_now)(struct u2_ctx *ctx);
    int dump_each;          /* --dump-each: print the grid after every present */
    int dump_hilite;        /* --dump-hilite: print the highlighted cells after
                             * every present. The grid dump carries characters
                             * only, so this is the only headless way to see the
                             * creation form's inverse-video highlight. */
    const char *dump_screen;/* --dump-screen F: SDL build saves the first
                             * presented frame to F as PPM, then the run ends.
                             * With SDL_VIDEODRIVER=dummy this makes the whole
                             * cell->font->upload path testable with no display. */
    int dump_screen_done;   /* set by the renderer after the dump */
    /* --debug cli: per-turn frame + state files. See debug/turnlog.h — this is the
     * answer to "how does an agent look at a game a person is playing right now",
     * which no combination of the startup dump flags can do. */
    int debug_cli;
    const char *debug_path;   /* --debug-path DIR; NULL until parsed */
    unsigned debug_limit;     /* --debug-limit N: stop after N turns. 0 = unlimited. */
    int selftest;           /* --selftest: draw the seam probe screen and stop */
    uint32_t rng_rolls;     /* rolls served, for dumps and tests */
    unsigned seed;          /* PRNG seed byte, i.e. the 0x40:6C value (core/rng.h) */
    int keys_consumed;      /* script tokens consumed so far (input_script sets it) */
    void *replay;           /* u2_replay* when --replay-report is active */
    int rng_probe;          /* --rng-sequence N: print N rolls and exit */
    /* Idle auto-PASS threshold, in polls of the overworld input loop.
     *   0  = not configured, use U2_AUTO_PASS_POLLS (the faithful default)
     *  -1  = disabled, for pure turn-based play
     *  >0  = explicit poll count
     * Reserved for the enhanced-mode knob: the original has one fixed value and
     * offers no way to change it, so `original` mode always uses the default and
     * only `enhanced` should ever set this. There is no fidelity-mode axis in the
     * engine yet (u2_mode is a renderer axis), so nothing sets it today. */
    int auto_pass_polls;
    /* Keep site state across a transition: write the outgoing sector's map and creature
     * table back to the content directory when leaving a town, tower or dungeon.
     * OFF by default and enhanced-only, because the original does not do it — measured
     * 2026-10-29 across several town entries and exits, where mapx31, monx31, mapx30 and
     * monx30 all came back byte-identical to pristine and only `player` was written.
     * The save path (Q) is separate and always writes a dirty sector, which asm/2305
     * supports; this knob is only about the transition. Gate: tests/site_state.py. */
    int keep_site_state;
    /* Program-flow signals set by the title flow and read by u2_game_run.
     * `restart_requested` reproduces the original's creation tail, which calls
     * the startup routine (runtime 0x0100) and so re-presents the splash; the
     * loop in game.c is the engine's equivalent of that call.
     * `enter_world` is the enhanced-mode shortcut: creation finishes and drops
     * the player straight into the overworld instead of restarting. */
    int restart_requested;
    int enter_world;
    int input_exhausted;    /* the player is gone: the script ran out, stdin hit EOF,
                             * or a quit was requested (window closed, SIGINT). Set by
                             * seam_key_poll and seam_key_wait together, and every flow
                             * reads it as "the player left". */
    const char *content_source; /* set by the game layer from the generated header */
    const char *script_path;/* --script PATH (input_script backend input) */
    const char *save_dir;    /* --save-dir PATH; "saves" when unset (core/save.h) */
    const char *data_dir;    /* --data-dir PATH; "." when unset — the original's
                                default drive, holding mapxNN / monxNN / tlkxNN.
                                Never created; see core/world.h. */
    int save_seeded;         /* set when the blank PLAYER file was created here */
    u2_renderer_choice renderer_choice;  /* --renderer=sdl|terminal; see the enum.
                                 Named apart from `renderer`, which is the backend
                                 pointer this selection resolves *to*. */
    int play_map;            /* --play-map: enter the CP4 overworld directly */
    int dev;                 /* --dev: development affordances, off by default.
                              * THE RULE, and it is the reason the flag exists: --dev may add
                              * observation and an exit path, and may NEVER change game
                              * behaviour -- not a damage number, not a spawn, not a message,
                              * not which tile is passable. If something would alter play, it
                              * is a fidelity question and belongs in a decision about that,
                              * not behind this flag.
                              * Deliberately independent of --play-map. Entering the world
                              * directly is not the same as accepting development keys, and
                              * implying one from the other would make every play-map test
                              * silently run under different rules.
                              * Background: cmd_Z used to quit on Escape and the overworld loop
                              * broke on Escape outright. Neither was the original -- nothing
                              * in asm/ compares against 0x1b or 0x11 -- they were dev
                              * conveniences with no sanctioned home, which is what this is. */
    int cleanroom;           /* --cleanroom: use the engine's own independently
                              * created assets instead of the game's — tiles now,
                              * sound and other media as they land. One axis, so a
                              * future asset class is a new consumer of this flag
                              * rather than a new flag.
                              * Opt-in by design: silently drawing substitute art
                              * where the player's own was expected would make the
                              * screen look finished while hiding what is absent —
                              * the failure mode this codebase refuses elsewhere
                              * (see the underlay note in game/overworld.c). Every
                              * consumer must announce the substitution. */
    int exit_code;          /* set by game code / backends to force termination */

    /* Per-backend private state, one slot each so backends stay independent
     * and a context can be torn down without backend-specific knowledge. */
    void *be_render;
    void *be_input;
    void *be_audio;
    void *be_clock;
    void *be_game;          /* game layer state (game/game.c) */
} u2_ctx;

#endif /* U2_CTX_H */
