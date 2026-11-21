# Handoff — 2026-10-21 (evening)

> **ARCHIVED 2026-10-22.** Superseded by `docs/archive/handoff-2026-10-22.md`. The § 4 open-work
> list has been triaged against current state and carried forward there: some items
> closed, some still open, and item 5 (`0x4C`) resolved in a way that exposed a
> defect in `docs/tile-art-spec.md` — see the new handoff. Not an entry point.

Supersedes `handoff-live-capture.md` for anything after midday. Everything below is
committed and pushed to `origin/main`. Working tree clean. Byte-exact proof green
(37,344 B identical) and `make -C engine test` CP0–CP4 all pass.

**Still running:** the map viewer HTTP server, `python3 -m http.server 8099`
(`http://192.168.86.50:8099/map-viewer.html`), pid was `2007992`. Nothing else —
no Spice86 session, no game window. Stop the server with `kill <pid>` if unwanted.

---

## 1. What the session actually established

These are new facts, not restatements of the morning handoff.

**The 64 art entries are not 64 terrains.** They split into ground, structures,
creatures, borders and the A–Z letter fixtures. Only `0x00`, `0x08`, `0x0C` appear
in 25+ of 33 sectors; 32 of the 64 values occur ≤50 times in total. Measured, see
`disasm/tile-art.tsv` and `docs/passability-findings.md` F13.

**Colour in this game is semantic.** Measured from `tmp/tiles/tiles.json`: 55 of 64
tiles use white alone; the only two cyan tiles are grass and forest, the only two
magenta are water and marsh. **Cyan = vegetation, magenta = liquid, white = solid.**
Graded inferred from a tight pairing.

**The map font's space is `0x7C`** (art `1F`), not `0x04`. `0x04` is the blank
ground town text sits on. Getting these apart is what makes `map_signs.py` output
read as sentences: `THINGS ARE NOT AS THEY SEEM`, `SAIL OUT THE NORTH CHANNEL`,
`CCCP TEST LAUNCH`.

**There is no Q glyph.** Art `30`, where Q falls in the A–Z sequence, is a
structural tile and never functions as a letter in any non-structural sector.
Confirmed by owner and by bytes.

**The letters spell real shop and place names**, recovered verbatim into
`disasm/map-signs.txt` via `tools/map_signs.py`. Each era is identifiable from its
shops, not from a year label: `mapx41` is Soviet (`CCCP DEFENCE`, `KGB`, `RED
SQUARE`), `mapx03` a dystopia (`THIEVES GUILD`, `TORTURE CHAMBER`), `mapx23` and
`mapx33` share identical castle signs — same place, two times.

**There is no examine command.** `cmd_L` is LAUNCH, `cmd_I` is IGNITE TORCH,
`cmd_K` is KLIMB. The time period is printed by a numeric dispatch at `0x3546` on
`%di`: 1→`9,000,000 B.C.`, 2→`1423 B.C.`, 3→`1990 A.D.`, 4→`2112 A.D.`, with
`LEGENDS!` reached from a branch outside the range. Renamed to
`asm/3546_time_index_message.asm`. **Who writes `%di` is still unknown.**

**Two Spice86 bugs found and fixed upstream-side.** The MCP screenshot returned raw
bytes instead of base64, and `GraphicalMoveChars` built its copy from window
*coordinates* instead of *extents* — so with `CL=0` the copy width was zero, the
scroll was a no-op that still blanked the row, and every message the game printed
was silently destroyed. That second one is why six render-pipeline experiments all
came back inert. Vendored build now carries both fixes: `tools/spice86/`,
`Spice86.dll` md5 `fec0a93f644bd056f5c14fcf93f8f0a3`.

**The engine already had a tile blit path.** See § 3.

## 2. What was built

| thing | what it's for |
| --- | --- |
| `tools/map_viewer.py` | self-contained HTML map viewer; sector picker, zoom, tile borders, class overlay, byte mask, and a cleanroom/original/side-by-side art switch |
| `tools/map_signs.py` + `disasm/map-signs.txt` | recovers sign text from the map files |
| `docs/tile-art-spec.md` | cleanroom specification, § 6 now fixes colour per tile |
| `engine/assets/tiles/cleanroom/` | 64 substitute SVGs + generator + `PROVENANCE.md` |
| `engine/tools/tiles2h.py` | SVG → `engine/src/core/tiles_cleanroom.h`, 2bpp, committed so builds need no Python |
| `engine/src/core/tiles_builtin.c` | compiled-in substitute art as a `u2_tiles` |
| `--cleanroom` | one opt-in flag for all substitute assets; tiles now, sound later |
| `make play` / `engine/tools/play.sh` | puts the game on screen; synthesises the display env |
| `tools/spice86_start.sh --seed-map` | installs a synthetic sector map **before** launch |
| `tools/u2_mkprobe.py --start-x` | probe maps that avoid the toroidal wrap |

Two guards worth preserving: `make test` fails if the committed header drifts from
the SVGs, and fails if the packed bytes don't decode back through `u2_tile_pixel()`
to the pixels the SVGs contain. Both were proven to fire by tampering, not assumed.

## 3. Corrections I made, so they don't get re-trusted

**"Nothing blits tiles" — wrong, said twice.** `u2_frame_blit_tile()` and
`u2_frame_draw_view()` already existed in `core/frame.c`, `overworld.c` already
called them, and `frame_check.c` already tested them. I grepped `renderer_sdl.c`,
found nothing, and concluded wrongly — but this codebase deliberately keeps every
rendering semantic in `core` so "a backend cannot get the game wrong", so the SDL
file was the wrong place to look.

**Three wrong answers about the blocked-move echo.** First the code claimed no
direction word was echoed (invented, and written into a comment as if observed).
Then I guessed a line break. Only on the third pass did I read the bytes:
`--INVALID MOVE!` at file `0x0C05` contains no CR, so it cannot start a line, so
the direction word must already be on it. One read settled what two guesses missed.

**"There's no display."** Wrong — my shell has no `DISPLAY`/`WAYLAND_DISPLAY`
because it isn't a graphical login session, but the sockets exist and
`spice86_common.sh` already synthesises them. I should have copied the working
mechanism instead of concluding from my own empty environment.

**A stale spec pin in the cleanroom repo.** Their `PROVENANCE.md` cites blob
`f214a89` (the original spec) but they implemented ink assignment from `c034198`.
Flagged, not yet fixed by them.

**Pattern worth naming:** every one of these came from asserting behaviour from a
reading of the engine instead of measuring the original or the working system. The
project rule exists for a reason.

## 4. Open work, in priority order

1. **No test drives the interactive command loop.** This is the structural gap that
   let three separate bugs hide behind a green suite. A scripted test exercising
   arrows, `P`, space and blocked moves through the same path the SDL build uses
   would have caught all of them.
2. **Status-cursor fix is unconfirmed.** `b310b27` makes `u2_status_draw` restore
   the cursor, which should end `GOLD=0000S` and the shifted `ASS/CMD:ASS`
   fragments. Owner confirmed the invalid-move fix but **never confirmed this one**.
   Check in `make play` before trusting it.
3. **Passability table is essentially unmeasured.** The first sweep measured 15 of 64
   and all 15 were letters, so it taught nothing. Needs: per-step retry so a dropped
   key can't read as terrain, and a mid-map start. Better still, use **warp** (edit
   save sector + entry, launch) to test real tiles in real context rather than
   synthetic probe maps — that also sidesteps the layout assumption entirely.
4. **Who writes `%di` before `0x3546`** — would settle five time periods against ten
   sector tens-groups.
5. **`0x4C` signpost question unresolved.** Twelve in a block in `mapx41` suggests a
   wall segment, not the signpost. The place-description text is not plain ASCII in
   `tlkxNN` (384 bytes, `strings` finds nothing) — probably in the exe.
6. **`0xC0` is an event tile** — threw the player y=20→57 in a probe. 1311
   occurrences, exactly 262 in each of `mapx25/35/45`. Needs its own investigation;
   a probe harness that can only report pass/block cannot represent it.
7. **i18n TODO** recorded at the end of `docs/phase4-state.md`. Six decisions noted;
   the ASCII-only 8×8 font is the biggest cost and should be scoped first.
8. **Upstream PRs** for both Spice86 fixes — owner said they're handling these.

## 5. Things that will bite you

- **`pgrep`/`pkill -f` matches its own shell.** Kill by pid.
- **Memory reads pause the CPU**; keys sent into that window are silently lost.
  Settle ~0.5–1 s after reads.
- **`ctx_execute_file` is blocked** for `/var/home/...` paths (workspace root is
  `/home/...`). Use `ctx_batch_execute` or `bash`.
- **`make play` needs the display synthesis** — a bare `./u2-sdl` in an agent shell
  silently renders to the terminal instead.
- **Probe maps must be installed before launch** (`--seed-map`); the game reads
  `mapxNN` at startup and will run real terrain while your harness believes
  otherwise.
- **`engine/build-sdl/` was tracked** until `c99aded`. If you see 64 modified `.o`
  files again, something re-added them.
