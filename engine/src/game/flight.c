/* The flight screens: the plane, the rocket, and everything above the sky.
 *
 * Extracted from commands.c (2026-11-22, #17). It was 880 of that file's 2610 lines — a
 * third of a module whose job is a 26-letter jump table — and it was not part of that job.
 * The letters LAUNCH (L) and the space-screen commands reach in, and nothing else in
 * commands.c calls any of it, so the seam is two functions wide.
 *
 * What lives here is a second main loop with its own pacing. The walk loop repaints on a
 * command; the orbit screen repaints on a timer, because asm/6B3D recomputes its star
 * tables every pass and the warp is the animation of those tables moving, not of the ship
 * moving. That is why this file owns its loop rather than borrowing the walk loop's, and
 * why the frame presentation here is its own call and not the overworld's.
 *
 * The screen rules themselves — the star field, the sweep, the geometry — are in
 * game/space_view.c, which is a renderer and knows nothing about keys. This file is the
 * part that reads keys.
 */

#include "game/flight.h"

#include <stdbool.h>
#include <string.h>

#include "core/seam.h"
#include "core/rng.h"
#include "core/world.h"
#include "core/frame.h"
#include "core/layout.h"   /* U2_ENH_MAP_ROWS, to place the doubled star field */
#include "core/font.h"
#include "core/state.h"
#include "game/commands.h"
#include "game/status.h"
#include "game/overworld.h"
#include "game/dungeon.h"
#include "game/combat.h"
#include "game/monsters.h"
#include "game/space.h"
#include "game/space_view.h"

#include "gen_content.h"

/* The flight lines all end the same way: the word, the game's newline, a fresh prompt.
 * asm/38B0 stores it as ONE string ("NORTH",0x0d,"CMD: ") and the engine composes it from
 * the two pool entries rather than carrying a third copy of the text. */
static void flight_line(u2_ctx *ctx, const char *word)
{
    if (word) seam_str_out(ctx, word);
    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_CMD_PROMPT);
}

/* asm/38B0's plane branch, runtime 0x3BFA.
 *
 *   "LAUNCH--PLANE" prints BEFORE the gate, so a plane without a BRASS BUTTON says both
 *   words and then explains: "FUNNY THIS PLANE IS" / "MISSING A BRASS BUTTON!".
 *
 *   Launched, the original enters a loop of its own (0x3C5F..0x3CB2) that is NOT the walk
 *   loop: it polls the key non-blocking, applies a heading delta of its own (DS:21/DS:22,
 *   initialised to north), masks both coordinates with and 0x3F, and redraws — one cell of
 *   travel per iteration, whether or not a key arrived. Arrows change the heading and echo
 *   the direction word; 'L' tries to land; any other key does nothing except let the
 *   iteration run. It consults no passability routine and charges no food, which is why
 *   airborne flight is a mode of the handler and not a mode of DS:13's ladder: DS:13 stays
 *   0x26 the whole time, and what the ladder at asm/2604 governs is the PLANE walked on the
 *   ground after landing.
 *
 *   Landing is accepted on tile 0x08 (grass) and nowhere else: "LAND PLANE." versus
 *   "LAND--NOT HERE!!!!!", and the refusal does not stop the plane — the bytes fall into
 *   the same delta apply as everything else. Both branches executed in
 *   captures/spice86/flight-launched-live (runtime 0x3B5A and 0x3B72 per the asm header),
 *   so this is observed flow, not inferred flow.
 *
 * The engine's scripted input has one poll per WAIT token, so a script drives the plane one
 * cell per WAIT and one cell per key — which is the original's cadence expressed in the only
 * clock a headless run has. When the script runs out the loop ends: the original has no such
 * exit, it flies until it lands or leaves the sector, and inventing one would be a guess about
 * what exhaustion means. That is a harness limit, not a behaviour.
 *
 * Not modelled, both named rather than hidden: the site-edge exit at 0x3CCD (crossing x==0 or
 * y==0 inside a site jumps to the sector reload at asm/243E, and the engine's sector-exit
 * plumbing is site_leave(), which this loop has no proven route into), and the redraw call at
 * asm/5CB1, which the engine's own draw replaces.
 */
/* The plane's own frame cost, in ms per march step.
 *
 * The two flight loops in cmd_L are paced by the same thing -- each iteration ends in the
 * whole sector render, `3cd6 call L_005cb1 ; 3cd9 jmp 3c5f` back to the top -- but they do
 * not do the same work. The plane loop also calls L_0060c7 every iteration and runs a key
 * dispatch over the arrow table, so its step is strictly more expensive than the rocket
 * dive's and its true figure sits somewhat ABOVE the dive's, not equal to it.
 *
 * So this number is BORROWED, on purpose and by ruling (owner, 2026-11-11: "borrow the dive
 * figure and we can do a measure later... give the plane its own constant, that way if we
 * have to change it later we don't have to decouple it then"). It is not a guess about the
 * plane: 103 ms is a measurement of asm/5AB1_map_render, the routine this loop calls, so it
 * is a floor. What it is not is the plane's own measurement.
 *
 * The two names are kept separate for that reason; the VALUES are not. They used to be
 * defined three times in this file under two names, one pair of them identical and
 * introduced by a comment claiming they differed, which is the kind of arrangement that
 * survives until someone edits one. Both now live in game/space.h as one definition each,
 * both marked borrowed, and tests/dive_pace_check.c asserts they are still equal so a
 * future divergence is a decision rather than an accident.
 *
 * The capture that replaces it: docs/needs-gameplay.md G77. Seed a brass button
 * (items_held[11], DS:E1), board, launch, poll x,y,cycles per step exactly as
 * captures/spice86/dive-sweep-rate did. flight-launched-live cannot substitute: one-shot
 * harvest, ExecutionFlow but no per-step cycle timestamps, so there are no deltas to take.
 *
 * What is NOT behind either loop: a timeout. The speed-delay slot this loop loads is slot 1
 * (`mov $0x2,%bx ; call L_000911`, a forced-keep, which is why the 2026-10-28d census
 * missed it) and its value is discarded by both of its callees, exactly like the dive's
 * slot 14. See asm/0711_speed_delay_lookup.
 */


u2_cmd_result u2_flight_launch_plane(u2_ctx *ctx, u2_overworld *ow)
{
    seam_str_out(ctx, U2_S_LAUNCH_PLANE);

    if (ctx->state.items_held[U2_ITEM_BRASS_BUTTON] == 0) {
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_LAUNCH_PLANE_REF_A);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_LAUNCH_PLANE_REF_B);
        u2_cmd_done(ctx);
        return U2_CMD_DONE;
    }

    /* DS:21 = 0, DS:22 = 0xFF: the heading the original initialises to, which is north. */
    int dx = 0, dy = -1;
    flight_line(ctx, NULL);

    for (;;) {
        u2_key k;
        const int r = seam_key_poll(ctx, &k);
        if (r < 0) break;                       /* input exhausted: no original counterpart */
        if (r == 0) {
            /* Audit class A (docs/phase4-state.md, 2026-11-10): this loop printed the
             * heading words via flight_line and never presented, so plane flight text was
             * invisible -- the same shape the owner found in space_loop and
             * rocket_descent, found here by grepping the shape rather than waiting for a
             * report. Presenting on the empty poll is what makes the text reach the
             * screen, and it also paces the loop instead of spinning it. */
            if (ctx->exit_code || ctx->input_exhausted) break;
            if (seam_present_frame(ctx)) break;
        }

        if (r) {
            switch (k.ch) {
            case U2_KEY_UP:    dx = 0;  dy = -1; flight_line(ctx, U2_S_DIR_NORTH); break;
            case U2_KEY_DOWN:  dx = 0;  dy =  1; flight_line(ctx, U2_S_DIR_SOUTH); break;
            case U2_KEY_RIGHT: dx =  1; dy =  0; flight_line(ctx, U2_S_DIR_EAST);  break;
            case U2_KEY_LEFT:  dx = -1; dy =  0; flight_line(ctx, U2_S_DIR_WEST);  break;
            case 'L': case 'l':
                /* asm/3D47: the tile under the player, compared to 0x08 after and 0x7F. */
                if ((u2_world_tile_at(&ow->world, ctx->state.sector_x, ctx->state.sector_y) & 0x7F) == 0x08) {
                    seam_str_out(ctx, U2_S_LAND_PLANE);
                    u2_cmd_done(ctx);
                    return U2_CMD_DONE;
                }
                flight_line(ctx, U2_S_LAND_NOT_HERE);
                break;
            default:
                break;                          /* any other key: the plane keeps going */
            }
        }

        /* asm/3C9B: x += dx, y += dy, both masked, with the carry chain intact so a
         * negative delta is the byte 0xFF and wraps the same way it does on the 8088. */
        ctx->state.sector_x = (uint8_t)(((uint8_t)ctx->state.sector_x + (uint8_t)dx) & 0x3Fu);
        ctx->state.sector_y = (uint8_t)(((uint8_t)ctx->state.sector_y + (uint8_t)dy) & 0x3Fu);
        u2_overworld_draw(ctx, ow);
        /* The original's frame boundary, and the reason this loop stopped being a blur.
         * Owner report 2026-11-11: "planes have the same issues as rockets, once launched
         * they fly really quick, no delays" -- correct, and the same defect: the loop drew
         * a frame per cell and paid nothing for it, so its rate was however fast a memcpy
         * is: seam_present_frame paints and returns and this backend enables no vsync, so
         * a present paces nothing (see seam.h, and note this comment used to claim the
         * present was worth ~16 ms of vsync, which was never true).
         * Placed after the draw and before the next poll, which is where the original's
         * render sits: `3cd6 call L_005cb1 ; 3cd9 jmp 3c5f` back to the top of the loop.
         * U2_PLANE_STEP_MS is now the measured march rate (49 steps in 4.00 s at ~1350 Hz,
         * three runs, zero spread); see its definition in space.h for the basis warning and
         * why it sits below the dive figure. */
        seam_wait_ms(ctx, U2_PLANE_STEP_MS);
    }

    u2_cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/6999's readout, in two passes, because the original draws it in two passes and the
 * split is what makes the space text pane work. Owner report 2026-11-10: the hyperwarp
 * prompt never appeared and letters were missing from the HP and FOOD display. Both were
 * the readout colliding with the CMD pane, and the bytes say exactly where:
 *
 *   LABELS -- asm/6999's head, once on entering orbit (L_006bd9..L_006c46):
 *     L_006bd9  `mov $0x28,%al ; mov %al,0x31` -- right_margin := 0x28 (40) first. The
 *               overworld leaves the margin at 0x1d (29) after its GOLD= tail, and at that
 *               margin a string starting at column 30 wraps on its FIRST character and
 *               resumes at the left margin of the next row. That is how `  XENO=9` landed
 *               over the CMD lines and ate characters out of H.P./FOOD/EXP/GOLD.
 *     L_006be0  `mov $0x1e,%bl` -- the cursor goes to column 30, not 32. The literals are
 *               "  FUEL=" with two leading spaces, so the visible text starts at column 32,
 *               which is the x=256 the frames measure (22-right.png, 21-space.png: the empty
 *               run ends at x=255 and the glyphs run x=256..309). Byte and frame agree once
 *               the padding is counted; passing 32 with a padded literal did not.
 *
 *   VALUES -- asm/6FA3 (L_0071a3), called at the head of EVERY CMD-loop pass and again by
 *             the hyperwarp flow, which is the repair the frames show and the mechanism this
 *             file previously had to guess at:
 *     L_0071ab  the four values print at column 37, rows 20..23, as two BCD digits each --
 *               so `FUEL=08`, not `FUEL=8`. The engine was printing one unpadded digit.
 *     L_0071fd  `mov $0x1f,%al ; mov %al,0x31` -- right_margin := 0x1f (31). This is the
 *               load-bearing byte: the row scroll runs from column 0 to the margin inclusive
 *               (asm/5025: `CL=0 DL=DS:0x31`), so at 31 the scroll window stops one column
 *               short of the readout's first visible glyph at 32. The CMD pane can scroll
 *               freely and cannot touch the readout. At the overworld's 0x1d, or at the 0x20
 *               asm/6999 sets between the two passes, it can and does.
 *     L_007202  `di = 0 ; si = 0x17` then the cursor setter -- the text cursor is parked at
 *               column 0, row 23 before returning, which is where "CMD: " prints from. Every
 *               CMD echo therefore starts at the left of the bottom row. Leaving the cursor
 *               wherever a positioned print put it (column 39) made the next "CMD: " land
 *               past the margin, wrap, and scroll the pane once per prompt.
 *
 * Neither pass restores the previous margin; the overworld's own status draw re-establishes
 * 0x1d when the rocket lands. */
#define U2_SPACE_LABEL_COL    30      /* asm/6999 L_006be0: mov $0x1e,%bl */
#define U2_SPACE_VALUE_COL    37      /* asm/6FA3  L_0071ab: mov $0x25,%bl */
#define U2_SPACE_READOUT_ROW  U2_MSG_TOP
#define U2_SPACE_MARGIN_DRAW  0x28    /* asm/6999 L_006bd9, asm/6FA3 L_0071a4 */
#define U2_SPACE_MARGIN_CMD   0x1f    /* asm/6FA3  L_0071fd: the scroll stops short of 32 */
#define U2_SPACE_CMD_ROW      U2_MSG_BOTTOM   /* asm/6FA3 L_00720a: si = 0x17 */

/* The four labels, drawn once when the space screen comes up (asm/6999's head). */
static void space_labels(u2_ctx *ctx)
{
    ctx->state.right_margin = U2_SPACE_MARGIN_DRAW;
    seam_str_out_at(ctx, U2_SPACE_LABEL_COL, U2_SPACE_READOUT_ROW + 0, U2_S_SPACE_FUEL);
    seam_str_out_at(ctx, U2_SPACE_LABEL_COL, U2_SPACE_READOUT_ROW + 1, U2_S_SPACE_XENO);
    seam_str_out_at(ctx, U2_SPACE_LABEL_COL, U2_SPACE_READOUT_ROW + 2, U2_S_SPACE_YAKO);
    seam_str_out_at(ctx, U2_SPACE_LABEL_COL, U2_SPACE_READOUT_ROW + 3, U2_S_SPACE_ZABO);
    /* asm/6999 L_006c46 sets 0x20 here and asm/6FA3 immediately narrows it to 0x1f; the
     * value pass below is what leaves the margin where the CMD loop needs it. */
    ctx->state.right_margin = 0x20;
}

/* asm/6FA3: the four values, redrawn on every status pass, then the margin and the cursor
 * positioned for the CMD prompt. FUEL is the TRI-LITHIUM BCD byte (DS:E5) after the launch
 * spend; the triple is DS:4CA/4CB/4CC, a property of the world byte -- an arrow moves the
 * ship sprite and leaves all three alone (measured, asm/6999 header). */
static void space_values(u2_ctx *ctx)
{
    u2_state *st = &ctx->state;
    const u2_space_world *w = st->world < U2_WORLD_COUNT
                            ? &u2_space_worlds[st->world]
                            : &u2_space_worlds[U2_WORLD_EARTH];
    char buf[8];

    ctx->state.right_margin = U2_SPACE_MARGIN_DRAW;

    snprintf(buf, sizeof buf, "%02u",
             (unsigned)u2_bcd_dec1(st->items_held[U2_ITEM_TRI_LITHIUM]));
    seam_str_out_at(ctx, U2_SPACE_VALUE_COL, U2_SPACE_READOUT_ROW + 0, buf);
    snprintf(buf, sizeof buf, "%02u", (unsigned)w->xeno);
    seam_str_out_at(ctx, U2_SPACE_VALUE_COL, U2_SPACE_READOUT_ROW + 1, buf);
    snprintf(buf, sizeof buf, "%02u", (unsigned)w->yako);
    seam_str_out_at(ctx, U2_SPACE_VALUE_COL, U2_SPACE_READOUT_ROW + 2, buf);
    snprintf(buf, sizeof buf, "%02u", (unsigned)w->zabo);
    seam_str_out_at(ctx, U2_SPACE_VALUE_COL, U2_SPACE_READOUT_ROW + 3, buf);

    ctx->state.right_margin = U2_SPACE_MARGIN_CMD;
    seam_goto(ctx, 0, U2_SPACE_CMD_ROW);
}

/* asm/6999's entry (L_006b99), before anything is printed in space. The first instruction
 * that does anything is `call L_004c3e` -- INT 10h AH=00 AL=04, set 320x200 graphics mode 4
 * -- and a mode set on real CGA clears the display. The game draws its text as glyphs in
 * that same buffer, so this is the space screen's clear, and it is load-bearing twice:
 * the overworld's H.P./FOOD/EXP/GOLD block is gone in every space frame, and so is
 * "PREPARE FOR LAUNCH!", which frame 22-right.png confirms (its pane is HYPERWARP
 * ENGAGED! / YOU ARE ORBITING EARTH. / CMD: RIGHT / CMD: and nothing above them).
 * Without this the engine re-composites the stale status block on every space frame. */
/* The staging pair, uniformly 0xFF while in space and holding a staged sector map otherwise
 * (docs/handoff-space-view.md § 3a, measured pad / orbit / landed).
 *
 * The engine does not model staging -- it reads sector maps straight through u2_world_* --
 * so it cannot reproduce the *cause* of the pattern, only the pattern. That is said out
 * loud rather than hidden, because it is the difference between a model and a mimic: the
 * original's 0xFF falls out of having no map to stage, and here it is written deliberately
 * at the two points that bracket the space loop. The value written on landing is a stand-in
 * whose only required property is that it is not all-0xFF; nothing reads these bytes.
 *
 * What matters is that u2_state_mode() sees the same thing on both executables, because
 * that function is the bot's only way to know where it is. § 3a forbids gating CONTROL FLOW
 * on these bytes and nothing here does -- this is a report, not a branch. */
static void space_stage_mark(u2_ctx *ctx, int in_space)
{
    memset(ctx->state.stage_a, in_space ? 0xFF : 0x00, sizeof ctx->state.stage_a);
    memset(ctx->state.stage_b, in_space ? 0xFF : 0x00, sizeof ctx->state.stage_b);
}

static void space_screen_enter(u2_ctx *ctx)
{
    space_stage_mark(ctx, 1);
    seam_clear_screen(ctx);
    space_labels(ctx);
    space_values(ctx);
}

/* asm/6999's status pass, called at the head of every CMD-loop pass (L_006c7a) and by the
 * hyperwarp flow. It redraws the four values, narrows the scroll margin to 0x1f so the CMD
 * pane cannot reach the readout, and parks the cursor at column 0 of the bottom row -- which
 * is where "CMD: " prints from, so every echo starts at the left edge as the frames show. */


/* asm/7030's orbit determination (entry L_007230), reached from the hyperwarp arm.
 * It is the only writer of the world byte. 4/4/4 prints the sun message and then
 * falls through to the table match (which finds no world, so deep space); otherwise
 * the typed triple must EQUAL a world's table entry to orbit it -- no tolerance, no
 * radius -- else deep space (0x0A). */
static void space_orbit_determine(u2_ctx *ctx, const uint8_t dest[3])
{
    u2_state *st = &ctx->state;
    if (u2_space_is_sun(dest))
        seam_str_out(ctx, U2_S_ORBIT_SUN);
    uint8_t world = u2_space_world_at(dest[0], dest[1], dest[2]);
    st->world = world;
    if (world == U2_WORLD_DEEP_SPACE) {
        seam_str_out(ctx, U2_S_ORBIT_DEEP_SPACE);
        seam_char_print(ctx, '\r');   /* trailing 0x8d in asm/7030 file 0x73a6, as with the
                                         orbiting line: the next "CMD: " parks at column 0 of
                                         the bottom row and would overwrite this one. */
        return;
    }
    seam_str_out(ctx, U2_S_ORBIT_ORBITING);
    /* Worlds 0..8 print their extracted "NAME." literal. Index 9's name routine sits
     * past 0x729B and has never executed, so "X." is what a frame showed, not a decode
     * -- the same grade as u2_space_worlds[9].name, and flagged for the same reason. */
    static const char *const names[U2_WORLD_COUNT] = {
        U2_S_WORLD_EARTH, U2_S_WORLD_MERCURY, U2_S_WORLD_VENUS, U2_S_WORLD_MARS,
        U2_S_WORLD_JUPITER, U2_S_WORLD_SATURN, U2_S_WORLD_URANUS, U2_S_WORLD_NEPTUNE,
        U2_S_WORLD_PLUTO, "X."
    };
    seam_str_out(ctx, names[world]);
    /* Every one of those literals carries a trailing 0x8d in the binary -- asm/7030 file
     * 0x73c2 is `db "EARTH.",0x8d,0x00` -- and the extractor splits there, so the newline
     * belongs to the caller. Without it the next "CMD: " prompt, which asm/6FA3 parks at
     * column 0 of the bottom row, prints over the first five characters of the orbiting
     * line instead of landing on a fresh row. */
    seam_char_print(ctx, '\r');
}

/* asm/6DFD's 'H' arm (L_0070c5). Spend one TRI-LITHIUM, read a typed XENO/YAKO/ZABO
 * triple (each a digit 0-9; L_00717f loops until one arrives), then the engage
 * animation and the off-course gate. Off-course fires iff (post-spend fuel & 3)==3
 * and replaces the typed triple with rand & 7 -- so world 9, whose triple is the only
 * one containing a 9, is unreachable by misfortune. Returns 0 only if input ran out
 * mid-entry (the original blocks); 1 means the warp resolved and the loop should
 * continue. */
static void space_paint(u2_ctx *ctx, u2_overworld *ow);   /* defined below, with the loop */
void space_warp_field(u2_ctx *ctx, u2_overworld *ow);     /* asm/6DFD phases A/B/C */
void space_warp_engage(u2_ctx *ctx, u2_overworld *ow);    /* asm/6DFD phases D/E */

static int space_hyperwarp(u2_ctx *ctx, u2_overworld *ow)
{
    u2_state *st = &ctx->state;
    const uint8_t pre = u2_bcd_dec1(st->items_held[U2_ITEM_TRI_LITHIUM]);
    if (pre == 0) {                       /* asm/6DFD L_0070c8: no fuel, nothing spent */
        seam_str_out(ctx, U2_S_HYPERWARP_NO_FUEL);
        return 1;
    }
    st->items_held[U2_ITEM_TRI_LITHIUM] = u2_bcd_dec1(st->items_held[U2_ITEM_TRI_LITHIUM]);

    /* asm/6DFD's first three redraw loops. They sit here, before the coordinate readout,
     * because that is where the bytes put them: phases A/B/C run, then the off-course
     * gate, then NO FUEL, then the readout. Placing them after the fuel test rather than
     * before it is the one deliberate liberty, so a warp with no fuel does not animate a
     * refusal. */
    space_warp_field(ctx, ow);

    seam_str_out(ctx, U2_S_HYPERWARP_TO);
    /* The original's literal is one string, `HYPERWARP TO:` 0x8d `XENO=` 0x00 (asm/6DFD
     * file 0x7100), and 0x8d is the game's newline glyph. The extractor splits it at that
     * byte into two names, so the newline has to be emitted here or the target entry starts
     * on the same row as the prompt. The three prompts then run together on that row --
     * `XENO=9 YAKO=9 ZABO=9` on ONE line, which is what g39 frame 23-hyperspace.png shows
     * and why no CR goes between them -- and the next literal is 0x8d `PREPARE FOR
     * HYPERWARP!` 0x8d, so the prepare line is bracketed by newlines too. */
    seam_char_print(ctx, '\r');
    static const char *const prompts[3] = {
        U2_S_HYPERWARP_XENO, U2_S_HYPERWARP_YAKO, U2_S_HYPERWARP_ZABO
    };
    uint8_t dest[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        seam_str_out(ctx, prompts[i]);
        for (;;) {
            u2_key k;
            const int r = seam_key_poll(ctx, &k);
            if (r < 0) return 0;                       /* input exhausted: original blocks */
            if (r == 0) {
                /* Audit class A, second occurrence. Owner report 2026-11-10: "i hit H and
                 * it doesn't show the command, nor does it show asking X Y Z, but it
                 * accepts 9 9 9 anyway". The remedy that went in then presented the frame
                 * on the empty poll -- but nothing repainted it, so it presented the frame
                 * as of before these prompts, which is the same invisible text with a
                 * present() in it, and it passed every text assertion in the tree because
                 * the grid really did hold the words. Owner report 2026-11-11 is the same
                 * sentence: "i'm in space and hit H but its not showing the hyperjump
                 * dialog". Paint, then present, then pace.
                 * No CRs are added between the three prompts on purpose: g39 frame
                 * 23-hyperspace.png shows `XENO=9 YAKO=9 ZABO=9` on ONE line, so the
                 * run-together form is the original's, not a defect. */
                if (ctx->exit_code || ctx->input_exhausted) return 0;
                space_paint(ctx, ow);
                seam_present_frame(ctx);
                seam_wait_ms(ctx, 16);
                continue;
            }
            if (k.ch >= '0' && k.ch <= '9') {
                /* The typed digit is echoed. asm/6DFD's reader (L_006f7f) is: repaint the
                 * field, poll, reject anything outside '0'..'9', and on a digit
                 * `push ax ; call L_0055f4 ; pop ax` -- char_print_wrap on the character
                 * itself -- before converting it with `sbb $0x30`. That echo is what makes
                 * g39 frame 23-hyperspace.png read `XENO=9 YAKO=9 ZABO=9` on one line;
                 * without it the player types into a prompt that never shows the answer.
                 * Push/pop means the echo cannot disturb the digit, and it cannot scroll
                 * the pane differently either, so it goes through the same seam the game
                 * uses for every other character. */
                seam_char_print(ctx, k.ch);
                dest[i] = (uint8_t)(k.ch - '0');
                break;
            }
        }
    }
    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_HYPERWARP_PREPARE);
    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_HYPERWARP_ENGAGED);
    seam_char_print(ctx, '\r');   /* the literal is "HYPERWARP ENGAGED!" 0x8d (asm/6DFD file
                                     0x6ffa), so the next line starts fresh. Without it the
                                     orbiting line runs on after the exclamation mark. */
    space_warp_engage(ctx, ow);   /* asm/6DFD phases D and E, after the PREPARE line */

    const uint8_t rng[3] = { seam_rand_byte(ctx), seam_rand_byte(ctx), seam_rand_byte(ctx) };
    /* u2_space_hyperwarp tests the POST-decrement count, so it is handed the PRE value
     * and subtracts one internally -- that is what makes fuel 8->7 fire and 7->6 not. */
    if (u2_space_hyperwarp(pre, rng, dest) == U2_HYPERWARP_OFF_COURSE)
        seam_str_out(ctx, U2_S_HYPERWARP_OFF_COURSE);
    space_orbit_determine(ctx, dest);
    return 1;
}

/* asm/38B0's descent, reached from the dive. The march is a function of the trajectory
 * alone: y += 2 every step, x += 1 whenever the new y is a multiple of 8 (asm/3B94), so
 * it visits the same 32 cells in every sector. It runs until a key arrives, and the tile
 * it stopped on is the verdict: 0x08 grass lives, anything else jumps to the death tail
 * at runtime 0x0E62. On a survived dive Earth clears the parked flag (its pads are
 * authored into mapx41) and off-Earth records the landing cell (nothing authored that
 * pad, so the save record is the only thing that puts the rocket back).
 *
 * THE SWEEP IS THE ANIMATION, and it is fidelity. The original's loop body is march, then
 * `call L_005cb1`, then a speed-delay lookup, then a key poll — and L_005cb1 is not what it
 * looked like: it is asm/5AB1_map_render, the whole sector render (viewport origin, stage
 * the visible 20x10 window from mapx, blit it to the CGA planes, copy the underlay). So one
 * march step is one frame, the ship the player watches is the stamp that render draws, and
 * the pattern the owner described — north to south, shift right, repeat — is exactly
 * y += 2 with x += 1 every fourth step. See asm/38B0_cmd_L's header and
 * captures/spice86/dive-sweep-rate/README.md.
 *
 * The landing rule was already observed on both arms before this capture — fatal on water at
 * (7,62) in sector 40 and on forest at (5,44) in sector 90 (captures/spice86/g39-rocket-flight),
 * survivable on 0x08 grass at (1,10) in sector 40 (captures/spice86/g59-grass-landing). This one
 * re-observed the fatal arm with a frame and added the rate and the render identity. The march
 * cycles 256 cells and 28 of them are grass on Earth, so aiming is a 1-in-9 timing window about
 * every 26 s at the measured step rate — see docs/needs-gameplay.md G63.
 */

/* The pace of one march step is U2_DIVE_STEP_MS, defined in game/space.h with its
 * derivation and its caveats, because the plane's loop uses the same borrowed figure and
 * they used to be defined three times under two names in this one file. */

static void rocket_descent(u2_ctx *ctx, u2_overworld *ow)
{
    u2_state *st = &ctx->state;
    ctx->state.sector_x = 0;
    ctx->state.sector_y = 0;
    st->view_mode = 0xFF;
    seam_str_out(ctx, U2_S_PRESS_ANY_KEY);

    for (;;) {
        u2_space_descent_step(&ctx->state.sector_x, &ctx->state.sector_y);
        u2_overworld_draw(ctx, ow);
        /* Audit class A. The draw was happening and being thrown away: nothing presented,
         * so the screen kept the space frame and the southward descent was invisible.
         * Owner's own diagnosis on 2026-11-10 was correct -- "maybe it just needs to
         * clear the space screen back out to show the overworld". u2_overworld_draw
         * repaints the whole frame including the text pane, so presenting it here is
         * exactly that clear-and-replace. */
        if (seam_present_frame(ctx)) return;
        /* The original's frame boundary. Placed after the present and before the poll so
         * a keypress is answered at the same point in the cycle the original answers it —
         * after the frame it belongs to has been drawn, not before it. */
        seam_wait_ms(ctx, U2_DIVE_STEP_MS);
        u2_key k;
        const int r = seam_key_poll(ctx, &k);
        if (r < 0) return;                             /* input exhausted: original blocks */
        if (r == 0 && (ctx->exit_code || ctx->input_exhausted)) return;
        if (r) {
            if (!u2_space_dive_survives(u2_world_tile_at(&ow->world, ctx->state.sector_x, ctx->state.sector_y))) {
                u2_death(ctx);
                /* Do not fall through to the parked-rocket bookkeeping: a dead character
                 * leaves no rocket behind, and the handoff that asks for a key belongs to
                 * the top-level flow, not to this loop. */
                return;
            } else if (st->world == 0) {
                st->rocket_parked = 0;
            } else {
                st->rocket_parked_x = (uint8_t)ctx->state.sector_x;
                st->rocket_parked_y = (uint8_t)ctx->state.sector_y;
                st->rocket_parked   = 1;
            }
            return;
        }
    }
}

/* asm/6999's in-space command loop (head L_006c79). The original has NO `ret` in this
 * routine and leaves only by jumping out: 'H' into the hyperwarp flow, 'L' into the
 * dive. The engine mirrors that -- the loop returns when 'L' dives (or when input runs
 * out, which in the original is an infinite block). Arrows move the ship sprite on
 * screen only and change no coordinate, so the engine prints the direction word and
 * loops; that is the whole observable effect of an arrow. */

/* How far an arrow moves the sprite. UNMEASURED -- the captures prove the sprite moves
 * and that no coordinate changes, but never read DS:47B across an arrow to get the
 * step. Named rather than inlined so it is a visible gap with a route to closing it:
 * peek 8be:47b before and after one Right in a live run. */
#define U2_SPACE_SHIP_STEP 4

static void space_paint_mode(u2_ctx *ctx, u2_overworld *ow, int warping,
                             int warp_frame, int keep_frame)
{
    /* The readout survives the scrolling CMD pane because space_values runs before every
     * prompt and asm/6FA3 L_0071fd narrows the scroll margin to 0x1f, one column short of
     * the readout's first visible glyph at 32. See that function's note. */
    u2_space_scene sc;
    u2_space_scene_from(ctx, &sc);
    /* The scene builder is stateless and always says "not warping", so a gate can build a
     * scene from state alone. The warp is the caller's business, so the caller overrides
     * here rather than the builder growing a hidden input. */
    sc.warping = warping;
    sc.warp_frame = warp_frame;
    sc.keep_frame = keep_frame;
    /* The field's 256x128 rectangle was measured on the original's 320x200 screen,
     * where it is most of the picture. On the 1024x512 enhanced canvas the same
     * pixels are a stamp in the corner — owner, 2026-11-20: "space is squished".
     * Doubled it fills the map area's height and runs 64 px under the panel, which is
     * opaque chrome drawn after it, so the overlap is hidden rather than clipped. */
    if (ctx->enhanced_layout) {
        sc.scale = 2;
        sc.ox = 0;
        sc.oy = (U2_ENH_MAP_ROWS * 32 - U2_SPACE_VIEW_ROWS * sc.scale) / 2;
    }
    /* The sweep drives `keep_frame` above: asm/6B3D gates the erase of each star's own old
     * pixel on DS:4C7 being non-zero, and that byte sweeps 128->1, holds at 0 for 128
     * frames, holds at 1, climbs 2->126 step 2, holds at 128. The 128-frame zero window is
     * the streak field — 128 frames x 64 stars plotted with nothing erased, which measures
     * as 3892 distinct dots on the original at the warp's peak. G90,
     * captures/spice86/g90-warp-table/.
     *
     * `sc.warp_source` stays U2_WARP_FROM_HASH on purpose. The stateful field the table
     * mode draws is seeded and stepped, but the step rule still strands stars, so drawing
     * through it is worse than the hash field. See space_warp_frame.
     */
    u2_space_scene_draw(&ow->frame, &sc);

    /* The field clear takes the WHOLE frame -- u2_frame_clear memsets every row, not
     * just the view rectangle -- so the text pane has to be composited back over it or
     * the `CMD:` prompt and the FUEL/XENO/YAKO/ZABO readout simply vanish. Owner
     * reported it on the first re-flight, 2026-11-10: "we make it to space but the cmd>
     * etc disappears, I'm sure it stays visible in the origin as you need to be able to
     * hit H and the XYZ coords". He is right and the frames agree: g39 frame 22-right
     * shows the text pane carrying `CMD: RIGHT`, `CMD:` and the four-value readout
     * under the star field.
     *
     * This is the same composite the overworld does at the end of its own draw
     * (overworld.c, u2_font_draw_vram_rows after draw_view). Omitting it is what made
     * the screen look inert even though the loop was alive and reading keys. */
    u2_compose_text_layer(ctx, ow, 1);
}

static void space_paint(u2_ctx *ctx, u2_overworld *ow)
{
    space_paint_mode(ctx, ow, 0, 0, 0);
}

/* ---- the hyperwarp animation ---------------------------------------------------
 *
 * asm/6DFD's bounded redraw loops, in the order the bytes run them. The structure is
 * bytes; the frame DURATION is not, and that asymmetry is the whole note on this
 * function. Read asm/6DFD's WARP ANIMATION block, which is where the finding lives.
 *
 * Five phases, each one `call asm/6B3D_space_field_draw` and then a counter step, with
 * DS:4C7 the field's motion mode and DS:4C8 the frame counter:
 *      A   128 frames, mode 0              plot in place
 *      B   128 frames, mode 1              erase and plot -- motion
 *      C    63 frames, mode 2,4,..,126     the >= 2 branch, mode rising by 2
 *      D   126 frames, mode 0x80 down to 3
 *      E    64 frames, mode 2
 *
 * The speed-slot lookups between them are NOT delays -- asm/0711's value is dropped at
 * every one of them -- so the original's frame rate was the cost of drawing 64 stars into
 * the CGA planes. This renderer is not a 4.77 MHz 6845, so the time that cost used to
 * supply has to come from somewhere else, and no measurement of asm/6B3D exists to say
 * what it was.
 *
 * U2_WARP_FRAME_MS IS THEREFORE INVENTED, not measured. It is named and labelled so one
 * line replaces it when a cycle count for the field draw exists (docs/needs-gameplay.md
 * G83). 2 ms puts all 509 frames near a second, which is the right order for 64 pixel
 * plots on a machine of that speed; that is a guess about an order of magnitude and
 * nothing more. The frame COUNTS are bytes. The frame TIME is not, and a smooth warp is
 * not evidence about the original -- it is evidence the loop runs.
 *
 * What the engine gets wrong, and it is buildable rather than a capture question: the
 * original's streaks come from mode 0 NOT erasing the star's old position while the warp
 * path never clears the screen, so frames accumulate into a trail radiating from the ship.
 * `u2_space_scene_draw` calls `u2_frame_clear` on every frame, so no mode here can leave a
 * trail — which is why the built warp reads as a shimmering field and not as the lines the
 * owner remembers. The mode >= 2 branch is not the gap; it draws nothing, it is a rate-
 * limited tone (see asm/6B3D_space_field_draw). Frame COUNTS are bytes; frame TIME is
 * invented; the TRAIL is missing and needs no capture to fix.
 */

/* The warp's tone. asm/6DFD seeds a CS-side accumulator once, at the head of phase A
 * (`mov $0x2200,%bx ; mov %bx,%cs:0x6990`), and flips its increment at the head of phase B
 * (`movw $0x1,%cs:0x6992` after `movw $0xffff,%cs:0x6992`). asm/6B3D then advances that
 * accumulator once per star and latches it into PIT channel 2 once per star, and it walks 64
 * table entries a frame -- so per frame the divisor moves by 64 x increment. A falling
 * divisor is a rising pitch: from 1193180/0x2200 = 137 Hz at the seed.
 *
 * Measured live 2026-11-16 in captures/spice86/g83-g87-warp/, which is also the record of a
 * wrong first reading of the same data -- the samples landed every 3.99 frames, so the
 * apparent 0x100 quantisation was the sampling interval and not the signal. Read that file
 * before "improving" these three constants.
 *
 * The mode >= 2 arm does not latch per star at all: it counts a word down and fires asm/7242
 * on zero, which reloads to 0xE0 on the path space takes (asm/6999 sets the selector byte to
 * 0xFF). 0xE0 star-calls is 3.5 frames, so that arm is a much slower, lower-pitched pulse. */
#define U2_WARP_TONE_SEED   0x2200  /* asm/6DFD L_007021 */
#define U2_WARP_TONE_STARS  64      /* asm/6B3D's table walk, one speaker latch each */
#define U2_WARP_TONE_SLOW   0xE0    /* asm/7242's reload on the space path */

/* tone_mode: -1 phase A (divisor falling), +1 phase B (rising), 0 the mode >= 2 arm. */
/* One frame of the warp: advance the field, paint it, sound the tone.
 *
 * The sweep is the clock. asm/6B3D gates the erase of each star's own old pixel on DS:4C7
 * being non-zero, so the frames where the sweep sits at 0 plot without erasing and the
 * field accumulates into streaks; every other frame erases and plots, which is clean
 * motion. That replaces the old five flat phases, which were a reading of asm/6DFD's loop
 * bounds and did not correspond to what the byte actually does (G90). */
static void space_warp_frame(u2_ctx *ctx, u2_overworld *ow, int frame)
{
    u2_warp_sweep sw = u2_space_warp_sweep(frame);

    /* The sweep is the clock and the accumulate window is now the measured one. What is
     * deliberately NOT here is u2_space_warp_advance(): the decoded step rule is
     * byte-verified and unit-tested, but it still strands stars (warp_step_check carries a
     * characterization test that pins the freeze at frame 251 and goes red when someone
     * fixes it), and rendering through it produces a near-empty screen — measured 31 white
     * px against the original's ~6200 at the same point. So the hash field draws the stars
     * and the sweep decides when the frame is kept. */
    space_paint_mode(ctx, ow, 1, frame, sw.accumulate);

    /* The tone follows the sweep's direction rather than a phase number: asm/6DFD seeds
     * the divisor's step at -1 and flips it to +1 partway through, and the point it flips
     * is not separately evidenced, so deriving it from the sweep is the honest version of
     * "falling while the field closes, rising while it opens". */
    int rising = u2_space_warp_sweep(frame + 1).value > sw.value;
    static uint16_t divisor;
    static unsigned slow;
    if (frame == 0) { divisor = U2_WARP_TONE_SEED; slow = 0; }
    if (rising >= 0) {
        divisor = (uint16_t)(divisor + U2_WARP_TONE_STARS);
        seam_tone(ctx, divisor, U2_WARP_FRAME_MS);
    } else if (++slow >= U2_WARP_TONE_SLOW / U2_WARP_TONE_STARS) {
        slow = 0;
        seam_tone(ctx, divisor, U2_WARP_FRAME_MS);
    } else {
        divisor = (uint16_t)(divisor - U2_WARP_TONE_STARS);
    }

    seam_present_frame(ctx);
    seam_wait_ms(ctx, U2_WARP_FRAME_MS);
}

/* Frames the first call owns: the descend and the accumulate window. The rest falls to
 * space_warp_engage, which asm/6DFD reaches only after the PREPARE line. */
#define U2_WARP_FIELD_FRAMES  (U2_SWEEP_DESCEND + U2_SWEEP_ZERO)

void space_warp_field(u2_ctx *ctx, u2_overworld *ow)
{
    for (int f = 0; f < U2_WARP_FIELD_FRAMES; f++) {
        if (ctx->exit_code || ctx->input_exhausted)
            return;          /* a closed window must not cost 600 more frames */
        space_warp_frame(ctx, ow, f);
    }
}

void space_warp_engage(u2_ctx *ctx, u2_overworld *ow)
{
    for (int f = U2_WARP_FIELD_FRAMES; f < u2_space_warp_sweep_frames(); f++) {
        if (ctx->exit_code || ctx->input_exhausted)
            return;
        space_warp_frame(ctx, ow, f);
    }
}

static void space_loop(u2_ctx *ctx, u2_overworld *ow)
{
    u2_state *st = &ctx->state;

    /* The frame is drawn once here and again after every handled command, not on
     * every poll: the original repaints the starfield when the ship moves, not when
     * nothing happens. */
    space_paint(ctx, ow);

    for (;;) {
        /* asm/6999's loop head is `call L_0071a3` and THEN the "CMD: " print: the status
         * pass redraws the values, sets the scroll margin to 0x1f and parks the cursor at
         * (0, 23) before the prompt is printed. Doing it in that order is what keeps the
         * readout whole while the pane scrolls under it. */
        space_values(ctx);
        seam_str_out(ctx, U2_S_CMD_PROMPT);

        /* Composite the prompt into the frame and show it BEFORE waiting. This ordering is
         * the whole defect: space_paint is what copies the text grid into ow->frame
         * (u2_font_draw_vram_rows at the end of it), and it used to run only at the top of
         * the pass, so the frame that got presented carried the text as of before this
         * prompt. Every string the loop wrote -- the CMD: prompt, and everything a command
         * printed while it waited -- reached the vram and never the window, while the same
         * unchanged frame was re-uploaded as fast as software GL could carry it. Measured
         * on 2026-11-11 at 550% CPU with the main thread parked in sdl_upload, which is
         * what the owner saw as a crash: "i'm in space and hit H but its not showing the
         * hyperjump dialog". */
        space_paint(ctx, ow);
        seam_present_frame(ctx);

        /* Wait for a key WITHOUT burning the CPU. The old shape was `if (!r) continue;`
         * back to the prompt print, which both spun at 100% and flooded the text pane with
         * `CMD: ` -- owner-reported 2026-11-10 as "I hit L to launch and it seems to have
         * crashed". A closed window still exits here: the SDL backend answers a quit with
         * -1 and seam_key_poll collapses that to input_exhausted. */
        u2_key k;
        int r;
        while ((r = seam_key_poll(ctx, &k)) == 0) {
            if (ctx->exit_code || ctx->input_exhausted) return;
            if (ctx->death_pending) return;    /* the dive killed you; unwind to the handoff */
            seam_wait_ms(ctx, 16);             /* ~60 fps, the overworld loop's cadence */
        }
        if (r < 0) return;                            /* input exhausted: original blocks */

        switch (k.ch) {
        /* Each arrow ends its line. The original shows `CMD: RIGHT` on its own row and
         * then a fresh `CMD:` below it (g39 frame 22-right.png); without the CR the
         * echoes ran together as `CMD: RIGHTCMD: LEFT`. */
        case U2_KEY_LEFT:  seam_str_out(ctx, U2_S_SPACE_LEFT);  seam_char_print(ctx, '\r');
            st->space_ship_x = (uint8_t)(st->space_ship_x - U2_SPACE_SHIP_STEP); break;
        case U2_KEY_RIGHT: seam_str_out(ctx, U2_S_SPACE_RIGHT); seam_char_print(ctx, '\r');
            st->space_ship_x = (uint8_t)(st->space_ship_x + U2_SPACE_SHIP_STEP); break;
        case U2_KEY_UP:    seam_str_out(ctx, U2_S_SPACE_CLIMB); seam_char_print(ctx, '\r');
            st->space_ship_y = (uint8_t)(st->space_ship_y - U2_SPACE_SHIP_STEP); break;
        case U2_KEY_DOWN:  seam_str_out(ctx, U2_S_SPACE_DIVE);  seam_char_print(ctx, '\r');
            st->space_ship_y = (uint8_t)(st->space_ship_y + U2_SPACE_SHIP_STEP); break;
        case 'H': case 'h':
            if (!space_hyperwarp(ctx, ow)) return;
            /* Repaint the fixed cells. The readout is positioned now, not streamed, so
             * it would otherwise keep showing the world you warped FROM -- stale in a
             * way the old stream version was not, because the old version simply never
             * refreshed either and just scrolled away. asm/6999 redraws it each status
             * pass, which is what this stands in for. */
            space_values(ctx);
            break;
        case 'L': case 'l': {
            /* asm/7030's dive arm (L_007385): "LANDING REQUESTED!", refused only from
             * deep space (which jumps back to the loop head), else the landing sector is
             * computed -- Earth -> 40, world N -> N0, ones digit forced to 0, so a rocket
             * always arrives in a wilderness sector -- and the descent runs. */
            seam_str_out(ctx, U2_S_DIVE_REQUESTED);
            if (!u2_space_dive_allowed(st->world)) {
                seam_str_out(ctx, U2_S_DIVE_DENIED);
                break;                                  /* deep space: back to the loop */
            }
            const int sector = u2_space_dive_sector(st->world);
            st->sector_first  = (uint8_t)(sector / 10);
            st->sector_second = (uint8_t)(sector % 10);

            /* Reload the sector before descending. Owner report 2026-11-10: after a
             * hyperwarp to planet X, `L` landed on Earth. Setting the state digits was
             * never enough -- ow->world still held the LAUNCH sector, so the descent drew
             * the wrong map AND u2_space_dive_survives tested a tile from the wrong world,
             * which can kill the player on terrain that is not there or spare them on
             * terrain that is. Correctness, not presentation.
             *
             * site_reload is reused rather than a hand-rolled pair of u2_world_load calls
             * because it is the only path that does the whole job in order: map layer,
             * demo delta, creature layer, spawn, spawn-cell clear, tlkx clear-then-load,
             * restamp, and the DS:12 derivation from the ones digit. Every one of those
             * closes a documented failure; skipping them would trade this bug for a quieter
             * one. The dive sector's ones digit is always 0 (u2_space_dive_sector forces
             * N0), so site_reload derives view_mode = 0xFF, which is the world map --
             * matching what rocket_descent sets, not fighting it. */
            ow->sector_hi = st->sector_first;
            ow->sector_lo = st->sector_second;
            if (site_reload(ctx, ow) != 0) {
                /* Same treatment the site paths give a failed reload: latch it rather than
                 * pressing on with a half-loaded world. Diving into an unloadable sector is
                 * a content problem, not something to paper over by landing anyway. */
                ow->reload_failed = 1;
                seam_str_out(ctx, ow->note);
                seam_char_print(ctx, '\r');
                return;     /* space_loop returns void; leaving the loop is the exit */
            }

            rocket_descent(ctx, ow);
            /* Landing restages the sector, which is what ends the all-0xFF pattern on the
             * original. Done after the reload succeeded, so a failed reload leaves the
             * state honest about still being in space rather than half-landed. */
            space_stage_mark(ctx, 0);
            return;
        }
        default:
            break;                                      /* asm/6999: no match, re-poll */
        }

        /* Arrows move the sprite and nothing else, so the redraw is what makes an
         * arrow observable at all. */
        space_paint(ctx, ow);
    }
}

/* asm/38B0's rocket branch, runtime 0x3AD2.
 *
 * "LAUNCH--ROCKET", then the DS:E5 (TRI-LITHIUM) gate -- without it, "A METALLIC VOICE
 * SAYS:" / "SHIP INCAPABLE OF LAUNCH!". Note the spelling differs from cmd_B's metallic
 * voice on purpose: the binary has "A METALIC VOICE COMMANDS:" in asm/3048 and "A
 * METALLIC VOICE SAYS:" here, and they are two strings, not one that drifted.
 *
 * Launched, the rocket prints "PREPARE FOR LAUNCH!", records the pad cell and SAVES
 * (see below), then enters the in-space command loop (asm/6999): a readout, a "CMD: "
 * prompt, and arrows / H / L. The southward descent belongs to the DIVE (the second L),
 * not to the launch -- asm/6999 has no `ret` and its 'L' arm returns through its own
 * stack frame to asm/38B0's descent. This is the correction that closed G62: the state
 * is which loop owns input, and it lives on the stack, not in a discriminator byte.
 *
 * Two behaviours in asm/6999's entry are deliberately NOT modelled, because the capture
 * record contradicts the straight-line reading of the bytes and inventing a placement
 * would be claiming a mechanism:
 *   * THE ARMOUR GATE (L_006c54, `cmp $0x5` on DS:62). The bytes put it on the launch
 *     path, but the g60 capture observed a PLATE character launching from Earth orbit,
 *     printing "HYPERWARP ENGAGED!" and going on ticking -- the handoff records the
 *     check as firing "from the status path, not the launch." The status path is not
 *     separately identified in the corpus, so the gate is left as the tested pure
 *     function u2_space_armour_ok (space.h) rather than wired to a guessed site. The
 *     explode arm is a HANG, not a death, and space_check.c carries that non-assertion.
 *   * THE LAUNCH-TIME OFF-COURSE ROLL. asm/6999's entry calls L_006ffd, which contains
 *     the off-course gate and the orbit determination, so on the bytes a launch could
 *     dump you off-course. No capture has shown the world changing on launch, so the
 *     engine prints "HYPERWARP ENGAGED!" (observed) but does not roll on launch. The
 *     gate is exercised on 'H', where it was observed on both arms.
 */
u2_cmd_result u2_flight_launch_rocket(u2_ctx *ctx, u2_overworld *ow)
{
    seam_str_out(ctx, U2_S_LAUNCH_ROCKET);

    if (ctx->state.items_held[U2_ITEM_TRI_LITHIUM] == 0) {
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_LAUNCH_ROCKET_REF_A);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_LAUNCH_ROCKET_REF_B);
        u2_cmd_done(ctx);
        return U2_CMD_DONE;
    }

    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_LAUNCH_ROCKET_GO);

    /* asm/38B0 at L_003b52: DS:6B := y, DS:6A := x, DS:69 := 1, then `call L_0024fd`.
     * Launching a rocket SAVES THE GAME, silently, and it does so BEFORE the trip is
     * known to be survivable, so the record holds the pad cell. The guard on that block
     * is [DS:6D]==0 AND [DS:4A]==0 -- Earth and a wilderness sector -- which is why
     * launching from a site sector leaves the disk alone; measured as a controlled pair
     * on 2026-11-03, launch from sector 40 rewrote `player` and launch from sector 41
     * did not. DS:69/6A/6B are the parked-rocket triple, modelled in state.h: the save
     * here is what makes a rocket left on a planet survive a restart. */
    if (ctx->state.world == 0 && ow->sector_lo == 0) {
        ctx->state.rocket_parked_y = (uint8_t)ctx->state.sector_y;
        ctx->state.rocket_parked_x = (uint8_t)ctx->state.sector_x;
        ctx->state.rocket_parked   = 1;
        (void)u2_overworld_autosave(ctx, ow);
    }

    /* asm/6999 entry: spend one TRI-LITHIUM, print the readout, run the launch
     * animation (which prints "HYPERWARP ENGAGED!"), then the command loop. The
     * position reset to (0,0) and the world-map view mode happen in the descent, which
     * is where asm/38B0 sets them -- after the space loop returns on the dive. */
    ctx->state.items_held[U2_ITEM_TRI_LITHIUM] =
        u2_bcd_dec1(ctx->state.items_held[U2_ITEM_TRI_LITHIUM]);
    space_screen_enter(ctx);
    seam_str_out(ctx, U2_S_HYPERWARP_ENGAGED);
    seam_char_print(ctx, '\r');   /* trailing 0x8d in the original's literal, as above */

    /* The sprite starts where the capture found it: DS:47B/47C read 0x80/0x40 in
     * orbit and did not move on the dive (captures/spice86/w6-orbit-dive). */
    ctx->state.space_ship_x = U2_SPACE_SHIP_X0;
    ctx->state.space_ship_y = U2_SPACE_SHIP_Y0;

    space_loop(ctx, ow);

    u2_cmd_done(ctx);
    return U2_CMD_DONE;
}
