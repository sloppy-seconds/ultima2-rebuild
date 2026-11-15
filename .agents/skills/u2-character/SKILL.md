---
name: u2-character
description: Create or seed a Ultima 2 save with a specific character at a specific entry location — sector, cell, stats, gold, items — for the original under Spice86 or for the engine's scenario directories. Use when a test or capture needs the player somewhere in particular, when a gate needs a synthetic sector, or before hand-writing any save-manipulation script.
---

# Seeding a character at a specific location

This is a lookup, not a script. **Do not hand-roll a struct pack** — the record is packed decimal in
places that look like integers, and a hand-sliced file produces a confident table of impossibilities.

## The original

```sh
python3 tools/u2_edit.py show  FILE              # decode a record
python3 tools/u2_edit.py set   FILE sector 20    # one field by name
python3 tools/u2_edit.py setpos FILE 20 20       # x, y
python3 tools/u2_edit.py maxout FILE             # the standard strong test character
```

Hand it to a runner: `tools/spice86_start.sh --player FILE --auto-play`, or
`deploy/spice86-novnc/up.sh --player FILE`. When the terrain matters, `--seed-map PATH` drops a
synthetic `mapxNN` into the run copy.

**Never `setpos` into a SITE sector.** The ones digit of the sector number *is* the site kind, so a
sector like 22 is a dungeon and dropping a coordinate into it creates a state the game never builds
itself. Pick a wilderness sector. `tools/u2_endgame_save.py` is the worked example of building a
whole scenario (max stats, chosen gold/food that does not wrap the packed-decimal field, specific
items deliberately absent so the acquisition can be tested).

To find where to stand to reach a site, ask the census rather than scanning map bytes:

```sh
python3 tools/census.py --what site --where dest_name=Pirates
python3 tools/census.py --what sector --where world=pluto
```

## The engine

A scenario is a directory, not a patched binary: `--data-dir` for content, `--save-dir` for saves,
and the record lives at `<save-dir>/<SLOT>/PLAYER`.

For a **synthetic** sector — the case a gate needs, because CI has no copy of the game — extend
`engine/tests/cp6_commerce_seed.py` rather than writing a new seeder. `python3
engine/tests/cp6_commerce_seed.py DIR CASE` writes content, save and the sector geometry the case
requires, and `--help` lists the cases. Each case exists because of a specific byte-level claim;
`docs/commerce.md` says which.

For a **real** sector, copy the game content and edit the record with `tools/u2_edit.py`.

## After changing save handling

Run `python3 tools/save_cross_load.py`: it loads the original's record into the engine and has the
original load a record the engine wrote, reading both back out of live memory. It is deliberately
absent from `make test` (it needs gitignored game files), so nothing else will catch a regression
here. Required whenever `engine/src/core/save.c`, the write-back in `engine/src/core/world.c`, the DS
layout in `engine/src/core/state.h`, or the harness changes. `docs/save-format.md` § 9 is the
recipe, § 8 the reason a whole-window byte comparison is not achievable.

Detail: `docs/tooling-procedures.md` § "Build, play, seed", `docs/save-format.md`.
