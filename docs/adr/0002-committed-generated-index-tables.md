# Generated index tables are committed, and the generator is their only writer

The code index writes generated tables under `disasm/index/`. We ruled they are **committed**,
mirroring `disasm/census/`, and that **the generator is the only writer**, enforced by the
existing freshness gate rather than by convention. Committed so a change to derived facts shows up
as a reviewable diff; sole-writer because the moment a human can type a conclusion into a
generated table it becomes a second hand-maintained copy and stops being a view.

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
