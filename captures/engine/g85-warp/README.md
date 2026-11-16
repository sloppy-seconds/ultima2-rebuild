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

## What the two actually look like — read at 3x

**The first version of this section was wrong in both of its findings, and the error is worth
recording more than the conclusion.** It was written from the 320x200 originals as attached,
which is a thumbnail. Re-rendered at 300% (`magick in.png -scale 300% -alpha off out.png`),
both claims collapse.

What was claimed, and what is true:

| claimed | actual |
| --- | --- |
| original radiates from the ship, engine smears along one diagonal axis — "a beam, not a burst" | **both are radial bursts through a full 360 degrees.** The engine's distribution is lumpier and has a diagonal emphasis, but it is not a beam. |
| engine draws no ship marker | **it does.** A `+` sits upper-left of the convergence knot, at a similar offset to the original's. |
| labels differ: original `XENO / YENO / ZARO`, engine `XENO / YARO / ZABO` | **identical on both sides: `XENO / YAKO / ZABO`.** `YENO` and `ZARO` do not occur anywhere in `asm/` — `grep -rh 'db "' asm/ \| grep -o '"[^"]*\(XEN\|YAK\|YEN\|ZAB\|ZAR\)[^"]*"'` returns `  XENO=`, `XENO=`, `  YAKO=`, ` YAKO=`, `  ZABO=`, ` ZABO=` and nothing else. The engine was never wrong, and the tree already said so. |

The left column matches exactly, both sides: `XENO=6 YAKO=2 ZABO=3` above `PREPARE FOR
HYPERWARP!` / `HYPERWARP ENGAGED!`.

## The divergence that survives looking

**Density, and it is the stagger.** The original's rays are sparse: mostly single dots with wide
gaps between them, a few short segments, plus clusters of stationary dots in the corners that
are stars not yet in motion. The engine's rays are near-solid lines. Same geometry, roughly
ten times the ink.

That is the mechanism G88 decoded, showing up as a visible difference. `cs:0x6cac` is 16
entries of `index + 1`, indexed by `(x ^ 0xff) >> 4`, compared against a global counter at
`DS:4C3` — a per-x-bucket gate, so only part of the field updates on a given pass. The engine
has no such gate and redraws every star every frame, which is exactly what turns a dotted ray
into a solid one. So G88 turned out to matter for the appearance after all, just not the way
its own entry claimed: it explains the sparseness, not the radial shape.

The remaining honest differences, in order of confidence:

1. **No stagger gate** — engine rays solid, original dotted. Byte-backed, and now actionable.
2. **Engine's ray distribution is lumpier**, with a diagonal emphasis and a denser knot. Cause
   unknown; this is what G89 is about.
3. **Right-column values differ** (`YAKO=06 ZABO=06` vs `YAKO=02 ZABO=03`), but this run went
   off-course so it is a different destination and not necessarily a divergence.

## What this does NOT settle

- The engine went **off-course** (`(fuel & 3) == 3` with fuel 7), so these frames are the
  deep-space warp, not a successful transit. The radial-vs-beam question should be re-checked
  on an on-course run before anyone calls the geometry settled.
- Frame rate is not visible in stills. G83 measured 119,055 cycles a frame on the original;
  whether the engine's 25 ms reads the same to a player is a different question from the one
  answered here.
- The tone (G87) is decoded and wired but is not in these frames, obviously.
