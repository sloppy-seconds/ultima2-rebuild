# The DS mirror names a byte; the asm tree owns what touches it

`engine/src/core/state.h` is the one offset table and the authority any other offset source must
derive from, and the footprint index the code epic generates is an offset source. We ruled the
split rather than picking a parent: **the mirror is authority for what a byte is called, `asm/`
is authority for what touches it.** The index may read the mirror's names and must never invent
one, so a generated table cannot become the silent parent of a hand-pinned struct.

## Considered Options

Making the index the parent — state.h derived from generated rows — was rejected: it would make a
generated view the authority over `_Static_assert`-pinned layout and over the save format, and a
parser bug would then propagate into the struct instead of being caught by it. Letting both
carry names was rejected as two hand-maintained copies of the same fact, which is the failure
`AGENTS.md` exists to prevent.

## Consequences

The index needs a real `state.h` parser, and offsets it surfaces that the mirror has not named
are published as a queue rather than named by the index itself. See `docs/plan-code-index.md`
§ 3.1, § 3.3 and § 9.
