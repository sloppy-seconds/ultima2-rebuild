# Character creation UI — dropdowns, stepping, and a bonus preview

Status: **partly built.** The input model and the bonus preview shipped on
`feat/fmtowns-char-creation` on 2026-11-06, monochrome, in every mode and every
backend. The colour roles (§ 5a) and the enhanced chrome (§ 7 T7) are deliberately
not built yet, on the owner's instruction to get the menu functionality right first
in the black-and-white ink the menu already uses.

Built: T1 navigation, T2 stepping, T3 dropdown, T4 preview, and the gate (T8).
Not built: T5/T6 (the colour resolver and the `good` role) and T7 (banner, teal box,
Title Case).

The CP2 evidence blocker in § 11 is **resolved**: the baseline was re-recorded
2026-11-06 with the owner's approval, 42 → 70 steps, and `make test` is green.

No engine code changed for this document's first draft. It is a presentation and
input change to `engine/src/game/creation.c`, behind the existing `--enhanced` flag
for the FM Towns chrome, with the input model applying in every mode.

Owner design rulings, 2026-11-06, all five accepted:

1. Sex, Race and Type open a dropdown. Up/Down move the highlight, Enter commits.
   No letter keys, so no naming collisions.
2. Save and Reset stay highlightable fields you press Enter on, not hotkeys.
3. Stats are modified with Left (-1) and Right (+1), not typed digits.
4. Selecting Sex, Race or Type shows the effect on each stat beside the stat.
5. Navigation is Up/Down. **There is no Tab binding.**

Ruling 4 was first written as "green for a gain and red for a loss". The loss half is
now ruled out: no creation choice can lower a stat, and the one mechanism that could is
unreachable, so **there is no red path** (§ 5b). The preview is a gain indicator only.

Scope: engine presentation only. Nothing here touches `asm/`, and the C engine is a
reimplementation and never will be byte-proven (`AGENTS.md` § 1).

---

## 1. Evidence, graded

### 1a. Byte-proven: the ten creation modifiers

Every one is `add al,$NN` / `daa` / `mov [stat],al` in `asm/5400`, with **no ceiling
guard between the add and the store**. Read off `disasm/redecoded/5400_sub_5400.txt`
and spot-checked at `5AA9 add al,$0x15` / `5AAB daa` / `5AAC mov [0x4b],al`.

| choice | stat | BCD | add site |
| --- | --- | --- | --- |
| MALE | STR | +05 | 5963 |
| FEMALE | CHA | +10 | 5992 |
| HUMAN | INT | +05 | 59D2 |
| ELF | AGI | +05 | 59FE |
| DWARF | STR | +05 | 5A2C |
| HOBBIT | WIS | +10 | 5A5E |
| FIGHTER | STR | +15 | 5AA9 |
| CLERIC | WIS | +10 | 5AD8 |
| WIZARD | INT | +10 | 5B07 |
| THIEF | AGI | +10 | 5B38 |

STAMINA is never modified by sex, race or class.

**All ten are positive.** A scan of every arithmetic op on a stat byte in `asm/5400`
finds no `sub`/`das` on any stat. The only `sub al,$0x31` sites are the digit-to-index
conversions on the race byte `[0x48]` and the class byte `[0x47]`, not stats. Grade:
measured, by a scan over the redecoded dump, with the sites above as the sample.

The budget byte is `56E6 mov al,$0x90` / `56E8 mov [0x29],al`: 90, packed BCD, at
`DS:0x29`.

### 1b. Measured: the FM Towns reference frames

From `/var/tmp/fmtowns-screenshots/` (five creation frames, TSUGARU, 2026-11-06).
Scripts in `/var/tmp/fmtowns_{rows,grid,pitch,detail}.py`. These are screen captures
of an emulator, so they are cleaner than the CRT photographs in
`references/fmtowns-screens/` and are the better source for this screen.

| region | measurement |
| --- | --- |
| yellow banner | y 23..40, 18 px |
| blue field | x 8..634, y 50..435, white rounded border |
| red bottom bar | y 454..469, x 19..622, 16 px |
| glyph cell | 16 px pitch both axes → **40 cols x 24 rows** |
| leader dots | 16 px period, x 300..412 |
| value right edge | x 456 → about col 27 |
| popup box | x 210..415, top y 316, 2 px white border, teal `#007777` fill |
| popup height | 16 px x option count + header + border (92 px for 4+1, 60 px for 2+1) |
| palette | every channel a multiple of 17 → FM Towns RGB4-4-4 |
| colours | blue `#0022EE`, teal `#007777`, yellow `#FFFF55`, red `#FF0000` |

Reference field order is Race, Sex, Type, Name. Labels are Title Case with a colon
(`Race : Human`), not the DOS caps-and-dash (`RACE-`). The active field is marked with
a literal `@` after the colon. The heading is `[Generate Character]` in red.

The reference popup is letter-keyed (`H)uman E)lf D)warf B)obbit`) and has **no
highlight at all**, because you type a letter rather than move a cursor. So the
highlight, and the whole Up/Down/Enter model, is ours. That is also the origin of the
`B)obbit` compromise, which ruling 1 removes.

### 1c. The floor of 10 is established

Six stats, a floor of 10 each, a budget of 90. The floor is a known rule and the engine
enforces it in `complete()` via `STAT_MIN`, so it is what § 5b's arithmetic rests on and
nothing here re-opens it.

One bookkeeping defect alongside it: `creation.c` cites `asm/5C69` for the floor check
and `asm/5C66` for the discard-generation jump, and neither address resolves in the
current tree. `disasm/redecoded/5400_sub_5400.txt` ends at `5C64`, so it cannot show an
instruction at `5C69`, and the range 5C49..5C6C belongs to `asm/5C49_sub_5c49.asm`,
whose body is six `mov` pairs. The rule is right and the pointer is stale. T0 repoints
it; it is not a question about the rule.

---

## 2. Why this fits every mode, including classic CGA

The vram grid is fixed at **40 x 25** (`platform/platform.h:39-40`) and does not change
with mode. What changes is the glyph:

| mode | glyph | text area |
| --- | --- | --- |
| classic | 8 px | 320 x 200 |
| enhanced | 16 px (`U2_ENH_SCALE 2`) | 640 x 400 inside a 1024 x 512 frame |

The FM Towns reference is 40 x 24 at 16 px. **It is a 1:1 fit for our grid in enhanced
mode and a half-scale fit in classic.** No new grid, no reflow logic, no second layout
engine.

The pieces all fit in 40 columns in both:

- popup box: reference is 204 x 92 px = about 13 cols x 6 rows
- bonus delta: values end at col ~27, so `+20` sits at col 29..32, inside 40
- the `@` / cursor marker: same cell either way

So the answer to "can it work in all themes including classic CGA" is: **the layout and
the input model, yes, everywhere, terminal backend included.** Colour is the exception,
and it is a hard one. See § 5.

---

## 3. Input model

`creation.c:467` today is Tab / Down / Right = next field, Up / Left = previous.
Rulings 1, 3 and 5 collapse that to one axis: **Up/Down moves fields, and while a
dropdown is open it moves the highlight instead.** Left/Right is stepping. There is no
Tab binding, so `input_sdl.c` needs no new key mapping for this feature at all.

| key | no dropdown open | dropdown open |
| --- | --- | --- |
| Up / Down | move field | move highlight |
| Enter | open dropdown on Sex / Race / Type; finish Name; fire Save / Reset | commit highlight, close, advance |
| Left / Right | stat -1 / +1 | inert |
| Esc | inert | close, no change |
| Backspace | name only | inert |

Notes:

- **Held keys already repeat.** `input_sdl.c` does not filter `e.key.repeat`, so
  distributing 30 points is a held Right, not thirty taps. No work needed.
- Stepping is clamped to `[STAT_MIN, STAT_MIN + remaining]`, so the budget can never go
  negative and a stat can never go below the floor. This is what retires D7 (§ 6).
- Save refuses while points remain unspent, and says how many in the note line. A
  mutation the player cannot see is a defect (`AGENTS.md` § 3).

---

## 4. Plumbing prerequisites

### P1 — text colour attributes are discarded by both renderers

This is the only real prerequisite, and it is only needed for the colour of the delta,
not for the dropdown or the stepping. The input model can ship ahead of it.

The cell model already carries an attribute (`vram.h`: `u2_cell { ch, attr }`), and
`seam.c` writes the game's own `DS:0x23E` byte into it (default `0x0F`, `state.c:83`).
Both renderers then throw it away:

- `renderer_sdl.c:404` — `(void)attr;` with a comment deferring it to CP9
- `renderer_term.c:24` — `(void)attr;`, while that file's own header comment at lines
  6-8 claims attributes *are* mapped to the 16 CGA colours. The comment is aspirational
  and should be corrected whichever way P1 lands.

`u2_font_draw_cell()` already takes explicit `fg` and `bg`, so the primitive exists.
What is missing is a route from a game-side decision ("this cell is a gain") to the
renderer.

The obvious cheaper route — have `creation.c` draw the delta straight into the frame
with explicit colours, the way `panel.c:108` does — **is not available on this path**.
The creation screen goes `seam_present` → `u2_vram_flush` → `renderer->cell`, and the
SDL text frame `v->text` is owned by the renderer, so the game has no handle on it. So
P1 is the real route, and it is CP9's unfinished business rather than new invention.

Design for P1: add a role resolver, not a raw attribute channel.

```
u2_attr_role(pal, role) -> {fg, bg, distinct}   /* roles: normal, good, inverse */
```

Roles are resolved against the palette actually in force, exactly the way
`u2_panel_pick_colours` already does (`panel.c:46-56`, `pick(pal, s_red, 1)`). The
game's own `text_attr` stays the game's byte and keeps flowing into cells unchanged;
engine annotations ask for a role. Provenance and presentation stay separate.

Blast radius: every existing cell carries `0x0F`, so honouring attr must be a visual
no-op everywhere except where a role is explicitly requested. That is the control in § 8.

### P2 — verify the enhanced text-layer geometry before assuming it

Measure what `v->text` is sized to in enhanced mode and whether the creation screen
fills, centres or letterboxes in a 1024 x 512 frame. This plan assumes 640 x 400
centred, with the spare height available for banner and bar chrome. Confirm it, do not
build on it.

---

## 5. Colour, and why there is no red path

### 5a. Classic CGA has no green — but it has a spare colour

The CGA path carries exactly four palette indices: `U2_CGA_BLACK`, `U2_CGA_CYAN`,
`U2_CGA_MAGENTA`, `U2_CGA_WHITE` (`frame.h:46-49`). **There is no green in classic
CGA.** EGA has green at index 2, light green at 10 (standard order per
`frame.h:156-159`), and a format-v2 theme may ship its own `PALETTE.RGB` — FM Towns
does, and its index 3 is black, which is the trap that previously made the creation
cursor invisible.

With the red path gone the requirement shrinks to a single role, and CGA can fill it
honestly: normal text is white, so **cyan is a distinct, legal gain colour in CGA**
rather than a pretend green. Two rules:

1. **Never hardcode a green index.** Ask the resolver for the `good` role and let the
   palette answer — cyan in CGA, green in EGA, whatever a theme's own table offers.
2. **The `+` always prints.** The sign carries the meaning, so it survives a monochrome
   terminal and any palette where the role cannot resolve to something distinct.

The reason for rule 1 is a specific failure mode: a naive `pick(pal, s_green)` against a
4-entry palette returns the nearest of four and reports success. That is a lying answer,
the exact shape `AGENTS.md` § 5 warns about, so the resolver returns a `distinct` flag
instead of silently handing back the text colour.

### 5b. There is no loss to mark, so there is no red path

Ruling 4 originally asked for red when a choice loses you a stat. Under the rules that
cannot happen:

- budget is 90, byte-proven at `56E6`
- the floor is 10 per stat, enforced by `STAT_MIN` in `complete()`
- six stats at 10 or more summing to 90 → **the largest any stat can be is 40**
- the largest bonus is +15 (FIGHTER to STR)
- 40 + 15 = 55, nowhere near the 99 where the unguarded `daa` would wrap

Nothing a creation choice does can lower a stat, and the only mechanism that could is
unreachable. **Owner ruled 2026-11-06: no red path.** An earlier note in this session
floated the BCD wrap as the honest red case; it is not reachable, and a branch written
for it would be dead code that reads like a feature.

Encoding, final:

| shown | meaning |
| --- | --- |
| `+10` | this selection adds 10 to the stat |
| nothing | no effect |

The number is the sum of the three current selections' bonuses for that stat, with the
highlighted row substituted into its slot while a dropdown is open. No totals, no
predictions, no negatives.

---

## 6. Deviations retired and added

`creation.c`'s header carries the deviation list. This change retires two and adds two.

- **D6 retired** — the two-digit shift register. Left/Right stepping replaces digit
  entry outright.
- **D7 retired** — "the budget may go negative while typing". With stepping clamped to
  `[10, 10+remaining]` the illegal state is unreachable rather than tolerated, which is
  a stronger guarantee than the one it replaces.
- **D1 amended** — it currently reads "Tab / arrows move between fields". Tab goes away
  with ruling 5; Up/Down is the only field navigation, and it changes duty to the
  highlight while a dropdown is open.
- **D4 amended** — the cursor still marks the field being edited, but on a choice field
  the dropdown highlight supersedes it while the dropdown is open.
- **D11 added** — choice fields are dropdowns. The original types a digit against a
  static `RACES: / TYPES:` legend; the FM Towns version keys off letters and had to
  rename Hobbit to `B)obbit` because H was taken. Neither is available without that
  collision, and the collision is the reason.
- **D12 added** — the engine shows the creation bonuses. **The original never shows
  them anywhere.** They exist only as bytes in `asm/5400`, and the DOS form's only hint
  is the `1-HUMAN` legend. Marked `[engine]` at the site. It is digits and a sign, so
  it costs no game strings and raises no licensing question.

D1, D2, D3, D5, D8, D9, D10 stand unchanged.

---

## 7. Work order

Each step is independently verifiable and lands as its own commit.

| # | task | verify by |
| --- | --- | --- |
| T0 | repoint the stale `asm/5C66` / `asm/5C69` citations in the `creation.c` header | the cited addresses resolve, or the header cites the engine rule instead of a byte |
| T1 | Up/Down field navigation, Tab binding removed | every field reachable by Up/Down alone on an SDL build |
| T2 | Left/Right stepping, clamped to `[10, 10+remaining]` | 30 held Rights land on 40 and the 31st is refused with a message |
| T3 | dropdown widget: open, highlight, commit, cancel | Esc leaves the committed value untouched |
| T4 | bonus preview vector, live on the highlight | Dwarf + Fighter shows `+20` on STR and nothing on STAMINA |
| T5 | `u2_attr_role()` resolver + honour attr in both renderers | existing screens pixel-identical before and after; that is the control |
| T6 | the `good` role on the delta | cyan on a CGA frame, green on an EGA one, `+` present with colour off |
| T7 | enhanced chrome: banner, bordered field, red bar, Title Case labels | D3 already permits invented text; mark each invented string at its site |
| T8 | gate wired into `make test` | see § 8 |

T1 through T4 are the input model and need no colour plumbing at all, so they can ship
first and be played. T5 is the only prerequisite, and only for T6. T7 is independent of
everything above it.

---

## 8. Gate design

A gate that cannot go red is worse than a tool that lies (`AGENTS.md` § 5), so each
assertion below names the control that makes it red.

Text assertions against the vram grid:

- dropdown open / closed state, highlight row, committed value
- Esc leaves the committed value untouched — control: commit a wrong value and confirm
  the assertion goes red
- stepping refuses the press that would exceed `remaining`, and says so
- Save refuses while points remain unspent
- Dwarf + Fighter → `+20` on STR, nothing on STAMINA
- switching a committed choice never prints a minus. **If a `-` can be produced at all,
  the encoding in § 5b has been broken and a dead path has been built.**

Colour assertions:

- the resolver run against the 4-entry CGA table returns cyan for the `good` role with
  `distinct` set, and returns the text colour with `distinct` clear when asked for a
  colour the palette genuinely cannot show. That is the control for the lying-`pick()`
  failure in § 5a.
- the `+` glyph is present in every palette, with colour suppressed entirely.

Frames, not assertions, for anything visible (`AGENTS.md` § 3): screenshot the creation
screen in classic CGA, an EGA theme, the FM Towns theme, and the terminal backend. Four
frames per visual change, because the whole class of bug this screen has already
produced — the invisible cursor — was invisible to text assertions and only showed in a
theme's pixels.

---

## 9. Decisions needed

**Q1 — dropdowns in classic too, or enhanced only?** Recommended: input model in both,
FM Towns chrome in enhanced only. The dropdown fixes a real DOS defect (a memorised
legend and digit typing) and fits at 8 px; the banner, teal box and Title Case are a
look.

**Q2 — does the preview track the highlight or only the committed selection?**
Recommended: the highlight, live. It is what turns the dropdown into a decision tool,
and the delta is a pure function of three selections so it is cheap.

**Q3 — is the slot chooser in scope?** The reference has `Create Entry(1-4)#  4` as a
preamble line. That is a save-slot feature, not a creation-screen one, and `saves/<slot>/`
already exists. Recommended: out of scope here.

**Q4 — `Points: 95`.** The reference reads 95 in all five frames with 65 points spent,
which reconciles with neither the DOS budget of 90 nor a remaining counter. The stats
never change across the frames, so whether the number decrements at all is unknown.
Recommended: one more frame taken immediately after changing a stat settles it. Not a
blocker for any task above.

---

## 11. What the build found, and the one thing that needs a ruling

**The CP2 evidence baseline moves.** `engine/evidence/cp2-playthrough.json` stores
its own keystrokes, and they are the old creation sequence (`1 5 1 5 ... M 1 1`).
Under stepping and dropdowns those bytes do something different, so
`tools/playlog.py verify` diverges and `make test` is red on CP2 and green on
everything else. Re-recording is a one-liner already documented in
`docs/phase4-state.md`:

```
mkdir -p /var/tmp/u2-cp2 && ./u2 --headless --save-dir /var/tmp/u2-cp2/disk \
     --script tests/play_to/reach_everything_today.keys \
     --seed 0 --replay-report evidence/cp2-playthrough.json
```

`tests/play_to/reach_everything_today.keys` has already been rewritten for the new
model, so the re-record reproduces.

**Done 2026-11-06, owner-approved.** Baseline moved 42 → 70 steps, 39 → 66 keys,
`REPLAY: REPRODUCES`. The `mkdir -p` above is not decoration and is the part the
first attempt got wrong: a `--save-dir` whose parent does not exist is read as
"disk not ready", which takes the `INSERT PLAYER DISK` branch instead of loading a
blank seeded record, so the recording and `playlog.py verify` played *different*
games — 112 steps against 70. Full measurement in the `docs/phase4-state.md` gate
log, 2026-11-06.

**The legend clear is one row lower than it looks.** The blob's `RACES: / TYPES:`
block lands on rows 18..22, not 17..21, because the `\015\012` before `RACES:`
advances two rows the way the one before `M/F-` does. Clearing from 17 left
`4-HOBBIT  4-THIEF` showing through the box. Found by looking at the frames, not by
reasoning about the blob.

**`--dump-each` is the tool for this screen.** It prints the grid after every
present, which is what makes a dropdown, a highlight and a delta assertable at all.
The pty route works but the cursor addressing makes the log unreadable; the grid log
is the one to assert against.

**Two gates were proven red before being trusted.** Flipping the delta's `+` to a `-`
in `draw_deltas` took three CP3 assertions red, including the "no negative delta"
one. A gate that cannot go red is not a gate.

---

## 12. Out of scope

- **The FM Towns modifier table.** The owner recalls Hobbit giving -5 STR. The DOS
  version has no negative modifier anywhere (§ 1a), so this is a plausible FM Towns
  difference rather than a misremembering, and FM Towns is a separate codebase (the
  FLAIR credits are photographed in `references/fmtowns-screens`). Settling it needs a
  TSUGARU capture against `references/fmtowns/`: one frame with Race set to Hobbit
  against one with Human. It is not evidence about `original/ultimaii.exe` either way,
  so it does not gate anything here.
- The Z status screen, which the owner ruled takes the whole screen in enhanced mode.
- Save slots, per Q4.
- The `0x8d` / `][` glyph mapping, still open from CP9 and unrelated.
