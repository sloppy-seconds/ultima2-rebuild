#ifndef U2_CREATION_H
#define U2_CREATION_H

/*
 * Character creation.
 * spec: asm/5400_sub_5400.asm (file 0x5600..0x5C66) — read via
 *       disasm/redecoded/5400_sub_5400.txt, our decode of the region Phase 3
 *       left as byte-proven but unreviewed db blobs.
 *
 * Shape of the flow, all of it proven by the code rather than assumed:
 *
 *   - Stats are BOUGHT, not rolled. asm/53B0 reads two typed digits and packs
 *     them as nibbles ((d1<<4)|d2); DS:29 holds a 90-point budget in the same
 *     packed form, set at file 0x56E6. asm/5C66 rejects a stat below 0x0a and
 *     rejects an overspend, and both rejections jump to 0x5698 — the whole
 *     generation restarts. That is harsh, and it is what the bytes do.
 *   - Sex, race and class each apply one BCD bonus to one stat (see the table
 *     in creation.c). STAMINA gets none from any of them.
 *   - Race and class are both converted with `sub $0x31` to 0..3; sex is stored
 *     as the typed 'M'/'F'.
 *   - After the record is written the original prints INSERT PROGRAM MASTER
 *     and restarts itself, because the player had to swap disks. The engine has
 *     no disk to swap, so it returns to the title menu instead. That is a
 *     documented deviation, not an oversight.
 *
 * Returns 1 when a character was created, 0 when the player bailed or input ran
 * out, and -1 when the save could not be written.
 */

#include "core/ctx.h"

int u2_creation_flow(u2_ctx *ctx);

/*
 * Declarative character creation, for the play API (docs/play-api.md A4, A5).
 *
 * The reason this exists next to u2_creation_flow() rather than instead of it: the form's
 * cursor -- which field is highlighted, which dropdown is open -- lives in a local struct and
 * NOT in the DS mirror. That is measured, not assumed (docs/demo-record-playback.md
 * deviation 5, which is also why recording is refused mid-form). A keystroke-level API would
 * therefore have to reimplement the form to know what it currently holds, and a second
 * hand-maintained copy of the form is exactly the thing this repo keeps being bitten by.
 *
 * So this builds a `form` and calls the same commit() the interactive flow calls. One source
 * for the BCD bonuses, the `sub $0x31` race/class conversions, the entry cell, and the
 * starting HP/food/gold. The API adds no rules and cannot drift from the form's.
 *
 * Validation is the form's own, from the same constants: each stat >= STAT_MIN, the six
 * stats within the 90-point budget, the name within the 12-byte input cap, and sex/race/class
 * inside the dropdown tables. A declarative API that accepted any stat would be a cheat
 * engine, and every digest taken over a state the game cannot reach would be evidence about
 * nothing.
 *
 * NOT modelled, deliberately: the original restarts the whole generation on an overspend or a
 * stat below the floor (asm/5C66 -> 0x5698). The interactive form already deviates by
 * refusing instead, so the API follows the form. `why` names the rule that refused.
 *
 * Returns 0 on success and writes the record the same way the form's [S]AVE does; -1 when
 * refused (with `why` filled) or when the save could not be written.
 */
typedef struct u2_character {
    const char *name;      /* <= 12 characters, asm/5BB7 */
    int sex;               /* 'M' or 'F' */
    int race;              /* 1 human, 2 elf, 3 dwarf, 4 hobbit */
    int cls;               /* 1 fighter, 2 cleric, 3 wizard, 4 thief */
    int stat[6];           /* decimal, in commit() order: STR AGI STA CHA WIS INT */
} u2_character;

int u2_creation_apply(u2_ctx *ctx, const u2_character *c, char *why, int whysize);

#endif /* U2_CREATION_H */
