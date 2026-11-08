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
