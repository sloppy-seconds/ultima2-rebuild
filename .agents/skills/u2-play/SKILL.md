---
name: u2-play
description: Run either binary so a person can see and play it — the original Ultima 2 under Spice86, or the Phase 4 engine — streamed to a browser over noVNC, or driven headlessly from a script. Use when asked to play, demo, show, screenshot, or run the game or the engine, and when a capture needs what the player actually saw on screen.
---

# Playing the original and the engine

Both run in containers on `ai` and need nothing from the host but podman. **Use `up.sh`; never a
hand-run `make` / `cp` / `podman build` / `podman run` chain** — that sequence is historically how a
player got art they had not asked for, because the build ran behind a swallowed redirect and
whatever binary happened to exist got copied into the image.

| binary | command | host port |
| --- | --- | --- |
| the original under Spice86 | `./deploy/spice86-novnc/up.sh` | 6085 |
| the Phase 4 engine | `./deploy/novnc/up.sh` | 6082 |

Both take `--down` to stop and remove, `--rebuild` to refresh the image, and `--status` to print
what the running container is actually drawing — which is the difference between "the container is
up" and "the game is on screen". Engine extras: `--cleanroom` for the substitute art, `--port N`.
Spice86 extras: `--player FILE`, `--no-player`, `--no-patch`.

## Before concluding it is broken

- **"no character on disk!" is not a broken container.** The pristine `references/ultima2/player`
  is the 384-byte blank the game uses for *creation*, not a character. `up.sh` seeds one and prints
  its name; if you bypassed `up.sh`, seed one (see the `u2-character` skill).
- **Do not copy a `tiles.u2tl` into a save dir to "help" it.** It is a cache the engine writes for
  itself, it belongs at `<data-dir>/cache/tiles.u2tl`, and the cleanroom path never overwrites it —
  a stale one looks like evidence about a run that is not happening.
- **The screen is a screenshot, never a memory decode.** The game draws text as glyphs in graphics
  mode; reading `0xB8000` returns box-drawing noise that looks half-plausible. Use
  `python3 tools/spice86_screenshot.py NAME.png`, before *and* after the action.

## Driving it headlessly

An agent shell has no display, which does **not** mean no keystrokes.

```sh
tools/spice86_start.sh --headless --patch-arrows --player FILE --auto-play
python3 tools/spice86_type.py --keys A,Right          # arrows only work because of --patch-arrows
tools/spice86_stop.sh                                  # the only safe teardown
```

Confirm a key landed by reading memory (`--peek`), never by the send returning ok — a dropped key
looks exactly like a no-op. Headless Spice86 ignores SIGTERM and SIGINT, so a CFG harvest must come
from `monitor dumpAll`, not a signal.

For the engine, drive it with a script and read the trace:

```sh
./engine/u2 --headless --play-map --cleanroom --data-dir DIR/content --save-dir DIR/save \
    --script keys.txt --trace=run.trace --dump-each --dump-state > run.out 2>&1
```

Full table of capture tools: `docs/spice86-capture.md` § "Which tool for which job". Container
detail: `deploy/novnc/README.md` and `deploy/spice86-novnc/README.md`.
