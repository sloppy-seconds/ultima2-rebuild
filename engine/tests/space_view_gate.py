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

Leg 5 (the dive sweep's pacing), added 2026-11-10 after the owner reported the descent moving
"much too fast ... you can't really see where to stop". In the original one march step is one
frame and the frame is paid for in CGA writes: asm/5AB1_map_render is called once per step, and
nothing else in the loop draws, so the rate is the render's cost -- measured at 492,074 emulated
cycles per step (captures/spice86/dive-sweep-rate). The engine's render is a memcpy, so the rate
cannot be inherited and has to be stated. This leg checks that it is stated, that the wait sits
inside the loop between the present and the key poll, and that the stated milliseconds still agree
with the cited cycle count -- so retuning the number without the measurement goes red.

Control for leg 5, demonstrated rather than asserted: the gate takes the source path from
U2_DIVE_GATE_SRC, so a copy of commands.c with the wait deleted can be run against it. From the
repo root: copy engine/src/game/commands.c somewhere scratch (under /var/tmp, not /tmp), delete
the `seam_wait_ms(ctx, U2_DIVE_STEP_MS);` line from the copy, and run

    U2_DIVE_GATE_SRC=/var/tmp/commands_nowait.c python3 engine/tests/space_view_gate.py

which must report the two pacing checks and the ordering check as FAIL. The copy is what makes
this a control instead of a comment: the gate is shown going red on the real file shape, from the
same directory and the same ref position CI will use.

Leg 7 (the presented frame), added 2026-11-11 after the owner's second report of the same
symptom -- "i'm in space and hit H but its not showing the hyperjump dialog". Legs 1 and 6
read the text grid, and the grid was correct: the loop printed the prompt into the vram and
presented a frame that had been composited before the prompt existed, so the window never
changed while every text assertion stayed green. This leg runs the SDL renderer headlessly,
captures the frame the engine actually uploaded with its own Ctrl-S, and requires that the
hyperwarp prompt adds ink over the orbit frame. It is the only leg in the file that looks at
pixels, which is the point: a gate that reads the same structure the code writes cannot catch
a failure between the write and the screen.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CMD = os.environ.get("U2_DIVE_GATE_SRC") or os.path.join(ROOT, "engine", "src", "game", "commands.c")
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
# The pacing constants moved to game/space.h so the dive and the plane could not each carry
# their own copy of one borrowed figure. This leg follows them; the point of the leg is that
# the rate is a named constant citing a measurement, not where the file sits.
PACE = os.environ.get("U2_PACE_GATE_SRC") or os.path.join(ROOT, "engine", "src", "game", "space.h")
pace = read(PACE)
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
# ...and WHERE it presents. Presenting is not enough: the frame is a composite, and the
# paint that copies the text grid into it used to run before the prompt was printed, so
# the window showed the screen as of one prompt ago. Every grid-level assertion in the
# suite stayed green while the owner saw nothing at all (2026-11-11, second report).
_body_prompt = body_code.find("seam_str_out(ctx, U2_S_CMD_PROMPT)")
_body_poll = body_code.find("seam_key_poll(ctx, &k)", _body_prompt)
_body_paint = body_code.find("space_paint(ctx, ow);", _body_prompt)
check(_body_prompt >= 0 and _body_poll > _body_prompt
      and _body_paint > _body_prompt and _body_paint < _body_poll,
      "the frame is repainted between the prompt print and the key poll (paint-first is what "
      "made the prompt invisible to the window while the grid looked correct; a later paint "
      "does not count, because the poll has already presented)")
check("seam_wait_ms" in body_code,
      "the empty poll is paced, not a free-running upload loop")
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
VIEW_C = os.environ.get("U2_VIEW_GATE_SRC") or os.path.join(ROOT, "engine", "src", "game", "space_view.c")
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

# ---- leg 5: the dive sweep is paced, and its rate is the measured render cost ------
# The period clock the game was written for. The figure below is not a taste setting: it is
# how the cited cycle count becomes milliseconds, and the check recomputes it.
PERIOD_HZ = 4_770_000
dm = re.search(r"#define\s+U2_DIVE_STEP_MS\s+(\d+)", pace)
check(dm is not None, "the dive step's pacing is a named constant, not an inline number")
if dm:
    ms = int(dm.group(1))
    # the comment next to the constant must cite the measurement it came from
    tail = pace[dm.end():dm.end() + 1200]
    cyc = re.search(r"([\d,]{6,}) emulated cycles", pace)
    check(cyc is not None,
          "the pacing cites a measured cycles-per-step figure, so it cannot be retuned blind")
    if cyc:
        cycles = int(cyc.group(1).replace(",", ""))
        check(round(cycles / PERIOD_HZ * 1000) == ms,
              f"{ms} ms is {cycles} cycles at {PERIOD_HZ/1e6} MHz "
              f"(expected {round(cycles / PERIOD_HZ * 1000)} ms)")

dm_loop = re.search(r"static void rocket_descent\(.*?\n\}\n", src, re.S)
check(dm_loop is not None, "rocket_descent found in commands.c")
if dm_loop:
    dbody = re.sub(r"/\*.*?\*/", "", dm_loop.group(0), flags=re.S)
    dbody = re.sub(r"//[^\n]*", "", dbody)
    check("u2_space_descent_step" in dbody and "u2_overworld_draw" in dbody,
          "each dive step marches the sweep and redraws the surface (the render IS the frame)")
    check("seam_wait_ms(ctx, U2_DIVE_STEP_MS)" in dbody,
          "the dive loop waits one step period per frame (an unpaced sweep is unaimable)")
    i_present = dbody.find("seam_present_frame")
    i_wait = dbody.find("seam_wait_ms")
    i_poll = dbody.find("seam_key_poll")
    check(-1 < i_present < i_wait < i_poll,
          "the wait sits after the present and before the key poll, as in the original's loop")

# ---- leg 8: the hyperwarp animation ------------------------------------------------
# asm/6DFD's five redraw loops. The frame COUNTS are bytes (128, 128, 63, 126, 64) and the
# frame TIME is not: asm/0711's slot value is dropped at every lookup inside those loops, so
# the original's rate was the cost of drawing the field. That split is exactly what invites a
# quiet regression -- a "tidy" that folds the phases, or a deleted wait on the reasoning that
# the slot is not a delay anyway -- and either one turns a warp into a single frame. So the
# counts and the present-before-wait ordering are both asserted, and the control in
# space_check_selftest.sh deletes one phase to prove this leg can go red.
wm = re.search(r"static void space_warp_phase\(.*?\n\}\n", src, re.S)
check(wm is not None, "space_warp_phase found in commands.c")
if wm:
    wbody = re.sub(r"/\*.*?\*/", "", wm.group(0), flags=re.S)
    i_p = wbody.find("seam_present_frame")
    i_w = wbody.find("seam_wait_ms(ctx, U2_WARP_FRAME_MS)")
    check(-1 < i_p < i_w,
          "each warp frame presents before it waits (an un-presented loop draws frames nobody sees)")
for fn, want in (("space_warp_field", [128, 128, 63]),
                 ("space_warp_engage", [126, 64])):
    m = re.search(r"void " + fn + r"\(u2_ctx \*ctx, u2_overworld \*ow\)\s*\{(.*?)\n\}\n",
                  src, re.S)
    check(m is not None, f"{fn} found in commands.c")
    if m:
        got = [int(x) for x in re.findall(r"space_warp_phase\(ctx, ow,\s*(\d+)", m.group(1))]
        check(got == want,
              f"{fn} runs {got} frames; asm/6DFD's counters are {want}")

# ---- leg 9: the warp trail ----------------------------------------------------------
# The streaks. asm/6B3D's mode byte decides whether the previous frame survives:
# `mov 0x4c7,%al ; or %al,%al ; je L_006e14` skips the erase of the star's old position when
# the mode is zero, and nothing in asm/6DFD clears the screen between redraws, so mode 0
# accumulates into a line along each ray. That is what the warp LOOKS like, and an engine that
# clears unconditionally cannot produce it at any mode — which is exactly the bug this leg
# exists to stop, because a per-frame clear is what a renderer does by default and deleting the
# guard looks like a simplification. The control in space_check_selftest.sh makes the clear
# unconditional again and requires this to go red.
view_c = read(VIEW_C)
m = re.search(r"void u2_space_scene_draw\(.*?\n\}", view_c, re.S)
check(m is not None, "u2_space_scene_draw found in space_view.c")
if m:
    body = m.group(0)
    clear = re.search(r"(if\s*\(\s*!\s*s->keep_frame\s*\)\s*\n?\s*)?u2_frame_clear\(", body)
    check(clear is not None and clear.group(1) is not None,
          "the space field clear is GUARDED on keep_frame — an unconditional clear makes the "
          "warp a shimmer instead of streaks, which is the original's whole visual")
    check("warp_frame" in body,
          "the warp field grows its star radii with warp_frame (a static field cannot trail)")
check(re.search(r"space_paint_mode\(ctx, ow, mode,\s*i,\s*mode == 0\)", src) is not None,
      "the warp keeps the previous frame exactly when the mode is 0, as asm/6B3D does")
for fld in ("warp_frame", "keep_frame"):
    check(re.search(r"int\s+" + fld + r"\s*;", read(VIEW_H)) is not None,
          f"u2_space_scene carries {fld}")

# ---- leg 6: the space text pane -----------------------------------------------------
# Owner playtest 2026-11-10, two symptoms: the hyperwarp target prompt never appeared, and
# letters were missing from the HP/FOOD display. Both were the readout colliding with the CMD
# pane, and the bytes name the three things that keep them apart (see space_values' note in
# commands.c): the labels draw at column 30 under a wide margin, the values draw at column 37
# as two BCD digits, and asm/6FA3 then narrows the scroll margin to 0x1f and parks the cursor
# at column 0 of the bottom row. The gate checks the bytes are still modelled, and then -- the
# part that matters -- checks the RENDERED GRID, because every one of these failures left the
# code shape intact and the screen wrong.
check("seam_clear_screen" in src and "space_screen_enter" in src,
      "entering space clears the pane (asm/6999 L_006b9a sets graphics mode 4, which clears)")
for name, want in (("U2_SPACE_VALUE_COL", "37"), ("U2_SPACE_MARGIN_CMD", "0x1f")):
    got = re.search(r"#define\s+%s\s+(\S+)" % name, src)
    check(got is not None and got.group(1).rstrip() == want,
          f"{name} is {want}, matching asm/6FA3 (got {got.group(1) if got else 'missing'})")
vm = re.search(r"static void space_values\(.*?\n\}\n", src, re.S)
check(vm is not None and vm.group(0).count('%02u') == 4,
      "the four readout values print as two BCD digits (FUEL=08, not FUEL=8)")
check(vm and re.search(r"seam_goto\(ctx, 0, U2_SPACE_CMD_ROW\)", vm.group(0)),
      "the status pass parks the cursor at column 0 of the bottom row (asm/6FA3 L_007202)")
check(re.search(r"seam_char_print\(ctx, k\.ch\)", src) is not None,
      "the typed hyperwarp digit is echoed (asm/6DFD L_006f7f calls char_print_wrap on it)")

# The rendered-grid half. Skips, loudly, when there is no build or no copy of the game.
U2_BIN = os.path.join(ROOT, "engine", "u2")
PLAYER = os.path.join(ROOT, "original", "player")
if not os.path.exists(U2_BIN) or not os.path.exists(PLAYER):
    print("  SKIP  grid check: needs engine/u2 and original/player (gitignored) — "
          "this leg cannot go red here, so run it locally after a build")
else:
    import shutil
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory(prefix="u2spacegate-") as work:
        save = os.path.join(work, "save", "PLAYER")
        os.makedirs(save)
        rec = os.path.join(save, "PLAYER")
        shutil.copyfile(PLAYER, rec)
        for step in (["maxout", rec], ["set", rec, "name", "SPACEGATE"], ["set", rec, "sector", "41"],
                     ["setpos", rec, "38", "3"], ["set", rec, "item", "TRI-LITHIUM", "9"]):
            subprocess.run([sys.executable, os.path.join(ROOT, "tools", "u2_edit.py")] + step,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        keys = os.path.join(work, "k.keys")

        def space_grid(script):
            """Run the scripted rocket and hand back the last grid the engine printed.
            --dump-each prints one grid per present but only the last one survives to stdout,
            so a frame you want to assert on has to be the frame the run ENDS on: pass the
            script that stops there."""
            with open(keys, "w") as fh:
                fh.write(script)
            r = subprocess.run([U2_BIN, "--headless", "--play-map",
                                "--data-dir", os.path.join(ROOT, "original"),
                                "--save-dir", os.path.join(work, "save"),
                                "--script", keys, "--dump-each"],
                               capture_output=True, text=True, timeout=120)
            rows = [l for l in r.stdout.splitlines() if l.startswith("|")]
            return r.stdout, "\n".join(rows[-5:])

        grid, pane = space_grid("B\nL\nH\n9\n9\n9\n")
        if "YOU ARE ORBITING" not in grid:
            # A seed that will not load must not be able to pass by producing an empty screen.
            # The blank-name refusal is the live trap: `NO CHARACTER ON DISK` leaves the
            # overworld status block on screen and every assertion below would read as a
            # different bug. Name the real cause instead.
            check(False, "the scripted rocket reached orbit -- the seed did not load "
                         "(a maxout record needs a name as well as stats)")
        check("FUEL=0" in pane and "XENO=0" in pane,
              "the readout reaches the screen whole (FUEL=/XENO= present in the final grid)")
        check("UEL=" in pane and "ENO=" not in pane.replace("XENO=", ""),
              "no readout label is clipped by the CMD scroll (a shaved `FUEL=` reads `UEL=`)")
        check("H.P." not in pane and "FOOD" not in pane,
              "the overworld's H.P./FOOD block is gone in space (the mode-4 clear)")
        # The clear's real discriminator, and it needed one: with the readout now covering
        # columns 30..38, deleting the clear left the H.P./FOOD check green anyway -- the
        # readout simply paints over the stale block. What the clear demonstrably removes is
        # the launch chatter above it, so assert that, on a run that STOPS at orbit (a later
        # scroll would hide the line for the wrong reason). Without the clear the orbit frame
        # reads `CMD: LAUNCH--ROCKET / HYPERWARP ENGAGED!! / CMD: / CMD:`; with it, the frame
        # starts at HYPERWARP ENGAGED! exactly as 22-right.png does.
        _, orbit_pane = space_grid("B\nL\n")
        check("LAUNCH--ROCKET" not in orbit_pane,
              "entering space wipes the launch chatter (asm/6999 L_006b9a sets mode 4, which "
              "clears; 22-right.png has HYPERWARP ENGAGED! at the top of the pane, not the "
              "launch line)")
        check("HYPERWARP ENGAGED!" in orbit_pane,
              "and what the space screen prints after the clear is on screen")
        # The prompt frame: stop the script at H so the prompt is the last thing presented.
        _, prompt_pane = space_grid("B\nL\nH\n")
        check("HYPERWARP TO:" in prompt_pane and "XENO=" in prompt_pane,
              "the hyperwarp target prompt is on screen while it waits (owner symptom 1)")
        check("FUEL=0" in prompt_pane,
              "the readout is still whole on the prompt frame, not just the last one")

        # ---- leg 7: the PRESENTED frame, not the grid ------------------------------
        # Every check above reads the text grid, and the grid is exactly where the defect
        # hid. space_loop painted the frame and THEN printed the prompt, so the frame that
        # reached the window always carried the text as of before the prompt. Owner report
        # 2026-11-11, the second time in two days: "i'm in space and hit H but its not
        # showing the hyperjump dialog". The grid said HYPERWARP TO:, the window said
        # nothing, and the process sat at 550% CPU re-uploading one unchanged frame.
        #
        # Only a pixel assertion can see that, so this leg drives the SDL renderer
        # headlessly (SDL_VIDEODRIVER=dummy, the route CP4 already uses) and takes the
        # presented frame with the engine's own Ctrl-S, which is swallowed at the key seam
        # and writes the frame that was actually uploaded. Byte-identical frames are the
        # failure mode; more ink in the prompt frame is the fix.
        SDL_BIN = next((p for p in (os.path.join(ROOT, "engine", "u2-sdl-static"),
                                    os.path.join(ROOT, "engine", "u2-sdl"))
                        if os.path.exists(p)), None)
        if SDL_BIN is None:
            print("  SKIP  presented-frame check: no SDL build (make sdl-static) — this is "
                  "the only leg that can see a prompt that never reached the window")
        else:
            import glob

            def presented(script, tag):
                d = os.path.join(work, tag)
                os.makedirs(d, exist_ok=True)
                keys = os.path.join(work, tag + ".keys")
                with open(keys, "w") as fh:
                    fh.write(script)
                subprocess.run([SDL_BIN, "--renderer=sdl", "--play-map",
                                "--data-dir", os.path.join(ROOT, "original"),
                                "--save-dir", os.path.join(work, "save"),
                                "--script", keys],
                               cwd=d, env=dict(os.environ, SDL_VIDEODRIVER="dummy"),
                               timeout=180, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, check=False)
                shots = sorted(glob.glob(os.path.join(d, "screenshots", "*.bmp")))
                return shots[-1] if shots else None

            def ink(path):
                """Non-black pixels of a 24-bit BMP, with no image library: the header
                gives the pixel offset and the rest is BGR triples."""
                with open(path, "rb") as fh:
                    b = fh.read()
                off = int.from_bytes(b[10:14], "little")
                px = b[off:]
                return sum(1 for i in range(0, len(px) - 2, 3)
                           if px[i] + px[i + 1] + px[i + 2] > 60)

            # --play-map because the SDL route has no title to click through, and the two
            # WAITs because Ctrl-S is swallowed inside seam_key_poll -- the screenshot is
            # taken before the poll that consumed it can paint, so a script that goes
            # straight from H to Ctrl-S captures the frame as of before the prompt. The
            # WAIT is what forces the empty poll that paints and presents. Both frames are
            # also required to differ: identical bytes are exactly the defect, and the
            # first draft of this leg passed while measuring two title screens, because
            # without --play-map the menu ate every key and the screenshot was of the
            # splash. The control caught that; a leg that cannot go red is not a leg.
            orbit = presented("B\nL\nWAIT\nWAIT\n\x13\n", "orbit-shot")
            prompt = presented("B\nL\nH\nWAIT\nWAIT\n\x13\n", "prompt-shot")
            if orbit is None or prompt is None:
                check(False, "the SDL run wrote a Ctrl-S screenshot of the presented frame "
                             f"(got {orbit!r} and {prompt!r})")
            else:
                i_orbit, i_prompt = ink(orbit), ink(prompt)
                check(i_prompt > i_orbit,
                      f"the hyperwarp prompt reaches the PRESENTED frame, not only the grid "
                      f"(ink {i_orbit} at orbit vs {i_prompt} at the prompt)")
                check(open(orbit, "rb").read() != open(prompt, "rb").read(),
                      "the orbit and prompt frames are not byte-identical — identical is the "
                      "defect, a window that never changed while the game printed into it")

if fails:
    print(f"\nSPACE VIEW GATE: FAIL ({len(fails)})")
    sys.exit(1)
print("\nSPACE VIEW GATE: PASS")
