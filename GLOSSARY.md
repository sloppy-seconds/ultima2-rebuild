# Glossary

The domain language of this project. Architecture vocabulary — **module**, **interface**,
**depth**, **seam**, **adapter**, **leverage**, **locality** — is defined in the
`codebase-design` skill and is not repeated here.

This file was created on 2026-11-21 when the architecture review settled the first terms that
needed an owner. Add a term when it stops being a phrase and becomes a thing two people refer
to; do not pre-seed it.

## Sector residency

The state of a sector's data being loaded and stamped and therefore drawable and walkable.
Not "loaded": a sector whose map bytes are in memory but whose creatures have not been stamped
is not resident, and the difference has produced repeat bugs.

Making a sector resident is an ordered sequence — map, demo overlay, monster data, demo
overlay, spawn, clear spawn cells, dialogue, restamp, view mode, status, the player-facing
note — and the order is load-bearing. It currently has no module and three callers, which is
the defect [issue #14](https://github.com/sloppy-seconds/ultima2-rebuild/issues/14) exists to
close.

**Ruled 2026-11-21.** The **DS mirror** (`engine/src/core/state.h`) is authoritative for
position, sector and view mode; the residency module publishes to it at one quiescent point
through named accessors. The overworld does not own a second copy.

## Quiescent point

The one moment in a turn at which state may be published to the DS mirror: after the world has
settled and before the next key is read. Publishing anywhere else is what produced the silent
revert recorded at `overworld.c:1793-1798`, where a resumed position correct on read-back
reverted to the entry point on the first drawn frame.

## Command tail

The shared epilogue every command owes the player: the echoed word, the prompt, a status redraw
where one is due, and the creature turn. Today it has no owner — six handlers set a bare letter
and six hand-written arms in the walk loop each re-assemble it, so whether the sweep and the
band redraw run is a fact about a call site rather than a fact about the command.

**Ruled 2026-11-21.** A handler awaiting a direction returns a continuation — the word, the
direction callback, whether the status redraws, whether it counts as a turn — and one
`u2_cmd_tail` owns the epilogue. The loop stops switching on a letter.

## Seed module

The module that puts the engine into a named state for a test: sector, cell, hit points,
inventory, a tile, a world directory. It is the interface the test suite has been re-implementing
by hand in about eleven places.

**Ruled 2026-11-21.** Offsets are generated from `state.h`, never copied from it. A test names
intent — "the player is in sector 32 with the ankh" — and does not write `p[0x49 - 0x36]`.

## DS mirror

`engine/src/core/state.h`: the C struct that *is* the original's DS segment, every named field
pinned by `_Static_assert` so a layout drift is a build failure. It is also the save format.
It is the one offset table, and the authority any other offset source must derive from.

## Art precedence

The order the engine searches for art: explicit theme dir, then the roots sublayers, then
`themes/`, then a dev fallback, once. It has three owners today and each gives a different
answer, which is a recorded owner-reported bug rather than a tidiness complaint. See
[issue #10](https://github.com/sloppy-seconds/ultima2-rebuild/issues/10).
