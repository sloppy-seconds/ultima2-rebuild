#ifndef U2_KEYS_H
#define U2_KEYS_H

/*
 * The key alphabet — one place, shared by every non-human input backend.
 *
 * `--script` files, the play API's `{"key":"..."}` field and anything added later all
 * spell keys the same way, and they must not each carry their own table: a second copy of
 * an alphabet is how `CR` means one thing in a script and another thing in an API call,
 * which is a bug that shows up as the game ignoring input rather than as an error.
 *
 * Accepted forms, exactly the ones input_script.c has always accepted:
 *   A               a single printable character
 *   SP CR LF TAB ESC BS NUL DEL
 *   UP DOWN LEFT RIGHT   the arrows, as arrows (scancode carried, not a control byte)
 *   0x41            explicit byte
 *   WAIT            "no key this poll" (ch = 0xFF sentinel)
 *
 * Arrows are named, not spelled as bytes: asm/4F58..4F60 makes the game discard three of the
 * four direction control bytes, so 0x0e is not Up, it is a key the game throws away.
 */

#include "core/ctx.h"

/* Fills *k and returns 1 on success, 0 if the token is not in the alphabet. */
int u2_key_from_token(const char *tok, u2_key *k);

#endif /* U2_KEYS_H */
