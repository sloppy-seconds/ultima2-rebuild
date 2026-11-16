# G85: the engine's warp, rendered and compared to the original's

G85 was filed as "built and gated structurally; nobody has watched it". The blocker on record
was "needs a frame per step", which meant a human. It did not. The harness attaches PNGs, and
the only thing actually missing was a flag that could write a frame from the middle of a
509-frame animation — `--dump-screen` could only write frame zero.

## How the frames were made

Both sides were staged the same way, which is the part that makes the comparison mean
something: the same seeded record, the same sector, the same cell beside the same rocket pad,
the same four keys.

```
# original, driven by the bot
python3 bot/launch.py                      # -> captures/spice86/bot-launch-mars/04-warp-trail.png

# engine, driven by a script
engine/u2-sdl-static --data-dir <game copy> --save-dir <saves> \
    --start-at 33,4,41 --script warp3.keys \
    --dump-screen-every 15 'eng-%d.ppm'    # warp3.keys = UP B L H 6 2 3
```

`--start-at` and the `UP` script token both had to be added to make this possible; see the
two commits before this capture. `UP` is not cosmetic: `0x0e` in a script is *not* Up, because
`asm/4F58..4F60` makes the game discard three of the four direction control bytes, so the
byte form is a key the game throws away and a script using it moves nobody while looking
correct.

## What the two actually look like

`original-warp-trail.png` — the ship is a `+`, sitting right of centre and above centre, and
the streaks radiate from **the ship** through a full 360 degrees. The rays are sparse and
dotted: one pixel per star per frame at a growing radius, so each ray is a sample tape.

`engine-warp-017.png`, `engine-warp-030.png` — the streaks converge on a dense knot near the
centre and extend overwhelmingly along **one diagonal axis**, upper-right to lower-left. The
field is far denser than the original's and reads as a beam rather than a burst. There is no
ship marker on screen at all.

**So the divergence is real and it is structural, not cosmetic:** the original's stars move
*away from the ship in every direction*; the engine's move along a shared direction, which is
what produces a smear instead of a burst. The trail mechanism added on 2026-11-15 (mode 0 not
erasing, radii growing with `warp_frame`) was necessary — without it the warp was a shimmer —
but it is not sufficient, because growing every star's radius along one vector is a streak
field, not a radial one.

## Second finding, in the readout

The left column matches the original exactly, including its odd spelling: `XENO=6 YAKO=2
ZARO=3` above `PREPARE FOR HYPERWARP!` / `HYPERWARP ENGAGED!`.

The right column does not. The original's target triple reads `XENO / YENO / ZARO`; the
engine's reads `XENO / YARO / ZABO`. Two of the three labels differ. The values differ too,
but that is expected — this run went off-course, so it is a different destination. The label
words are strings, and one of the two sets is wrong.

## What this does NOT settle

- The engine went **off-course** (`(fuel & 3) == 3` with fuel 7), so these frames are the
  deep-space warp, not a successful transit. The radial-vs-beam question should be re-checked
  on an on-course run before anyone calls the geometry settled.
- Frame rate is not visible in stills. G83 measured 119,055 cycles a frame on the original;
  whether the engine's 25 ms reads the same to a player is a different question from the one
  answered here.
- The tone (G87) is decoded and wired but is not in these frames, obviously.
