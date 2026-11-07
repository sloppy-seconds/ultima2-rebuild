/*
 * tests/ai_check.c — CP5 W6/W6b: the creature turn, against the real ai.c.
 *
 * Every expectation cites the byte it came from; the routine is asm/0A6D..0AB5
 * (per-slot pass) and asm/2122..2260 (the move), written up in
 * docs/monster-spawn.md § 13. The four that are easiest to get wrong, and are
 * therefore pinned hardest here:
 *
 *   - an empty slot is mon_type == 0 (asm/0A74 `or %al,%al; je next`), NOT 0x40;
 *   - a creature never attacks for being adjacent: the special is a proximity
 *     gate plus a 1-in-8 roll (asm/0AAA), and contact stops the move rather than
 *     causing a blow (asm/2201 -> asm/218b);
 *   - the retreat threshold (hp < 15, asm/0B07) is not the regen threshold
 *     (hp < 16, asm/2126), so a creature on exactly 15 heals and keeps coming;
 *   - the step is a diagonal (asm/212e), which is two cells of Manhattan
 *     distance in one turn — the measured "movers shift 2 cells".
 *
 * Build and run by tests/run_cp5.sh.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/rng.h"
#include "core/seam.h"
#include "core/state.h"
#include "core/vram.h"
#include "core/world.h"
#include "game/ai.h"
#include "game/monsters.h"
#include "game/status.h"   /* u2_status_draw, so the CMD window exists to print into */
#include "gen_content.h"

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

#define GRASS 0x08u          /* passable for the player and for land creatures */
#define WATER 0x00u          /* asm/21d8: the only tile a sea monster accepts */

static const char *msg_text(u2_ctx *ctx, int row, char *buf)
{
    int n = 0;
    for (int c = 0; c <= 29; c++)
        buf[n++] = (char)ctx->vram.cell[row][c].ch;
    while (n > 0 && buf[n - 1] == ' ') n--;
    buf[n] = '\0';
    char *p = buf;
    while (*p == ' ') p++;
    return p;
}

/* A sector of one tile, with map_base matching so a restamp has something to
 * restore. Engine-authored filler; no game bytes here (same rule as
 * monsters_check.c). */
static void ground(u2_world *w, uint8_t tile)
{
    memset(w, 0, sizeof *w);
    for (int i = 0; i < U2_SECTOR_TILES; i++)
        w->map[i] = w->map_base[i] = tile;
    w->map_loaded = 1;
}

static void arm(u2_ctx *ctx, u2_world *w, uint8_t tile,
                int slot, uint8_t x, uint8_t y, uint8_t hp, uint8_t type)
{
    memset(ctx, 0, sizeof *ctx);
    u2_vram_init(&ctx->vram);
    u2_state_init(&ctx->state);
    u2_rng_seed(ctx, 7);
    ground(w, tile);

    ctx->state.mon_x[slot]    = (uint8_t)x;
    ctx->state.mon_y[slot]    = (uint8_t)y;
    ctx->state.mon_hp[slot]   = hp;
    ctx->state.mon_type[slot] = type;
}

/* The first roll a freshly-seeded ctx serves, so a test can pick a seed whose
 * gate roll lands under 0x20 instead of hoping one does. */
static uint8_t first_roll(uint8_t seed)
{
    u2_ctx p;
    memset(&p, 0, sizeof p);
    u2_rng_seed(&p, seed);
    return u2_rand_byte(&p);
}

/* The second roll a seeded context yields. The role-1 contact path spends one roll on asm/0AAA's
 * special gate before the melee roll, so anything running on that path must search on this. */
static uint8_t second_roll(uint8_t seed)
{
    u2_ctx p;
    memset(&p, 0, sizeof p);
    u2_rng_seed(&p, seed);
    (void)u2_rand_byte(&p);
    return u2_rand_byte(&p);
}

int main(void)
{
    u2_ctx ctx;
    u2_world w;
    u2_ai_report rep;
    char buf[64];

    /* --- an empty sector does nothing, and touches no randomness ------------ */
    arm(&ctx, &w, GRASS, 0, 0, 0, 0, 0);
    rep = u2_ai_creature_turn(&ctx, &w, 10, 10);
    CHECK(rep.examined == 0 && rep.moved == 0 && u2_rng_rolls(&ctx) == 0,
          "empty sector: nothing examined, nothing moved, no roll");

    /* --- the step is a diagonal toward the player (asm/212e) ---------------- */
    /* Everything in this file down to the role-gate section runs on the world map
     * (sector digit 0), which asm/0A38 sends to the UNGATED sweep — mon_role is
     * not consulted there, so the role-1 writes below are belt-and-braces for
     * creatures that would act either way. The gated sweep is tested on its own.
     */
    arm(&ctx, &w, GRASS, 3, 5, 5, 40, 0x30);       /* Fighter, plain */
    ctx.state.mon_role[3] = 1;
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(ctx.state.mon_x[3] == 6 && ctx.state.mon_y[3] == 6,
          "pursuit is the diagonal (5,5)->(6,6), i.e. two Manhattan cells in one turn");
    CHECK(rep.moved == 1 && u2_rng_rolls(&ctx) == 0,
          "outside the 3-cell ring the gate is not rolled at all (asm/0AB6/0AC0)");

    /* --- asm/225A: the slot's terrain byte follows the creature ------------------
     *
     * DS:1B7 is not a static spawn terrain. asm/2233 restores it into the cell the
     * creature leaves and asm/225a immediately overwrites it with the terrain of the
     * cell it entered, so it is a rolling one-cell memory: "the ground under this
     * creature right now". The engine had no equivalent update. That is invisible
     * while map_base is accurate, and wrong across a save: the saved mapx carries the
     * stamp at the creature's CURRENT cell while the saved monx column would still
     * carry its SPAWN cell's terrain, so a reload restores the wrong byte there and
     * clear_spawn_cells faithfully propagates the error.
     *
     * Fixture: paving everywhere, grass only under the creature, slot byte set to
     * that grass. The pursuit test above establishes this setup moves (5,5)->(6,6),
     * so the destination is paving and the two terrains genuinely differ - which is
     * the only shape that can tell a rolling save from a stale one.
     */
    arm(&ctx, &w, 0x0Cu, 3, 5, 5, 40, 0x30);       /* paving everywhere */
    const int from = 5 * U2_SECTOR_EDGE + 5;
    const int to   = 6 * U2_SECTOR_EDGE + 6;
    w.map[from] = w.map_base[from] = GRASS;        /* grass only under it */
    ctx.state.mon_kind[3] = GRASS;
    ctx.state.mon_role[3] = 1;
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 1 && ctx.state.mon_x[3] == 6 && ctx.state.mon_y[3] == 6,
          "fixture: moved (5,5)->(6,6), off grass and onto paving");
    CHECK(w.map[from] == GRASS,
          "the cell it left was restored to its own terrain, not to a constant");
    CHECK(w.map[to] == 0x30, "the cell it entered carries its stamp");
    CHECK(ctx.state.mon_kind[3] == 0x0C,
          "asm/225A: the slot's terrain byte now names the cell it is standing on");

    /* --- the two HP thresholds are different numbers ------------------------ */
    arm(&ctx, &w, GRASS, 4, 5, 5, 15, 0x30);
    ctx.state.mon_role[4] = 1;
    u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(ctx.state.mon_hp[4] == 16 && ctx.state.mon_x[4] == 6,
          "hp 15 regenerates (asm/2126, <16) and still pursues (asm/0B07, >=15)");

    arm(&ctx, &w, GRASS, 4, 5, 5, 14, 0x30);
    ctx.state.mon_role[4] = 1;
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(ctx.state.mon_hp[4] == 15 && ctx.state.mon_x[4] == 4 && ctx.state.mon_y[4] == 4,
          "hp 14 regenerates AND flees: the vector is negated (asm/0B0F)");
    CHECK(rep.retreated == 1, "the retreat is reported, so a caller can log it");

    /* --- species: a sea monster lives in water and nowhere else ------------- */
    arm(&ctx, &w, GRASS, 6, 5, 5, 40, 0x2c);
    ctx.state.mon_role[6] = 1;       /* Sea Monster */
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0 && ctx.state.mon_x[6] == 5,
          "asm/21d8: a sea monster refuses grass");

    arm(&ctx, &w, WATER, 6, 5, 5, 40, 0x2c);
    ctx.state.mon_role[6] = 1;
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 1 && ctx.state.mon_x[6] == 6,
          "asm/21d8: the same sea monster accepts water");

    arm(&ctx, &w, WATER, 7, 5, 5, 40, 0x30);
    ctx.state.mon_role[7] = 1;       /* Fighter on water */
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0,
          "asm/21c9: the general ladder refuses tile 0x00, so land creatures do not swim");

    /* --- contact stops the move and does not cause a blow ------------------- */
    arm(&ctx, &w, GRASS, 8, 7, 7, 40, 0x30);
    ctx.state.mon_role[8] = 1;
    const uint32_t rolls_before = u2_rng_rolls(&ctx);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(ctx.state.mon_x[8] == 7 && ctx.state.mon_y[8] == 7,
          "asm/2201: the candidate cell is the player's, so the creature does not step");
    /* Corrected the same day it was written: the first version of this check said
     * "and nothing else — there is no melee site". There is: asm/0B4D's contact arm
     * also draws asm/0B62's hit roll, so two rolls per contact, and a landed blow
     * adds mon_hp>>2 + 1 to the pool. */
    CHECK(rep.moved == 0 && rep.gates == 1 && rep.contacts == 1 &&
          u2_rng_rolls(&ctx) == rolls_before + 2,
          "contact: the special gate AND asm/0B62's melee roll, and no step");
    CHECK(rep.damage_pool == 0 || rep.damage_pool == (40 >> 2) + 1,
          "a landed melee blow pools mon_hp>>2 + 1 (asm/0B79); a dodged one pools 0");

    /* --- the special gate: proximity, then 1 in 8, then the type dispatch --- */
    uint8_t seed = 0;
    while (first_roll(seed) >= 0x20 && seed < 255) seed++;
    CHECK(first_roll(seed) < 0x20, "a seed exists whose first roll passes asm/0ACF");

    arm(&ctx, &w, GRASS, 9, 7, 7, 40, 0x34);
    ctx.state.mon_role[9] = 1;       /* legs paralyser */
    u2_rng_seed(&ctx, seed);
    u2_status_draw(&ctx);
    seam_goto(&ctx, 0, U2_MSG_BOTTOM);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.gates == 1 && rep.landed == 1,
          "asm/0E70 with no boots: the effect lands with no resist roll (C2)");
    /* The message scrolls the moment its own newline lands, exactly like the death
     * tail, so it is searched across the window rather than assumed to sit on the
     * bottom row. */
    int found = 0;
    for (int row = U2_MSG_TOP; row <= U2_MSG_BOTTOM; row++)
        if (strstr(msg_text(&ctx, row, buf), "LEGS PARALIZED!") != NULL)
            found = 1;
    CHECK(found, "asm/0E70 announces the paralysis before it rolls anything");

    /* And a creature outside the ring must not print or roll: without this, a
       gate that always fired would look the same in the check above. */
    arm(&ctx, &w, GRASS, 9, 1, 1, 40, 0x34);
    ctx.state.mon_role[9] = 1;
    u2_rng_seed(&ctx, seed);
    rep = u2_ai_creature_turn(&ctx, &w, 20, 20);
    CHECK(rep.gates == 0 && rep.landed == 0 && u2_rng_rolls(&ctx) == 0,
          "control: the same paralyser far away neither rolls nor prints");

    /* --- DS:26A freezes every creature before the species test (asm/2199) --- */
    arm(&ctx, &w, GRASS, 11, 5, 5, 40, 0x30);
    ctx.state.mon_role[11] = 1;
    ctx.state.counter_26A = 1;
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0 && ctx.state.mon_x[11] == 5,
          "asm/2199: a non-zero DS:26A refuses every move (inert until W7 ticks it)");

    /* --- the grid follows the creature, and the terrain follows the grid ---- */
    arm(&ctx, &w, GRASS, 12, 5, 5, 40, 0x30);
    ctx.state.mon_role[12] = 1;
    w.map[5 * U2_SECTOR_EDGE + 5] = 0x30;          /* the spawn stamp */
    u2_ai_creature_turn(&ctx, &w, 8, 8);
    u2_monsters_restamp(&ctx, &w);
    CHECK(w.map[5 * U2_SECTOR_EDGE + 5] == GRASS,
          "the vacated cell is terrain again, not a permanent invisible monster");
    CHECK(w.map[6 * U2_SECTOR_EDGE + 6] == 0x30,
          "the new cell carries the creature's type byte, which is what the renderer draws");

    /* --- the role gate (asm/102B / asm/1033): most slots are inert ----------- */
    /* Only on the TOWN sweep: asm/0A38 sends sector digits 1..3 to the role-gated
     * chain at asm/1013/1015, so every check in this block sets the second digit
     * first. monxNN ships role 0 in six to eleven slots and
     * 0x81..0x85 in four or five, and both of those do nothing at all. Without
     * this control the checks above would pass while every creature in a town
     * acted. */
    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 0;
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0 && ctx.state.mon_x[13] == 5,
          "role 0: asm/102B sends the slot to next, so an inert creature stays put");

    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 0x81;          /* a scripted townsperson */
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0,
          "role 0x81: asm/1033 is `js next`, so bit 7 excludes the scripted folk");

    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x34);   /* a paralyser... */
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 2;                /* ...but alerted */
    u2_rng_seed(&ctx, seed);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);  /* far enough that it is not contact */
    CHECK(rep.gates == 0 && rep.contacts == 0 && u2_rng_rolls(&ctx) == 0,
          "role 2 goes straight to the move routine: it chases, never casts, rolls nothing");

    arm(&ctx, &w, GRASS, 13, 5, 5, 14, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 2;
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.retreated == 0 && ctx.state.mon_x[13] == 6,
          "role 2 does not run: the retreat test is on the role-1 path only");

    /* --- the world-map sweep is NOT role-gated (asm/0A38 -> asm/0A6D) -------- */
    /* The regression this exists for. asm/0A38 dispatches the post-command tail on
     * the sector's second decimal digit: 0 jumps to the sweep at asm/0A6D, which
     * never reads mon_role; 1..3 branch to the gated chain; 6..9 fall through to
     * 0A6D too. The engine applied the town gate to both, so every wandering
     * spawn — born role 0, because monx20 ships role 0 in all 32 slots — stood
     * still while the player walked past it. Owner-reported 2026-10-28: "any
     * monster I can see in the viewport has a turn and usually comes to attack me".
     * Flip the gated_sweep test in ai.c and one of these two goes red. */
    arm(&ctx, &w, GRASS, 14, 5, 5, 40, 0x30);
    ctx.state.sector_second = 0;         /* the world map */
    ctx.state.mon_role[14]  = 0;         /* what a wandering spawn leaves behind */
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 1 && ctx.state.mon_x[14] == 6 && ctx.state.mon_y[14] == 6,
          "world map: a role-0 creature takes the full pass and pursues (asm/0A6D has no role test)");

    arm(&ctx, &w, GRASS, 14, 5, 5, 40, 0x30);
    ctx.state.sector_second = 1;         /* a town sector, same creature */
    ctx.state.mon_role[14]  = 0;
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0 && ctx.state.mon_x[14] == 5,
          "control: the identical role-0 creature in a digit-1..3 sector stays put");

    /* --- role 3: the wander (asm/1086..asm/10CE), decoded 2026-10-26 ----------------------
     * This replaces a test that asserted role-3 creatures do NOT move, with the stated reason
     * "asm/1086 is undecoded and the engine under-moves rather than guess". That was honest when it
     * was written and is now the regression being retired: the chain stages a per-slot step vector,
     * re-rolls it when rand_byte < 0x40 (one turn in four), and moves the single staged cell through
     * the same asm/2199 gate the pursuit paths use. Freezing those slots was the owner-reported bug —
     * the LINDA thief standing like a statue — and role 3 is 179 of the shipped monx role bytes.
     */
    uint8_t keep = 0, reroll = 0;
    while (keep < 255 && first_roll(keep) < 0x40) keep++;
    while (reroll < 255 && first_roll(reroll) >= 0x40) reroll++;
    CHECK(first_roll(keep) >= 0x40 && first_roll(reroll) < 0x40,
          "seeds exist for both branches of asm/10A0 (keep the direction / re-roll)");

    /* 1. A fresh sector has zero vectors (monx ships zeros), so a role-3 creature has not started
          moving until the first re-roll. Faithful, not a gap. */
    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 3;
    u2_rng_seed(&ctx, keep);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0 && ctx.state.mon_x[13] == 5,
          "role 3 with a zero vector and a keep-roll stays put (asm/10A0 jae)");

    /* 2. THE CONTROL FOR THE BUG ITSELF: a stored vector MUST move the creature. Under the old
          `moves = (role == 1 || role == 2)` gate this goes red, which is the point. */
    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 3;
    ctx.state.mon_wander_x[13] = 1;
    ctx.state.mon_wander_y[13] = 1;
    u2_rng_seed(&ctx, keep);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 1 && ctx.state.mon_x[13] == 6 && ctx.state.mon_y[13] == 6,
          "role 3 moves on its stored vector (asm/1086 -> asm/10CE stages it as the candidate)");

    /* 3. The re-roll branch writes a UNIT vector, not a raw random byte. */
    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 3;
    u2_rng_seed(&ctx, reroll);
    (void)u2_ai_creature_turn(&ctx, &w, 8, 8);
    const int8_t wx = (int8_t)ctx.state.mon_wander_x[13];
    const int8_t wy = (int8_t)ctx.state.mon_wander_y[13];
    CHECK(wx >= -1 && wx <= 1 && wy >= -1 && wy <= 1 && (wx != 0 || wy != 0),
          "the re-roll branch writes a unit vector in {-1,0,+1}, not a raw byte");

    /* 4. THE DISTRIBUTION TRAP, and the reason this block exists. asm/50D8_sign_of_al is NOT a
          uniform draw over {-1,0,+1}: it returns 0 only when the byte is exactly 0x00, so 0 happens
          1/256 of the time and a wander step is almost always a diagonal. Driven through the real
          path rather than a helper, because the helper is static and the path is what matters. A
          `rand()%3-1` implementation gives 0 about a third of the time and this goes red — and it
          would still pass any test that only asks whether the creature eventually moved. */
    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 3;
    u2_rng_seed(&ctx, 7);
    int wz = 0, wnz = 0;
    for (int i = 0; i < 4000; i++) {
        ctx.state.mon_x[13] = 5; ctx.state.mon_y[13] = 5;
        (void)u2_ai_creature_turn(&ctx, &w, 8, 8);
        if ((int8_t)ctx.state.mon_wander_x[13] == 0) wz++; else wnz++;
    }
    CHECK(wz * 4 < wnz,
          "wander x is zero in only %d of %d turns — sign_of_al, not a uniform -1/0/+1 draw",
          wz, wz + wnz);

    /* 5. ONE candidate, no fallback. Block the diagonal only. A wanderer must stay put; a role-1
          pursuer must NOT, because it has the diagonal -> y-only -> x-only ladder. The pair is the
          control: if someone routes the wander through creature_step, the first CHECK goes red. */
    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x30);
    u2_world_tile_put(&w, 6, 6, WATER);        /* the diagonal is impassable, the orthogonals are not */
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 3;
    ctx.state.mon_wander_x[13] = 1;
    ctx.state.mon_wander_y[13] = 1;
    u2_rng_seed(&ctx, keep);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0 && ctx.state.mon_x[13] == 5 && ctx.state.mon_y[13] == 5,
          "wander refuses a blocked candidate and does NOT fall back to an orthogonal");

    arm(&ctx, &w, GRASS, 13, 5, 5, 40, 0x30);
    u2_world_tile_put(&w, 6, 6, WATER);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 1;
    u2_rng_seed(&ctx, keep);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 1 && ctx.state.mon_x[13] == 5 && ctx.state.mon_y[13] == 6,
          "control: the same blocked diagonal, a role-1 pursuer takes the y-only fallback (asm/2155)");

    /* 6. The wander goes through the SAME step gate: a land creature on an all-water grid cannot
          wander anywhere, no matter what vector it holds. */
    arm(&ctx, &w, WATER, 13, 5, 5, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[13] = 3;
    ctx.state.mon_wander_x[13] = 1;
    ctx.state.mon_wander_y[13] = 0;
    u2_rng_seed(&ctx, keep);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.moved == 0 && ctx.state.mon_x[13] == 5,
          "the wander is refused by the same asm/2199 tile gate the pursuit paths use");

    /* --- the melee pool and the armour dodge (asm/0B62 / asm/0B79) ----------- */
    /* These run on the role-1 path, because asm/0AA7's contact test is only reachable from role 1
       inside a town (see the `contact` gate in ai.c). Role 1 means asm/0AAA's special gate rolls
       FIRST, so the melee roll is the SECOND roll of the turn. An earlier revision used role 2
       precisely to dodge that extra roll — which was only valid because the engine's contact test
       was ungated, and gating it correctly is what moved the roll. Find a seed whose second roll is
       non-negative with low three bits 0: with armour 0 the blow lands, with armour 1 it is dodged.
       That is the whole of what armour does in the original, and it is the only damage a plain
       melee creature can ever do. */
    uint8_t mseed = 0;
    while (mseed < 255) {
        const uint8_t r = second_roll(mseed);
        if (r < 0x80 && (r & 7) == 0) break;
        mseed++;
    }
    const uint8_t mr = second_roll(mseed);
    CHECK(mr < 0x80 && (mr & 7) == 0, "a seed exists whose second roll is hit-able at SKIN");

    arm(&ctx, &w, GRASS, 14, 7, 7, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[14] = 1;
    ctx.state.worn_armour = 0;                      /* SKIN */
    u2_rng_seed(&ctx, mseed);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.contacts == 1 && rep.damage_pool == (40 >> 2) + 1,
          "SKIN: the blow lands and pools mon_hp>>2 + 1 = %d", (40 >> 2) + 1);

    arm(&ctx, &w, GRASS, 14, 7, 7, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[14] = 1;
    ctx.state.worn_armour = 1;                      /* one step of armour */
    u2_rng_seed(&ctx, mseed);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.contacts == 1 && rep.damage_pool == 0,
          "control: the same roll against armour index 1 is dodged (asm/0B71)");

    /* --- the contact gate: only role 1 touches you inside a town -----------------
     * asm/0AA7 holds the candidate-vs-player test (asm/0B25/0B39) and the thief branch (asm/0B4D).
     * The town sweep enters asm/0AA7 only from asm/1082, the role-1 branch: asm/1035 sends role >= 3
     * to the wander and asm/1039 sends role 2 to the move-only path, and neither returns.
     *
     * Measured against the ORIGINAL in sector 21 ("forrest town"), whose thieves are all role 3:
     * many turns adjacent to slot 31 at (34,58) left DS:D6..DF byte-identical and HP at 9990, while
     * the thief wandered to (35,59). The engine had been stealing there on film (turn 342) because
     * this test ran before the role gate. Forrest town ships no role-1 creature at all.
     */
    for (int role = 2; role <= 3; role++) {
        arm(&ctx, &w, GRASS, 14, 7, 7, 40, 0x30);
        ctx.state.sector_second = 1;
        ctx.state.mon_role[14] = (uint8_t)role;
        ctx.state.worn_armour = 0;
        u2_rng_seed(&ctx, mseed);
        rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
        CHECK(rep.contacts == 0 && rep.damage_pool == 0,
              "town: role %d in contact makes no contact and pools no damage (asm/0AA7 unreachable)", role);
    }

    /* CONTROL for that loop: the identical setup one role number up MUST make contact, so the two
       checks above cannot pass by never reaching anything at all. */
    arm(&ctx, &w, GRASS, 14, 7, 7, 40, 0x30);
    ctx.state.sector_second = 1;
    ctx.state.mon_role[14] = 1;
    ctx.state.worn_armour = 0;
    u2_rng_seed(&ctx, mseed);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.contacts == 1, "control: role 1 in the same town cell DOES make contact");

    /* And the world map is ungated: asm/0A6D..0AB5 never reads mon_role, so a role-3 creature out
       there really can hit you. That asymmetry is the original's, not ours — see E6. */
    arm(&ctx, &w, GRASS, 14, 7, 7, 40, 0x30);
    ctx.state.sector_second = 0;
    ctx.state.mon_role[14] = 3;
    ctx.state.worn_armour = 0;
    u2_rng_seed(&ctx, mseed);
    rep = u2_ai_creature_turn(&ctx, &w, 8, 8);
    CHECK(rep.contacts == 1, "world map: role 3 in contact DOES make contact (asm/0A6D has no role test)");

    /* --- the alarm (asm/113A): violence inside a site wakes slots 0..7 -------- */
    arm(&ctx, &w, GRASS, 3, 5, 5, 40, 0x30);
    ctx.state.sector_second = 0;                /* the world map */
    u2_ai_alarm(&ctx);
    CHECK(ctx.state.mon_role[0] == 0 && ctx.state.mon_role[7] == 0,
          "asm/113A: on the world map there is nobody to alert");

    ctx.state.sector_second = 2;                /* inside a town */
    u2_ai_alarm(&ctx);
    int all_one = 1;
    for (int i = 0; i <= 7; i++)
        if (ctx.state.mon_role[i] != 1) all_one = 0;
    CHECK(all_one, "asm/114A: slots 7..0 all take role 1");
    CHECK(ctx.state.mon_role[8] == 0, "and slot 8 does not: the loop counts 7 down to 0");

    /* --- the step gate, exhaustively: asm/2199..21FC, corroborated by Apple $6672 ----------
     *
     * The existing checks above probe the gate one case at a time, which is how a gate passes
     * while being wrong about a tile nobody thought to try. This block enumerates every tile
     * byte that can actually occur in a sector map — all multiples of 4, per
     * docs/passability-findings.md F12 — against an expectation written from the clause list in
     * asm/2199's header and independently confirmed clause for clause on the Apple II side at
     * $6672. The expectation is NOT read out of ai.c: if the engine and this table ever
     * disagree, one of them is wrong, and a test that copies the implementation proves nothing.
     *
     * The accept set the original has five members: 0x08, 0x0C, 0x5C, 0x70, and 0x00 for the two
     * water kinds only. Anything else must be refused, and that "anything else" is the part a
     * spot-check cannot cover. */
    {
        /* Written from asm/2199's rule, not from ai.c. */
        int bad = 0, cases = 0;
        const uint8_t types[] = { 0x2c, 0x48, 0x30, 0x34, 0x74, 0x78, 0x7c, 0xfc };
        for (int ti = 0; ti < (int)(sizeof types / sizeof types[0]); ti++) {
            for (int tile = 0; tile <= 0xfc; tile += 4) {
                uint8_t t = types[ti];
                int water = (t == 0x2c || t == 0x48);
                int want;
                if (water) {
                    want = (tile == 0x00);
                } else {
                    int ladder = !(tile == 0x10 || tile == 0x04 || tile == 0x00)
                              && (tile < 0x14 || tile == 0x70);
                    want = (t < 0x78) ? (tile == 0x5c || ladder) : ladder;
                }
                arm(&ctx, &w, GRASS, 7, 5, 5, 40, t);
                ctx.state.mon_role[7] = 1;
                ctx.state.sector_second = 0;      /* world sweep: no site guard, no role gate */
                w.map[6 * U2_SECTOR_EDGE + 6] = (uint8_t)tile;
                rep = u2_ai_creature_turn(&ctx, &w, 9, 9);
                int got = (ctx.state.mon_x[7] == 6 && ctx.state.mon_y[7] == 6);
                cases++;
                if (got != want) {
                    bad++;
                    if (bad <= 6)
                        printf("  FAIL  gate: type %02X onto tile %02X -> %s, spec says %s\n",
                               t, tile, got ? "accept" : "refuse", want ? "accept" : "refuse");
                }
            }
        }
        CHECK(bad == 0, "asm/2199 step gate: %d/%d (type x tile) combinations agree with the "
                        "clause list (%d disagree)", cases - bad, cases, bad);

        /* The freeze counter refuses everything, before any tile is consulted (asm/2199). */
        int frozen_bad = 0;
        for (int tile = 0; tile <= 0xfc; tile += 4) {
            arm(&ctx, &w, GRASS, 7, 5, 5, 40, 0x30);
            ctx.state.mon_role[7] = 1;
            ctx.state.sector_second = 0;
            ctx.state.counter_26A = 0x14;     /* the value asm/3C04_coin_rub stores */
            w.map[6 * U2_SECTOR_EDGE + 6] = (uint8_t)tile;
            u2_ai_creature_turn(&ctx, &w, 9, 9);
            if (ctx.state.mon_x[7] != 5) frozen_bad++;
        }
        CHECK(frozen_bad == 0,
              "asm/2199: DS:26A = 0x14 refuses all 64 tiles, whatever the species (%d leaked)",
              frozen_bad);

        /* The site guard (asm/21e2): inside a site, column 0 and row 0 are refused; on the
         * world map the grid wraps and they are walkable. */
        arm(&ctx, &w, GRASS, 7, 1, 5, 40, 0x30);
        ctx.state.mon_role[7] = 1;
        ctx.state.sector_second = 1;
        u2_ai_creature_turn(&ctx, &w, 0, 8);      /* step lands on column 0 */
        CHECK(ctx.state.mon_x[7] == 1,
              "asm/21e2: inside a site a step onto column 0 is refused");

        arm(&ctx, &w, GRASS, 7, 1, 5, 40, 0x30);
        ctx.state.mon_role[7] = 1;
        ctx.state.sector_second = 0;
        u2_ai_creature_turn(&ctx, &w, 0, 8);
        CHECK(ctx.state.mon_x[7] == 0,
              "control: on the world map the same step is allowed — the guard is the site digit");

        arm(&ctx, &w, GRASS, 7, 2, 5, 40, 0x30);
        ctx.state.mon_role[7] = 1;
        ctx.state.sector_second = 1;
        u2_ai_creature_turn(&ctx, &w, 0, 8);      /* step lands on column 1 */
        CHECK(ctx.state.mon_x[7] == 1,
              "control: inside a site a step onto column 1 is fine — the guard is the edge, not x");

        /* Row 0 is a separate arm of the same `||`, and a table that only tries column 0 will
         * pass while row 0 is unguarded. Found by mutation, not by writing the test. */
        arm(&ctx, &w, GRASS, 7, 5, 1, 40, 0x30);
        ctx.state.mon_role[7] = 1;
        ctx.state.sector_second = 1;
        u2_ai_creature_turn(&ctx, &w, 9, 0);      /* step lands on row 0 */
        CHECK(ctx.state.mon_y[7] == 1,
              "asm/21e2: inside a site a step onto row 0 is refused");

        arm(&ctx, &w, GRASS, 7, 5, 1, 40, 0x30);
        ctx.state.mon_role[7] = 1;
        ctx.state.sector_second = 0;
        u2_ai_creature_turn(&ctx, &w, 9, 0);
        CHECK(ctx.state.mon_y[7] == 0,
              "control: on the world map the same row-0 step is allowed");

        arm(&ctx, &w, GRASS, 7, 5, 2, 40, 0x30);
        ctx.state.mon_role[7] = 1;
        ctx.state.sector_second = 1;
        u2_ai_creature_turn(&ctx, &w, 9, 0);      /* step lands on row 1 */
        CHECK(ctx.state.mon_y[7] == 1,
              "control: inside a site a step onto row 1 is fine");
    }

    /* --- two creatures may not finish a pass on one cell -------------------- */
    /* The original has no occupancy clause in asm/2199: the step gate tests the candidate
     * cell's TILE only. It needs none, because a creature's type byte is stamped into the
     * grid and no creature type is in the gate's accept set — the stamp IS the occupancy
     * rule. That depends on the stamp being CURRENT, and the engine used to restamp once
     * after the whole pass, leaving the grid one creature stale. Two creatures then landed
     * on one cell; the renderer draws one sprite per cell, so the stack was invisible and
     * the symptom was a `KILLED` with the creature still standing there, needing a second
     * kill. The owner reported exactly that.
     *
     * Evidence the original never stacks: 91,851 occupied slot-samples across the 3000
     * per-turn dumps of captures/spice86/fight-20261021-castle03 contain zero stacked
     * cells, and the shipped monx data has one stacked cell in 33 sectors. */
    arm(&ctx, &w, GRASS, 3, 17, 17, 40, 0x30);      /* wants (18,18) */
    ctx.state.mon_role[3] = 1;
    ctx.state.mon_x[4] = 17; ctx.state.mon_y[4] = 18;/* also reaches (18,18) */
    ctx.state.mon_hp[4] = 40; ctx.state.mon_type[4] = 0x30;
    ctx.state.mon_role[4] = 1;
    /* Force the collision: slot 4's diagonal (18,19) and its y-only fallback (17,19) are
     * mountains, so its only remaining candidate is the cell slot 3 is about to take.
     * Both creatures sit 3 cells out on at least one axis, which is outside the ring where
     * asm/0AB6 rolls a proximity gate — otherwise the roll, not the stamp, decides. */
    for (int k = 0; k < 2; k++) {
        const int bx = k ? 17 : 18, by = 19;
        w.map[by * U2_SECTOR_EDGE + bx] = w.map_base[by * U2_SECTOR_EDGE + bx] = 0x10;
    }
    u2_monsters_restamp(&ctx, &w);
    u2_ai_creature_turn(&ctx, &w, 20, 20);
    CHECK(!(ctx.state.mon_x[3] == ctx.state.mon_x[4]
            && ctx.state.mon_y[3] == ctx.state.mon_y[4]),
          "two creatures ended one AI pass on the same cell (%d,%d) — the stamp is stale again",
          ctx.state.mon_x[4], ctx.state.mon_y[4]);
    /* NOT asserted, and deliberately so: which of the two cells each creature ends on. The
     * pass turned out not to resolve in ascending slot order — the pair swapped (17,17)/(17,18)
     * for (18,18)/(17,19) is refused — and whether the original scans slots upward is not
     * established anywhere. Asserting a specific cell here would freeze an unproven ordering,
     * which is the mistake this project keeps writing down. What is asserted is the invariant
     * the owner's bug report is actually about: one creature per cell. */
    CHECK(ctx.state.mon_x[3] != ctx.state.mon_x[4] || ctx.state.mon_y[3] != ctx.state.mon_y[4],
          "control: the two creatures occupy distinct cells");

    printf("ai_check: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
