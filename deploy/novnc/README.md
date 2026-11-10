# u2-novnc — play the engine in a browser, on `ai`

The engine's SDL build running headless in a podman container, streamed over noVNC.
This is how a person without a local display plays the reconstruction. Watching the
original through Spice86 is a separate, already-working setup
(`docs/phase4-state.md` § "TODO: VNC desktop container for remote watching"); this is
the same shape pointed at our own binary.

## Run it

```sh
cd /var/home/paul/Projects/decompile-u2-v2
./deploy/novnc/up.sh
#   --cleanroom   deliberately ship the substitute art
#   --port N      host port (default 6082; 6081 is taken by an unrelated container)
#   --status      print what the running container is actually drawing
#   --down        stop and remove it
```

**Use `up.sh` rather than the manual chain.** Until 2026-10-29 this README documented a
hand-run sequence of `make` / `cp` / `podman build` / `podman run`, and that sequence is
how a player got art they had not asked for. The build ran behind a swallowed redirect, so
a build that quietly did not rebuild produced no signal, and whatever `engine/u2-sdl`
happened to be got copied into the image. Nothing between `make` and the running container
looked at the artifact.

`up.sh` checks the artifact at four points: the build runs with output visible, the binary
must be newer than every source it is built from (or it forces a clean rebuild), the
binary is **run** and must announce the art mode it was asked for, and after the container
starts its own log must say the same. The third check is the load-bearing one: it catches a
stale binary, a wrong default, a dropped flag, or a build system that did nothing, and it
does not care which. It has a control -- a planted wrapper that prints the substitute-art
line makes `up.sh` exit 1 and refuses to stage the binary.

If you want to seed a character or change the save dir, the manual steps below are still
the right reference for that; they are just no longer the way to deploy.

<details>
<summary>Manual steps, for seeding a character or picking a different port</summary>

```sh
mkdir -p /var/tmp/u2/novnc-saves/PLAYER
cp original/player /var/tmp/u2/novnc-saves/PLAYER/PLAYER
tools/u2_edit.py set /var/tmp/u2/novnc-saves/PLAYER/PLAYER sector 20
tools/u2_edit.py setpos /var/tmp/u2/novnc-saves/PLAYER/PLAYER 20 20
```

The record lives inside a slot directory that shares its name (`<save-dir>/PLAYER/PLAYER`,
docs/save-scoped-worlds.md D1), so the path is spelled twice on purpose.

Do not copy a `tiles.u2tl` into the save dir to "help" it. It is a cache the engine writes
for itself, it lives beside the content it came from (`<data-dir>/cache/tiles.u2tl`) and
not with a save at all, and the cleanroom path never overwrites it -- so a stale one sits
there looking like evidence about a run that is not happening, and it cost one
investigation its correct conclusion. `up.sh` deletes one from the old save-dir location
on every deploy. Because `/content` is mounted read-only here the engine cannot write its
cache at all, and it re-extracts the atlas from the executable on every start; that is
the designed fallback, not a defect.
</details>

Keyboard from the browser reaches the game, so this is play, not watching: arrow keys
walk, `A` + a direction attacks, `E` enters a town, `Q` saves, `ESC` leaves.

## Five things that cost time, in the order they bit

1. **SELinux.** Without `:z` on the bind mounts the container starts, the engine
   starts, and the only trace is `ls: cannot open directory '/content': Permission
   denied` inside it. The failure looks like a missing file, not a denied one, and
   the engine's own message ("no executable and no cached tiles") points at the
   wrong thing entirely.
2. **Xvfb must be 24-bit.** `320x200x8` — which is what you reach for when the game is
   four colours — makes SDL2's X11 backend refuse the visual with
   `Palettized video modes are no longer supported`, and the engine reports it as
   `backend initialisation failed`. The display depth has nothing to do with the
   game's palette; the engine composes a 32-bit RGBA surface and applies the CGA
   palette itself.
3. **The tiles cache is validated against the executable's digest**, and since D4 it
   lives beside the content (`<data-dir>/cache/tiles.u2tl`), not in the save
   directory. This container mounts `/content` read-only, so the cache is never
   written and every start re-extracts; that is the designed fallback and it costs
   extraction time, not correctness. Mount the save directory separately either way.
4. **Ports.** 6080 and 5900 belong to the Spice86 watching container, so this one
   defaults to 6081/5901 on display `:98`. Override with `-e NOVNC_PORT= -e
   VNC_PORT= -e DISPLAY=` if you run a second one.
6. **`podman restart` needs the X lock gone.** A restart reuses the container's
   writable layer, `/tmp/.X98-lock` survives, and Xvfb dies with the fatal
   `Server is already active for display 98` — the container looks quiet, not broken.
   `entrypoint.sh` removes its own display's lock before starting Xvfb.

**Where you start.** Sector 20 at (20,20) is the game's own starting map:
`engine/src/game/creation.c` sets `entry_x = entry_y = 20`, `sector_first = 2` citing
bytes `0x5BDC..0x5BE8`, and `DS:4A` is never written by creation so the second digit
stays 0. The container's `PLAYER` is built from the pristine `original/player` plus
those two edits rather than from a test character parked in a town.

5. **Xvfb must be at least as big as the SDL window**, which is `U2_SCREEN_W/H *
   SDL_SCALE` = **640x400**, not the 320x200 the game draws into. Setting the display
   to the native resolution — which is what you would guess — shows the top-left
   quarter of the game and nothing anywhere reports it: no error, no warning, just a
   cropped picture. If `SDL_SCALE` in `renderer_sdl.c` changes, `GEOMETRY` changes
   with it.

## Exposure, and why it is the way it is

Published on `0.0.0.0:6081` with **no VNC password**. That is the owner's call, made
on 2026-10-27 — *"it's just our LAN, it's secure, no need for a password"* — and it is
recorded here so nobody re-litigates it or, worse, half-applies it: the earlier
draft of this file bound loopback and required an SSH tunnel, and the difference
matters if this host ever stops being on a trusted network.

One layer is still there and is not just decoration: `x11vnc` runs with `-localhost`,
so the raw VNC port (5901) is reachable only from inside the container's network
namespace and never from the LAN. What is exposed is the noVNC web socket on 6081,
which is the same thing either way, but it means adding a password later is a
one-flag change in `entrypoint.sh` and not a re-plumb.

Audio is deliberately silent: the container has no sound server, `audio_sdl` fails to
load `libjack.so.0`, and the engine continues without it and says so. That is the
choice the TODO asked for made explicitly — silent by default, not broken by default.

## What is and is not in the image

The image carries the engine binary, SDL2, Xvfb, x11vnc, novnc and websockify. **No
game content.** `original/` is mounted read-only at `/content` and the save directory
read-write at `/saves`, per the licensing rules in `AGENTS.md`. The binary is copied
in rather than compiled in the image so that what runs is what the host's
`make -C engine test` actually tested.

## Known gaps

- The engine exits when the player presses `ESC`, and the entrypoint then tears down
  Xvfb and the container stops. Restart it to play again. A long-lived service would
  want a quadlet `.container` with `Restart=on-failure`, and the TODO in
  `docs/phase4-state.md` says so too; this is deliberately a run-it-when-you-want-to-
  play thing until the game has an ending worth resuming.
- `--play-map` skips the title and character creation. Drop `U2_ARGS` to
  `--sdl` for the full flow.
- Nothing here is wired into `make test`, and nothing in `make test` may come to
  depend on it: the CP4 dummy-driver assertion already covers the SDL pixel path for
  CI, and this container exists for a human with a browser.

## Asking a running session what it shows

`u2-shot` is in the image and speaks VNC to the local `x11vnc`, so it needs no display,
no xwd and no ImageMagick:

```bash
./up.sh --shot /var/tmp/u2/frame.png          # what is on screen right now
podman exec u2-novnc u2-shot --press "P,Left" --gap 0.6 --out /tmp/f.png
```

`--press` injects key events (letters, or `Up/Down/Left/Right/Return/Esc/Space/Tab/
BackSpace`, case-insensitive) and then writes the frame, so a whole interaction can be
driven and observed from one command. It connects `shared=1`, so it never evicts the
browser tab a person is playing in — but it does put keys into that same game, so do not
press anything while the owner is holding the keyboard.

The reason both exist is that nothing else in the tree can answer "what is on the screen
right now": the play API returns the 40x25 text grid, and `--dump-screen` writes only
the first presented frame. For a scripted run through the shipped menu path prefer
`SDL_VIDEODRIVER=dummy ./u2-sdl-static --renderer=sdl --script FILE` (CP5's menu-reload
case) over injecting into a live display; this is for the live one.
