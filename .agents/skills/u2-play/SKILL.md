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

Both take `--down` to stop and remove, `--name LABEL` to rename the container, and `-h` for the
real list. They are **not** the same flag set, and the skill used to claim they were:

| flag | original | engine | what it does |
| --- | --- | --- | --- |
| `--rebuild` | yes | **no** | refresh the image |
| `--status` | **no** | yes | print what the running container is actually drawing — the difference between "the container is up" and "the game is on screen" |
| `--player FILE` | yes | — | seed a saved character (then press `P`) |
| `--resume [DIR]` | yes | — | resume a previous run exactly; `DIR` defaults to the newest run dir |
| `--no-player`, `--no-patch` | yes | — | blank disk; leave arrows unpatched |
| `--cleanroom`, `--port N`, `--load-save` | — | yes | substitute art; port; a specific save |

## Death, and getting back into a game

**Death in the original is terminal.** The screen prints `ENDGAME IS DEAD!` and sits there: no
"press any key", no return to the title menu, nothing in-game to press. The only recovery is a hard
restart of the program, and a plain restart builds a fresh run dir from the pristine files, which
orphans everything the last run wrote. So:

```sh
./deploy/spice86-novnc/up.sh --resume            # newest run dir
./deploy/spice86-novnc/up.sh --resume DIR        # a named one
```

`--resume` overlays that run's `player` **and** its `mapx*`/`monx*`/`tlkx*` over the pristine copy
before launch, and prints which files it restored. Sector files are included on purpose: the point
is to resume exactly the last game, damage and all. Note the image bakes `entrypoint.sh` in, so
editing it needs `--rebuild`.

What is worth resuming is only what the game actually wrote, and that list is short: `Q` outdoors on
Earth writes `player` plus that sector's `mapx`/`monx`, and entering or leaving a site on Earth
writes `player`. **Nothing off Earth is ever written**, so a Planet X trip survives only if you got
home and saved. `asm/0C62_death.asm` and `docs/save-format.md` § 10 carry the byte evidence.

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
