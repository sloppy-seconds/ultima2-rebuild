/*
 * commands.c -- the command-letter dispatch table, in the shape asm/0A1F has it.
 *
 * See commands.h for why this is a table rather than a switch, and for the two
 * structural facts the bytes force: the A-Z range test happens before dispatch (asm/09FB
 * then asm/0A18), and every handler ends by jumping to asm/0A33_cmd_done, which prints CR
 * and falls into the post-command tail. The epilogue is therefore owned here.
 *
 * Content policy: every word printed here comes from the extractor's table, which reads it
 * out of the user's own copy at build time. Nothing in this file hard-codes game text.
 */
#include "game/commands.h"

#include <stdbool.h>
#include <string.h>

#include "core/seam.h"
#include "core/rng.h"
#include "core/world.h"   /* u2_combat_* now take the world: a kill restores the corpse's cell */
#include "core/frame.h"
#include "core/layout.h"   /* U2_ENH_MAP_ROWS, to place the doubled star field */
#include "core/font.h"
#include "game/status.h"
#include "game/overworld.h"
#include "game/dungeon.h"
#include "game/combat.h"
#include "game/commerce.h"   /* the TRANSACT / STEAL / OFFER continuations */
#include "game/monsters.h"
#include "core/state.h"
#include "game/space.h"
#include "game/flight.h"      /* the LAUNCH / orbit screens; their own loop */

/* Generated at build time from the user's copy (engine/tools/extract/extractor.py). */
#include "gen_content.h"

/* The indexed name pool may be absent (CI has no copy of the game), and the degradation is
 * deliberate rather than silent -- same rule and same reason as status.c's Z_NAME. */
#ifdef U2_T_SPELLS_N
#  define SPELL_NAME(i) (((i) >= 0 && (i) < U2_T_SPELLS_N) ? U2_T_SPELLS[(i)] : "")
#else
#  define SPELL_NAME(i) ""
#endif

/* The armour pool, same degradation rule. The WEAR ARMOUR menu number indexes it directly:
 * 0 = SKIN (bare), 1 = CLOTH ... 6 = POWER, and 7 = NONE is the pool's own spare entry. */
#ifdef U2_T_ARMOUR_N
#  define ARMOUR_NAME(i) (((i) >= 0 && (i) < U2_T_ARMOUR_N) ? U2_T_ARMOUR[(i)] : "")
#else
#  define ARMOUR_NAME(i) ""
#endif

/* The weapon pool, indexed the same way: asm/3EA5 does `adc $0x13,%al` before the pool printer,
 * and 0x13 is the WEAPONS group base the extractor already folds into U2_T_WEAPONS. So the READY
 * WEAPON menu number indexes the pool directly, 0 = HANDS .. 9 = QUICK SWORD — which is the same
 * index space as state.h's readied_weapon (DS:61). */
#ifdef U2_T_WEAPONS_N
#  define WEAPON_NAME(i) (((i) >= 0 && (i) < U2_T_WEAPONS_N) ? U2_T_WEAPONS[(i)] : "")
#else
#  define WEAPON_NAME(i) ""
#endif

/* DS:E3 is not a separate counter and must not be modelled as one: items_held is 16 slots from
 * DS:D6, so DS:E3 is items_held[13], and the name table at file 0x501D calls slot 13 STRANGE
 * COIN. That is what NEGATE TIME spends — the byte has exactly three sites in the binary, all of
 * them in asm/3BCE..asm/3C04, and the seeded capture's value of 0x01 was simply maxout's one
 * strange coin (captures/spice86/cmd-nr-live). */
#define ITEM_STRANGE_COIN 13

/* asm/45B8: `stc ; cmc ; sbb $0x1,%al ; das ; cmc` — a packed-decimal subtract of one, so the
 * nibble borrows in decimal and not in hex: 0x10 becomes 0x09, not 0x0F. Item counts are BCD
 * everywhere in this binary; treating them as binary is the same class of mistake as reading the
 * strength byte as its displayed number. */
#define cmd_done u2_cmd_done

#define bcd_dec1 u2_bcd_dec1

/* The letters the original's jump table covers, in table order. cs:0730 holds 26 words and
 * `sbb $0x41,%al` indexes them, so A is entry 0 and Z is entry 25. */
#define CMD_COUNT 26

/*
 * A handler. Returns what the loop should do next; see u2_cmd_result.
 *
 * `letter` is passed so a shared helper can name which command it was reached from. That
 * matters more than it sounds: the aim prompt is not one routine the handlers call, it is a
 * family that shares asm/2362, and the callers of that routine are exactly cmd_U (through
 * asm/4307), cmd_S (through asm/3F37), cmd_O, cmd_T (through asm/405D) and two unnamed sites
 * (asm/2BBE, asm/35EB). cmd_C is NOT one of them -- it takes no direction, which is corrected
 * in its own header -- and "the direction commands" is not a set you can treat uniformly.
 *
 * cmd_T's membership is worth the note because it was missed. asm/405D stores the aim call as
 * `.byte 0xe8 0x02 0xe3` -- a forced-keep, because the vendored assembler re-encodes the
 * displacement -- so the `# calls:` metadata never listed it and asm/2362's own header
 * concluded from that silence that cmd_T aimed by some other mechanism. It does not; the call
 * edge was hidden by the listing, not absent from the binary.
 */
typedef u2_cmd_result (*cmd_fn)(u2_ctx *ctx, u2_overworld *ow, uint8_t letter);

/* ---- the epilogue every handler shares ------------------------------------ */

/*
 * asm/0A33_cmd_done: load 0x0D and call char_print, then FALL THROUGH to
 * asm/0A38_post_command_tail. The fall-through is not a return -- there is no `ret` in the
 * routine -- and the tail it reaches is what runs the creature sweep, which is the measured
 * answer to "what triggers a wandering spawn" (docs/monster-spawn.md section 15.2: not a
 * step and not a clock tick, but the end of a command).
 *
 * The engine's sweep lives in overworld.c as creature_turn(), and the loop already runs it
 * on the arms-paralysis rule, so this function prints the CR and the prompt and leaves the
 * sweep to the caller. That split is deliberate rather than tidy: whether the sweep runs
 * after a command that did nothing is NOT established for every arm, and asm/0A38's own
 * dispatch on the sector's low digit (docs/monster-spawn.md) means "after every command"
 * is an over-claim. Encoding it here would make an unproven claim structural.
 */
void u2_cmd_done(u2_ctx *ctx)
{
    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_CMD_PROMPT);
}

/* ---- handlers ------------------------------------------------------------- */

/* CAPTURE-CONFIRMED 2026-10-25 (was UNPROVEN): cmd_H was never observed running until
   captures/spice86/cmd-letters-20261025, where the CMD log reads `HYPER WHAT?` and
   ExecutedInstructions contains 3782. The marker is removed because the marker gate's R2
   treats a stale UNPROVEN as a lie in the other direction -- it would tell the next agent that
   a captured handler still needs capturing. Body is unchanged: asm/3782 is `call str_out` on
   the literal then `jmp L_000c33`. */
static u2_cmd_result cmd_H(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)ow; (void)letter;
    seam_str_out(ctx, U2_S_HYPER_WHAT);
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* CAPTURE-CONFIRMED 2026-10-25 (was UNPROVEN): cmd_J ran in
   captures/spice86/cmd-letters-20261025 and printed `JUMP...WHEE...`. The observation the
   capture settled is the one this comment used to flag as suspicious: the message promises a
   jump the body never performs, and the body really is only print-and-return -- there is no
   tail-call hiding the action. So the original ships a message that overstates what the
   handler does. That is now observed rather than inferred.

   And it is intentional. The owner reports the official manual describing J as letting the
   character "jump up and down", a way to release frustration, with no effect on obstacles,
   terrain or any mechanic. The bytes agree, and go further: a handler with three instructions
   and no write cannot touch a mechanic. So the emptiness is the feature, not a stub waiting to
   be finished -- do not "fix" it by implementing a jump. Recorded in docs/third-party-claims.md. */
static u2_cmd_result cmd_J(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)ow; (void)letter;
    seam_str_out(ctx, U2_S_JUMP_WHEE);
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* CAPTURE-CONFIRMED: cmd_Q saves. asm/3DE4 refuses indoors with ONLY OUTDOORS! and asm/3DFA
   saves outdoors echoing ONE MOMENT PLEASE!; both live in u2_overworld_save(), which owns the
   printing, so this handler adds nothing and must not: the original's Q arm prints no CR and
   no prompt of its own, which is why it does not call cmd_done().

   The only thing chosen here is the policy: original mode runs all three guards, enhanced mode
   drops them and saves anywhere (owner, 2026-10-29). Gate: tests/save_gate.py. */
static u2_cmd_result cmd_Q(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    const u2_save_policy *pol = (ctx->fidelity == U2_FIDELITY_ENHANCED)
                                    ? &U2_SAVE_POLICY_ENHANCED
                                    : &U2_SAVE_POLICY_ORIGINAL;
    ctx->store_reason = "Q";
    (void)u2_overworld_save(ctx, ow, pol);
    return U2_CMD_DONE;
}

/* asm/3DBC_cmd_P: PASS as a command letter, reached through cs:0730 entry P. It is a
   different site from the walk loop's space bar (asm/0936's `cmp $0x20,%al` at file
   0x0B3C), and the two do NOT cost the same: the space bar charges 10 food (measured
   live), but cmd_P prints PASS and jmps to asm/0C33 (the status-counter tick) and charges
   NOTHING - measured live, food flat across three letter-P passes with a move control
   dropping 19 (captures/spice86/g70-pass-vs-move). The engine keeps them apart for that
   reason and for the one that matters structurally: only cmd_P is in the jump table, so
   only it belongs in this file's table. The body lives in overworld.c as
   u2_overworld_pass(), which takes the food charge as a flag; cmd_P passes 0. Like cmd_Q
   it does not call cmd_done(): echo_cmd already emits the CR and the prompt. */
static u2_cmd_result cmd_P(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    u2_overworld_pass(ctx, ow, 0);
    return U2_CMD_DONE;
}

/* asm/4568 cmd_Z: the whole-page status screen, then wait for a key. Migrated out of
   u2_overworld_loop with its evidence where it was: docs/status-screen-z.md section 4
   measured that Z costs no food, moves no creature, does not advance the turn, and that the
   page is a full overwrite rather than an overlay -- which is why the world has to be
   redrawn on the way out. Whether the post-command sweep runs after Z is explicitly
   unmeasured (it needs live monsters on the field), so this handler runs none and does not
   call cmd_done(); inventing either would be exactly the kind of guess the project's rules
   exist to stop.
   Escape is NOT special here, and must not be made special. An earlier revision of this
   handler returned a quit code on Escape as a convenience for the --play-map development
   entry. That was never the original's behaviour -- docs/status-screen-z.md section 4 measured
   that ANY key closes the page -- and a development affordance living inside a command handler
   reads as fidelity when it is not. Removed 2026-10-26 at the owner's direction. Runs end on
   input exhaustion, which is verified to work without it. */
static u2_cmd_result cmd_Z(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;

    /* Z is an OVERLAY, not an edit of the screen.
     *
     * The page is drawn into the persistent text grid, which is also what the overworld compositor
     * reads back for the status band and the CMD prompt. So the page has to be taken out again
     * rather than painted over, and the way to take it out is to never have changed anything the
     * game still needs: snapshot the grid, draw the page, and put the snapshot back on dismissal.
     * The map and the command band are then untainted by construction, not by reconstruction --
     * nothing has to be redrawn correctly, because nothing was written.
     *
     * u2_vram is a plain struct (fixed cell array plus cursor and flags), so the copy is the whole
     * state. `writes` is a diagnostic counter of cells actually written, and the page really did
     * write them, so it is kept rather than rolled back.
     */
    const u2_vram saved = ctx->vram;
    const uint64_t writes = ctx->vram.writes;
    /* The vram snapshot does NOT carry the text cursor, and that is the cursor the seams use:
     * seam_char_print walks ctx->state.cursor_row / cursor_col (DS:2E/2F), while u2_vram keeps
     * its own drawing position. u2_status_draw_full leaves the state cursor wherever the item
     * lists finished, so restoring only the vram put the next command's output a row or two too
     * high. cmd_S is what showed it — an unimplemented letter printed nothing, so for as long as
     * S was a gap the page looked traceless. The CP5 property "Z leaves no trace" is the rule
     * being enforced here; this is the half of it the snapshot was missing.
     *
     * right_margin goes in the same snapshot for the same reason. status.c restores it itself
     * today (0x26 in, back out at the tail), but a teardown that claims no trace should not
     * depend on the drawer having tidied up after itself. */
    const uint8_t cur_row = ctx->state.cursor_row;
    const uint8_t cur_col = ctx->state.cursor_col;
    const uint8_t margin_r = ctx->state.right_margin;
    const uint8_t margin_l = ctx->state.left_margin;
    u2_status_draw_full(ctx);

    /* Present the page as a WHOLE-GRID frame, not through the overworld compositor.
     *
     * u2_overworld_draw owns pixel rows 0..159 for the map window and composites only vram rows
     * U2_MSG_TOP..24 into the game frame. That is right for the play screen and completely wrong
     * for a full-screen page: the map was painted over the top twenty rows of the Z page, so in
     * the SDL build the player saw the tile view with the tail of the item list underneath it —
     * the owner's "it still shows the stats in the 4 line cmd section". Headless could never show
     * this, because the null renderer has no map layer to paint over anything.
     *
     * So a full-screen text page composes its own frame: black, all U2_ROWS of the grid, no map.
     * The overworld compositor is restored by the u2_overworld_draw() at the end of this handler.
     */
    if (ctx->frame) {
        u2_frame_clear(ctx->frame, U2_CGA_BLACK);
        u2_font_draw_vram_rows(ctx->frame, &ctx->vram, 0, U2_ROWS - 1);
        seam_present_frame(ctx);
    }

    u2_key dismiss;
    /* seam_key_wait returns 1 only when it filled `dismiss`; a quit request (window
     * closed, Ctrl-C) and a spent script both answer 0 now, so neither can be read as
     * "the player pressed a key". An earlier revision tested `== 0` here and then read
     * `dismiss` uninitialised, so its Escape branch never fired. That branch is gone
     * now; the wait itself is the byte, and which key arrived is deliberately not
     * looked at. */
    const int got_key = seam_key_wait(ctx, &dismiss);

    /* The original BLOCKS here: no timeout, no path out without a keypress. So the page is on
     * screen until a key actually arrives, and only then does it come down. seam_key_wait
     * returning 0 means "scripted input exhausted", which has no counterpart in the original --
     * tearing the page down on that condition made it vanish in the same command that drew it,
     * and left the CP5 gates reading a screen that had already been restored.
     *
     * Restoring the snapshot is the whole teardown. The cursor goes back with it, so the CMD
     * prompt resumes at the column it was interrupted at.
     */
    if (got_key) {
        ctx->vram = saved;
        ctx->vram.writes = writes;
        ctx->state.cursor_row = cur_row;
        ctx->state.cursor_col = cur_col;
        ctx->state.right_margin = margin_r;
        ctx->state.left_margin = margin_l;
    }
    u2_overworld_draw(ctx, ow);
    return U2_CMD_DONE;
}

/* asm/3389 cmd_E: enter the site on this cell. The body is u2_overworld_enter() in
   overworld.c, because entering needs the sector store and the tile-to-digit table and
   those are that file's; what belongs here is that E is a letter the table answers, so
   u2_cmd_implemented() tells the truth about it. No cmd_done(): every path through the
   handler prints its own CR and prompt, including the refusals. */
static u2_cmd_result cmd_E(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    u2_overworld_enter(ctx, ow);
    return U2_CMD_DONE;
}

/* asm/2B8F_cmd_A: print "ATTACK--" with no CR and fall into the gate chain, whose last step
   reads a direction. The direction arrives turns later, so the handler leaves the pending
   command on the overworld and returns U2_CMD_AWAIT: no turn passes and no sweep runs until
   the aim is answered, which is what the loop's direction arms then do. If the turn expires
   first, the waiting PASS lands on this same line -- `CMD: ATTACK--PASS`, captured live
   (docs/spice86-live-findings.md section 18), and that is why the word is printed without a
   CR rather than through echo_cmd. */
static u2_cmd_result cmd_A(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    /* asm/2B9B is the site-kind gate at the head of this chain, and it gates
     * nothing: both of its destinations reach asm/2BA5. What a site changes is
     * that the facing vector already names the cell, so there is no direction to
     * await and the command completes on the letter. */
    if (u2_site_is_dungeon(ctx))
        return u2_dungeon_attack(ctx, ow);
    seam_str_out(ctx, U2_S_ATTACK);
    ow->pending_cmd = 'A';
    return U2_CMD_AWAIT;
}

/* asm/4050 cmd_T: TRANSACT. Print "TRANSACT-", take a direction from the same aim gate as
 * cmd_A and cmd_U, and let asm/4050's ray decide whether anything will deal with you. The
 * whole of what happens next -- the two-cell ray, the quadrant selector, the shops -- lives
 * in game/commerce.c, because it is a subsystem and not a command.
 *
 * Same AWAIT shape as cmd_A, for the same reason: the original consumes the direction inside
 * the handler chain (asm/2362 polls up to 65536 times and prints PASS when it gives up,
 * which is the `CMD: TRANSACT-PASS` line seen in captures), so the letter alone is not a
 * completed turn. */
static u2_cmd_result cmd_T(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    seam_str_out(ctx, U2_S_TRANSACT);
    ow->pending_cmd = 'T';
    return U2_CMD_AWAIT;
}

/* asm/3C2E cmd_O: OFFER GOLD. Same AWAIT shape as cmd_T and cmd_S — asm/3C2E calls the
 * asm/2362 aim gate, so the letter alone is not a completed turn and the direction word
 * lands on the OFFER GOLD DIRECT- line. What happens after the direction is known, including
 * the amount prompt, lives in game/commerce.c because it is a subsystem: it is the only
 * place the game grants the QUICK SWORD and the RING, and the only stat-raising path in the
 * binary. Note that no gold is ever deducted — asm/3C2E contains no write to the gold pair —
 * so an offer is free, and the grind is a loop rather than a purchase. */
static u2_cmd_result cmd_O(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    seam_str_out(ctx, U2_S_OFFER_GOLD_DIRECT);
    ow->pending_cmd = 'O';
    return U2_CMD_AWAIT;
}

/* asm/3F25 cmd_S: STEAL. Same AWAIT shape as cmd_T for the same reason — the direction is
 * consumed inside the handler chain, by asm/2362, whose call sits at asm/3F36 as a bare
 * forced-keep `e8` byte and so never appeared in any `# calls:` list.
 *
 * The word is the trap here. This is not pickpocketing and it is not combat: there is no
 * creature scan anywhere in the chain. The ray lands on the merchant marker two cells away —
 * the same marker TRANSACT deals with — two rolls decide luck, and then the quadrant the
 * PLAYER is standing in decides which category of goods is added to the inventory. A village
 * north-east gives food, a town north-east gives armour, a town north-west gives weapons,
 * and everything else is NO LUCK!. The shop's stock is not touched because there is no stock;
 * the owned-count arrays simply go up.
 *
 * It is also not asm/0FCD_combat_thief_steal, which is a THIEF monster emptying one of the
 * player's slots in a fight. This file used to say the effect was "modelled in combat.c for
 * CP5 ticket C6 and merely unwired", which conflated the two on the strength of the shared
 * English word; the decode and the CP6 gate now keep them apart.
 *
 * A THIEF (class 3) is not required, it skips one of the two rolls. Owner recollection said
 * "probably needs a thief" and "can steal from shops" — the second is exactly right, the
 * first is half right. */
static u2_cmd_result cmd_S(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    seam_str_out(ctx, U2_S_STEAL_DIRECT);
    ow->pending_cmd = 'S';
    return U2_CMD_AWAIT;
}

/* asm/35C1 cmd_F: FIRE — the frigate's cannon.
 *
 *   print "FIRE"
 *   [DS:13] != 0x24  ->  " WHAT?" and done. That is the entire off-ship command.
 *   [DS:13] == 0x24  ->  " DIRECT-", take a direction from the aim gate, and fire.
 *
 * The mode byte is what makes this a vehicle weapon and not an attack: 0x24 is the ship stamp
 * asm/2FAD's BOARD dispatch assigns, so FIRE exists only aboard the frigate. cmd_A is the
 * attack. This file used to file F as "the combat/attack arm" from the bare word.
 *
 * Same AWAIT shape as cmd_T and cmd_S: asm/35EA calls the asm/2362 aim gate, so the direction
 * is consumed inside the handler chain and the letter alone is not a turn. */
static u2_cmd_result cmd_F(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    seam_str_out(ctx, U2_S_FIRE);

    if (ctx->state.player_stamp != 0x24u) {
        /* asm/35D0. The literal is " WHAT?" with a leading space, so the line reads
         * `CMD: FIRE WHAT?` — which is also why it is its own extractor entry rather than
         * sharing cmd_E's or cmd_B's identical text: same bytes, different site, and the
         * citation should point here. */
        seam_str_out(ctx, U2_S_FIRE_WHAT);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    seam_str_out(ctx, U2_S_FIRE_DIRECT);
    ow->pending_cmd = 'F';
    return U2_CMD_AWAIT;
}

/* asm/3E50 cmd_R: READY WEAPON — cmd_W's shape again, one array over.
 *
 *   print the menu (nine entries, 1-DA .. 9-QU)
 *   read a digit into DS:1F and echo it
 *   print the weapon NAME from the pool (`adc $0x13,%al` + asm/508C)
 *   ownership: [DS:76 + digit] == 0 and digit != 0  ->  " NOT OWNED!"
 *   agility gate: digit*8 >= [DS:4C]  ->  " <-THOU ART NOT / AGILE ENOUGH TO WIELD!"
 *   success: " READY." and DS:61 = digit
 *
 * Two things worth saying out loud because both are places a similar-looking handler goes wrong.
 * The gate is **agility** (DS:4C), not strength — cmd_W uses DS:4B and the two are otherwise
 * identical, so copying the armour handler is the trap. And the comparison is against the raw
 * packed byte, so agility 40 displayed is 0x40 = 64 stored, and digit 9 (72) is out of reach for
 * a maxed character while digit 8 is not.
 *
 * Every branch is on a frame, not just in the listing: `AXE READY.` with DS:61 = 03, `MACE NOT
 * OWNED!` with DS:77 poked to 0, and the agility refusal at digit 9, all in
 * captures/spice86/cmd-nr-live. Digit 0 is HANDS and the original lets it through the ownership
 * test (`or %al,%al ; je` past it) — recorded, not tidied away. */
static u2_cmd_result cmd_R(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)ow; (void)letter;
    seam_str_out(ctx, U2_S_READY_WEAPON_MENU);

    int digit = -1;
    while (digit < 0) {
        u2_key k;
        /* _shown: the menu above this line is invisible until something presents it, and
         * the loop that presents sits above this handler. Owner-reported 2026-11-02 as a
         * crash; it was the menu arriving only after the digit was typed. */
        if (seam_key_wait_shown(ctx, &k) == 0)
            break;                       /* input exhausted: no original counterpart */
        if (k.ch >= '0' && k.ch <= '9') {
            digit = k.ch - '0';
            seam_char_print(ctx, (char)('0' + digit));
        }
    }
    if (digit < 0) { cmd_done(ctx); return U2_CMD_DONE; }

    seam_str_out(ctx, WEAPON_NAME(digit));

    const int owned = (digit == 0) ||
                      (digit < (int)sizeof ctx->state.weapons_owned &&
                       ctx->state.weapons_owned[digit] != 0);
    if (!owned) {
        seam_str_out(ctx, U2_S_NOT_OWNED);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    if ((unsigned)(digit * 8) >= (unsigned)ctx->state.stat_agi) {
        /* The original's literal is one str_out with the newline glyph mid-string
           (" <-THOU ART NOT \x8dAGILE ENOUGH TO WIELD!"). The engine emits the newline itself
           rather than passing a raw 0x8D through str_out, same convention as cmd_V's helm line. */
        seam_str_out(ctx, U2_S_READY_NOT_AGILE_A);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_READY_NOT_AGILE_B);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    seam_str_out(ctx, U2_S_READY_OK);
    ctx->state.readied_weapon = (uint8_t)digit;      /* DS:61 */
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/3BCE cmd_N: NEGATE TIME. The last of the 26 letters that had never been seen running in
 * any harvest, and it turned out to be a fall-through rather than a routine of its own:
 *
 *   print "NEGATE TIME"
 *   if [DS:E3] == 0  ->  " HOW?" newline "YOU'RE NOT EINSTEIN", done
 *   else                  ->  fall into asm/3C04, the coin rub: spend one, print
 *                             "YOU RUB A COIN...", and store 0x14 into DS:26A
 *
 * [DS:E3] is items_held[STRANGE COIN] — see ITEM_STRANGE_COIN — so the cost of stopping time is
 * one strange coin, and 0x14 is the creature-move freeze counter, which asm/2199 refuses on and
 * which the engine already honours in ai.c. The counter then ticks down one per command like the
 * paralysis bytes (asm/0C54/asm/0C5B), which is why status_tick had to learn about it.
 *
 * Observed live 2026-10-29 (captures/spice86/cmd-nr-live): with one strange coin, `N` printed
 * `CMD: NEGATE TIME` then `YOU RUB A COIN...` and DS:26A went 00 -> 0x14 (read 0x13 one command
 * later, i.e. it had been set and had ticked); with the byte poked to 0 it printed
 * `NEGATE TIME HOW?` / `YOU'RE NOT EINSTEIN` and changed nothing.
 *
 * The Apple II build has the same pair as its own routine ($7DC6: print, then `STA $D9` with 20),
 * which asm/3C04's header records — so this is a mechanic the game shipped, not a DOS artifact. */
static u2_cmd_result cmd_N(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)ow; (void)letter;
    seam_str_out(ctx, U2_S_NEGATE_TIME);

    if (ctx->state.items_held[ITEM_STRANGE_COIN] == 0) {
        seam_str_out(ctx, U2_S_NEGATE_HOW);
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_NEGATE_NO_CHARGES);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    /* asm/3C04: `stc ; cmc ; sbb $0x1,%al ; das ; cmc` — the same one-step packed-decimal
     * subtract asm/45B8 uses for X-IT, so the coin count borrows in decimal. */
    ctx->state.items_held[ITEM_STRANGE_COIN] =
        bcd_dec1(ctx->state.items_held[ITEM_STRANGE_COIN]);
    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_COIN_RUB);
    ctx->state.counter_26A = 0x14;
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/42F1 cmd_U: print "UNLOCK DIRECTION-" and fall into the same kind of aim gate. The
   prompt is answered by a direction key, and only a sideways one can ever work -- see
   u2_unlock_rule and asm/4309_unlock_aim_gate. Same AWAIT shape as cmd_A, and for the same
   reason: the original consumed the next key inside the handler chain, so the engine must
   not treat the letter itself as a completed turn. */
static u2_cmd_result cmd_U(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    seam_str_out(ctx, U2_S_UNLOCK_DIRECTION);
    ow->pending_cmd = 'U';
    return U2_CMD_AWAIT;
}

/* asm/3B92_cmd_M: ready a spell by number. Print "MAGIC SPELL READY #", read ONE decimal
   digit, store it at DS:63 as the readied slot, print "SPELL READY => " and the spell's
   NAME, then cmd_done. The digit reader is asm/2028, which blocks, rejects anything outside
   '0'..'9' by simply reading again, and echoes the digit it accepted.

   The NAME is the part that used to be wrong here. asm/508C is the indexed name pool printer
   and the `adc $0x24,%al` in front of it is the SPELLS group base, not an ASCII digit bias:
   M echoes a WORD (`SPELL READY => UP LADDER`), which is the same constant cmd_C uses to
   print the spell it is about to try. See asm/3B92_cmd_M.asm's correction of 2026-10-25.

   Two things this deliberately does NOT copy, both from that reader's own DOUBT list in
   asm/2028_read_decimal_digit.asm, and both recorded rather than settled:
     * whether a rejected key is consumed or left in the typeahead ring. The engine consumes
       it. If the original leaves it, a stray key here would be seen by whatever runs next.
     * the 0x8D the reader echoes after the digit. 0x8D is this binary's newline glyph, and
       emitting a line break nobody can explain is a worse guess than not emitting one.
   Neither is testable from the engine side; both need the reader stepped through live. */
static u2_cmd_result cmd_M(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)ow; (void)letter;
    seam_str_out(ctx, U2_S_MAGIC_SPELL_READY);

    int digit = -1;
    while (digit < 0) {
        u2_key k;
        /* seam_key_wait returns 1 when it got a key and 0 when it did not — a spent
         * script and a quit request both answer 0, so neither can be mistaken for
         * input. Getting this backwards is invisible in the dispatch gate, which is
         * why the gate for M reads the echoed digit and not just the prompt.
         *
         * _shown for the same reason as cmd_R: the prompt is printed above and nothing
         * between there and here puts it on the screen. */
        if (seam_key_wait_shown(ctx, &k) == 0)
            break;                       /* input exhausted: no original counterpart */
        if (k.ch >= '0' && k.ch <= '9') {
            digit = k.ch - '0';
            seam_char_print(ctx, (char)('0' + digit));
        }
    }
    if (digit < 0) {
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    ctx->state.spell_pick = (uint8_t)digit;
    seam_str_out(ctx, U2_S_SPELL_READY_TO);
    seam_str_out(ctx, SPELL_NAME(digit));
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/43F3_cmd_W: WEAR ARMOUR.
 *
 * The menu is ONE static string printed before anything is read, so the original always shows all
 * six types and only tells you afterwards whether you own one. Verified live 2026-10-26 with
 * DS:97..9C read as all zero — owning nothing — and the full list still on screen
 * (captures/spice86/wonly/). That is why there is no filtered-list logic in fidelity mode: the
 * filtering is in the post-choice refusal branches, not in the menu.
 *
 * Order, straight off the bytes:
 *   print menu
 *   read a digit into DS:1F; 7 or above is forced to 0 (0 = SKIN, going bare)
 *   print the armour NAME from the pool (`adc $0x1d,%al` + the printer at L_00528c), so a
 *     successful line reads "CLOTH READY." — observed on screen, not inferred
 *   ownership: DS:0x96[digit] == 0 and digit != 0  ->  " NOT OWNED!"
 *   strength gate: digit*8 < DS:4B, the PACKED byte, else the refusal
 *   success: " READY." and DS:62 = digit
 * All three exits tail-jump to L_000c33, the status tick.
 *
 * The strength gate is the BCD trap again: agility/strength 40 is stored 0x40 = 64, so digit*8
 * tops out at 48 and a starting character passes every armour. Reading the decimal 40 instead
 * would refuse things the original allows.
 *
 * ENHANCED MODE, owner's call 2026-10-26: list only armour actually owned, keeping each type's
 * original number so the digit still indexes the same table. The point is to stop the player
 * guessing; fidelity mode keeps the guessing because the original does it. Composed from the
 * original's own header and "WHICH? " pieces plus the pool names, not hand-copied text.
 */
static u2_cmd_result cmd_W(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)ow; (void)letter;

    if (ctx->fidelity == U2_FIDELITY_ENHANCED) {
        seam_str_out(ctx, U2_S_WEAR_HEADER);
        seam_char_print(ctx, '\r');
        int shown = 0;
        for (int n = 1; n <= 6; n++) {
            if (ctx->state.armour_owned[n - 1] == 0) continue;
            char line[24];
            snprintf(line, sizeof line, "%d-%s, ", n, ARMOUR_NAME(n));
            seam_str_out(ctx, line);
            shown++;
        }
        if (shown == 0) {
            /* Nothing to wear. The original never asks this question, so it has no words for it;
             * reuse its own refusal rather than inventing a sentence nobody wrote. */
            seam_str_out(ctx, U2_S_NOT_OWNED);
            cmd_done(ctx);
            return U2_CMD_DONE;
        }
        seam_char_print(ctx, '\r');
        seam_str_out(ctx, U2_S_WHICH);
    } else {
        seam_str_out(ctx, U2_S_WEAR_ARMOUR_MENU);
    }

    int digit = -1;
    while (digit < 0) {
        u2_key k;
        /* _shown: same as cmd_R two handlers up. The owner reported W alongside R for
         * exactly this reason — the WEAR menu arrived after the digit. */
        if (seam_key_wait_shown(ctx, &k) == 0)
            break;                       /* input exhausted: no original counterpart */
        if (k.ch >= '0' && k.ch <= '9') {
            digit = k.ch - '0';
            seam_char_print(ctx, (char)('0' + digit));
        }
    }
    if (digit < 0) { cmd_done(ctx); return U2_CMD_DONE; }

    if (digit >= 7) digit = 0;                       /* asm/464B: cmp $0x7 ; jb, else zeroed */
    seam_str_out(ctx, ARMOUR_NAME(digit));           /* the pool echo */

    const int owned = (digit == 0) ||
                      (digit >= 1 && digit <= 6 && ctx->state.armour_owned[digit - 1] != 0);
    if (!owned) {
        seam_str_out(ctx, U2_S_NOT_OWNED);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    if ((unsigned)(digit * 8) >= (unsigned)ctx->state.stat_str) {
        seam_str_out(ctx, U2_S_NOT_STRONG_ENOUGH);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    seam_str_out(ctx, U2_S_ARMOUR_READY);
    ctx->state.worn_armour = (uint8_t)digit;         /* DS:62 */
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/44D4 cmd_X: X-IT, the dismount — the other half of the boarding rule the vehicle handoff
   could not find. BOARD consumes the tile and sets DS:13; X-IT writes 2*[DS:13] back through
   the same cell pointer and returns the player to foot, so the tile was never destroyed, only
   carried. The guard order is the byte order: on foot there is nothing to destroy (" WHAT?"),
   then the cell under the player decides — grass anywhere, or water if and only if it is the
   ship, since the ship may only be dropped afloat. Anything else is "-NOT HERE!".

   The restored stamp is 0x78 + 2*pclass, not the constant 0x78: every capture shows 0x78
   because every capture so far was a fighter. See asm/44D4_cmd_X.asm and G41.

   Not observed end to end: the three harvests that ran cmd_X pressed every letter on a foot
   character in a grass sector, so they exercised the " WHAT?" arm only. The dismount arms are
   byte-proven and unplayed; the "-NOT HERE!" arm is not even reachable from a foot start (it
   needs a non-ship vehicle standing on water, i.e. a plane flown over the sea, which is G43
   territory), so it is covered at unit level in tests/site_check.c. Gate: vehicle_dismount.py. */
static u2_cmd_result cmd_X(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    seam_str_out(ctx, U2_S_XIT);

    const uint8_t stamp = ctx->state.player_stamp;
    const uint8_t mode_a = (uint8_t)(u2_world_tile_at(&ow->world, ctx->state.sector_x, ctx->state.sector_y) >> 1);

    switch (u2_dismount_rule(stamp, mode_a)) {
    case U2_DISMOUNT_ON_FOOT:
        seam_str_out(ctx, U2_S_XIT_WHAT);
        break;
    case U2_DISMOUNT_NOT_HERE:
        seam_str_out(ctx, U2_S_XIT_NOT_HERE);
        break;
    default:
        /* A terrain write, same reasoning as BOARD: the tile has to survive the creature turn
         * that follows, or the vehicle vanishes the moment anything moves. */
        u2_world_terrain_put(&ow->world, ctx->state.sector_x, ctx->state.sector_y, u2_dismount_tile(stamp));
        ctx->state.player_stamp = u2_foot_stamp(ctx->state.pclass);
        break;
    }

    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* The helm's VIEW, drawn.
 *
 * Geometry measured 2026-11-11 in captures/spice86/vhelm-sector-map-live: the sector's
 * 64x64 cells at 4x2 logical pixels each from (17,16), so the picture is 256x128 and the
 * CMD window below it is left alone. The origin is not a guess — in sector 00 the topmost
 * land row of mapx00 is row 8 and the lit pixels begin at y=32; in sector 30 the topmost
 * land row is 2 and they begin at y=20. Same origin either way, and the left edge agrees
 * from each map's leftmost land column.
 *
 * Water is black and land is a dither whose density rises with the art index; mean
 * luminance in sector 30 was art 0 -> 9.6, art 1 -> 50, art 2 -> 47, art 3 -> 76,
 * art 4 -> 114 out of 255. Arts above 4 occur once or twice there, so there is no measured
 * table for them: the ladder below is fitted to what was measured and is a description of
 * the frames, not a model of the mechanism, because the per-art dither has not been traced
 * to anything in asm/. */
static int helm_cell_density(uint8_t map_byte)
{
    if (map_byte == 0)
        return 0;                                   /* water */
    const unsigned art = (unsigned)(map_byte >> 2);
    if (art <= 2) return 2;
    if (art == 3) return 3;
    return 4;
}

static void helm_map_draw(u2_ctx *ctx, u2_overworld *ow)
{
    enum { OX = 17, OY = 16, CW = 4, CH = 2 };
    /* A fixed spread over the 8 pixels of a cell, so density k reads as a dither rather
     * than as a solid corner. */
    static const uint8_t spread[CW * CH] = { 0, 5, 2, 7, 4, 1, 3, 6 };

    u2_frame_clear(&ow->frame, U2_CGA_BLACK);
    for (int cy = 0; cy < U2_SECTOR_EDGE; cy++) {
        for (int cx = 0; cx < U2_SECTOR_EDGE; cx++) {
            const int lit = helm_cell_density(u2_world_tile_at(&ow->world, cx, cy));
            for (int i = 0; i < lit; i++)
                u2_frame_put_logical(&ow->frame, OX + cx * CW + spread[i] % CW,
                                     OY + cy * CH + spread[i] / CW, U2_CGA_WHITE);
        }
    }
    /* The CMD window keeps drawing over the map, which is what the frames show: the view
     * replaces the play field and not the text band beneath it. */
    u2_font_draw_vram_rows(&ow->frame, &ctx->vram, U2_MSG_TOP, U2_ROWS - 1);
    seam_present_frame(ctx);
}

/* asm/437E_cmd_V: VIEW.
 *
 * DS:DB is items_held[5], and U2_T_ITEMS[5] is "HELM" — so the gate is literally "do you hold a
 * HELM", with no inference left in it. Second gate is DS:4A (the sector's second decimal digit)
 * below 4; at or above 4 it prints the same VIEW WHAT? refusal.
 *
 * Observed live 2026-10-26 (captures/spice86/vhelm/) and re-observed 2026-11-11
 * (captures/spice86/vhelm-sector-map-live, which is the one to read): with a helm held and
 * DS:4A = 00, pressing V replaced the play field with a map and DS:DB read one lower
 * afterwards. The 2026-10-26 note called the picture a WORLD MAP with continents. It is
 * not: it is the sector you are standing in, and the oval that read as continents in
 * sector 00 is mapx00's own landmass. The charge is one per press, measured at 07 -> 06
 * -> 05, and at zero the handler prints VIEW WHAT? rather than going negative.
 *
 * The map is now drawn. See helm_map_draw for the geometry and the density ladder, both
 * measured, and engine/tests/helm_map_check for the gate — which is a pixel assertion on
 * purpose, because the message was never the missing part and a message-level test stayed
 * green for months over an empty screen.
 */
static u2_cmd_result cmd_V(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;

    if (ctx->state.items_held[5] == 0 || ctx->state.sector_second >= 4) {
        seam_str_out(ctx, U2_S_VIEW_WHAT);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    seam_str_out(ctx, U2_S_VIEW_HELM_A);
    seam_char_print(ctx, '\r');            /* the original's 0x8d newline glyph */
    seam_str_out(ctx, U2_S_VIEW_HELM_B);
    ctx->state.items_held[5] = bcd_dec1(ctx->state.items_held[5]);

    /* The view is modal and it costs one helm, both measured rather than inferred:
     * DS:DB poked to 07 read back 06 after one press and 05 after two, and at 00 the
     * handler prints VIEW WHAT? instead of going negative. asm/437E polls L_00514d
     * until AH == 0xFF, i.e. it waits for a key, then clears the two staging tables at
     * DS:272 and DS:372 and tail-jumps to the tick.
     *
     * Polled rather than blocked, and presented by hand rather than through
     * seam_key_wait: the registered presenter redraws the WORLD, which would paint over
     * the map before the player ever saw it. A spent script makes poll return -1, so this
     * cannot hang a headless run. */
    helm_map_draw(ctx, ow);
    for (;;) {
        u2_key k;
        const int r = seam_key_poll(ctx, &k);
        if (r != 0)
            break;
        if (ctx->clock && ctx->clock->sleep_ms)
            ctx->clock->sleep_ms(ctx, 32);
    }

    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* ---- the vehicles -------------------------------------------------------- */

/* asm/2F6F cmd_B: print "BOARD", then asm/2FAD dispatches on [DS:14], which is the map
 * byte under the player shifted right once — the same quantity the passability ladder
 * tests, and NOT the art index.
 *
 * The dispatch, in the original's order, with what each arm actually does:
 *
 *   0x22 horse   " HORSE", mode := 0x22, the cell becomes grass. No gate.
 *   0x24 ship    gated on DS:E2. With it: " FRIGATE", mode := 0x24, the cell becomes
 *                water. Without it: " SHIP" / "THE CREW OF THIS SHIP" / "WILL NOT LET
 *                YOU BOARD!" and nothing changes. So the thing you can board is the
 *                FRIGATE, and asm/3038 is not a fifth vehicle — it is the ship arm's
 *                success path. § 6 of the handoff had it as untested; the gate byte names it.
 *   0x26 plane   gated on DS:DF (SKULL KEY): " PLANE" either way, and "STRANGE YOU CAN'T
 *                GET IN!" without it.
 *   0x28 rocket  " ROCKET" first, then the DS:DD (ANKH) gate: "A METALIC VOICE
 *                COMMANDS:" / "YOU MUST HAVE AN ANKH!" without it.
 *   anything else  the tail at asm/2FB8, which prints " WHAT?" — so the whole line reads
 *                "BOARD WHAT?". That is the original's own refusal and nothing is invented here.
 *
 * The gate bytes are item slots, not flags: DS:DD/DF/E1/E2/E5 are items_held[7/9/11/12/15],
 * and the slot names are already recorded in docs/spice86-live-findings.md § 11 — ANKH, SKULL
 * KEY, BRASS BUTTON, BLUE TASSLE, TRI-LITHIUM. Each refusal message names its own item, which
 * corroborates that pairing from the message side; see the block comment in overworld.h.
 *
 * Boarding does not move the player, and it consumes the tile: each arm writes a ground byte
 * through the cell pointer DS:06 (grass for the three land vehicles, water for the ship).
 * The live sample is captures/spice86/ship-port-boniface, where [DS:14] went 0x24 -> 0x00 on
 * the BOARD with the position unchanged. Whether the consumed tile comes back is answered by
 * asm/44D4 (X-IT), which writes 2*[DS:13] back into the cell and returns the player to foot —
 * that is the dismount the handoff could not find, and it is a separate letter.
 *
 * NOT modelled, because the pointer it writes through is not modelled: the original writes to
 * [[DS:6]], the cached cell pointer asm/50B3 maintains, and which cell that holds at the moment
 * BOARD runs is inferred (the player's own) rather than traced. The live [DS:14] change is what
 * makes the inference worth shipping; if a capture ever shows the write landing on a different
 * cell, this is the line that changes.
 */
static void board_vehicle(u2_ctx *ctx, u2_overworld *ow, uint8_t mode, uint8_t leaves)
{
    ctx->state.player_stamp = mode;
    /* A terrain write, not a stamp: the tile is gone from the world, and the creature
     * restamp rebuilds the live grid from the pristine copy on every redraw. See
     * u2_world_terrain_put's note in core/world.h — without this the boarded tile is
     * back the next time anything moves, which the live capture rules out.
     */
    u2_world_terrain_put(&ow->world, ctx->state.sector_x, ctx->state.sector_y, leaves);
}

static u2_cmd_result cmd_B(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;

    seam_str_out(ctx, U2_S_BOARD);

    switch (u2_world_tile_at(&ow->world, ctx->state.sector_x, ctx->state.sector_y) >> 1) {
    case U2_MODE_HORSE:
        seam_str_out(ctx, U2_S_BOARD_HORSE);
        board_vehicle(ctx, ow, U2_MODE_HORSE, U2_TILE_AFTER_BOARD_LAND);
        break;

    case U2_MODE_SHIP:
        if (ctx->state.items_held[U2_ITEM_BLUE_TASSLE] == 0) {
            /* One str_out call in the original, with the newline glyph mid-string; the
             * engine emits the breaks itself, the same convention cmd_V's helm line uses. */
            seam_char_print(ctx, '\r');
            seam_str_out(ctx, U2_S_BOARD_SHIP_A);
            seam_char_print(ctx, '\r');
            seam_str_out(ctx, U2_S_BOARD_SHIP_B);
            seam_char_print(ctx, '\r');
            seam_str_out(ctx, U2_S_BOARD_SHIP_C);
        } else {
            seam_str_out(ctx, U2_S_BOARD_FRIGATE);
            board_vehicle(ctx, ow, U2_MODE_SHIP, U2_TILE_AFTER_BOARD_WATER);
        }
        break;

    case U2_MODE_PLANE:
        if (ctx->state.items_held[U2_ITEM_SKULL_KEY] == 0) {
            seam_char_print(ctx, '\r');
            seam_str_out(ctx, U2_S_BOARD_PLANE_REF_A);
            seam_char_print(ctx, '\r');
            seam_str_out(ctx, U2_S_BOARD_PLANE_REF_B);
        } else {
            seam_str_out(ctx, U2_S_BOARD_PLANE);
            board_vehicle(ctx, ow, U2_MODE_PLANE, U2_TILE_AFTER_BOARD_LAND);
        }
        break;

    case U2_MODE_ROCKET:
        seam_str_out(ctx, U2_S_BOARD_ROCKET);
        if (ctx->state.items_held[U2_ITEM_ANKH] == 0) {
            seam_char_print(ctx, '\r');
            seam_str_out(ctx, U2_S_BOARD_ROCKET_REF_A);
            seam_char_print(ctx, '\r');
            seam_str_out(ctx, U2_S_BOARD_ROCKET_REF_B);
        } else {
            board_vehicle(ctx, ow, U2_MODE_ROCKET, U2_TILE_AFTER_BOARD_LAND);
        }
        break;

    default:
        seam_str_out(ctx, U2_S_BOARD_WHAT);
        break;
    }

    cmd_done(ctx);
    return U2_CMD_DONE;
}


/* asm/38B0 cmd_L: LAUNCH. The handler dispatches on DS:13 alone — plane at 0x26, rocket at
 * 0x28, and "LAUNCH WHAT?" for everything else, including the horse and the ship, which have
 * no launch arm at all. */
static u2_cmd_result cmd_L(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;

    if (ctx->state.player_stamp == U2_MODE_PLANE)
        return u2_flight_launch_plane(ctx, ow);
    if (ctx->state.player_stamp == U2_MODE_ROCKET)
        return u2_flight_launch_rocket(ctx, ow);

    seam_str_out(ctx, U2_S_LAUNCH_WHAT);
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/4542 cmd_Y, together with the input loop it falls into at asm/4551..asm/4567.
 *
 * That fall-through was recorded as unresolved for as long as the loop lay cut into seven
 * bulk-wave fragments, each headed "behavior not yet reviewed". Read as one address-ordered
 * stream it is a complete routine, and it is the freeform line input the owner remembered from
 * the manual: type anything, it echoes, Enter ends it. The bytes agree with the recollection,
 * and they say something stronger than "no gameplay effect" — nothing in the whole range
 * writes memory, so the yell is display-only and the text evaporates.
 *
 *   4551  L_004751: call key_poll         ; asm/4F4D, the seam that arrow-maps and upper-cases
 *   4554          cmp $0xff,%ah           ; AH=0xFF is the seam's "a key was consumed"
 *                 jne L_004751            ; nothing typed: spin, no turn advances
 *   4559          cmp $0xd,%al            ; Enter
 *                 je  L_004762
 *   455D          call char_print_wrap    ; echo the character
 *   4560          jmp L_004751
 *   4562  L_004762: call char_print_wrap  ; AL still holds 0x0D, so the Enter prints too
 *   4565          jmp L_000c33            ; cmd_done
 *
 * Two mappings, both stated rather than assumed. `seam_key_wait` stands for the spin loop,
 * because a spin on a key seam is a blocking read; it returns 0 only when a scripted input has
 * run dry, a condition the original cannot reach, and that exit is shared with cmd_M. And the
 * seam's upper-casing is NOT applied here: it belongs to asm/4F4D, which the engine does not
 * model engine-wide, and putting it in one handler would make a seam behaviour look like a
 * command's own.
 *
 * This is also the explanation for a capture we had misread. The 2026-10-25 letter run found
 * that after `YELL WHAT?` later letters accumulated on the CMD line and never dispatched, and
 * logged it as the command not returning to the prompt. It does return — on Enter. Those
 * letters were the yell being echoed.
 */
static u2_cmd_result cmd_Y(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)ow; (void)letter;
    seam_str_out(ctx, U2_S_YELL_WHAT);
    seam_char_print(ctx, '\r');          /* the 0x8d newline glyph in its db line */

    for (;;) {
        u2_key k;
        /* Show YELL WHAT? before waiting for the yell. Without this the prompt is written to
         * the vram and never presented, because the loop that presents sits above this
         * handler, so the player stares at an unchanged screen while the game waits for a
         * word. Now the shared seam helper; see seam.h for why the helper is opt-in per site
         * and cannot simply be folded into seam_key_wait. */
        if (seam_key_wait_shown(ctx, &k) == 0)
            break;                        /* input exhausted: no original counterpart */
        if (k.ch == '\r') {
            seam_char_print(ctx, '\r');   /* asm/4562 prints the Enter itself */
            break;
        }
        seam_char_print(ctx, (char)k.ch); /* asm/455D: echo one character */
    }
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* ---- the three site handlers: I (IGNITE TORCH), D (DESCEND), K (KLIMB) ------ */

/* asm/53B5 renders one packed-decimal byte as two ASCII digits, high nibble first, and
 * asm/3823 hands it `add $0x0,%al ; daa` rather than a stored BCD byte -- so a binary floor
 * is pushed through DAA on the way to the screen. DAA with AF and CF clear adds 6 when the
 * low nibble is over 9 and 0x60 when the result is over 0x99, which is why a floor past 99
 * prints a wrapped pair instead of a third digit. Emulated rather than formatted with
 * %02u so that quirk survives instead of being quietly smoothed over. */
static void daa_bcd2(uint8_t v, char *out)
{
    unsigned a = v;
    if ((a & 0x0Fu) > 9u) a += 6u;
    if (a > 0x99u) a += 0x60u;
    a &= 0xFFu;
    out[0] = (char)('0' + (a >> 4));
    out[1] = (char)('0' + (a & 0x0Fu));
    out[2] = '\0';
}

/* asm/363B cmd_G: GET. The handler is one unit read across its own fragments.
 *
 * Inside a site (the `cmp $4 ; jb` gate on [DS:4A]) the whole command is: the cell
 * UNDER the player must be 0x40, which is the same bit the renderer draws as a
 * chest; print " CHEST!", clear the cell, and roll one random byte against 0x40
 * for gold or TRI-LITHIUM. That arm is u2_dungeon_get and it is byte-read.
 *
 * Outside a site the arm reads the cell under the player against 0x2a for ARMOUR and 0x2c
 * for WEAPON, clears the cell, and picks WHICH item with a masked roll: `and $0x3` for
 * armour and `and $0x7` for weapons, with zero meaning EMPTY!. Both arrays are indexed the
 * same way the shoppes index them -- DS:96+i armour, DS:76+i weapons -- so the roll IS the
 * pool index.
 *
 * That makes the weapon pool reachable from a chest exactly DAGGER..LIGHT SWORD (indices
 * 1..7), with PHASER (8) and QUICK SWORD (9) excluded, which is what the third-party guide
 * claims independently: "some weapons can be found randomly in chests, up to the light
 * sword (n.7)". Index 7 of our pool is literally LIGHT SWORD. Armour is indices 1..3 =
 * CLOTH, LEATHER, CHAIN, so PLATE, REFLECT and POWER are never chest loot.
 *
 * Reachability, measured rather than assumed: 0x2c occurs in the shipped maps only at seven
 * cells -- mapx31 (4,23), (4,60), (4,61); mapx32 (26,43), (26,44); mapx41 (16,35), (25,41)
 * -- each an isolated point surrounded by land or water, which is what a deliberate marker
 * looks like. **0x2a occurs in no mapx file at all**, so the armour arm is unreachable in
 * this pack. original/updated/GCHANGES.TXT explains how that can happen: the Ultima I-VI CD
 * dumped three disks into one directory and "cities, dungeons, and worlds on the Galactic
 * disk with the same name as a location on Earth are not available", so a tile that only
 * ever appeared on the Galactic disk is simply gone from what we hold. The arm is
 * implemented because the bytes say it exists, and its unreachability is recorded here
 * rather than quietly dropped. */
static u2_cmd_result cmd_G(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    if (u2_site_is_dungeon(ctx))
        return u2_dungeon_get(ctx, ow);

    seam_str_out(ctx, U2_S_GET);

    /* asm/363B compares 0x2a and 0x2c, and those are GATE VALUES (`map_byte >> 1`), not map
     * bytes and not art indices: the tiles that actually carry them in the shipped data are
     * 0x54 and 0x58, and 0x54>>1 == 0x2A, 0x58>>1 == 0x2C. Their ART indices are 0x15 and
     * 0x16, because art is `>> 2`. The two scalings are different quantities with different
     * names -- `>> 1` is the gate value asm/2FAD dispatches the vehicle modes on, `>> 2` is the
     * art index into the 64-entry tile table (G1, docs/art-index.md) -- so do NOT read this as
     * the same shift site_for_tile uses: that switch is `tile >> 2`. Confirmed live 2026-11-09
     * in the VAULT of Lord British's castle, sector 23: standing
     * on the drawn shields and pressing G gives GET ARMOUR, on the drawn swords GET WEAPON,
     * each sometimes followed by EMPTY!, and a non-empty result lands in the inventory. */
    const uint8_t under = (uint8_t)(u2_world_tile_at(&ow->world, ctx->state.sector_x, ctx->state.sector_y) >> 1);
    const int armour = (under == 0x2A);
    const int weapon = (under == 0x2C);
    if (!armour && !weapon) {
        seam_str_out(ctx, U2_S_GET_WHAT);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    seam_str_out(ctx, armour ? U2_S_GET_ARMOUR : U2_S_GET_WEAPON);

    /* asm/361C runs BEFORE the roll, so the marker is consumed even when the roll comes
     * back EMPTY -- an empty chest is still gone. The stamp is 0x08, the plain land tile,
     * read via asm/3611's own header rather than re-derived here. */
    u2_world_terrain_put(&ow->world, ctx->state.sector_x, ctx->state.sector_y, 0x08);

    const unsigned mask = armour ? 0x3u : 0x7u;
    const unsigned idx = (unsigned)(u2_rand_byte(ctx) & mask);
    if (idx == 0) {
        /* asm/388d: EMPTY! and nothing granted. Note the cell is already cleared above. */
        seam_str_out(ctx, U2_S_GET_EMPTY);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    uint8_t *pool = armour ? ctx->state.armour_owned : ctx->state.weapons_owned;
    pool[idx] = u2_bcd_inc_sat(pool[idx], U2_SATURATE(ctx));
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/3794 cmd_I: IGNITE TORCH.
 *
 * The handler is cut in half by the Phase 2 map -- asm/3794 is a single `e8` and the operand
 * and the text are in asm/3795 -- which is why nobody had read it as a unit. Read together it
 * is four instructions and a store:
 *
 *   print "IGNITE TORCH"
 *   if [DS:4A] < 4            -> cmd_done, no effect at all
 *   if [DS:64] == 0           -> print CR "NONE OWNED!", cmd_done
 *   else packed-decimal [DS:64] -= 1 ; [DS:26] = 0x96 ; cmd_done
 *
 * The `cmp $0x4` / `jae` pair is the same site-kind gate as DESCEND and KLIMB: a torch is
 * only worth lighting in a tower or a dungeon, and out here the command prints its own name
 * and does nothing. That emptiness is the real behaviour, not a stub -- three instructions
 * and one store between the gate and the return.
 *
 * [DS:26] is the byte state.h carries as moved_flag, and it is ALIASED: outside a site the
 * walk loop uses it as a moved-since-command flag, inside one asm/1151 uses it as the light
 * counter, printing IT'S DARK! at zero and TORCH BURNED OUT! when a step takes it there.
 * The LIGHT spell writes the same 0x96 into the same byte, so a torch and that spell are one
 * running count and casting LIGHT in a lit corridor spends a spell to refill a burning
 * torch. Confirmed live in captures/spice86/cmd-c-cast-tower.
 *
 * WHAT THE ENGINE DOES NOT YET CONSUME: nothing reads [DS:26] or [DS:64] as light today --
 * asm/1151_dungeon_darkness is inside the parked dungeon span (AGENTS.md section 3) and the
 * engine has no darkness state. So the spend and the refill are correct mutations of the
 * saved record with no visible effect yet. That is a rendering gap with a named owner, not a
 * claim that the torch does something. */
static u2_cmd_result cmd_I(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)ow; (void)letter;
    seam_str_out(ctx, U2_S_IGNITE_TORCH);

    if (ctx->state.sector_second < 4) {
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    if (ctx->state.torches == 0) {
        seam_char_print(ctx, '\r');      /* the 0x8d line marker on asm/37AE's db line */
        seam_str_out(ctx, U2_S_IGNITE_NONE_OWNED);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    /* asm/37C8: `stc ; cmc ; sbb $0x1,%al ; das ; cmc` -- one packed-decimal step, the same
     * idiom asm/3C04 spends a STRANGE COIN with. Torches are BCD, so 0x10 goes to 0x09. */
    ctx->state.torches = bcd_dec1(ctx->state.torches);
    ctx->state.moved_flag = 0x96;
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* The two floor blocks DESCEND and KLIMB share, selected by direction rather than duplicated.
 *
 * asm/3381 is `incb [DS:25]` then a jump straight to the print. asm/3823 is `decb [DS:25]`,
 * a `js` to the exit block, then the same print at asm/3A2A. So the decrement is the only
 * arm that can leave the site, and the increment has no bound check at all -- climbing above
 * a tower's top increments past it and prints the result.
 *
 * Which command takes which arm is the interesting part, and it is what makes the pair
 * consistent rather than contradictory: in a TOWER KLIMB increments and DESCEND decrements,
 * in a DUNGEON KLIMB decrements and DESCEND increments. [DS:25] is therefore not "height";
 * it is a floor index that counts up out of a tower and up into a dungeon, and the two
 * commands are mirrors. That is corroborated by the UP LADDER and DOWN LADDER spells in
 * asm/3139, which took the same byte 0 -> 1 -> 0 in the sector-24 tower, and by the live
 * DESCEND in captures/spice86/d12-ladder-ds25 leaving it at 0xFF -- a decrement below zero
 * with no guard, which is the `js` firing. */
static u2_cmd_result site_shift(u2_ctx *ctx, u2_overworld *ow, bool down)
{
    if (!down) {
        ctx->state.site_floor = (uint8_t)(ctx->state.site_floor + 1u);
    } else {
        ctx->state.site_floor = (uint8_t)(ctx->state.site_floor - 1u);
        /* `js`, not `jle`: the sign flag, so it is the high BIT that fires, and any value at
         * or above 0x80 would take the exit arm. Floors stay under 0x80 in practice. */
        if ((int8_t)ctx->state.site_floor < 0) {
            /* asm/3A44: back to the world. The original restores the entry cell, zeroes the
             * site kind and re-reads the sector, which is exactly what site_leave() does,
             * and then branches on [DS:6D] -- either staging a cell from [DS:6A]/[DS:6B] or
             * writing the 256-byte player record. That last branch means leaving a dungeon
             * saves the game, and it is modelled by the call below: the record only, no
             * sector, Earth only. Observed 2026-11-15 by breakpoint at runtime 0x389d, which
             * fired on the KLIMB and moved `player` and nothing else. */
            ctx->store_reason = "site exit";
            u2_overworld_site_leave(ctx, ow);
            (void)u2_overworld_autosave_record(ctx, ow);
            cmd_done(ctx);
            return U2_CMD_DONE;
        }
    }

    char d[3];
    daa_bcd2(ctx->state.site_floor, d);
    seam_char_print(ctx, '\r');          /* the 0x8d on asm/3823's db line */
    seam_str_out(ctx, U2_S_TO_LEVEL);
    seam_str_out(ctx, d);
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* True when the cell under the player carries the ladder feature the command wants.
 *
 * The gate is [DS:14], which state.h carries as mode_a and asm/5B71 fills from DS:2DF, and
 * which inside a site holds the cell the player is standing on -- the sector-24 tower's grid
 * uses 0x20 for its down-ladder cell and 0x10 for the up one. The engine keeps no DS:14
 * mirror (nothing writes state.mode_a), so this reads the live world buffer the same way
 * cmd_X reads it, which is also the only version a test can seed.
 *
 * The known gap: the engine loads a tower or dungeon mapxNN as one flat grid and does not
 * index it by [DS:25], so on any floor above the first this reads page 0's cell. asm/1451
 * computes word[DS:472] + floor*256 + y*16 + x, and that floor term is the missing piece. */
static bool site_cell_has(u2_ctx *ctx, u2_overworld *ow, uint8_t bit)
{
    /* asm/334D and asm/3812 both test [DS:14], and inside a site [DS:14] is the cell
     * under the player as asm/6000 wrote it — paged by [DS:25], which a flat 64-wide
     * read of the same buffer cannot see. Re-probe first, so the byte is current
     * rather than whatever the last render happened to leave there. */
    if (u2_site_is_dungeon(ctx)) {
        u2_dungeon_probe(ctx, ow);
        return (ctx->state.mode_a & bit) != 0;
    }
    return (u2_world_tile_at(&ow->world, ctx->state.sector_x, ctx->state.sector_y) & bit) != 0;
}

/* asm/37F1 cmd_K: KLIMB. Refuses outside a tower or dungeon, refuses unless the cell under
 * the player has the up-ladder feature, then moves one floor up. See site_shift. */
static u2_cmd_result cmd_K(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    seam_str_out(ctx, U2_S_KLIMB);

    const uint8_t kind = ctx->state.sector_second;
    if (kind != 4 && kind != 5) {
        /* asm/37F2 owns `-WH` and asm/380B owns `AT?`: one string across a fragment
         * boundary. Its bytes are identical to DESCEND's refusal, so the engine prints the
         * one the extractor can express as a single run; see the note in the manifest. */
        seam_str_out(ctx, U2_S_DESCEND_WHAT);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }
    if (!site_cell_has(ctx, ow, 0x10)) {
        seam_str_out(ctx, U2_S_DESCEND_WHAT);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }
    /* Tower climbs up (increment); dungeon climbs toward the surface (decrement). */
    return site_shift(ctx, ow, kind == 5);
}

/* asm/334D cmd_D: DESCEND. The mirror of KLIMB, and the one handler of the three that has
 * been seen running -- captures/spice86/d12-ladder-ds25, the first time cmd_D appeared in
 * ExecutedInstructions in 189 harvests. Its header's original "key byte" reading of [DS:4A]
 * is retired: that byte is the site kind, and the compares are 4 and 5. */
static u2_cmd_result cmd_D(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    seam_str_out(ctx, U2_S_DESCEND);

    const uint8_t kind = ctx->state.sector_second;
    if (kind != 4 && kind != 5) {
        seam_str_out(ctx, U2_S_DESCEND_WHAT);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }
    if (!site_cell_has(ctx, ow, 0x20)) {
        seam_str_out(ctx, U2_S_DESCEND_WHAT);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }
    /* Tower descends (decrement, and out of the world at the bottom); dungeon goes deeper
     * (increment). */
    return site_shift(ctx, ow, kind == 4);
}



/* asm/330C: the CAST target scan, shared by MAGIC MISSILE, PRAYER and KILL.
 *
 * Slots 31 down to 1. Slot 0 is never offered: the loop is `mov $0x1f,%bl` then
 * `dec %di ; jne`, so it stops when the index reaches zero instead of testing it, and a
 * creature parked in slot 0 is untargetable by CAST while remaining perfectly attackable
 * by cmd_A, which has its own scan.
 *
 * Three tests, in the order the bytes run them: a type of zero is an empty slot; the kind
 * byte must equal [DS:25]; and the position must be exactly the player's cell plus the
 * facing vector. ONE cell — no ray, no range, no line of sight. A creature three squares
 * away down a lit corridor is out of reach of every damaging spell in the game.
 *
 * The kind test is a same-floor test inside a site, not a species match: asm/147F writes
 * `slot >> 1` into that byte for a dungeon creature and asm/143C refuses its move unless it
 * equals [DS:25]. Same byte, same meaning, two routines that had never been read against
 * each other. See asm/330C_cast_target_finder.asm.
 */
static int cast_target(u2_ctx *ctx, const u2_overworld *ow)
{
    int dx, dy;
    u2_dungeon_facing(ctx, &dx, &dy);
    const uint8_t tx = (uint8_t)(ctx->state.sector_x + dx);
    const uint8_t ty = (uint8_t)(ctx->state.sector_y + dy);
    for (int i = U2_MON_SLOTS - 1; i >= 1; --i) {
        if (ctx->state.mon_type[i] == 0) continue;
        if (ctx->state.mon_kind[i] != ctx->state.site_floor) continue;
        if (ctx->state.mon_x[i] != tx) continue;
        if (ctx->state.mon_y[i] != ty) continue;
        return i;
    }
    return -1;
}

/* Every spell arm's refusal is the same three instructions at asm/3139 L_003432: print
 * `-FAILED!` and return. The charge is already gone by the time any arm can reach this, which
 * is the point — see the ordering note in cmd_C. */
static u2_cmd_result cast_failed(u2_ctx *ctx)
{
    seam_str_out(ctx, U2_S_CAST_FAILED);
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/1451, the way the ladder arms use it: the cell at the BEHIND position on the CANDIDATE
 * floor must be exactly zero. "Behind" is literal and facing-relative — [DS:23]/[DS:24] is
 * player minus facing, measured three ways in one session (facing north gave (5,6), west
 * (6,5), east (4,5)), and the third-party rule "if the square behind your destination is
 * open" turned out to be the bytes speaking literally rather than loosely.
 *
 * The engine keeps no [DS:23] mirror, so the pair is computed here from the same two inputs
 * the original does it from. */
static bool cast_ladder_open(u2_ctx *ctx, const u2_overworld *ow, int floor)
{
    int dx, dy;
    u2_dungeon_facing(ctx, &dx, &dy);
    return u2_world_dungeon_at(&ow->world, floor,
                               (ctx->state.sector_x - dx) & 0xff, (ctx->state.sector_y - dy) & 0xff) == 0;
}

/* asm/3A2A, the tail both ladder arms jump to when the move is allowed: CR, then
 * `TO LEVEL ` and the new floor in packed decimal. Same block cmd_D and cmd_K reach. */
static void cast_to_level(u2_ctx *ctx)
{
    char d[3];
    daa_bcd2(ctx->state.site_floor, d);
    seam_char_print(ctx, '\r');
    seam_str_out(ctx, U2_S_TO_LEVEL);
    seam_str_out(ctx, d);
}

/* One ladder spell. `decrement` names which way the byte moves, and which way it moves is
 * decided by the site kind, not by the spell: asm/3139's slot 2 takes the decrement path only
 * when [DS:4A] == 4 and slot 3 takes the increment path only when it does, so in a tower the
 * two are inverted relative to a dungeon.
 *
 * That is a second floor ORIENTATION on one byte, not two spells that do the opposite of
 * their names. Entering a dungeon puts the player on floor 0 and DOWN LADDER took [DS:25]
 * 0 -> 1 -> 2 with UP LADDER taking it back (captures/spice86/magic-casts-2026-11-15), so
 * [DS:25] is depth below the entrance and in a dungeon both spells do exactly what they say.
 * An earlier header read the same branch as "tower and dungeon are SWAPPED" and retracted it
 * the day the dungeon half was filmed. */
static u2_cmd_result cast_ladder(u2_ctx *ctx, u2_overworld *ow, bool decrement)
{
    int cand;
    if (decrement) {
        if (ctx->state.site_floor == 0) {
            /* UP LADDER at the entrance floor IS SURFACE. The arm opens `or %al,%al ;
             * je L_0034a2` and L_0034a2 is the slot-5 target, so at floor 0 "up" falls into
             * the way out. Filmed: bare `CAST-UP LADDER`, charge spent, player back on the
             * world map at the cell entered by. There is no -FAILED! on this path, which is
             * how it differs from every other refusal. */
            u2_overworld_site_leave(ctx, ow);
            cmd_done(ctx);
            return U2_CMD_DONE;
        }
        cand = ctx->state.site_floor - 1;
    } else {
        /* `cmp $0xf,%al ; jb` — the bound is 15 pages, and it is tested before the cell, so a
         * cast at the bottom of a 16-floor site fails on the bound rather than on the map. */
        if (ctx->state.site_floor >= 0xf) return cast_failed(ctx);
        cand = ctx->state.site_floor + 1;
    }
    if (!cast_ladder_open(ctx, ow, cand)) return cast_failed(ctx);
    ctx->state.site_floor = (uint8_t)cand;
    cast_to_level(ctx);
    cmd_done(ctx);
    return U2_CMD_DONE;
}

/* asm/310B + asm/3139: CAST, all of it.
 *
 * The handler itself is three instructions — print `CAST-`, jump into the flow at runtime
 * 0x3153 — and the flow is five gates and a nine-way dispatch. Everything below is transcribed
 * in the original's order, because the order is observable and two of the five gates are in
 * the place an implementer would not put them.
 *
 * The gates, in byte order:
 *   1. print the readied spell's NAME from the pool (pool[0x24 + DS:63]) — before any test,
 *      so the band always names the spell that is about to fail;
 *   2. DS:D7 + DS:D8 non-zero, else NEED WAND OR STAFF!. A SUM on the raw packed-decimal
 *      bytes, so a wand and a staff are interchangeable and neither is stronger;
 *   3. DS:63 == 0 casts nothing, silently, after printing CAST-NONE;
 *   4. DS:B6[pick] non-zero, else NO SPELL!; then SPEND it, in packed decimal;
 *   5. DS:4A >= 4, else -FAILED!.
 *
 * Gate 5 after gate 4 is the one to get wrong. The natural implementation checks the location
 * first and spends after, and the original does not: a wizard who casts in the middle of a
 * field loses the spell and gets `-FAILED!`. Observed twice, and it is the kind of thing no
 * test that only checks the success path will ever notice.
 *
 * There is no class test anywhere on this path. A Fighter holding a staff with a non-zero
 * count casts, and the shop gate (asm/184C, asm/19BA) is the entire class restriction. Adding
 * a class gate here would be a new bug, not a missing one.
 */
static u2_cmd_result cmd_C(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    (void)letter;
    u2_state *st = &ctx->state;

    seam_str_out(ctx, U2_S_CAST);
    const int pick = st->spell_pick;
    seam_str_out(ctx, SPELL_NAME(pick));

    if (st->items_held[U2_ITEM_WAND] + st->items_held[U2_ITEM_STAFF] == 0) {
        seam_str_out(ctx, U2_S_CAST_NEED_WAND);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }
    if (pick == 0) { cmd_done(ctx); return U2_CMD_DONE; }
    if (pick < 0 || pick >= (int)(sizeof st->spell_counts / sizeof st->spell_counts[0]))
        return cast_failed(ctx);
    if (st->spell_counts[pick] == 0) {
        seam_str_out(ctx, U2_S_CAST_NO_SPELL);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }
    /* `stc ; mov DS:B6[pick],al ; cmc ; sbb $0x1,al ; das ; cmc` — a packed-decimal minus one.
     * Spent here, before the location gate. See the header. */
    st->spell_counts[pick] = u2_bcd_dec1(st->spell_counts[pick]);

    if (st->sector_second < 4) return cast_failed(ctx);

    switch (pick) {
    case 1: /* LIGHT. Not a parallel light source: it refills the counter a torch burns down,
             * so casting it in a lit corridor spends a spell to top up a running count. */
        st->moved_flag = U2_DG_LIGHT_FULL;
        cmd_done(ctx);
        return U2_CMD_DONE;

    case 2: /* DOWN LADDER: deeper in a dungeon, down a floor in a tower. */
        return cast_ladder(ctx, ow, st->sector_second == 4);

    case 3: /* UP LADDER: the mirror, including the floor-0 exit. */
        return cast_ladder(ctx, ow, st->sector_second != 4);

    case 4: { /* PASSWALL. */
        int dx, dy;
        u2_dungeon_facing(ctx, &dx, &dy);
        const int tx = (ctx->state.sector_x + dx) & 0xf;
        const int ty = (ctx->state.sector_y + dy) & 0xf;
        /* The mask is a WRAP, not a bounds check, and that is a bug in the original rather
         * than a rule about edges. Standing at x = 0 facing west gives (0 - 1) & 0xf = 15, and
         * the cell on the far side of the map is then a perfectly ordinary target: filmed
         * twice, at (0,8) clearing (15,8) from 0x80 and at (0,2) clearing (15,2) from 0xc0
         * (captures/spice86/passwall-wrap-2026-11-15). The third-party "does not work on outer
         * edge walls" is this mask approximating a bounds check for the three quarters of
         * cases where the arithmetic does not wrap. Reproduced rather than fixed, in both
         * modes, because a fixed version would be a game the original is not.
         *
         * A coordinate that masks to zero is refused, which is the half of the edge rule that
         * does hold: an attempt from (0,0) spent the charge and cleared nothing, because the
         * y term masked to zero before the cell was ever read. */
        if (tx == 0 || ty == 0) return cast_failed(ctx);
        /* The precondition the flag test hides. asm/1451 returns the cell in AL and does
         * `or %al,%al ; ret`, and where the ladder arms test that with `jne` — proceed only if
         * the cell is open — PASSWALL tests it with `jns`, so it proceeds only when BIT 7 IS
         * SET. Casting at open floor refuses and spends. Note that 0xC0 and 0xE0, the door and
         * the fake wall, both pass this test, so PASSWALL clears a doorway too. */
        if ((u2_world_dungeon_at(&ow->world, st->site_floor, tx, ty) & 0x80) == 0)
            return cast_failed(ctx);
        /* `mov $0x0,%al ; mov 0x8,%bx ; mov %al,(%bx,%si)` — bx and si are the address the
         * validity callee left behind, so the write lands exactly on the cell that was tested,
         * and it writes zero rather than clearing the rock bit.
         *
         * `put` and NOT `terrain_put`, and that is the whole behaviour. `terrain_put` also
         * writes map_base, which is the ground record the grid is restamped from, so a write
         * through it survives leaving the site and coming back. PASSWALL does not: nothing in a
         * town or a dungeon persists between entries, and the wall you punched is standing
         * again when you walk back in. Owner ruling 2026-11-15, and it is the kind of rule a
         * byte-level port gets wrong by being tidier than the original — the cleared cell is
         * exactly as dirty-looking as a chest you emptied, and only the between-entries test
         * tells them apart. */
        u2_world_dungeon_put(&ow->world, st->site_floor, tx, ty, 0);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    case 5: /* SURFACE. Depth is irrelevant: cast from floor 1 it returned the player to the
             * world cell entered by, not to a cell derived from where the cast happened. */
        u2_overworld_site_leave(ctx, ow);
        cmd_done(ctx);
        return U2_CMD_DONE;

    case 6: /* PRAYER is KILL behind a coin: `call rand_byte ; js L_003432 ; jmp L_003501`.
             * The flag is bit 7 of the generator's new byte, set in the tail of the routine
             * rather than its body, which is why the third-party 50% sat marked NOT FOUND for
             * as long as it did. Measured fair at 50.0% of 200000 draws. */
        if (u2_rand_byte(ctx) & 0x80u) return cast_failed(ctx);
        /* falls through into the kill arm, exactly as the bytes do */
        /* FALLTHROUGH */

    case 9: { /* KILL, and PRAYER's success arm. No damage step: asm/30D1 is reached directly,
               * so the creature dies at any HP and the reward block runs as it does for melee. */
        const int slot = cast_target(ctx, ow);
        if (slot < 0) return cast_failed(ctx);
        u2_attack a;
        u2_combat_cast_kill(ctx, &ow->world, slot, &a);
        u2_announce_kill(ctx, ow, &a);
        return U2_CMD_DONE;
    }

    case 7: { /* MAGIC MISSILE.
             *
             * The amount is u2_cast_missile_amount(), which is asm/3139 L_0034b2 transcribed
             * instruction by instruction, and it is non-monotone on purpose: DS:56 is the high
             * packed-decimal byte of experience, doubling it with no DAA is a sawtooth, and the
             * `cmc` before the `sbb` takes the +1 back whenever that doubling overflowed. Peaks
             * at 255 for 0x70, collapses to 0 for 0x71. Validated at the two live-measured
             * points (0x01 -> 33, 0x99 -> 82) and pinned across the peak in commerce_check.
             *
             * This is the single most likely place for a well-meaning rewrite to diverge,
             * because it looks like a bug rather than a fidelity. Port it literally.
             */
        const int slot = cast_target(ctx, ow);
        if (slot < 0) return cast_failed(ctx);
        const unsigned amount = u2_cast_missile_amount(st->exp_lo);
        u2_attack a;
        /* DS:56 is state.h's `exp_lo` and is the HIGH byte; the naming wart is documented in
         * state.h and the field name is an address, not a significance. */
        const int r = u2_combat_cast_damage(ctx, &ow->world, slot, amount, &a);
        /* Survived: the slot keeps the reduced HP and NOTHING prints. That silence is the
         * original's, cited to asm/3139 L_0034ca, where the `jb` after the second `cmc` is
         * taken on survival — the opposite of how the idiom reads at a glance. */
        if (r == 2) u2_announce_kill(ctx, ow, &a);
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    case 8: { /* BLINK: two draws, each masked to a nibble and forced odd, then the same
               * asm/1451 gate the ladders use, then a direct overwrite of the position. */
        const int nx = (int)((u2_rand_byte(ctx) & 0xfu) | 1u);
        const int ny = (int)((u2_rand_byte(ctx) & 0xfu) | 1u);
        if (u2_world_dungeon_at(&ow->world, st->site_floor, nx, ny) != 0)
            return cast_failed(ctx);
        ctx->state.sector_x = (uint8_t)nx;
        ctx->state.sector_y = (uint8_t)ny;
        cmd_done(ctx);
        return U2_CMD_DONE;
    }

    default:
        /* Slot 0 is handled above and the pool holds ten names, so this is unreachable for
         * anything the ready command can select. The original has no default either: its
         * dispatch is a chain of compares that falls through to the KILL arm, so an
         * out-of-range DS:63 would reach KILL rather than refuse. Refusing is the safer
         * engine behaviour and is named here rather than left as a silent fall-through. */
        return cast_failed(ctx);
    }
}



/* ---- the table ----------------------------------------------------------- */

/*
 * One entry per jump-table slot. A NULL entry means "this letter is not implemented", which
 * is a different state from a handler that runs and declines to act, and is reported
 * separately by u2_cmd_implemented() so the marker gate can count what is really there.
 *
 * The letters that are still NULL each lack something specific, and the something is named
 * here rather than left as a gap. Note the correction of 2026-10-25: C, O, S and T are no
 * longer evidence-blocked -- G31 captured them and they now show execution evidence -- so what
 * stops each one below is a missing engine subsystem, not a missing capture. Listed in letter
 * order, because a list that drifts out of order is how entries get duplicated.
 *
 *   C  implemented 2026-11-15, off captures/spice86/magic-casts-2026-11-15, where all nine arms
 *      were filmed working in the original. The gates and all nine arms are in cmd_C above and
 *      each carries its own citation. What is still NOT modelled, named rather than left as a
 *      gap: the low-nibble stamp that reappears in a cell PASSWALL cleared, which the engine
 *      does not model at all because it draws creatures from the arrays rather than stamping
 *      them. Note what that costs, because it is not cosmetic: the walk gate is `(c & 0x8f) != 0`
 *      and the low nibble is one of the two tests, so a stamp landing back in a cleared cell
 *      would make the ORIGINAL print -BLOCKED- where the engine walks. Whether that ever
 *      happens wants a DS:1800 read, not another read of these bytes — see
 *      docs/outstanding.md § 2. What IS gated is the settled half: a hole you do clear is
 *      walkable, pinned by tests/passwall_check.c, whose mutant control writes 0x01 instead
 *      of 0 and goes red on the walk assertion specifically. Two things this entry used to call unobserved are closed and are recorded here so
 *      they do not get reopened. The tower's floor orientation is captured, not inferred:
 *      captures/spice86/d12-ladder-ds25 filmed DESCEND at tower floor 0 taking [DS:25] to 0xFF
 *      and ejecting the player, which is the inversion, and the same capture filmed UP LADDER
 *      at tower floor 0 FAILING rather than exiting — which is what the split in cast_ladder
 *      predicts, since in a tower UP LADDER is the increment arm and the floor-0 exit belongs
 *      to the decrement arm. And PASSWALL's write does NOT persist between entries (owner
 *      ruling 2026-11-15), which is why it goes through `put` and not `terrain_put`.
 *      One ordering rule there is easy to get wrong and is now gated: the count is decremented
 *      BEFORE the location gate, so a cast that fails with -FAILED! outside a tower or dungeon
 *      still spends the spell. Check the stock, spend, then fail — not check, fail, spend. The
 *      control for that assertion is recorded in tests/run_cp7.sh section 9.
 *   D  implemented 2026-10-29, and this entry used to say its key-code test was unresolved -- it
 *      is not. [DS:4A] is the site kind, not a key, and DESCEND and KLIMB are exact mirrors: the
 *      same 4-or-5 gate, the same cell-feature gate (0x20 down, 0x10 up), and the same two floor
 *      blocks with the direction swapped. asm/334D and asm/37F1. What is NOT modelled is the
 *      below-zero exit's [DS:6D] branch, which writes the player record on leaving a dungeon, and
 *      the floor term of asm/1451's grid index, so a tower above its first floor still draws
 *      page 0. Both belong to the dungeon span AGENTS.md section 3 parks.
 *   F  FIRE is the FRIGATE'S CANNONS, not a melee attack: asm/35C9 gates on [DS:13] == 0x24,
 *      the ship stamp from asm/2FAD's BOARD dispatch, and off the ship the whole command is
 *      `FIRE WHAT?`. On the ship it prints " DIRECT-" and uses the aim gate. Recorded in
 *      asm/35C1_cmd_F.asm. This entry used to say "the combat/attack arm", which was a guess
 *      from the word; cmd_A is the attack.
 *   G  GET needs ground items, and where they come from is a dungeon question. PARKED with
 *      dungeons by the owner 2026-10-29, alongside cmd_D and G47.
 *   N  asm/3BCE prints NEGATE TIME and falls through into asm/3BDD (`mov 0xe3,%al`), which
 *      is not cmd_done. Where that fragment ends is not established; guessing a time effect
 *      from the word "NEGATE" is the invention this project's rules exist to stop.
 *   R  ready weapon.
 *   S  implemented 2026-10-29. asm/3F25 prints STEAL DIRECT- and falls into the aim gate at
 *      asm/3F36 (a forced-keep `e8`, so the call edge was never listed), then asm/3F37 and
 *      asm/3F5D run the same two-cell ray as TRANSACT against the merchant marker. The effect
 *      is asm/3F5D_steal_attempt and it is NOT asm/0FCD, which is a monster stealing from the
 *      player in combat; the earlier note here claimed combat.c already modelled it, and that
 *      was the two conflated. What remains unproven is the [DS:1D7] == 1 refusal, whose
 *      un-indexed read is recorded in asm/3F5D_steal_attempt.asm.
 *   T  implemented 2026-10-29. asm/4050 DOES call the asm/2362 aim gate (at asm/405D, hidden
 *      behind a forced-keep byte so the call edge was never listed), and then it seeds a
 *      cursor with asm/23E6 and scans slots -- so it uses the aim family AND its own cursor
 *      scan, which is why it could not share the unlock/steal plumbing wholesale. The shops are
 *      game/commerce.c; the street-merchant half (asm/412C, asm/41D9, the tlkx printer at
 *      asm/154E) is still CP6.
 *   V  done 2026-11-11. An earlier note here said the helm view "needs an inventory view"
 *      and a "per-item selection cmd_V walks". Neither is in asm/437E: it tests the HELM
 *      count at DS:DB, tests the site digit at DS:4A, prints, decrements, draws the sector
 *      and waits for a key. No selection, no menu.
 *   W  the armour menu needs armour ownership and the strength gate; asm/43F3's menu is read
 *      but the ownership bytes are not modelled.
 *   X  implemented 2026-10-29 as the dismount (asm/44D4); it was listed here as "destroy item"
 *      from the string alone, which was the wrong reading of the letter.
 *   Y  implemented 2026-10-29: asm/4542 falls into asm/4551..4567, which is a freeform echo
 *      loop terminated by Enter, not an unresolved tail. See cmd_Y. The 0x8d on the prompt's
 *      db line is the game's newline glyph, not a mystery glyph, so the CP9 note about it is
 *      retired as well.
 *
 * All six letters that used to live in u2_overworld_loop are here now, and A and U came last
 * on purpose: they leave the loop waiting for a direction key that arrives turns later, so
 * the pending state had to move onto u2_overworld first -- in the original the direction is
 * consumed inside the handler chain, which makes the pending aim the command's business and
 * not the loop's. M joins them as the readying half of the spell story; C above is the
 * spending half, and it is the one still waiting on dungeons.
 */
static const cmd_fn cmd_table[CMD_COUNT] = {
    cmd_A, /* A  attack aim: asm/2B8F, prints ATTACK-- and awaits a direction */
    cmd_B, /* B  board vehicle: asm/2F6F -> asm/2FAD, gated on the item the refusal names */
    cmd_C, /* C  cast: asm/310B prints CAST- and asm/3139 is the gates and the nine arms.
              Wired 2026-11-15 off captures/spice86/magic-casts-2026-11-15. */
    cmd_D, /* D  descend: asm/334D, the mirror of K, gated on site kind and the cell feature */
    cmd_E, /* E  enter site: asm/3389, body in u2_overworld_enter() */
    cmd_F, /* F  fire: asm/35C1, the frigate's cannon, gated on the ship stamp */
    cmd_G,  /* GET: the site arm is asm/363B's chest; the overworld arm is a gap */
    cmd_H,
    cmd_I, /* I  ignite torch: asm/3794, spends a torch and refills the light counter */
    cmd_J,
    cmd_K, /* K  climb: asm/37F1, the mirror of D */
    cmd_L, /* L  launch plane/rocket: asm/38B0, its own flight loops, gated on the item named */
    cmd_M, /* M  ready a spell by number: asm/3B92, echoes the spell NAME */
    cmd_N, /* N  negate time: asm/3BCE, spends a STRANGE COIN and freezes creature movement */
    cmd_O, /* O  offer gold: asm/3C2E, the ray, the amount, and the ONLY source of the
            * QUICK SWORD and the RING. Grants nothing outside sector 32, and never
            * deducts the gold it is given. */
    cmd_P, /* P  PASS: asm/3DBC. The space bar is a different site, in overworld.c */
    cmd_Q, /* Q  save: asm/3DE4 refuses indoors, asm/3DFA saves outdoors */
    cmd_R, /* R  ready weapon: asm/3E50, menu then NAME then ownership and the AGILITY gate */
    cmd_S, /* S  steal: asm/3F25, the TRANSACT ray at a shoppe, quadrant loot, no creature scan */
    cmd_T, /* T  transact: asm/4050, the ray and the shops in game/commerce.c */
    cmd_U, /* U  unlock aim: asm/42F1, prints UNLOCK DIRECTION- and awaits a direction */
    cmd_V, /* V  view: asm/437E, needs a HELM held and sector digit < 4; draws the sector
            *    as a 64x64 overview at 4x2 px per cell and waits for a key (CP5 H1) */
    cmd_W, /* W  wear armour: asm/43F3, full menu then digit then ownership and strength gates */
    cmd_X, /* X  X-IT dismount: asm/44D4, returns the tile and the class-dependent foot stamp */
    cmd_Y, /* Y  yell: asm/4542 + its echo loop asm/4551..4567, display-only */
    cmd_Z, /* Z  status screen: asm/4568, a full-page overwrite, no turn passes */
};

u2_cmd_result u2_cmd_dispatch(u2_ctx *ctx, u2_overworld *ow, uint8_t letter)
{
    /* asm/09FB `cmp $0x41,%al ; jae` then asm/0A18 `cmp $0x5a,%al ; jbe`. Both bounds,
     * before the table is touched. Anything else is the asm/0A1C arm, and the caller owns
     * what that prints -- the illegal-command path is a different site (asm/277B region)
     * and is not this function's claim to make. */
    if (letter < 'A' || letter > 'Z')
        return U2_CMD_UNKNOWN;

    const cmd_fn fn = cmd_table[letter - 'A'];
    if (!fn)
        return U2_CMD_UNKNOWN;

    return fn(ctx, ow, letter);
}

int u2_cmd_implemented(char *out, int cap)
{
    int n = 0;
    for (int i = 0; i < CMD_COUNT; ++i) {
        if (!cmd_table[i])
            continue;
        if (out && n + 1 < cap)
            out[n] = (char)('A' + i);
        n++;
    }
    if (out && n < cap)
        out[n] = '\0';
    return n;
}

/* ---------------------------------------------------------------------------
 * The continuations. See u2_cmd_cont in commands.h for the shape and for why
 * FIRE is not here; the loop that runs them is u2_cmd_tail in game/overworld.c.
 * Each body carries the citation its old in-loop arm carried, so asm/4310,
 * asm/405D, asm/3F36, asm/3C2E and asm/2BAD still name the site they were argued
 * from -- in one place instead of once per arm.
 */

/* A direction arriving while UNLOCK DIRECTION- is on the line answers the unlock and
 * is not a step. asm/4310 and asm/4345 both end at jmp L_000c30 and asm/435E at
 * jmp L_000c33, and every one of those reaches cmd_done, so the post-command sweep
 * runs on the refusal as well as on the success -- the same consequence § 15.2 of
 * docs/monster-spawn.md derives for the command path in general. The player does not
 * move. */
static void cont_unlock(u2_ctx *ctx, u2_overworld *ow, int dx, int dy, const char *word)
{
    (void)word;                       /* UNLOCK prints no direction word */
    const int tx = ctx->state.sector_x + dx;
    const int ty = ctx->state.sector_y;
    const int off = tx < 0 || tx >= U2_SECTOR_EDGE || ty < 0 || ty >= U2_SECTOR_EDGE;
    const uint8_t target = off ? 0 : u2_world_tile_at(&ow->world, tx, ty);
    switch (u2_unlock_rule(dx, target, ctx->state.keys)) {
    case U2_UNLOCK_NO_AIM:
    case U2_UNLOCK_NO_DOOR:
        /* One print, not two: the original has a single NO DOOR THERE! site
         * (asm/4310) and reaches it from both causes. */
        seam_str_out(ctx, U2_S_NO_DOOR_THERE);
        break;
    case U2_UNLOCK_NO_KEYS:
        seam_str_out(ctx, U2_S_NO_KEYS_FIT);
        break;
    default: {
        /* asm/435E: spend one key, then pave the cell with the ground the
         * player is standing on. terrain_put, not tile_put: the creature sweep runs
         * immediately after and rebuilds the grid from map_base, so a tile_put
         * paving is erased before the player's next key -- the door would
         * re-close the instant it was opened. Measured in the original
         * 2026-10-29: openings survived a walk through a town with 32 live
         * creatures. Gate: tests/site_state.py case C, which failed here
         * (cell came back 0xa0) before this line changed. */
        ctx->state.keys = u2_unlock_spend_key(ctx->state.keys);
        const uint8_t ground = u2_world_tile_at(&ow->world, ctx->state.sector_x, ctx->state.sector_y);
        u2_world_terrain_put(&ow->world, tx, ty, u2_unlock_replacement(ground));
        break;
    }
    }
}

/* TRANSACT: the direction is the ray. asm/405D calls the same aim gate cmd_A and
 * cmd_U use (asm/2362), which prints the direction word and leaves the delta in
 * DS:19/1A; asm/4060 then seeds the cursor and asm/4063 stores the gate's own
 * counter byte. The word therefore lands on the TRANSACT- line, exactly as
 * `CMD: TRANSACT-NORTH` reads in the captures. The ray does not move the player,
 * and it is a turn: every exit from asm/4050 is a tail jump (asm/1590, asm/0A33,
 * asm/4209, asm/4249) and the two refusal exits reach the post-command tail like
 * any other command. */
static void cont_transact(u2_ctx *ctx, u2_overworld *ow, int dx, int dy, const char *word)
{
    (void)word;
    u2_commerce_transact(ctx, ow, dx, dy);
    /* A purchase moves gold, food, the mount byte or an owned count, and the band
     * would otherwise go on showing the pre-purchase number for the rest of the
     * session. The original's post-command tail reaches the status block; the
     * engine's creature sweep does not redraw it, so the redraw belongs here,
     * where the mutation happened. u2_status_draw restores the cursor, which is
     * why it is safe to print the prompt after it.
     *
     * KNOWN UNCOVERED: turning this redraw off leaves `make -C engine test` green.
     * No gate reads the status band after a purchase, so this line is asserted by
     * the asm site and not by a test. Filed with #16. */
    u2_status_draw(ctx);
}

/* STEAL DIRECT: same shape as TRANSACT and for the same byte-level reason: asm/3F36
 * calls the asm/2362 gate inside the handler chain, so the letter alone is not a
 * completed turn and the word lands on the STEAL DIRECT- line. No status redraw
 * here, unlike TRANSACT: a successful steal moves food or an owned count, and
 * asm/3F5D's exits all tail-jump to asm/0C33 without touching the status block, so
 * the original lets the number go stale too. Matching it means NOT redrawing. */
static void cont_steal(u2_ctx *ctx, u2_overworld *ow, int dx, int dy, const char *word)
{
    (void)word;
    u2_commerce_steal(ctx, ow, dx, dy);
}

/* OFFER GOLD DIRECT: the direction names the recipient. Same shape again, and the
 * same byte-level reason: asm/3C2E calls the asm/2362 aim gate inside the handler
 * chain. The amount prompt follows inside u2_commerce_offer, because the original
 * asks for it only once a recipient exists. Unlike STEAL, and like TRANSACT, this
 * can move the ledger: a raise lands in a stat byte and the status band shows
 * stats, so it redraws. */
static void cont_offer(u2_ctx *ctx, u2_overworld *ow, int dx, int dy, const char *word)
{
    (void)word;
    u2_commerce_offer(ctx, ow, dx, dy);
}

/* ATTACK: the direction is the swing, not a step -- the original consumed it inside
 * the cmd_A chain and never moved the player. Live: `ATTACK--EAST--HIT!!!` with the
 * player standing still. The body prints the direction word itself, which is why
 * print_word is off for this entry.
 *
 * The swing is the turn: the original's monsters advance on the expiry of a
 * half-typed `A` too (`ATTACK--PASS`), so an attack that was actually completed
 * cannot sensibly advance nothing.
 *
 * And the ARMS-PARALYSIS REFUSAL is a turn as well. This used to be
 * `if (ctx->state.paral_arms == 0) creature_turn(...)`, which made arm paralysis
 * permanent for anyone who kept trying to swing - the same stranding the owner had
 * already reported for legs. Two independent proofs, both found without a new
 * capture:
 *
 * BYTES: the refusal is asm/2BAD, whose whole tail is `jmp L_000c30`. That is
 * asm/0A30, the 3-byte stub that falls through to asm/0A33_cmd_done and so to
 * asm/0A38_post_command_tail - the IDENTICAL target of asm/0A15, the terrain refusal
 * whose tick was proven by forced-poke. Same jump, same tail, same tick. asm/2052,
 * asm/228A and asm/277B refuse their way to the same address too.
 *
 * CORPUS: captures/spice86/fight-20261021-castle03/turns, 3000 per-turn DS dumps.
 * DS:26C fell 6 times, every one of them with the food odometer UNCHANGED, and zero
 * times with food changed. A counter that only decremented on spent turns would show
 * the opposite. Legs in the same corpus: 91 decrements, all same-food.
 *
 * So the guard is gone: a completed ATTACK command always reaches the tail. */
static void cont_attack(u2_ctx *ctx, u2_overworld *ow, int dx, int dy, const char *word)
{
    u2_overworld_attack_dir(ctx, ow, word, dx, dy);
}

static const u2_cmd_cont k_cmd_cont[] = {
    { 'U', 0, 0, cont_unlock },
    { 'T', 1, 1, cont_transact },
    { 'S', 1, 0, cont_steal },
    { 'O', 1, 1, cont_offer },
    { 'A', 0, 0, cont_attack },
};

const u2_cmd_cont *u2_cmd_continuation(char letter)
{
    for (size_t i = 0; i < sizeof k_cmd_cont / sizeof k_cmd_cont[0]; i++)
        if (k_cmd_cont[i].letter == letter)
            return &k_cmd_cont[i];
    return NULL;
}
