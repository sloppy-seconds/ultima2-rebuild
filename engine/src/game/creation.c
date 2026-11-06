/*
 * creation.c — character generation, the form, and the save.
 *
 * spec: asm/5400_sub_5400.asm, file 0x5600..0x5C66. Citations are FILE offsets
 * into original/ultimaii.exe, readable in disasm/redecoded/5400_sub_5400.txt.
 *
 * What is reproduced exactly (rules and state):
 *   - the form layout and every string on it, from the extracted content header
 *   - stats bought from a 90-point budget, each stat >= 0x0a (asm/5C69). The
 *     original reads two typed digits packed as nibbles (asm/53B0); the engine
 *     keeps them as decimal 0..99 and packs to BCD only at commit, which is the
 *     same value in a different representation.
 *   - the ten BCD bonuses for sex / race / class, and that STAMINA gets none
 *   - race and class stored 0..3 after `sub $0x31`; sex stored as the letter
 *   - starting values HP=4 FOOD=4 GOLD=4 and sector_first=2, entry_x=entry_y=20
 *   - the 256-byte record written to a byte-compatible PLAYER file
 *
 * What is deliberately improved (docs/phase4-plan.md § 4a, deviations D1-D4):
 *   D1 Up/Down move between fields and any field can be re-edited. The original
 *      is strictly sequential (asm/53B0 reads, stores, moves on).
 *   D2 Backspace deletes one character. The original restarts the whole name
 *      field (asm/5BA5).
 *   D3 An illegal entry is refused in place. The original discards the entire
 *      generation and jumps back to asm/5698.
 *   D4 The cursor is shown on the field being edited. The original shows no
 *      cursor, so there is no indication which field is active. While a dropdown
 *      is open it marks the highlighted row instead (D12).
 * D6 RETIRED 2026-11-06, replaced by Left/Right stepping. It was a two-digit
 *    shift register: typing 5 read 05, a following 0 read 50. The original takes
 *    the first digit as the tens, so typing 5 means 50 and immediately moves on
 *    (asm/53B0) — which reads as a bug to anyone who meant 5.
 * D7 RETIRED 2026-11-06, unreachable under stepping. It allowed the budget to go
 *    negative mid-edit. Stepping is clamped to [STAT_MIN, STAT_MIN + remaining],
 *    so the illegal state cannot be entered at all, which is a stronger guarantee
 *    than tolerating it and refusing to commit. The original refuses an overspend
 *    by throwing the whole generation away (asm/5C66 -> 0x5698).
 * D8 Control characters are refused in the name. The original masks the high bit
 *    off every typed byte (asm/5207 `and al,$0x7f`) and then stores whatever is
 *    left, so a control character really can land in the name field. The mask is
 *    reproduced; the control characters are dropped because they render as
 *    garbage and cannot be typed usefully.
 * D9 The confirm line reads [S]AVE / [R]ESET instead of SATISFACTORY (Y/N), and
 *    it is a pair of fields you Tab to and press Enter on. The original asks
 *    "SATISFACTORY (Y/N)-" (asm/5B6E, in the form blob) and restarts the whole
 *    generation from scratch on N (asm/5BD5 -> 0x5698); "not satisfactory" is
 *    not an instruction a player can guess and does not say what happens next.
 *    These are deliberately not hotkeys: as r/R shortcuts they fired the moment
 *    anyone typed the name BRITISH. This is the one place the engine overwrites
 *    an extracted game string, so it paints over row 16 rather than editing the
 *    blob — the blob stays faithful to the binary and the override is visible as
 *    engine text.
 * D12 Sex, Race and Type open a dropdown: Up/Down move a `>` highlight, Enter
 *    commits, Esc cancels. The original types a digit against a static
 *    `RACES: / TYPES:` legend the player has to memorise; the FM Towns version
 *    keys off letters and had to rename Hobbit to `B)obbit` because H was taken
 *    by Human. Neither is available to us without that collision, and the
 *    collision is the reason for the dropdown. The legend is painted over the way
 *    D9 paints over the confirm line: the blob stays byte-faithful and the
 *    override is engine presentation.
 * D13 The engine prints the creation bonuses beside the stats as `+N`. The
 *    original never shows them anywhere; they exist only as bytes in asm/5400.
 *    While a dropdown is open the preview tracks the highlight, so the number is
 *    what the highlighted choice would do, not only what is committed. There is
 *    no negative form and none is planned: all ten bonuses are additions, and
 *    with a 90 budget over six stats floored at 10 the largest any stat can
 *    reach is 40, so the unguarded `daa` cannot wrap here. Colour is deliberately
 *    not part of this round — the delta prints in the same ink as everything else
 *    and the `+` carries the meaning on its own.
 * D10 Every stat starts at 10, the minimum the original enforces, so the player
 *    distributes the 30 points left over rather than building all 90 from zero.
 *    Same solution set — six stats of at least 10 summing to 90 — but an illegal
 *    value now needs deliberate effort instead of a careless keystroke. The
 *    original starts at 0 and throws the generation away when a stat is under 10
 *    (asm/5C69 `cmp al,$0xa; jb` -> asm/5C96).
 * None of these change what is achievable or what lands in DS or on disk.
 *
 * All display text comes from the generated content header; nothing here is a
 * hardcoded game string. Engine-authored text is marked "[engine]".
 */

#include <string.h>
#include "game/creation.h"
#include "core/save.h"
#include "core/seam.h"
#include "gen_content.h"

#define STAT_COL       26      /* asm/58B7: every stat value at column 26 */
#define STAT_ROW_FIRST 4       /* rows 4..9, one per stat (asm/58B0 etc.) */
#define PROMPT_COL     20
#define PROMPT_ROW     11      /* M/F row 11, RACE 12, TYPE 13, NAME 14 */
#define BUDGET_ROW     2
#define BUDGET_COL     33      /* asm/5C7D: remaining points printed here */
#define BUDGET_START   0x90    /* packed 90, asm/56E6 */
#define STAT_MIN       0x0A    /* asm/5C69: below this the original restarts */
#define NAME_MAX       12      /* asm/5BB7: `cmp bl,$0xc; jb` caps input at 12.
                                * The field itself is 16 bytes ([0x36..0x45]) so
                                * a full name leaves 4 zero pad bytes, which is
                                * what the original writes and what the save
                                * comparison expects. Typing more is impossible
                                * in the original (it just ends the field), and
                                * 13..16 would be a name the original can never
                                * produce, so the cap is a rule, not UX. */
#define CONFIRM_ROW    16      /* the blob's SATISFACTORY line, overwritten (D9) */
#define CONFIRM_COL    10      /* the original indents that line by 10 spaces */
#define CONFIRM_TEXT   "[S]AVE   [R]ESET"
#define LEGEND_ROW     18      /* asm blob's RACES:/TYPES: legend; painted over (D12) */
#define LEGEND_ROWS    5       /* rows 18..22: the legend plus its blank leader */
#define NOTE_ROW       24      /* engine-authored status line */
#define HINT_MINUS_COL 25      /* `-` tight left of a stat value: Left takes one */
#define HINT_PLUS_COL  28      /* `+` tight right of it: Right adds one */
#define BONUS_COL      30      /* the bonus slot, blank until a choice adds (D13) */
#define BONUS_W        3       /* widest is `+25`: two digits and a sign */
#define DD_TOP         17      /* dropdown box: top border row (D12) */
#define DD_LEFT        13
#define DD_W           14      /* box width in cells, borders included */

typedef enum { F_STAT, F_SEX, F_RACE, F_CLASS, F_NAME, F_SAVE, F_RESET } field_kind;

typedef struct {
    field_kind kind;
    int row, col;
    int index;                 /* stat index for F_STAT */
} field;

/* Order is the order the original asks in, so Tab walks the form the same way.
 * SAVE and RESET are the last two stops: they are fields you navigate to and
 * press Enter on, not hotkeys. They started life as r/R shortcuts and that
 * failed the first time anyone typed the name BRITISH. */
static const field FIELDS[] = {
    { F_STAT,  4, STAT_COL, 0 }, { F_STAT,  5, STAT_COL, 1 },
    { F_STAT,  6, STAT_COL, 2 }, { F_STAT,  7, STAT_COL, 3 },
    { F_STAT,  8, STAT_COL, 4 }, { F_STAT,  9, STAT_COL, 5 },
    { F_SEX,   11, PROMPT_COL, -1 }, { F_RACE,  12, PROMPT_COL, -1 },
    { F_CLASS, 13, PROMPT_COL, -1 }, { F_NAME,  14, PROMPT_COL, -1 },
    { F_SAVE,  CONFIRM_ROW, CONFIRM_COL, -1 },
    { F_RESET, CONFIRM_ROW, CONFIRM_COL + 9, -1 },   /* "[S]AVE   " is 9 cols */
};
#define NFIELDS ((int)(sizeof FIELDS / sizeof FIELDS[0]))

/* The BCD bonuses. Each is `mov al,[stat]; add al,$NN; daa; mov [stat],al`:
 *   MALE +05 STR (0x5960)     HUMAN  +05 INT (0x59CF)   FIGHTER +15 STR (0x5AA6)
 *   FEMALE +10 CHA (0x598F)   ELF    +05 AGI (0x59FB)   CLERIC  +10 WIS (0x5AD5)
 *                               DWARF  +05 STR (0x5A29)  WIZARD  +10 INT (0x5B04)
 *                               HOBBIT +10 WIS (0x5A5B)  THIEF   +10 AGI (0x5B35)
 * STAMINA is never modified by sex, race or class. */

/* The same ten bonuses as data, so the preview (D13) and commit() cannot drift
 * apart. Stat index order is the order commit() uses:
 * 0 STR, 1 AGI, 2 STA, 3 CHA, 4 WIS, 5 INT. */
typedef struct { int stat; int bcd; } bonus;
static const bonus BONUS_SEX[2]   = { { 0, 0x05 }, { 3, 0x10 } };
static const bonus BONUS_RACE[4]  = { { 5, 0x05 }, { 1, 0x05 }, { 0, 0x05 }, { 4, 0x10 } };
static const bonus BONUS_CLASS[4] = { { 0, 0x15 }, { 4, 0x10 }, { 5, 0x10 }, { 1, 0x10 } };

/* Dropdown contents (D12). Every label is an extracted game string, so the box
 * invents no text; only the title, the `>` marker and the border are ours. */
typedef struct { const char *label; int key; } choice_opt;
static const choice_opt OPT_SEX[] = {
    { U2_S_SEX_MALE,    'M' }, { U2_S_SEX_FEMALE,  'F' },
};
static const choice_opt OPT_RACE[] = {
    { U2_S_RACE_HUMAN,  '1' }, { U2_S_RACE_ELF,    '2' },
    { U2_S_RACE_DWARF,  '3' }, { U2_S_RACE_HOBBIT, '4' },
};
static const choice_opt OPT_CLASS[] = {
    { U2_S_CLASS_FIGHTER, '1' }, { U2_S_CLASS_CLERIC, '2' },
    { U2_S_CLASS_WIZARD,  '3' }, { U2_S_CLASS_THIEF,  '4' },
};

static const choice_opt *opts_for(field_kind k, int *n)
{
    switch (k) {
    case F_SEX:   *n = (int)(sizeof OPT_SEX   / sizeof OPT_SEX[0]);   return OPT_SEX;
    case F_RACE:  *n = (int)(sizeof OPT_RACE  / sizeof OPT_RACE[0]);  return OPT_RACE;
    case F_CLASS: *n = (int)(sizeof OPT_CLASS / sizeof OPT_CLASS[0]); return OPT_CLASS;
    default:      *n = 0; return NULL;
    }
}

/* Box titles are engine-authored: the DOS blob carries the prompts only inside
 * U2_S_CREATE_FORM, as `M/F-`, `RACE-` and `TYPE-`, so there is no extracted
 * string that is just the word. Marked per docs/char-creation-ui-plan.md. */
static const char *dd_title(field_kind k)
{
    switch (k) {
    case F_SEX:   return "Sex";
    case F_RACE:  return "Race";
    case F_CLASS: return "Type";
    default:      return "";
    }
}

typedef struct {
    int     val[6];           /* decimal, floored at STAT_MIN and capped by the budget */
    int     sex;              /* 'M' / 'F' */
    int     race;             /* '1'..'4' */
    int     cls;              /* '1'..'4' */
    char    name[NAME_MAX];
    int     name_len;
    int     sel;              /* index into FIELDS */
    int     dd;               /* FIELDS index with a dropdown open, or -1 (D12) */
    int     dd_hi;            /* highlighted row inside that dropdown */
    char    note[48];         /* engine-authored status line */
} form;

static int pack_bcd(int d) { return ((d / 10) << 4) | (d % 10); }
static int unpack_bcd(int p) { return (p >> 4) * 10 + (p & 0x0F); }

static int bcd_add(int a, int b)
{
    int v = (a & 0x0F) + (b & 0x0F);
    int t = ((a >> 4) & 0x0F) + ((b >> 4) & 0x0F) + (v / 10);
    return (uint8_t)(((t % 16) << 4) | (v % 10));
}

static int spent(const form *f)
{
    int total = 0;
    for (int i = 0; i < 6; i++) total += f->val[i];
    return total;
}

static int remaining(const form *f)
{
    /* step_stat is the only writer of a stat and it refuses a press that would
     * take this below zero, so the total is always 00..90. */
    return unpack_bcd(BUDGET_START) - spent(f);
}

/* ---- low-level I/O ------------------------------------------------------ */

static int wait_key(u2_ctx *ctx)
{
    u2_key k;
    if (!seam_key_wait(ctx, &k)) return -1;   /* input exhausted: the player left */
    if (k.ch == 0x11) {                       /* Ctrl-Q quits from anywhere (D5) */
        ctx->exit_code = 0;
        return -1;
    }
    return k.ch;
}

static void print_pair(u2_ctx *ctx, int v)
{
    char b[3];
    b[0] = (char)('0' + ((v >> 4) & 0x0F));
    b[1] = (char)('0' + (v & 0x0F));
    b[2] = '\0';
    seam_str_out(ctx, b);
}

/* The remaining total as the original prints it: two decimal digits in the value
 * slot of the POINTS line. Stepping keeps it in 00..90, so there is no sign case
 * to format; an earlier version carried one for D7, and D7 is retired. */
static void print_budget(u2_ctx *ctx, int v)
{
    print_pair(ctx, pack_bcd(v));
}

/* ---- drawing ------------------------------------------------------------ */

/* Mark a span of cells inverse. Presentation only: the game's own attribute byte
 * at DS:0x23E is never touched, so this cannot change what the original would have
 * drawn anywhere else. See U2_ATTR_HILITE in core/vram.h for the bit and why
 * borrowing it is safe. */
static void hilite(u2_ctx *ctx, int row, int col, int len)
{
    for (int i = 0; i < len; i++) {
        int c = col + i;
        if (row < 0 || row >= U2_ROWS || c < 0 || c >= U2_COLS) continue;
        ctx->vram.cell[row][c].attr |= U2_ATTR_HILITE;
    }
    ctx->vram.dirty = 1;
}

/* What an unfilled choice field shows. The original shows a bare dash, which says
 * the field is empty and nothing about how to fill it; ENTER says both. Engine
 * text, not extracted. It is padded to the widest value it can be replaced by
 * (FIGHTER, 7 cells) so the box border under it never shifts when a choice lands.
 */
#define BLANK_TEXT  "ENTER  "
#define CONFIRM_W   6                 /* "[S]AVE" and "[R]ESET" are both 6 cells */

/* One value slot on the form, highlighted as a whole when the cursor is on it and
 * no dropdown is open. While a dropdown is open the box owns the highlight. */
static void draw_value(u2_ctx *ctx, const form *f, int which, int col, const char *word)
{
    const field *fd = &FIELDS[which];
    seam_str_out_at(ctx, col, fd->row, word);
    if (which == f->sel && f->dd < 0) {
        /* Highlight the word, not the padding that keeps the column width steady. */
        int n = (int)strlen(word);
        while (n > 0 && word[n - 1] == ' ') n--;
        hilite(ctx, fd->row, col, n);
    }
}

static void draw_field(u2_ctx *ctx, form *f, int which)
{
    const field *fd = &FIELDS[which];
    switch (fd->kind) {
    case F_STAT:
        seam_goto(ctx, fd->col, fd->row);
        print_pair(ctx, pack_bcd(f->val[fd->index]));
        if (which == f->sel && f->dd < 0)
            hilite(ctx, fd->row, fd->col, 2);
        break;
    case F_SEX:
        draw_value(ctx, f, which, fd->col + 5,
                   f->sex ? (f->sex == 'M' ? U2_S_SEX_MALE : U2_S_SEX_FEMALE)
                          : BLANK_TEXT);
        break;
    case F_RACE:
        draw_value(ctx, f, which, fd->col + 6,
                   f->race ? (f->race == '1' ? U2_S_RACE_HUMAN
                            : f->race == '2' ? U2_S_RACE_ELF
                            : f->race == '3' ? U2_S_RACE_DWARF
                                              : U2_S_RACE_HOBBIT) : BLANK_TEXT);
        break;
    case F_CLASS:
        draw_value(ctx, f, which, fd->col + 6,
                   f->cls ? (f->cls == '1' ? U2_S_CLASS_FIGHTER
                           : f->cls == '2' ? U2_S_CLASS_CLERIC
                           : f->cls == '3' ? U2_S_CLASS_WIZARD
                                            : U2_S_CLASS_THIEF) : BLANK_TEXT);
        break;
    case F_NAME: {
        seam_goto(ctx, fd->col + 6, fd->row);
        char b[NAME_MAX + 1];
        memcpy(b, f->name, (size_t)f->name_len);
        b[f->name_len] = '\0';
        seam_str_out(ctx, b);
        /* Pad the field with the bare primitive, not char_print: the cell is already
         * inside the margin and the field must not wrap or sync the cursor. */
        for (int i = f->name_len; i < NAME_MAX; i++) seam_char_out(ctx, ' ');
        break;
    }
    case F_SAVE:
    case F_RESET:
        break;                          /* static labels, nothing to redraw */
    }
}

/* ---- the bonus preview (D13) --------------------------------------------- */

/* The delta the current choices apply to each stat. While a dropdown is open the
 * highlighted row is substituted into its field, so the numbers describe the
 * choice being looked at and not only the one already made. Every entry is a
 * sum of additions: there is no negative case, because all ten bonuses add and
 * a stat cannot exceed 40 under the budget, so the unguarded `daa` cannot wrap. */
static void bonus_delta(const form *f, int out[6])
{
    for (int i = 0; i < 6; i++) out[i] = 0;
    int sex = f->sex, race = f->race, cls = f->cls;
    if (f->dd >= 0) {
        int n = 0;
        const choice_opt *o = opts_for(FIELDS[f->dd].kind, &n);
        if (o && f->dd_hi >= 0 && f->dd_hi < n) {
            field_kind k = FIELDS[f->dd].kind;
            if (k == F_SEX)   sex = o[f->dd_hi].key;
            if (k == F_RACE)  race = o[f->dd_hi].key;
            if (k == F_CLASS) cls = o[f->dd_hi].key;
        }
    }
    const bonus *b;
    if (sex)  { b = &BONUS_SEX[sex == 'M' ? 0 : 1]; out[b->stat] += unpack_bcd(b->bcd); }
    if (race) { b = &BONUS_RACE[race - '1'];        out[b->stat] += unpack_bcd(b->bcd); }
    if (cls)  { b = &BONUS_CLASS[cls - '1'];        out[b->stat] += unpack_bcd(b->bcd); }
}

/* The `-` / `+` tight against a stat value, so the three cells read as one
 * control and not three unrelated glyphs, and the bonus (D13) in its own column
 * to the right: `STRENGTH..... -10+` becomes `STRENGTH..... -10+ +20` once a
 * choice adds fifteen to it. */
static void draw_stat_hints(u2_ctx *ctx, const form *f)
{
    int d[6];
    bonus_delta(f, d);
    for (int i = 0; i < 6; i++) {
        int row = STAT_ROW_FIRST + i;
        /* The blob's leader dots run to col 25 and the value starts at 26, so the
         * hint takes col 25 and a space takes 24. The value column itself is the
         * byte-proven one and does not move. */
        seam_goto(ctx, HINT_MINUS_COL - 1, row);
        seam_char_out(ctx, ' ');
        seam_char_out(ctx, '-');
        seam_goto(ctx, HINT_PLUS_COL, row);
        seam_char_out(ctx, '+');
        int v = d[i] > 99 ? 99 : d[i];   /* three bonuses cap at 25; a format bound */
        char b[BONUS_W + 1];
        if (v > 0) snprintf(b, sizeof b, "+%d", v);
        else b[0] = '\0';
        seam_goto(ctx, BONUS_COL, row);
        int j = 0;
        for (; b[j] && j < BONUS_W; j++) seam_char_out(ctx, (uint8_t)b[j]);
        for (; j < BONUS_W; j++) seam_char_out(ctx, ' ');
    }
}

/* ---- the dropdown (D12) -------------------------------------------------- */

/* ASCII borders, not box-drawing glyphs: u2_font_draw_cell renders anything
 * outside 32..126 as a space, so the box has to be built from characters every
 * backend can draw. Monochrome on purpose — the highlight is the `>` marker, so
 * it survives a terminal with colour off and a theme whose palette has nothing
 * spare. */
static void draw_box_row(u2_ctx *ctx, int row, const char *text, int mark, int centre)
{
    char b[DD_W + 1];
    int inner = DD_W - 4;                 /* "| " + marker + text + pad + " |" */
    int w = (int)strlen(text);
    if (w > inner) w = inner;
    int pad = centre ? (inner - w) / 2 : 0;
    b[0] = '|'; b[1] = ' '; b[2] = (char)(mark ? '>' : ' ');
    int i = 3;
    for (int j = 0; j < pad; j++) b[i++] = ' ';
    for (int j = 0; j < w; j++) b[i++] = text[j];
    while (i < DD_W - 1) b[i++] = ' ';
    b[DD_W - 1] = '|';
    b[DD_W] = '\0';
    seam_str_out_at(ctx, DD_LEFT, row, b);
    /* The whole word, not a caret beside it: an inverted block is the same idiom
     * the form fields use, so the box and the form say "this one" the same way. */
    if (mark) hilite(ctx, row, DD_LEFT + 3, w);
}

static void draw_box_border(u2_ctx *ctx, int row)
{
    char b[DD_W + 1];
    b[0] = '+';
    for (int i = 1; i < DD_W - 1; i++) b[i] = '-';
    b[DD_W - 1] = '+';
    b[DD_W] = '\0';
    seam_str_out_at(ctx, DD_LEFT, row, b);
}

static void draw_dropdown(u2_ctx *ctx, const form *f)
{
    if (f->dd < 0) return;
    int n = 0;
    const choice_opt *o = opts_for(FIELDS[f->dd].kind, &n);
    if (!o) return;
    draw_box_border(ctx, DD_TOP);
    draw_box_row(ctx, DD_TOP + 1, dd_title(FIELDS[f->dd].kind), 0, 1);
    for (int i = 0; i < n; i++)
        draw_box_row(ctx, DD_TOP + 2 + i, o[i].label, i == f->dd_hi, 0);
    draw_box_border(ctx, DD_TOP + 2 + n);
}

static void place_cursor(u2_ctx *ctx, form *f)
{
    /* The block cursor is the caret for the name field and nothing else. Every
     * other field is now shown inverse as a whole, which says the same thing more
     * clearly and does not depend on a renderer that can paint a block. */
    if (f->dd >= 0 || FIELDS[f->sel].kind != F_NAME) {
        ctx->vram.visible = 0;
        return;
    }
    const field *fd = &FIELDS[f->sel];
    ctx->vram.visible = 1;
    seam_goto(ctx, fd->col + 6 + f->name_len, fd->row);
}

static void draw_form(u2_ctx *ctx, form *f)
{
    seam_clear_screen(ctx);
    /* asm/569E prints the heading and asm/56EB prints the budget right after it,
     * on the same line — not at (0,0), which is what collided before. */
    seam_str_out(ctx, U2_S_CREATE_GENERATION);
    /* The GENERATION heading carries no number. asm/56C8 prints "POINTS LEFT TO
     * DISTRIBUTE : " and asm/5C85 addresses the total at column 33 row 2, which
     * is where this 28-character line leaves the cursor when it starts at col 5. */
    seam_goto(ctx, BUDGET_COL - (int)strlen(U2_S_CREATE_POINTS), BUDGET_ROW);
    seam_str_out(ctx, U2_S_CREATE_POINTS);
    print_budget(ctx, remaining(f));
    /* The blob carries its own layout as CR/LF, and the original addresses the
     * stat prompts at absolute rows 4..9 (asm/58B0 etc.), M/F at 11, RACE 12,
     * TYPE 13, NAME 14, SATISFACTORY 16. Its leading CR LF therefore has to
     * start from row 2 to land the first line on row 4; printing it from row 0
     * shifted every field up two rows and wrote values over the wrong labels. */
    seam_goto(ctx, 0, 2);
    seam_str_out(ctx, U2_S_CREATE_FORM);
    /* D9: paint over the blob's "SATISFACTORY (Y/N)-" line. The blob itself is
     * left exactly as extracted — this is a presentation override, not an edit
     * to the content. */
    seam_clear_row(ctx, CONFIRM_ROW);
    seam_str_out_at(ctx, CONFIRM_COL, CONFIRM_ROW, CONFIRM_TEXT);
    /* The two buttons are fields, so the one the cursor is on is shown inverse
     * exactly like a value slot. */
    for (int i = 0; i < NFIELDS; i++) {
        const field *fd = &FIELDS[i];
        if ((fd->kind == F_SAVE || fd->kind == F_RESET) && i == f->sel && f->dd < 0)
            hilite(ctx, CONFIRM_ROW, fd->col, CONFIRM_W);
    }
    /* D12: the blob's `RACES: / TYPES:` legend is the DOS answer to "what are the
     * numbers", and with dropdowns there are no numbers to look up. Cleared here
     * so the box has the space; the blob itself is untouched, same arrangement as
     * D9's confirm line. */
    for (int r = LEGEND_ROW; r < LEGEND_ROW + LEGEND_ROWS; r++) seam_clear_row(ctx, r);
    for (int i = 0; i < NFIELDS; i++)
        draw_field(ctx, f, i);
    draw_stat_hints(ctx, f);
    draw_dropdown(ctx, f);
    if (f->note[0]) {
        seam_clear_row(ctx, NOTE_ROW);
        seam_str_out_at(ctx, 1, NOTE_ROW, f->note);
        f->note[0] = '\0';
    }
    place_cursor(ctx, f);
    seam_present(ctx);
}

/* ---- editing ------------------------------------------------------------ */

static int complete(const form *f, char *why, size_t n)
{
    /* Only the reachable refusals. Stepping clamps every stat to
     * [STAT_MIN, STAT_MIN + remaining], so "a stat is under the floor" and "the
     * budget is overspent" are not states this form can be in, and a check for
     * either would be a dead branch that reads like a rule. */
    int left = remaining(f);
    if (left > 0) { snprintf(why, n, "[engine] %d point%s left to spend", left, left == 1 ? "" : "s"); return 0; }
    if (!f->sex)   { snprintf(why, n, "[engine] choose a sex"); return 0; }
    if (!f->race)  { snprintf(why, n, "[engine] choose a race"); return 0; }
    if (!f->cls)   { snprintf(why, n, "[engine] choose a type"); return 0; }
    if (!f->name_len) { snprintf(why, n, "[engine] a name is needed"); return 0; }
    return 1;
}

/* Left/Right stepping, which replaces digit entry outright (D6 retired). The
 * clamp is the whole legality story: a stat can never leave
 * [STAT_MIN, STAT_MIN + remaining], so the budget cannot go negative and no
 * illegal value can be entered (D7 retired). The original has no editing at all —
 * a bad value throws the generation away (asm/5C66 -> 0x5698) — so there is
 * nothing here to match, only a rule that cannot be broken. */
static void step_stat(form *f, int dir)
{
    int i = FIELDS[f->sel].index;
    f->note[0] = '\0';
    if (dir > 0) {
        if (remaining(f) <= 0) {
            snprintf(f->note, sizeof f->note, "[engine] no points left to spend");
            return;
        }
        f->val[i]++;
    } else {
        if (f->val[i] <= STAT_MIN) {
            snprintf(f->note, sizeof f->note, "[engine] %d is the minimum", STAT_MIN);
            return;
        }
        f->val[i]--;
    }
}

/* Returns 1 to advance to the next field. Stats step and the three choice fields
 * use a dropdown, so the name is the only field that takes typed characters. */
static int edit_field(u2_ctx *ctx, form *f, int c)
{
    const field *fd = &FIELDS[f->sel];
    f->note[0] = '\0';

    switch (fd->kind) {
    case F_STAT:
        snprintf(f->note, sizeof f->note, "[engine] Left -1   Right +1");
        return 0;
    case F_SEX:
    case F_RACE:
    case F_CLASS:
        /* D12: the choice is made in the dropdown, not by a keystroke here. */
        snprintf(f->note, sizeof f->note, "[engine] Enter to choose");
        return 0;
    case F_NAME:
        if (c == '\b') {                          /* D2: delete one char */
            if (f->name_len > 0) f->name_len--;
            draw_field(ctx, f, f->sel);
            return 0;
        }
        /* asm/5203 masks the high bit off every typed character
         * (`and al,$0x7f`) and otherwise stores whatever arrives, control
         * characters included. The mask is reproduced; the control characters
         * are refused (D8) because a BEL or ESC stored in a name renders as
         * garbage and no player can type one usefully. The original has no
         * length problem to guard against because it stops at 12 by itself. */
        c &= 0x7F;                                /* asm/5207 */
        if (c < 0x20 || c == 0x7F) {
            snprintf(f->note, sizeof f->note, "[engine] printable characters only");
            return 0;
        }
        if (f->name_len < NAME_MAX) f->name[f->name_len++] = (char)c;
        if (f->name_len == NAME_MAX) {
            snprintf(f->note, sizeof f->note, "[engine] 12 characters is the limit");
            return 1;
        }
        draw_field(ctx, f, f->sel);
        return 0;
    case F_SAVE:
    case F_RESET:
        return 0;                       /* activated by Enter, not by typing */
    }
    return 0;
}

/* Apply the bonuses and starting values, exactly as the original writes them. */
static void commit(u2_ctx *ctx, form *f)
{
    u2_state *st = &ctx->state;
    uint8_t *stats[6] = { &st->stat_str, &st->stat_agi, &st->stat_sta,
                          &st->stat_cha, &st->stat_wis, &st->stat_int };
    for (int i = 0; i < 6; i++)
        *stats[i] = (uint8_t)pack_bcd(f->val[i]);

    if (f->sex == 'M') st->stat_str = bcd_add(st->stat_str, 0x05);   /* 0x5960 */
    if (f->sex == 'F') st->stat_cha = bcd_add(st->stat_cha, 0x10);   /* 0x598F */

    switch (f->race) {
    case '1': st->stat_int = bcd_add(st->stat_int, 0x05); break;      /* 0x59CF */
    case '2': st->stat_agi = bcd_add(st->stat_agi, 0x05); break;      /* 0x59FB */
    case '3': st->stat_str = bcd_add(st->stat_str, 0x05); break;      /* 0x5A29 */
    case '4': st->stat_wis = bcd_add(st->stat_wis, 0x10); break;      /* 0x5A5B */
    }
    switch (f->cls) {
    case '1': st->stat_str = bcd_add(st->stat_str, 0x15); break;      /* 0x5AA6 */
    case '2': st->stat_wis = bcd_add(st->stat_wis, 0x10); break;      /* 0x5AD5 */
    case '3': st->stat_int = bcd_add(st->stat_int, 0x10); break;      /* 0x5B04 */
    case '4': st->stat_agi = bcd_add(st->stat_agi, 0x10); break;      /* 0x5B35 */
    }

    st->sex = (uint8_t)f->sex;                 /* stored as the letter */
    st->race = (uint8_t)(f->race - '1');       /* asm/5A7A: sub $0x31 */
    st->pclass = (uint8_t)(f->cls - '1');      /* asm/5B57: sub $0x31 */
    memset(st->name, 0, sizeof st->name);
    memcpy(st->name, f->name, (size_t)f->name_len);

    st->entry_x = 20; st->entry_y = 20; st->sector_first = 2;  /* 0x5BDC..0x5BE8 */
    /* DS:4A (sector_second) is never written by creation: a fresh segment
     * holds 0 there and the saved record carries it as the overworld digit. */
    st->hp = 4; st->food = 4;                          /* 0x5BEB..0x5BF3 */
    st->gold_lo = 4; st->gold_hi = 0;
}

/* [S]AVE / [R]ESET are standing shortcuts on the confirm line (D9), handled in
 * the main loop rather than as a modal prompt: the original only asks the
 * question once the form is already complete, and a player who presses the key
 * early deserves an answer about what is missing, not a swallowed keystroke. */

int u2_creation_flow(u2_ctx *ctx)
{
    if (u2_save_ensure(ctx) == U2_SAVE_NOT_READY) {
        /* The honest analogue of a drive that is not ready. */
        seam_str_out_at(ctx, 1, 24, U2_S_CREATE_BLANK);
        return -1;
    }

    for (;;) {
        form f;
        memset(&f, 0, sizeof f);
        /* Every stat starts at the minimum the original enforces. Six stats at 10
         * consume 60 of the 90, so the player distributes the remaining 30 — the
         * same solution set as "90 total, each at least 10", with no way to enter
         * an illegal value. */
        for (int i = 0; i < 6; i++) f.val[i] = STAT_MIN;
        f.dd = -1;                                  /* no dropdown open (D12) */
        draw_form(ctx, &f);

        int done = 0;
        while (!done) {
            int c = wait_key(ctx);
            if (c < 0) return 0;                    /* bailed or input ended */

            /* D12: a dropdown owns every key while it is open. Up/Down move the
             * highlight, Enter commits and advances, Esc closes and changes
             * nothing. Anything else is swallowed rather than passed down to the
             * field underneath, so a stray letter cannot land somewhere the player
             * has stopped looking. */
            if (f.dd >= 0) {
                int n = 0;
                const choice_opt *o = opts_for(FIELDS[f.dd].kind, &n);
                if (!o) { f.dd = -1; draw_form(ctx, &f); continue; }
                if (c == U2_KEY_UP)        f.dd_hi = (f.dd_hi + n - 1) % n;
                else if (c == U2_KEY_DOWN) f.dd_hi = (f.dd_hi + 1) % n;
                else if (c == '\r' || c == '\n') {
                    switch (FIELDS[f.dd].kind) {
                    case F_SEX:   f.sex  = o[f.dd_hi].key; break;
                    case F_RACE:  f.race = o[f.dd_hi].key; break;
                    case F_CLASS: f.cls  = o[f.dd_hi].key; break;
                    default: break;
                    }
                    f.dd = -1;
                    f.sel = (f.sel + 1) % NFIELDS;   /* the choice is made; move on */
                } else if (c == 0x1b) {
                    f.dd = -1;                       /* cancel, change nothing */
                } else {
                    continue;
                }
                draw_form(ctx, &f);
                continue;
            }

            if (c == 0x1b) return 0;                /* ESC abandons creation (D11) */

            /* D1: Up/Down is the only field navigation. There is no Tab binding,
             * and Left/Right belongs to the stat under the cursor. */
            if (c == U2_KEY_UP || c == U2_KEY_DOWN) {
                f.sel = (f.sel + NFIELDS + ((c == U2_KEY_UP) ? -1 : 1)) % NFIELDS;
                draw_form(ctx, &f);
                continue;
            }

            if (c == U2_KEY_LEFT || c == U2_KEY_RIGHT) {
                if (FIELDS[f.sel].kind == F_STAT)
                    step_stat(&f, (c == U2_KEY_RIGHT) ? 1 : -1);
                else
                    edit_field(ctx, &f, c);         /* says why nothing happened */
                draw_form(ctx, &f);
                continue;
            }

            if (c == '\r' || c == '\n') {
                /* Enter activates whatever the cursor is on: it opens a dropdown
                 * on a choice field, finishes the name, validates and commits on
                 * SAVE, and rebuilds on RESET. Nothing else, so no letter that can
                 * be typed into a name is also a command. */
                field_kind k = FIELDS[f.sel].kind;
                if (k == F_SEX || k == F_RACE || k == F_CLASS) {
                    int n = 0;
                    const choice_opt *o = opts_for(k, &n);
                    int cur = k == F_SEX ? f.sex : k == F_RACE ? f.race : f.cls;
                    f.dd_hi = 0;
                    for (int i = 0; o && i < n; i++) if (o[i].key == cur) f.dd_hi = i;
                    f.dd = f.sel;
                    draw_form(ctx, &f);
                } else if (k == F_RESET) { done = -1; break; }
                else if (k == F_SAVE) {
                    char why[48];
                    if (!complete(&f, why, sizeof why)) {
                        snprintf(f.note, sizeof f.note, "%s", why);
                        draw_form(ctx, &f);
                        continue;
                    }
                    commit(ctx, &f);
                    done = 1;
                } else {
                    f.sel = (f.sel + 1) % NFIELDS;  /* Name: Enter means finished */
                    draw_form(ctx, &f);
                }
                continue;
            }

            {
                int adv = edit_field(ctx, &f, c);
                if (adv) f.sel = (f.sel + 1) % NFIELDS;
                /* Only repaint the whole screen when something actually changed
                 * up there; a keystroke that just moves the cursor should not
                 * clear and redraw the form. */
                if (f.note[0]) draw_form(ctx, &f);
                else { place_cursor(ctx, &f); seam_present(ctx); }
            }
        }
        if (done == 1) {
            if (u2_save_store(ctx) != U2_SAVE_OK) return -1;
            return 1;
        }
        /* done == -1: RESET was activated; the outer loop rebuilds an empty form */
    }
}
