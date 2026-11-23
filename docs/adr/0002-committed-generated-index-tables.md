# Generated index tables are committed, and each file has exactly one writer

The code index writes generated tables under `disasm/index/`. We ruled they are **committed**,
mirroring `disasm/census/`, and that **each file has exactly one writer** — the generator that
produced it, enforced by the existing freshness gate rather than by convention. Committed so a
change to derived facts shows up as a reviewable diff; sole-writer because the moment a human can
type a conclusion into a generated table it becomes a second hand-maintained copy and stops being
a view.

**Amended 2026-11-23, the same day, for scope.** The original wording read "the generator is the
only writer" of a directory, which the game bible's fact store immediately broke: behavioural
rules come out of `asm/` headers, stat-encoding facts come out of `tools/bcd_census.py`, and
place facts come out of `tools/census.py`, and all three belong in `disasm/facts/`. The rule is
therefore per file, not per directory — `asm.tsv` has one writer, `bcd.tsv` has one writer, and
so on — because sole-writer and freshness are only meaningful as per-file properties. The
alternative, one extractor re-emitting another tool's output so a directory has a single author,
is a copy with extra steps that also drops the grade the originating tool already carried.

## Considered Options

A gitignored build dir was rejected: it loses reviewable generated diffs, and an unreviewed
derived table is exactly how a stale conclusion gets cited as authority. Hand-editing with a
gate that flags it was rejected because a gate that permits a bad state and complains about it
still permits it.

## Consequences

Every index ships `--verify` and `--check-fresh`, both wired into `tools/check_all.sh` and
`tools/generated_fresh.sh`, and both declared CI-runnable — an index gate that needs
`original/` is a bug in the gate. The freshness check is not optional ceremony: this project had
`place-index.tsv` sit stale with unjoined rows while its own `--verify` reported green, because
`--verify` re-derives in memory and never compares against the file everyone reads.
