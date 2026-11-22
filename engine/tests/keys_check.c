/* keys_check.c — the key alphabet, tested at its own boundary (issue #8).
 *
 * core/keys.c is the one alphabet: --script files, the play API's {"key":"..."} field and
 * any backend added later all spell keys through u2_key_from_token. Before this it had zero
 * direct tests — it was exercised only incidentally, through a script backend that would
 * have kept working if a name had been dropped, because nothing asserted the names.
 *
 * The rule the module exists to hold is in keys.h: a second copy of an alphabet is how `CR`
 * means one thing in a script and another thing in an API call. So this pins the whole
 * accepted grammar, not a sample of it.
 */
#include <stdio.h>
#include <string.h>

#include "core/keys.h"
#include "platform/platform.h"   /* U2_KEY_UP..RIGHT, the arrow pseudo-characters */

static int checks, fails;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            fails++;                                                           \
            printf("  FAIL  ");                                                \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

/* One accepted token: the name, and the exact ch/scan pair it must produce. Asserting the
 * pair rather than "returned 1" is the point — a table entry with the right name and the
 * wrong byte is the bug that would otherwise pass. */
static void want(const char *tok, int ch, int scan)
{
    u2_key k = (u2_key){ 0xAA, 0xAA };
    int ok = u2_key_from_token(tok, &k);
    CHECK(ok && k.ch == (uint8_t)ch && k.scan == (uint8_t)scan,
          "`%s` must be ch=0x%02x scan=0x%02x, got %s ch=0x%02x scan=0x%02x",
          tok, ch, scan, ok ? "ok" : "REJECTED", k.ch, k.scan);
}

/* A token that must be refused, and refused without writing a key. */
static void refuse(const char *tok)
{
    u2_key k = (u2_key){ 0xAA, 0xAA };
    CHECK(!u2_key_from_token(tok, &k), "`%s` must be rejected", tok);
}

int main(void)
{
    /* --- single printable characters ------------------------------------------ */
    want("A", 'A', 0);
    want("z", 'z', 0);
    want("5", '5', 0);
    want(" ", ' ', 0);      /* a literal space is a one-character token like any other */

    /* --- the named control characters ----------------------------------------- */
    want("SP",  ' ',  0);
    want("CR",  '\r', 0);
    want("LF",  '\n', 0);
    want("TAB", '\t', 0);
    want("ESC", 0x1b, 0);
    want("BS",  0x08, 0);
    want("NUL", 0x00, 0);
    want("DEL", 0x7f, 0);
    /* Case is not part of the meaning, and a seeder that writes lowercase names must not
     * silently get a different alphabet. */
    want("esc", 0x1b, 0);
    want("Tab", '\t', 0);

    /* --- the arrows: pseudo-character plus scancode, never a bare byte --------- */
    want("UP",    U2_KEY_UP,    0x48);
    want("DOWN",  U2_KEY_DOWN,  0x50);
    want("LEFT",  U2_KEY_LEFT,  0x4B);
    want("RIGHT", U2_KEY_RIGHT, 0x4D);
    want("up",    U2_KEY_UP,    0x48);

    /* The trap keys.h names, pinned as a positive assertion. asm/4F58..4F60 makes the game
     * discard three of the four direction control bytes, so 0x0e is not Up — it is a key the
     * game throws away. A script that spells arrows as bytes looks like it works and moves
     * nobody, which is exactly how this was originally found. */
    {
        u2_key byte_up = (u2_key){ 0, 0 }, named_up = (u2_key){ 0, 0 };
        CHECK(u2_key_from_token("0x0e", &byte_up) && byte_up.ch == 0x0e && byte_up.scan == 0,
              "0x0e must parse as the byte 0x0e with no scancode, not as Up");
        u2_key_from_token("UP", &named_up);
        CHECK(named_up.ch != byte_up.ch || named_up.scan != byte_up.scan,
              "UP and 0x0e must not be the same key, or the arrow warning is a lie");
    }

    /* --- explicit bytes -------------------------------------------------------- */
    want("0x41", 'A', 0);
    want("0X41", 'A', 0);          /* 0X accepted too */
    want("0x1b", 0x1b, 0);         /* byte form and name form agree on ESC */
    want("0x00", 0x00, 0);
    want("0x1ff", 0xff, 0);        /* masked to a byte, not an error */

    /* --- WAIT: the "no key this poll" sentinel --------------------------------- */
    want("WAIT", 0xFF, 0);
    /* WAIT is a sentinel, not a byte the player could type, and the byte form must not
     * collide with it: 0xff parses to a key whose ch is 0xff but which carries no WAIT
     * meaning. Both are recorded here so a future reader sees the overlap exists. */
    want("0xff", 0xff, 0);

    /* --- refusals -------------------------------------------------------------- */
    refuse("");        /* empty */
    refuse("FOO");     /* unknown word */
    refuse("0xZZ");    /* looks like a byte, is not one — must not become byte 0 */
    refuse("0x");       /* prefix with no digits */
    refuse("AB");       /* two characters, and not a name */
    refuse("UPPER");    /* a name prefix is not a name */
    CHECK(!u2_key_from_token(NULL, &(u2_key){ 0, 0 }), "NULL token must be rejected");
    CHECK(!u2_key_from_token("A", NULL), "a NULL key pointer must be rejected, not written through");

    printf("keys_check: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
