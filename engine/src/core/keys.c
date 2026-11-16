/* See core/keys.h. Extracted from platform/input_script.c so the play API and the
 * script backend cannot drift apart; the table and the accepted forms are unchanged. */
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/keys.h"

int u2_key_from_token(const char *tok, u2_key *k)
{
    static const struct { const char *name; int ch; } table[] = {
        { "SP", ' ' }, { "CR", '\r' }, { "LF", '\n' }, { "TAB", '\t' },
        { "ESC", 0x1b }, { "BS", 0x08 }, { "NUL", 0x00 }, { "DEL", 0x7f },
    };

    if (!tok || !k || !*tok) return 0;
    *k = (u2_key){ 0, 0 };

    if (strcmp(tok, "WAIT") == 0) {
        k->ch = 0xFF;                 /* sentinel: "no key this poll" */
        return 1;
    }
    /* Arrows are not bytes, and naming the byte is a trap. The original translates them
     * through a scancode table at CS:0x4F09, so the engine carries a scancode alongside the
     * key exactly as input_sdl.c does for a real keyboard. Writing 0x0e in a script does NOT
     * press Up: seam_key_poll deliberately swallows 0x05/0x0E/0x17 because asm/4F58..4F60
     * does, so the byte form is a key the game is written to discard. That is how this gap
     * was found -- a script that appeared to move nobody. */
    static const struct { const char *name; int ch; int scan; } arrows[] = {
        { "UP",    U2_KEY_UP,    0x48 },
        { "DOWN",  U2_KEY_DOWN,  0x50 },
        { "LEFT",  U2_KEY_LEFT,  0x4B },
        { "RIGHT", U2_KEY_RIGHT, 0x4D },
    };
    for (size_t j = 0; j < sizeof arrows / sizeof arrows[0]; j++) {
        if (strcasecmp(tok, arrows[j].name) == 0) {
            k->ch   = (uint8_t)arrows[j].ch;
            k->scan = (uint8_t)arrows[j].scan;
            return 1;
        }
    }
    for (size_t j = 0; j < sizeof table / sizeof table[0]; j++) {
        if (strcasecmp(tok, table[j].name) == 0) {
            k->ch = (uint8_t)table[j].ch;
            return 1;
        }
    }
    if (strncmp(tok, "0x", 2) == 0 || strncmp(tok, "0X", 2) == 0) {
        char *end = NULL;
        const long v = strtol(tok, &end, 16);
        if (!end || *end) return 0;   /* "0xZZ" is an unknown token, not byte 0 */
        k->ch = (uint8_t)(v & 0xFF);
        return 1;
    }
    if (strlen(tok) == 1) {
        k->ch = (uint8_t)(unsigned char)tok[0];
        return 1;
    }
    return 0;
}
