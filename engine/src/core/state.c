/*
 * state.c — DS working segment helpers.
 * spec: asm/0100_sub_0100.asm (segment setup, tick seeding),
 *       asm/50B3_map_tile_ptr.asm, asm/51FD_str_out_core.asm,
 *       asm/5217_rand_byte.asm
 */
#include <stdio.h>
#include <string.h>

#include "core/state.h"

int u2_state_load_ds(u2_state *s, const char *path)
{
    FILE *f;
    unsigned char *base;
    size_t n = 0;
    unsigned int v;
    int got = 1;

    if (s == NULL || path == NULL) return -1;
    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "u2: cannot open checkpoint '%s'\n", path);
        return -1;
    }
    base = (unsigned char *)s;
    while ((got = fscanf(f, "%2x", &v)) == 1) {
        if (n >= sizeof(u2_state)) {
            fprintf(stderr, "u2: checkpoint '%s' is larger than the DS mirror "
                            "(%zu bytes)\n", path, sizeof(u2_state));
            fclose(f);
            return -1;
        }
        base[n++] = (unsigned char)v;
    }
    fclose(f);
    if (n == 0 || got != EOF) {
        fprintf(stderr, "u2: checkpoint '%s' is empty or not whitespace-separated hex\n", path);
        return -1;
    }
    fprintf(stderr, "u2: resumed DS from checkpoint '%s': %zu bytes at DS:0000\n", path, n);
    return 0;
}

/* The in-memory twin of u2_state_load_ds, and the reason it is separate rather than a
 * refactor of it: a demo carries its checkpoint inside a text file with its own
 * framing, its own digest and its own refusal rules (core/demo.h), so the bytes are
 * already decoded and already verified by the time this is reached. Writing them out
 * to a temporary file to reuse the path-based loader would put a demo's state on disk
 * behind the back of the digest that is supposed to prove what it was.
 *
 * The span is demanded in full, not accepted at any length. `--dump-ds` writes 0x280
 * and the demo needs 0x650 (D3), and a loader that quietly took a short buffer would
 * leave the creature arrays, the speed fields and the tick pair at whatever the
 * process happened to hold — a divergence that reads like an engine bug. Refusing is
 * the whole difference between a checkpoint and a partial one. */
int u2_state_load_ds_buf(u2_state *s, const unsigned char *buf, size_t n)
{
    if (s == NULL || buf == NULL) return -1;
    if (n != sizeof(u2_state)) {
        fprintf(stderr, "u2: checkpoint is %zu bytes but the DS mirror is %zu; "
                        "a partial checkpoint is not a checkpoint\n",
                n, sizeof(u2_state));
        return -1;
    }
    memcpy(s, buf, n);
    return 0;
}

uint8_t u2_bcd_dec1(uint8_t v)
{
    if (v == 0) return 0;
    if ((v & 0x0Fu) == 0) return (uint8_t)(v - 0x07u);
    return (uint8_t)(v - 1u);
}

/* Packed-decimal add one, with the original's MISSING ceiling guard: a count at 99 goes to
 * 00 and keeps going. Note the asymmetry with u2_bcd_dec1 above, which does clamp at zero --
 * the two are not a matched pair, because the sites they come from are not symmetric. dec1
 * serves the tool spend, which the bytes guard; inc1 serves the kill drops and the chest
 * grants (asm/2D07, asm/2CD8, asm/363B), none of which contain a compare anywhere. Adding n
 * by calling this n times is byte-equivalent to one `adc $n ; daa`, including across the
 * nibble carry and the 99 wrap; combat_check walks both forms rather than assuming it. */
uint8_t u2_bcd_inc1(uint8_t v)
{
    unsigned t = (unsigned)v + 1u;
    if ((t & 0xFu) > 9u)
        t += 6u;
    if (t > 0x99u)
        t += 0x60u;              /* and the carry falls off the end, by design */
    return (uint8_t)(t & 0xFFu);
}

/* The enhanced-mode ceiling on a packed-decimal counter, as one function rather than a
 * dozen hand-written guards, because a guard that exists in only some of the places that
 * increment is worse than no guard: it makes the wrap look intentional everywhere else.
 *
 * The original has no ceiling guard anywhere. asm/2045's `clc ; adc $1,%al ; daa` turns 99
 * into 00, asm/2DC1 drops the carry out of the gold add the same way, and asm/3C2E's Hotel
 * clerk grind is four chained DAA adds with nothing after them — which is the real reason
 * the period walkthrough says "stop at 95", since 95 + 4 = 99 is the last raise that does
 * not wrap. The owner ruled on 2026-11-11 that the enhanced game caps instead of rolling
 * over, at every counter: stats, gold, exp, food, and held counts.
 *
 * Fidelity mode keeps the wrap. It is byte-proven, and a fidelity run that silently
 * saturated would stop being a differential.
 *
 * The test is `n < v`, not `v == 0x99`: a packed-decimal add can only ever come out below
 * what it went in if it wrapped, so this holds for any valid BCD input and does not need a
 * table of ceilings. */
uint8_t u2_bcd_inc_sat(uint8_t v, int saturate)
{
    const uint8_t n = u2_bcd_inc1(v);
    if (saturate && n < v)
        return v;                 /* 99 + 1 would have rolled over to 00 */
    return n;
}

/* The same rule for a two-byte field, where the wrap is the pair going backwards rather
 * than one byte. `amount` is added with the same chained-DAA shape asm/2DC1 uses. */
uint8_t u2_bcd_add_sat(uint8_t cur, uint8_t amount, int saturate)
{
    unsigned t = (unsigned)cur + (unsigned)amount;
    if ((t & 0xFu) > 9u)
        t += 6u;
    if (t > 0x99u)
        t += 0x60u;
    const uint8_t n = (uint8_t)(t & 0xFFu);
    if (saturate && n < cur)
        return cur;
    return n;
}

/* DAA for the ADD case: the caller has already done the binary add and passes
 * the raw 16-bit result plus the carry and auxiliary-carry it produced. Kept
 * separate from overworld.c's bcd_sbb (the `das` half) so the pair reads as the
 * deliberate mirror the B1 ticket asks for. */
static uint8_t daa_add(unsigned raw, int cf, int af, int *carry_out)
{
    unsigned v = raw & 0xFFu;
    if ((v & 0x0Fu) > 9u || af) v += 6u;
    if (v > 0x9Fu || cf)        v += 0x60u;
    *carry_out = (v > 0xFFu) ? 1 : 0;
    return (uint8_t)(v & 0xFFu);
}

/*
 * Add a small amount to a two-byte packed-decimal field, low byte first, with
 * the carry out of the pair DISCARDED. asm/2DC1 (combat gold), asm/2DEE (exp)
 * and asm/363B (the dungeon chest's gold) all use this shape:
 *   al = [lo]; adc amount,al ; daa ; [lo] = al
 *   al = [hi]; adc $0,al     ; daa ; [hi] = al
 * No carry escapes the pair, which is why 9999 + 15 lands on 14 instead of
 * faulting or saturating — the B1 ticket's measured value, and the same wrap
 * seen live when a +2 exp on a 9999 character displayed 0001.
 *
 * `hi` and `lo` are the address-ordered bytes (state.h's gold_lo is DS:58, the
 * high byte), so callers pass &gold_lo, &gold_hi and the inversion stays local.
 */
void u2_bcd_adc2(uint8_t *hi, uint8_t *lo, uint8_t amount, int saturate)
{
    const uint8_t hi_was = *hi, lo_was = *lo;
    int c = 0, af = 0;
    unsigned raw = (unsigned)(*lo) + amount;
    af = (((*lo & 0x0Fu) + (amount & 0x0Fu)) > 9u) ? 1 : 0;
    *lo = daa_add(raw, raw > 0xFFu, af, &c);

    raw = (unsigned)(*hi) + (unsigned)c;
    af = (((*hi & 0x0Fu) + (unsigned)c) > 9u) ? 1 : 0;
    *hi = daa_add(raw, raw > 0xFFu, af, &c);   /* this carry is dropped on purpose */

    /* Enhanced mode stops at 9999 instead of rolling to 0001. The pair going DOWN is the
     * wrap — a BCD add that does not overflow can only raise the value — and comparing the
     * pair with the same helper on both sides keeps the byte-order inversion of state.h out
     * of the test. */
    if (saturate && u2_bcd16(*hi, *lo) < u2_bcd16(hi_was, lo_was)) {
        *hi = hi_was;
        *lo = lo_was;
    }
}

int u2_bcd16(uint8_t lo, uint8_t hi)
{
    return (((lo >> 4) * 10 + (lo & 0x0F)) * 100) +
            (((hi >> 4) * 10 + (hi & 0x0F)));
}

/*
 * There is deliberately NO u2_bcd16_encode() here.
 *
 * The one that used to be here clamped to [0, 9999] and had zero callers. The
 * original does not clamp: asm/2DC1_combat_gold_gain and asm/2DEE_combat_exp_gain
 * add with adc/daa and discard the carry out of the top byte, so a reward landed
 * on the 9999 ceiling WRAPS — 9999 + 15 -> 14, measured at turn 209 of the
 * castle-03 fight. A clamping encoder sitting in core/ is a trap for whoever
 * writes the first BCD add: it compiles, it looks reasonable, and it silently
 * deviates. Owner decision 2026-10-22 (docs/phase4-state.md § BCD tickets, B1):
 * original mode reproduces the wrap.
 *
 * When CP5 needs an add, write bcd_adc() as the deliberate mirror of bcd_sbb()
 * in game/overworld.c — discard the carry, cite the asm, and pin it with the
 * measured 9999 + 15 -> 14. Do NOT route it through an int round-trip: decoding
 * a packed-decimal pair and re-encoding it REWRITES invalid nibbles
 * (0x9A 0x99 -> 10099 -> clamped -> 0x99 0x99), and the original preserves them.
 * That is ticket B4; this comment is what keeps it true.
 */

void u2_state_init(u2_state *s)
{
    memset(s, 0, sizeof *s);
    /* The rand_byte register at DS:0x23F..0x244 is seeded by the caller
     * (u2_rng_seed, mirroring asm/0100_sub_0100.asm), not here. */
    s->text_attr = 0x0F;      /* asm/53D9_sub_53d9.asm: movb $0xf,0x23e */
    s->right_margin = 0x28;   /* asm/4A2F_sub_4a2f.asm: movb $0x28,0x31 */
    s->left_margin = 0x00;    /* asm/48A7_sub_48a7.asm: movb $0x0,0x54f */
}
