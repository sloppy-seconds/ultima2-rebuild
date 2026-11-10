#!/usr/bin/env python3
"""
space_view_gate.py — CP4 gate for the in-space screen (handoff § 5, legs 1 and 2).

Why this exists. Owner playtest 2026-11-10: "I hit L to launch and it seems to have
crashed." The engine was alive. `space_loop` was

    for (;;) {
        seam_str_out(ctx, U2_S_CMD_PROMPT);
        const int r = seam_key_poll(ctx, &k);
        if (r < 0) return;
        if (!r) continue;          <-- back to the prompt print

which spun at 100% CPU, re-printed `CMD: ` on every empty poll, and **never presented a
frame** -- so the screen sat on the overworld image the rocket launched from and looked
frozen. Three defects in six lines, and no test could see any of them because nothing in
the suite ever entered space.

Leg 1 (the spin): the loop must present while waiting for a key, must not carry a bare
empty-poll `continue` back to the prompt, and must paint. Control: reintroduce
`if (!r) continue;` and drop the present -- both assertions go red.

Leg 2 (the sprite): asm/6CC4_space_ship_erase erases a plus of 6 vertical + 7 horizontal
pixels about DS:47B/DS:47C, and the draw is the mirror of the erase. The gate reads the
counts out of the asm header rather than restating them here, so the check is against the
evidence and not against the fix.

Legs 3 and 4 of the handoff's plan (orbit-vs-deep-space text, dive sector) are NOT here:
leg 4 is already covered by space_check.c, and leg 3 needs a scripted run that can reach
space, which the suite cannot do yet. Recorded as open in docs/handoff-space-view.md.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CMD = os.path.join(ROOT, "engine", "src", "game", "commands.c")
VIEW_H = os.path.join(ROOT, "engine", "src", "game", "space_view.h")
ASM = os.path.join(ROOT, "asm", "6CC4_space_ship_erase.asm")

fails = []


def check(cond, msg):
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def read(p):
    with open(p) as fh:
        return fh.read()


# ---- leg 1: the loop must not spin, and must present and paint ---------------------
src = read(CMD)
m = re.search(r"static void space_loop\(.*?\n\}\n", src, re.S)
check(m is not None, "space_loop found in commands.c")
body = m.group(0) if m else ""

# Strip comments before looking for code shapes. The function's own comment quotes the
# defective line it replaced, so a naive search finds `if (!r) continue;` inside prose
# and reports a bug that is not in the code -- a gate whose failure mode is "it matched
# a comment" is worse than no gate, because it trains you to distrust the red.
body_code = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
body_code = re.sub(r"//[^\n]*", "", body_code)

check("seam_present_frame" in body_code,
      "space_loop presents a frame while it waits for a key (no invisible 100% spin)")
check(not re.search(r"if\s*\(\s*!r\s*\)\s*continue\s*;", body_code),
      "no bare empty-poll `continue` back to the prompt print")
check("space_paint" in body_code,
      "space_loop paints the scene (once on entry and after each handled command)")
check(body.count("seam_str_out(ctx, U2_S_CMD_PROMPT)") == 1,
      "the CMD: prompt is printed once per command, not once per poll")

# ---- leg 2: the ship sprite is the plus asm/6CC4 erases ----------------------------
asm = read(ASM)
v_n = re.search(r"vertical stroke.*?\((\d+) calls\)", asm, re.S)
h_n = re.search(r"horizontal stroke.*?\((\d+) calls\)", asm, re.S)
check(v_n and h_n, "asm/6CC4 header states the two sweep counts")

hdr = read(VIEW_H)
def span(lo_name, hi_name):
    pat = r"#define\s+%s\s+\(?([+-]?\d+)\)?"   # the offsets are written (-3), (+2)
    lo = int(re.search(pat % lo_name, hdr).group(1))
    hi = int(re.search(pat % hi_name, hdr).group(1))
    return hi - lo + 1

if v_n and h_n:
    check(span("U2_SPACE_SHIP_V_FROM", "U2_SPACE_SHIP_V_TO") == int(v_n.group(1)),
          f"vertical stroke is {v_n.group(1)} px, matching asm/6CC4")
if h_n:
    check(span("U2_SPACE_SHIP_H_FROM", "U2_SPACE_SHIP_H_TO") == int(h_n.group(1)),
          f"horizontal stroke is {h_n.group(1)} px, matching asm/6CC4")

# The star field must state honestly what is and is not decoded.
VIEW_C = os.path.join(ROOT, "engine", "src", "game", "space_view.c")
vc = read(VIEW_C)
check("NOT decoded" in vc or "NOT DECODED" in vc,
      "the star field states plainly that the per-star algorithm is undecoded")

# ---- leg 3: the field is plotted into the measured asm/4B76 view -------------------
# The five warp frames in g39 have white pixels spanning exactly x 16..271 and
# y 16..143 -- 256 columns and 128 rows from asm/4B76's 16/16 origin pair. A field that
# drifts outside that rectangle is drawing somewhere the original cannot.
vx = int(re.search(r"#define\s+U2_SPACE_VIEW_X\s+(\d+)", hdr).group(1))
vy = int(re.search(r"#define\s+U2_SPACE_VIEW_Y\s+(\d+)", hdr).group(1))
vcols = int(re.search(r"#define\s+U2_SPACE_VIEW_COLS\s+(\d+)", hdr).group(1))
vrows = int(re.search(r"#define\s+U2_SPACE_VIEW_ROWS\s+(\d+)", hdr).group(1))
check((vx, vy) == (16, 16),
      f"view origin is ({vx},{vy}), matching asm/4B76's DS:4BF/DS:4C1 = 16/16")
check(vcols == 256 and vrows == 128,
      f"view is {vcols}x{vrows}, matching the measured x 16..271 / y 16..143 extent")
check(vx + vcols - 1 == 271 and vy + vrows - 1 == 143,
      "the view rectangle's far corner is exactly the measured far corner")

cx = re.search(r"#define\s+U2_SPACE_CENTRE_X\s+\(([^)]+)\)", hdr).group(1)
cy = re.search(r"#define\s+U2_SPACE_CENTRE_Y\s+\(([^)]+)\)", hdr).group(1)
check(cx.endswith("/ 2") and cy.endswith("/ 2"),
      "the radiating field is centred on the view centre (measured centroid 127,63)")

if fails:
    print(f"\nSPACE VIEW GATE: FAIL ({len(fails)})")
    sys.exit(1)
print("\nSPACE VIEW GATE: PASS")
