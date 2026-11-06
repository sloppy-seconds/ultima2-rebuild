#ifndef U2_STATE_H
#define U2_STATE_H

/*
 * Game state — the DS working segment.
 *
 * At startup the original sets DS to its own 64 KiB working segment
 * (asm/0100_sub_0100.asm: `mov $0x741,%ax` patched by the MZ loader to the
 * real segment, `mov %ax,%ds`; runtime DS = 0x08BE per captures/README.md).
 * All the bare-displacement globals in the asm tree (mov 0x23, mov 0x240, ...)
 * live there. The engine mirrors that segment as an explicit struct so a
 * lifted routine can be checked field-by-field against the asm it cites.
 *
 * Two rules keep 16-bit fidelity:
 *   - fields are uint8_t/uint16_t, never int; arithmetic that the original
 *     relies on wrapping is masked at the assignment site;
 *   - counters the original keeps in packed decimal (gold, food, exp) stay
 *     BCD pairs, hundreds byte first — see u2_bcd16(), and the note in
 *     core/state.c on why there is deliberately no encoder.
 *
 * A second variable space exists in the original and is NOT here: the CS-side
 * data area (jump table at cs:0x730, the timed_wait parameter block at
 * cs:0x4F04..cs:0x4F0A, item tables around cs:0x48D0). Those are static
 * per-module state and live with the module that owns them, cited in its
 * header. See disasm/selfmod-capture.md.
 *
 * Offsets below are proven by the asm that reads them; each is cited.
 * Offsets not yet lifted stay in `reserved` and are filled in as phases
 * progress — never guessed.
 *
 * THIS STRUCT IS THE ONE TABLE FOR DS OFFSETS. It is compiled, and every named
 * field is pinned by a _Static_assert below, so a drift is a build failure
 * rather than a stale document. docs/spice86-live-findings.md § 11 is the live
 * evidence behind it and defers to it on layout; when a field is filled in
 * here, § 11 should not grow a competing copy. When you prove a DS byte, name
 * it here in the same commit and add its _Static_assert.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct u2_state {
    /* 0x00 */ uint8_t sector_x;      /* asm/5AE1 fill loop: window origin is (x-9, y-5);
                                        also the >= 0x40 sector-reload test (asm/242D).
                                        Unreconciled: asm/5AB1 computes (x-10, y-6) -- see
                                        that file's header, this is an open question. */
    /* 0x01 */ uint8_t sector_y;      /* asm/5AE1, asm/2434 */
    /* 0x02 */ uint8_t face_dx;       /* THE FACING VECTOR, as a signed byte pair.
                                        * Proven 2026-10-29 from the dungeon turn commands
                                        * (asm/277B, runtime 0x28C3..0x2974): TURN LEFT is
                                        * `if ([0x03]) { [0x02]=[0x03]; [0x03]=0 }
                                        *  else { [0x03]=-[0x02]; [0x02]=0 }` and TURN RIGHT
                                        * is its mirror, which is a quarter-turn on (dx,dy).
                                        * The dungeon ADVANCE/RETREAT probes add this pair to
                                        * DS:00/01 to name the cell ahead and behind, and
                                        * asm/6000 copies it to DS:483/484 to walk the depth
                                        * columns. Values are -1, 0 or 1; north is (0,-1).
                                        * Signed: the turns negate, so a reader that takes the
                                        * byte as unsigned reads every westward facing as
                                        * 0xFF. */
    /* 0x03 */ uint8_t face_dy;
    /* 0x04 */ uint8_t reserved_04[0x0E];
    /* --- movement and passability cluster. Filled 2026-10-21 from the full
     * re-decode of asm/2423_passability.asm and the raw writer scan; narrative
     * in docs/passability-findings.md. Before this these bytes sat inside
     * `reserved_02`, which is why the walk loop had to be re-derived by hand. */
    /* 0x12 */ uint8_t view_mode;       /* RENAMED 2026-10-28 from tile_offsector,
                                        * owner-approved. It was never a tile and never an
                                        * off-sector tile source: it is a mode byte --
                                        * 0xFF on the world map, 4 in an interior sector --
                                        * written by asm/0C1F/asm/0C2B on the DS:4A branch
                                        * (DS:4A is the sector's low decimal digit), and its
                                        * only readers are asm/5AB1_view_origin_by_mode and
                                        * asm/5AF9. 5AB1 is the routine the monster spawn
                                        * arms call, and DS:12 == 0xFF is how it decides
                                        * whether the viewport origin wraps (`and $0x3f`).
                                        * Live: 0xFF in sector 20, 4 in sector 32.
                                        * The new name says what the byte selects: how the
                                        * view origin is computed. Nothing in the engine
                                        * reads it; it is written so the state block matches
                                        * a live capture. docs/monster-spawn.md § 15.2. */
    /* 0x13 */ uint8_t player_stamp;    /* the sprite stamped INTO the map cell.
                                         * 0x78 = on foot. NOT the terrain under the
                                         * player: observed 0x78 while standing on
                                         * tile 0x0C (create-test-live), and constant
                                         * 0x78 across every in-world capture. The
                                         * old "player tile id" reading was wrong and
                                         * the asm/2423 first ladder never fires on
                                         * foot because of it. */
    /* 0x14 */ uint8_t mode_a;          /* <- DS:2DF (asm/5B71_mode_table_copy) */
    /* 0x15 */ uint8_t mode_walk;       /* the walk loop's passability argument on
                                         * three of four arms: mov al,[0x15] at
                                         * runtime 0x0962. Origin is DS:2CB via
                                         * asm/5B71_mode_table_copy.asm. */
    /* 0x16 */ uint8_t mode_b;          /* <- DS:2F3 (asm/5B71_mode_table_copy) */
    /* 0x17 */ uint8_t mode_c;          /* <- DS:2E0 (asm/5B71_mode_table_copy) */
    /* 0x18 */ uint8_t mode_walk_alt;   /* the fourth arm's argument: mov al,[0x18]
                                         * at runtime 0x09AF. Origin is DS:2DE. */
    /* 0x19 */ uint8_t reserved_19[7];
    /* 0x20 */ uint8_t move_scratch;    /* DISPUTED: docs called this an
                                         * invalid-move counter, but the harvest
                                         * shows it decremented in the walk loop and
                                         * overwritten at runtime 0x2BC7. Treat as
                                         * scratch; do not use as a reject signal. */
    /* 0x21 */ uint8_t reserved_21[2];
    /* 0x23 */ uint8_t player_x;      /* asm/50B3_map_tile_ptr.asm: x + (y<<6) + map base.
                                        The relationship between this pair and the
                                        DS:00/01 pair above is not yet established by
                                        the asm, so both are kept rather than merged. */
    /* 0x24 */ uint8_t player_y;      /* asm/50B3_map_tile_ptr.asm */
    /* 0x25 */ uint8_t site_floor;    /* THE SITE FLOOR INDEX — proven 2026-10-29 by
                                         * captures/spice86/d12-ladder-ds25, which is the capture
                                         * this field waited on.
                                         * Observed: in the sector-24 tower, casting UP LADDER
                                         * took it 0 -> 1 and DOWN LADDER took it back 1 -> 0,
                                         * through the game's own `incb`/`decb` at asm/3139's
                                         * ladder arms; pressing D (cmd_D, DESCEND) on a 0x20
                                         * down-ladder cell left the site and left it at 0xFF,
                                         * i.e. a decrement below zero with no guard.
                                         * Corroborating structure, all measured: a tower's
                                         * mapxNN is exactly 16 pages of 256 bytes, matching the
                                         * `cmp $0xf,%al` bound on this byte; asm/1451 computes
                                         * word[DS:472] + AL*256 + y*16 + x with AL = the
                                         * candidate floor; and asm/68C0 reads the dungeon grid
                                         * through [DS:25]*256 as its page selector.
                                         * What it is NOT: a creature attribute. See
                                         * asm/330C_cast_target_finder.asm — the CAST target scan
                                         * compares this byte against mon_kind, and mon_kind is
                                         * demonstrably not a floor.
                                         * It is also the byte the BLINK branch hands to asm/1451
                                         * before it overwrites the position.
                                         * NOT PERSISTED, and this is easy to miss: the saved record
                                         * is DS:36..DS:135, and DS:25 is BELOW it, so the floor is
                                         * scratch and the game does not remember which floor you
                                         * were on. Found the hard way 2026-10-29 when a CP6 seed
                                         * tried to set it through the player file and the byte came
                                         * back zero; the gate now drives the floor with keystrokes
                                         * instead, which is the better test anyway. */
    /* 0x26 */ uint8_t moved_flag;    /* the accept tail of the walk loop xors it
                                         * with 0xFF (runtime 0x09E9) and it is
                                         * zeroed at 0x09F7; a rejected step never
                                         * reaches the xor.
                                         * ALIASED — this byte has a second, unrelated life inside
                                         * a site, and reading it as moved_flag there reads a
                                         * light counter. asm/1151_dungeon_darkness tests it
                                         * (`mov 0x26,%al ; or %al,%al`), prints IT'S DARK! when it
                                         * is zero, and otherwise `decb 0x26` per step, printing
                                         * TORCH BURNED OUT! when the decrement reaches zero; and
                                         * the CAST dispatch for LIGHT writes 0x96 (150) into it
                                         * (asm/3139_cast_spell_flow). Confirmed live 2026-10-29 in
                                         * captures/spice86/cmd-c-cast-tower: 0x96 on cast, then
                                         * 0x95 and 0x94 on the two steps after it. So torches and
                                         * the LIGHT spell share ONE counter — LIGHT is not a
                                         * parallel light source, which is why casting it in a lit
                                         * corridor spends a spell to refill a running count. Same
                                         * mode-dependent reuse as DS:49/DS:4A. Any future reader of
                                         * moved_flag must know which mode it is in. */
    /* 0x27 */ uint8_t reserved_27[2];
    /* 0x29 */ uint8_t mode_arg;      /* asm/2423_passability.asm stores its AL
                                         * argument here on entry and the mode
                                         * ladder reads it back masked & 0x7F.
                                         * NB it has FOUR writers (runtime 0x20B7,
                                         * 0x2423, 0x54E8, 0x5A7A), so it is shared
                                         * scratch: a snapshot of it does not tell
                                         * you the last passability argument. */
    /* 0x2A */ uint8_t reserved_2A[4];
    /* 0x2E */ uint8_t cursor_col;    /* asm/51FD_str_out_core.asm: DL for INT 10h AH=02h */
    /* 0x2F */ uint8_t cursor_row;    /* asm/51FD_str_out_core.asm: DH for INT 10h AH=02h */
    /* 0x30 */ uint8_t reserved_30;
    /* 0x31 */ uint8_t right_margin;  /* last printable column; 0x26 or 0x28 at the
                                         sites asm/456B_sub_456b.asm, asm/48A7_sub_48a7.asm,
                                         asm/4A2F_sub_4a2f.asm; read by char_print's wrap
                                         test in asm/504A_sub_504a.asm */
    /* --- player record: the 256 bytes the game saves, DS:36..135 ----------
     * Proven by assignment in the creation routine (see
     * disasm/redecoded/5400_sub_5400.txt) and by the FCB read/write sites in
     * asm/5400 and asm/3823; narrative in
     * docs/phase4-implementation.md § CP2.5. Anything not proven by an
     * assignment or a display site stays in `reserved` — never guessed. */
    /* 0x32 */ uint8_t reserved_32[4];
    /* 0x36 */ uint8_t name[16];      /* cleared, then typed; entry ends on CR */
    /* 0x46 */ uint8_t sex;           /* 'M' or 'F' as typed, not converted */
    /* 0x47 */ uint8_t pclass;        /* 0..3 after `sub $0x31`: FIGHTER..THIEF */
    /* 0x48 */ uint8_t race;          /* '1'..'4' as typed: HUMAN ELF DWARF HOBBIT */
    /* 0x49 */ uint8_t sector_first;   /* first digit of the sector filename: asm/2290
                                        * patches [0x49],[0x4A] into MAPXFF. Set to 2
                                        * at creation (redecoded 5400 tail, file 0x5BE8),
                                        * so a fresh character starts in sector 2x. */
    /* 0x4A */ uint8_t sector_second;  /* second digit of the sector filename and,
                                        * outside a sector read, the site kind:
                                        * 0 overworld, 1 village, 2 town, 4 tower.
                                        * Evidence: asm/2290 patches [0x49],[0x4A]
                                        * into MAPXFF; asm/243E zeroes it on sector
                                        * reload; asm/343F writes 2 and 4 beside the
                                        * -TOWN/-TOWER strings; the P path branches
                                        * `cmp $0x4` on it (file 0x0A76). The plan §1
                                        * "last key" reading is RETIRED: seam.c used
                                        * to store polled keys here, which corrupted
                                        * the saved sector digit (observed live as
                                        * "sector 289" — 'Y' from the creation
                                        * confirm). cmd_D's compares against 4 and 5
                                        * are site-kind compares, not key compares. */
    /* 0x4B */ uint8_t stat_str;
    /* 0x4C */ uint8_t stat_agi;
    /* 0x4D */ uint8_t stat_sta;
    /* 0x4E */ uint8_t stat_cha;      /* PROVEN charisma 2026-10-29, not a guess: a
                                        * character with 0x4E=0x20 and 0x4F=0x40 printed
                                        * CHARISMA-20 on the Z screen, and asm/20AB priced a
                                        * horse off this byte (52 gp, not the 32 gp that
                                        * 0x4F would give). docs/spice86-live-findings.md
                                        * § 11 had the pair swapped and is corrected. */
    /* 0x4F */ uint8_t stat_wis;      /* PROVEN wisdom by the same capture */
    /* 0x50 */ uint8_t stat_int;
    /* 0x51 */ uint8_t hp;            /* set to 4 at creation */
    /* 0x52 */ uint8_t unk_52;        /* adjacent to hp; no assignment traced yet */
    /* 0x53 */ uint8_t food;          /* set to 4 at creation; BCD pair with 0x54,
                                        read at asm/5129_sub_5129.asm (FOOD=) */
    /* 0x54 */ uint8_t food_hi;
    /* 0x55 */ uint8_t food_lo;       /* FOOD's third BCD byte. The status band
                                         prints only 0x53/0x54 (4 digits), but the
                                         drain routines subtract from 0x55 first and
                                         borrow upward: PASS does `sbb $0x0a` on this
                                         byte (file 0x0B10..0x0B15), so one PASS is
                                         10 food. asm/2599/asm/2626 do the same with
                                         $0x19 (19). Underflow is the death trigger
                                         (jb to runtime 0x0E62, which zeroes
                                         0x51..0x59). */
    /* 0x56 */ uint8_t exp_lo;        /* EXP.= display loads [0x56],[0x57] — note
                                         it is NOT 0x55; file 0x5367.
                                         * NAMING WART, do not fix one side alone:
                                         * exp_lo is the HIGH byte and exp_hi the
                                         * LOW one. asm/2DEE adds into DS:57 first
                                         * and carries into DS:56, so 57 is least
                                         * significant. asm/2E3D and attack_check.c
                                         * both depend on that reading. */
    /* 0x57 */ uint8_t exp_hi;
    /* 0x58 */ uint8_t gold_lo;       /* BCD pair, hundreds byte first; display site
                                         asm/5175_sub_5175.asm (string at file 0x5382).
                                         * NAMING WART, same as exp_lo/exp_hi:
                                         * gold_lo (DS:58) is the HIGH byte and
                                         * gold_hi (DS:59) the LOW one — asm/2DC1
                                         * adds into DS:59 first and carries into
                                         * DS:58. Read at face value the reward adds
                                         * are byte-swapped and every payout is
                                         * corrupted silently. Rename both pairs
                                         * together, with their _Static_asserts and
                                         * every call site, or leave them alone. */
    /* 0x59 */ uint8_t gold_hi;
    /* 0x5A */ uint8_t entry_x;        /* spawn x: the P path copies [0x5A]->[0x00]
                                        * (file 0x0A26); creation sets 20 (file 0x5BDE);
                                        * the sector reload restores [0x5A]->[0x00] too
                                        * (asm/243E), so it is the entry point of the
                                        * current sector. */
    /* 0x5B */ uint8_t entry_y;        /* spawn y: [0x5B]->[0x01] (file 0x0A2C),
                                        * creation sets 20 (file 0x5BE3) */
    /* 0x5C */ uint8_t reserved_5C[4];
    /* 0x60 */ uint8_t spawn_selector; /* NOT a spawn counter. The wandering spawner does
                                        * `incb` here, walks the bits of the result from 0
                                        * upward, and takes the arm belonging to the LOWEST
                                        * set bit; an arm whose terrain test fails does
                                        * `decb`, which is why the byte creeps instead of
                                        * marching. Pinned at runtime for all nine arms by
                                        * forcing this byte — docs/monster-spawn.md 15.4.
                                        * asm/0CEE_spawn_tile_store, asm/0D4F, asm/0D6F,
                                        * asm/0DAF, asm/0DC4. */
    /* 0x61 */ uint8_t readied_weapon; /* weapon index 0..9, 0 = HANDS. Proven by
                                        * live read/write-back, findings § 11;
                                        * name table at file 0x4F53. */
    /* 0x62 */ uint8_t worn_armour;    /* armour index 0..7, 0 = SKIN. Proven,
                                        * findings § 11; name table at file 0x4F9E. */
    /* 0x63 */ uint8_t spell_pick;    /* readied spell slot */
    /* 0x64 */ uint8_t torches;        /* one packed-decimal byte, ceiling 99.
                                        * asm/2D22 adds, asm/37AE subtracts. */
    /* 0x65 */ uint8_t keys;           /* packed decimal, ceiling 99. asm/2CD8
                                        * adds 2, asm/435E subtracts 1. */
    /* 0x66 */ uint8_t tools;          /* packed decimal, ceiling 99. asm/2CF6
                                        * adds 1, asm/277B subtracts 1. */
    /* 0x67 */ uint8_t reserved_67[2];   /* DS:67, DS:68: no reader or writer found in
                                        * the whole-tree sweep that named DS:69..6B. Left
                                        * unnamed on purpose rather than guessed at. */
    /* 0x69 */ uint8_t rocket_parked;    /* flag: a rocket is standing at
                                        * rocket_parked_x/rocket_parked_y and must be
                                        * re-staged onto the map.
                                        *
                                        * It is inside the save record (DS:36..135) and
                                        * that is the entire reason for it. A rocket left
                                        * on a planet has no authored pad, so the only
                                        * thing that puts it back after a restart is the
                                        * record — which is also why launching a rocket
                                        * silently saves (asm/38B0 L_003b52 sets this and
                                        * then calls the save body, with no message).
                                        *
                                        * Writers: asm/38B0 L_003b52 on launch (:= 1) and
                                        * the survived-dive branch at L_003bd7, which
                                        * CLEARS it on Earth (L_003bde) and sets it off
                                        * Earth (L_003be6). Earth clears because its pads
                                        * are baked into the map: mapx41 at (33,3),
                                        * (38,3) and (43,3) is 0x50.
                                        * Readers: asm/080D on the 'P' load path
                                        * (`mov 0x69,%al ; or %al,%al ; je`), falling into
                                        * asm/0839, which stamps U2_TILE_ROCKET at the
                                        * recorded cell. asm/245a is a second copy of that
                                        * stamp and asm/3823 re-stages on site exit.
                                        *
                                        * Observed live 2026-11-03: 01 01 0a after
                                        * launching from (1,10) in sector 40, and 00 00 00
                                        * on a character that had never been in space. */
    /* 0x6A */ uint8_t rocket_parked_x;  /* the parked rocket's cell column */
    /* 0x6B */ uint8_t rocket_parked_y;  /* and row */
    /* 0x6C */ uint8_t reserved_6C;      /* no reader or writer found */
    /* 0x6D */ uint8_t world;          /* WHICH WORLD the player is on. 0 = Earth,
                                        * 1..9 = the planets, 0x0A = deep space. Written by
                                        * asm/7030 (`mov $0xa,%al` beside 'YOU ARE IN DEEP
                                        * SPACE.', and a second store beside 'YOU ARE
                                        * ORBITING '), read back as a dispatch (`cmp $0x0`,
                                        * `$0x1`, `$0x2` …) and copied into DS:49 — the
                                        * sector TENS digit — so the world byte and the
                                        * sector's high digit are the same fact. It is why
                                        * asm/3DFA's refusal reads 'ONLY ON EARTH!': that is
                                        * a literal description of the test, not a metaphor.
                                        * NAMED WRONG FOR TWO YEARS: this byte was called
                                        * boots_count because tools/u2_edit.py wrote a boots
                                        * count into it, and state.h then named the field
                                        * after our own write. Boots are items_held[BOOTS]
                                        * at DS:D9 (see below), which is what the paralysis
                                        * resist actually reads — and that is the whole
                                        * explanation of the 'STALE COPY' note that used to
                                        * sit here: the byte never was boots, so of course a
                                        * thief stealing boots left it alone. */
    /* 0x6E */ uint8_t gate_index;     /* TIME-DOOR rotation index. Steps by 2 and is masked
                                       * to 3 bits, so it cycles 00,02,04,06 and the active
                                       * slot is index/2. OBSERVED LIVE 2026-10-29: frozen
                                       * while the countdown runs, +2 when it expires, in
                                       * every era including 0. Not attributed to any routine
                                       * — the asm/2744 citation that stood here was refuted
                                       * by a stop across observed rotations. docs/
                                       * moongates.md § 2 and § 6. */
    /* 0x6F */ uint8_t gate_countdown; /* TIME-DOOR countdown, reloaded to exactly 8 on each
                                       * rotation (measured in memory, not attributed to code).
                                       * ONE PER TURN, measured: 12 landed direction keys moved
                                       * it exactly 12, idling 20 s moved it 2, because a turn
                                       * also ends on the idle auto-PASS (asm/08FF) at roughly
                                       * one per 10 s. The recalled "every 8 turns" is correct.
                                       * Both bytes are inside the saved record, so the
                                       * rotation state persists across a save. */
    /* 0x70 */ uint8_t reserved_70[4];  /* 0x70..0x73 */
    /* 0x74 */ uint16_t speed;        /* THE GAME SPEED. Two writers, one consumer.
                                        * Seeded at startup by asm/06AC_clock_tick_wait,
                                        * which counts how many 100-iteration busy passes
                                        * fit inside a 20-tick INT 1Ah window (`addw
                                        * $0x1,0x74`). Overwritten on demand by F7/F8:
                                        * asm/4FA5_speed_faster_f7 adds 0x200 capped at
                                        * 0x3a00, asm/4F8E_speed_slower_f8 subtracts 0x200
                                        * floored at [0x554]; both then call the rescaler
                                        * via asm/4FB3_speed_rescale_call.
                                        * MEASURED: 0x3A17 = 14,871, static across 103
                                        * samples in captures/spice86/g30-clockcal/.
                                        * Note it sits 31 ABOVE the F7 ceiling: that clamp
                                        * guards the key handler's arithmetic and never the
                                        * startup write. */
    /* 0x76 */ uint8_t weapons_owned[10]; /* one count per weapon, 10 entries.
                                        * Proven, docs/save-format.md. asm/363B and
                                        * asm/19D7 add; asm/3F5D steals. */
    /* 0x80 */ uint8_t reserved_80[8];    /* DS:80..87: purpose unknown. This was briefly
                                        * named owned_armour on the belief the Z screen's
                                        * ARMOUR: list read it, but that was a mis-citation
                                        * (asm/464E is the SPELL- fragment). The Z armour
                                        * loop at asm/47D0 actually reads `mov 0x96(%di),%al`
                                        * with di=1..6, i.e. DS:97..9C = armour_owned below.
                                        * Nothing writes DS:80, so it is reserved again. */
    /* 0x88 */ uint8_t reserved_88[0x0E];
    /* 0x96 */ uint8_t reserved_96;        /* DS:96 itself: G8 found nothing reads it */
    /* 0x97 */ uint8_t armour_owned[6];    /* owned count per armour type, indexed by the WEAR
                                        * ARMOUR menu number MINUS ONE: [0]=CLOTH .. [5]=POWER.
                                        * Proven by captures/spice86/armour-buy/: buying CLOTH
                                        * (menu item 1) took DS:97 from 00 to 01 and gold 9999 to
                                        * 9987. cmd_W reads it as `mov 0x96(%di),%al` with di set
                                        * to the raw digit, so the C index is one lower than the
                                        * asm index. Do not apply the digit at face value. */
    /* 0x9D */ uint8_t reserved_9D[0x19];  /* 0x9D..0xB5 */
    /* 0xB6 */ uint8_t spell_counts[10];   /* the SPELLS: list counts, `mov al,[di+0xB6]`,
                                        * bounds 1..9. New finding from the same read; no
                                        * other reader or writer is identified yet, so
                                        * nothing but the Z screen should touch it. */
    /* 0xC0 */ uint8_t reserved_C0[0x16];   /* 0xC0..0xD5 */
    /* 0xD6 */ uint8_t items_held[16]; /* the LIVE inventory array, one count per
                                        * item, 16 slots, so the span is 0xD6..0xE5 INCLUSIVE.
                                        * Say it in full because '16 slots from 0xD6' written as
                                        * 0xD6..0xDF sent an agent looking for the vehicle gates
                                        * in the wrong place: DS:E1/E2/E5 are inside this array
                                        * and a truncated range hides them. Slot names and
                                        * indices: docs/spice86-live-findings.md § 11 (7 ANKH,
                                        * 9 SKULL KEY, 11 BRASS BUTTON, 12 BLUE TASSLE,
                                        * 15 TRI-LITHIUM). The C2 resist gates read
                                        * slots inside it (0xD9 BOOTS, 0xDA CLOAK,
                                        * 0xE4 GREEN IDOL), asm/0FCD theft deletes
                                        * from it, and asm/363B GET increments DS:E5.
                                        * Name table at file 0x501D. */
    /* 0xE6 */ uint8_t reserved_E6[0x51];   /* 0xE6..0x136 */
    /* ------------------------------------------------------------------------
     * The creature table: six parallel 32-slot arrays, loaded from monxNN by
     * asm/22D5 (CX=0x100 = 256 bytes, which covers all six arrays at 192 bytes
     * with 64 to spare). Field order and the monx offset of each array are
     * measured, not inferred: docs/monster-spawn.md § 6 matched them live against
     * four sectors, `type` at 32/32 in all of them and `hp` at 31-32/32.
     * DI is the slot index throughout the original (`mov 0x197(%di),%al`).
     * ---------------------------------------------------------------------- */
    /* 0x137 */ uint8_t mon_x[32];    /* monx[0..31]. Spawn x; drifts in play,
                                       * because monsters move (observed live: a
                                       * Guard walked (30,14)->(36,20) while idle). */
    /* 0x157 */ uint8_t mon_y[32];    /* monx[32..63]. Spawn y. */
    /* 0x177 */ uint8_t mon_hp[32];   /* monx[64..95]. PROVEN field: asm/2E3D
                                       * subtracts the player's damage from it and
                                       * clears the record on borrow. One byte, so
                                       * 255 is the ceiling (docs/bestiary.md § 6). */
    /* 0x197 */ uint8_t mon_type[32]; /* monx[96..127]. Sprite/type byte: 0x60 Guard,
                                       * 0x68 Merchant, 0xF0 Fighter. asm/40CB compares
                                       * it against 0x6c. NOT the art index — art comes
                                       * from the map cell, docs/art-index.md. */
    /* 0x1B7 */ uint8_t mon_kind[32]; /* monx[128..159]. Kind index; asm/0AD3 dispatches
                                       * the special attacks on it. Does not hold across
                                       * every sector (docs/monster-spawn.md § 6). */
    /* 0x1D7 */ uint8_t mon_role[32]; /* monx[160..191]. Per-slot state/identity.
                                       * 0 = empty slot (asm/102B tests it for zero);
                                       * 0x81/0x82/0x83 = scripted recipients, enumerated
                                       * one by one by asm/3C2E_cmd_O (ENILNO / the RING /
                                       * a third), so bit 7 is "has a scripted response";
                                       * and 0x01 reads as "will attack the player". It is
                                       * BOTH a birth value and an acquired one: monx31 ships
                                       * 0x01 on the 0xFC and 0x2C slots, while Guards (0x60)
                                       * are born 0x03/0x00 and every guard in the sector
                                       * flips to 0x01 the moment a blow lands on anything in
                                       * town — a missed swing and 65 s of idle flip nothing
                                       * (docs/spice86-live-findings.md § 16, capture
                                       * cp5-g25-aggro; the monx31 column check is
                                       * engine/tests/monsters_check.c). Merchants are 0x02
                                       * and were never seen to attack.
                                       * Whether 0x01 is read back as the cause of attacking
                                       * is OPEN: the born-hostile kinds were out of range in
                                       * every capture so far, so their half of the reading is
                                       * inferred, not observed. Do not treat it as a cause
                                       * without a test that proves it. */
    /* 0x1F7 */ uint8_t mon_wander_x[32]; /* role-3 wander step, x component. Signed byte in
                                         * {-1,0,+1}, written by asm/10AA as sign_of_al(rand_byte)
                                         * and read back by asm/1086 into DS:19. Non-zero ONLY in
                                         * role-3 slots; the shipped monx files carry zeros here
                                         * (monx[192]), so this is runtime state, not data. NOT
                                         * persisted: it sits outside the 256-byte save block at
                                         * DS:36..0x135, so a save/load legitimately resets it. */
    /* 0x217 */ uint8_t mon_wander_y[32]; /* the y component, written by asm/10BF into DS:1A. */
    /* 0x237 */ uint8_t reserved_237[2];  /* 0x237..0x238 */
    /* 0x239 */ uint8_t story_flag;   /* "story/poem shown"; cleared at asm/0133_sub_0133.asm,
                                         set at asm/03AC_sub_03ac.asm, branched on in the
                                         same region */
    /* 0x23A */ uint8_t reserved_23A[4];
    /* 0x23E */ uint8_t text_attr;    /* INT 10h AH=09h attribute: 0x0F normal, 0x70 dim,
                                         0x02 red — asm/53D9_sub_53d9.asm,
                                         asm/53E6_sub_53e6.asm, asm/53EE_sub_53ee.asm */
    /* 0x23F */ uint8_t rng_state[6]; /* the rand_byte shift register, DS:0x23F..0x244.
                                         Seeded once at asm/0100_sub_0100.asm from the BIOS
                                         keyboard flags at 0x40:6C, then rewritten only by
                                         asm/5217_rand_byte.asm + asm/522B_sub_522b.asm.
                                         rng_state[0] is DS:0x23F, the last value produced;
                                         rng_state[1..5] are DS:0x240..0x244.
                                         NB: docs/phase4-plan.md §1 called these "tick
                                         counters"; the full decode shows PRNG state, and
                                         nothing else in the binary writes them. */
    /* 0x245 */ uint8_t reserved_245[0x25];  /* 0x245..0x269 */
    /* 0x26A */ uint8_t counter_26A;   /* NAME UNKNOWN — do not invent one. Ticks and
                                        * is zeroed exactly alongside the three paralysis
                                        * counters (asm/0C33..0C53 decrements it, asm/080D
                                        * zeroes DS:26A..26D), but it read 0x00 in every
                                        * frame of the 3000-turn fight recording and in
                                        * every live run since. Something sets it and no
                                        * observation has met that something.
                                        * docs/needs-gameplay.md G27. */
    /* 0x26B */ uint8_t paral_legs;   /* C1: legs-paralysis turns, 0..15.
                                       * Set by asm/0E70 (`and 0xf`), read live by the
                                       * movement check. Resisted by a roll on
                                       * items_held[BOOTS] (DS:D9), not by immunity. */
    /* 0x26C */ uint8_t paral_arms;   /* C1: arms-paralysis turns, 0..15 (asm/0EC2,
                                       * gate items_held[CLOAK] at DS:DA). */
    /* 0x26D */ uint8_t paral_sleep;   /* C1: sleep turns, 0..15 (asm/0F48,
                                       * gate items_held[GREEN IDOL] at DS:E4). */
    /* 0x26E */ uint8_t reserved_26E[0x204];  /* 0x26E..0x471 */
    /* 0x472 */ uint16_t map_base;    /* map table base offset, asm/50B3_map_tile_ptr.asm */
    /* 0x474 */ uint8_t reserved_474[0x0DB];
    /* 0x54F */ uint8_t left_margin;  /* column a CR returns to; 0 or 1 at the sites
                                         asm/456B_sub_456b.asm, asm/48A7_sub_48a7.asm;
                                         read by asm/500D_sub_500d.asm */
    /* 0x550 */ uint16_t tick_low;    /* INT 1Ah DX (tick low) saved by asm/06B0:
                                        * `mov %dx,0x550`. Measured 0x008D. */
    /* 0x552 */ uint16_t tick_high;   /* INT 1Ah CX, saved by `mov %cx,0x552`. Measured 0. */
    /* 0x554 */ uint16_t speed_baseline;  /* the rescaler's baseline AND the F8 floor:
                                        * asm/06ED does `sub 0x554,%ax` then `divw 0x554`,
                                        * asm/4F8E does `cmp 0x554,%ax`. MEASURED 0x068A =
                                        * 1674. No writer found anywhere in asm/ — searched
                                        * absolute forms and `(%bx)`-relative stores — so it
                                        * is initialised by code not yet decoded, or by the
                                        * loader. The value is measured, the origin is not. */
    /* 0x556 */ uint8_t reserved_556[2];
    /* 0x558 */ uint16_t speed_mult[20];  /* per-slot multipliers, read as `mulw
                                        * 0x558(%bx)` with bx = 0x26 down to 0 step -2.
                                        * MEASURED (g30-clockcal): 6, 734, 8192, 1280, 3, 3,
                                        * 3, 1280, 10, 4096, 480, 320, 576, 512, 2048, 1, 0,
                                        * 0, 0, 0 for slots 0..19. Origin unknown, as 0x554. */
    /* 0x580 */ uint16_t speed_delay[20]; /* the 20 scaled delay slots, written by
                                        * `mov %ax,0x580(%bx)` and read back by
                                        * asm/0711_speed_delay_lookup (`mov 0x580(%bx),%bx`),
                                        * which 14 routines call with bx set to a slot byte
                                        * offset. Value = ([0x74]-[0x554]) * mult / [0x554],
                                        * clamped to 0 when [0x74] < [0x554]. VERIFIED 20/20
                                        * against the live dump, engine/tests/speed_check.c.
                                        * The spread (47 .. 64581) says these are not all
                                        * millisecond delays; what each gates is OPEN. */
    /* 0x5A8 */ uint8_t reserved_5A8[0xA8];  /* 0x5A8..0x64F */
} u2_state;

/* The two packed-decimal bytes behind each status field, most-significant pair
 * first. Proven in game/status.h's table: H.P.= [0x51],[0x52], FOOD= [0x53],[0x54],
 * EXP.= [0x56],[0x57], GOLD= [0x58],[0x59].
 *
 * This exists as ONE definition because the classic status band and the enhanced
 * panel used to choose their bytes independently, and FOOD drifted: the panel read
 * [0x54],[0x55] instead of [0x53],[0x54]. Food is a six-digit triple and a step
 * costs 19 on the LOW byte, so showing that byte made every single step look like
 * it burned 19 food, when the original hides it and the displayed number only moves
 * about once every five steps. A shared accessor is the fix; a test that re-asserts
 * my reading of the same table would not have caught the next one. */
typedef enum {
    U2_BCD_HP, U2_BCD_FOOD, U2_BCD_EXP, U2_BCD_GOLD
} u2_bcd_field;

static inline void u2_state_bcd_pair(const u2_state *st, u2_bcd_field f,
                                     uint8_t *msb, uint8_t *lsb)
{
    switch (f) {
    case U2_BCD_HP:   *msb = st->hp;       *lsb = st->unk_52;  break;
    case U2_BCD_FOOD: *msb = st->food;     *lsb = st->food_hi; break;
    case U2_BCD_EXP:  *msb = st->exp_lo;   *lsb = st->exp_hi;  break;
    case U2_BCD_GOLD: *msb = st->gold_lo;  *lsb = st->gold_hi; break;
    default:          *msb = 0;            *lsb = 0;           break;
    }
}

/* One packed-decimal byte as its two digits, the way asm/55B5 renders it. The six
 * stats are stored packed decimal and printed as stored, with no conversion, so the
 * record and the screen agree.
 *
 * Shared for the same reason u2_state_bcd_pair is: the enhanced panel printed these
 * bytes with %u instead, so a STR held as 0x60 -- BCD for 60 -- came out as 96. Same
 * defect, same cause: two renderers each deciding how to read one stored format. */
static inline void u2_bcd2_str(uint8_t v, char out[3])
{
    out[0] = (char)('0' + ((v >> 4) & 0x0F));
    out[1] = (char)('0' + (v & 0x0F));
    out[2] = '\0';
}

#define U2_OFF(f) ((int)offsetof(u2_state, f))

/* Compile-time proof that the struct still matches the asm offsets. */
_Static_assert(U2_OFF(face_dx)   == 0x002, "face_dx moved");
_Static_assert(U2_OFF(face_dy)   == 0x003, "face_dy moved");
_Static_assert(U2_OFF(player_x)   == 0x023, "player_x moved");
_Static_assert(U2_OFF(player_y)   == 0x024, "player_y moved");
_Static_assert(U2_OFF(view_mode)      == 0x012, "view_mode moved");
_Static_assert(U2_OFF(player_stamp)   == 0x013, "player_stamp moved");
_Static_assert(U2_OFF(mode_a)         == 0x014, "mode_a moved");
_Static_assert(U2_OFF(mode_walk)      == 0x015, "mode_walk moved");
_Static_assert(U2_OFF(mode_b)         == 0x016, "mode_b moved");
_Static_assert(U2_OFF(mode_c)         == 0x017, "mode_c moved");
_Static_assert(U2_OFF(mode_walk_alt)  == 0x018, "mode_walk_alt moved");
_Static_assert(U2_OFF(move_scratch)   == 0x020, "move_scratch moved");
_Static_assert(U2_OFF(moved_flag)     == 0x026, "moved_flag moved");
_Static_assert(U2_OFF(mode_arg)       == 0x029, "mode_arg moved");
_Static_assert(U2_OFF(cursor_col) == 0x02E, "cursor_col moved");
_Static_assert(U2_OFF(cursor_row) == 0x02F, "cursor_row moved");
_Static_assert(U2_OFF(right_margin)== 0x031, "right_margin moved");
_Static_assert(U2_OFF(sector_second)== 0x04A, "sector_second moved");
_Static_assert(U2_OFF(sector_first)== 0x049, "sector_first moved");
_Static_assert(U2_OFF(entry_x)    == 0x05A, "entry_x moved");
_Static_assert(U2_OFF(entry_y)    == 0x05B, "entry_y moved");
_Static_assert(U2_OFF(name)       == 0x036, "name moved — it anchors the save record");
_Static_assert(sizeof(((u2_state *)0)->name) == 16, "name is 16 bytes");
_Static_assert(U2_OFF(sex)        == 0x046, "sex moved");
_Static_assert(U2_OFF(pclass)     == 0x047, "pclass moved");
_Static_assert(U2_OFF(race)       == 0x048, "race moved");
_Static_assert(U2_OFF(stat_str)   == 0x04B, "stat_str moved");
_Static_assert(U2_OFF(stat_int)   == 0x050, "stat_int moved");
_Static_assert(U2_OFF(hp)         == 0x051, "hp moved");
_Static_assert(U2_OFF(food)       == 0x053, "food moved");
_Static_assert(U2_OFF(exp_lo)     == 0x056, "exp_lo moved");
_Static_assert(U2_OFF(gold_lo)    == 0x058, "gold_lo moved");
_Static_assert(U2_OFF(spell_pick) == 0x063, "spell_pick moved");
_Static_assert(U2_OFF(readied_weapon) == 0x061, "readied_weapon moved");
_Static_assert(U2_OFF(spawn_selector) == 0x060, "spawn_selector moved");
_Static_assert(U2_OFF(worn_armour)    == 0x062, "worn_armour moved");
_Static_assert(U2_OFF(torches)        == 0x064, "torches moved");
_Static_assert(U2_OFF(rocket_parked)   == 0x069, "rocket_parked moved");
_Static_assert(U2_OFF(rocket_parked_x) == 0x06A, "rocket_parked_x moved");
_Static_assert(U2_OFF(rocket_parked_y) == 0x06B, "rocket_parked_y moved");
_Static_assert(U2_OFF(keys)           == 0x065, "keys moved");
_Static_assert(U2_OFF(tools)          == 0x066, "tools moved");
_Static_assert(U2_OFF(world)         == 0x06D, "world moved");
_Static_assert(U2_OFF(weapons_owned)  == 0x076, "weapons_owned moved");
_Static_assert(U2_OFF(items_held)     == 0x0D6, "items_held moved");
_Static_assert(U2_OFF(reserved_80)    == 0x080, "reserved_80 moved");
_Static_assert(U2_OFF(spell_counts)   == 0x0B6, "spell_counts moved");
_Static_assert(U2_OFF(paral_legs)     == 0x26B, "paral_legs moved");
_Static_assert(U2_OFF(paral_arms)     == 0x26C, "paral_arms moved");
_Static_assert(U2_OFF(paral_sleep)    == 0x26D, "paral_sleep moved");
_Static_assert(U2_OFF(counter_26A)    == 0x26A, "counter_26A moved");
/* The creature table: six parallel arrays, each exactly 32 slots, contiguous in
 * the order asm/22D5 lays them out in monx. Any drift here silently reassigns
 * every monster's x/y/hp/type/kind/role, so all six are pinned. */
_Static_assert(U2_OFF(armour_owned) == 0x097, "armour_owned moved");
_Static_assert(U2_OFF(mon_x)    == 0x137, "mon_x moved");
_Static_assert(U2_OFF(mon_y)    == 0x157, "mon_y moved");
_Static_assert(U2_OFF(mon_hp)   == 0x177, "mon_hp moved");
_Static_assert(U2_OFF(mon_type) == 0x197, "mon_type moved");
_Static_assert(U2_OFF(mon_kind) == 0x1B7, "mon_kind moved");
_Static_assert(U2_OFF(mon_role) == 0x1D7, "mon_role moved");
_Static_assert(sizeof(((u2_state *)0)->mon_x)    == 32, "mon_x is 32 slots");
_Static_assert(sizeof(((u2_state *)0)->mon_role) == 32, "mon_role is 32 slots");
_Static_assert(U2_OFF(mon_role) + 32 == 0x1F7, "mon_role must end where the wander arrays start");
_Static_assert(U2_OFF(mon_wander_x) == 0x1F7, "mon_wander_x moved");
_Static_assert(U2_OFF(mon_wander_y) == 0x217, "mon_wander_y moved");
_Static_assert(sizeof(((u2_state *)0)->mon_wander_x) == 32, "mon_wander_x is 32 slots");
_Static_assert(sizeof(((u2_state *)0)->mon_wander_y) == 32, "mon_wander_y is 32 slots");
_Static_assert(U2_OFF(mon_wander_x) + 32 == U2_OFF(mon_wander_y),
               "the two wander arrays must be adjacent 32-slot pairs");
_Static_assert(U2_OFF(reserved_237) == 0x237 && sizeof(((u2_state *)0)->reserved_237) == 2,
               "the split of the old reserved_1F7 block must still cover 0x1F7..0x238");
_Static_assert(sizeof(((u2_state *)0)->weapons_owned) == 10,
               "weapons_owned is 10 entries");
_Static_assert(sizeof(((u2_state *)0)->items_held) == 16,
               "items_held is 16 slots");
/* The struct mirrors the whole DS working segment. Naming fields must never
 * change its size; if this fires, a split lost or gained bytes. */
_Static_assert(sizeof(u2_state) == 0x650, "u2_state size changed");
/* the speed system's fields, pinned where the bytes put them */
_Static_assert(U2_OFF(speed)          == 0x074, "speed is DS:0x74");
_Static_assert(U2_OFF(tick_low)       == 0x550, "tick_low is DS:0x550");
_Static_assert(U2_OFF(tick_high)      == 0x552, "tick_high is DS:0x552");
_Static_assert(U2_OFF(speed_baseline) == 0x554, "speed_baseline is DS:0x554");
_Static_assert(U2_OFF(speed_mult)     == 0x558, "speed_mult is DS:0x558");
_Static_assert(U2_OFF(speed_delay)    == 0x580, "speed_delay is DS:0x580");
_Static_assert(sizeof(((u2_state *)0)->speed_mult)  == 40, "20 word slots");
_Static_assert(sizeof(((u2_state *)0)->speed_delay) == 40, "20 word slots");
_Static_assert(U2_OFF(story_flag) == 0x239, "story_flag moved");
_Static_assert(U2_OFF(text_attr)  == 0x23E, "text_attr moved");
_Static_assert(U2_OFF(rng_state)  == 0x23F, "rng_state moved");
_Static_assert(sizeof(((u2_state *)0)->rng_state) == 6, "rng_state is DS:0x23F..0x244");
_Static_assert(U2_OFF(map_base)   == 0x472, "map_base moved");
_Static_assert(U2_OFF(left_margin)== 0x54F, "left_margin moved");

/* Packed-decimal decode: a BCD pair is stored hundreds byte first
 * (0x99 0x97 == 9997), per captures/README.md and the display sites.
 * There is no encoder on purpose — see the note in core/state.c. */
int  u2_bcd16(uint8_t lo, uint8_t hi);

/* asm/45B8: `stc ; cmc ; sbb $0x1,%al ; das ; cmc` — subtract one from a packed
 * decimal byte so the nibble borrows in decimal: 0x10 becomes 0x09, not 0x0F.
 * Shared because torches, keys, tools, gold and every item count are BCD and
 * three modules now spend them. */
uint8_t u2_bcd_dec1(uint8_t v);

void u2_state_init(u2_state *s);

/* Resume the DS mirror from a checkpoint file: whitespace-separated hex bytes starting at
 * DS:0000. Returns 0 on success.
 *
 * The save file only carries DS:36..135 — the 256 bytes the original persists. A mid-session
 * checkpoint needs the creature arrays, the wander vectors, the RNG register and the status
 * counters too, all of which live outside the record and all of which evolve during play, so a
 * replay that resumes from the save alone starts from a different world. That is why this exists
 * separately from u2_save_load().
 *
 * Fails loudly on an empty, malformed or oversized file: a silently-partial checkpoint would fill
 * a diff with divergences caused by the loader rather than by the engine.
 */
int u2_state_load_ds(u2_state *s, const char *path);

/* Apply a checkpoint that is already in memory. Demands the whole span; see the
 * comment at the definition for why a short one is refused rather than tolerated. */
int u2_state_load_ds_buf(u2_state *s, const unsigned char *buf, size_t n);

#endif /* U2_STATE_H */
