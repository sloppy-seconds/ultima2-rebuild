# Prose is a generated view; humans never edit it

The bible's prose is model-written, committed, and anchored to fact ids, and we ruled the
generator is its **only writer** — the same rule ADR 0002 sets for generated tables. A human who
wants different wording changes the fact or the prompt, never the sentence. This is what makes the
epic's central claim true rather than aspirational: a corrected decode invalidates the prose,
because the prose is a view over facts and not a text someone maintains.

## Considered Options

Allowing in-place prose edits was rejected: the first awkward sentence invites it, and once it is
allowed the invalidation guarantee is false and the store is a hand-maintained copy at scale — the
G90 failure with better grammar. Gitignoring the prose and regenerating on demand was rejected
because it loses reviewability, which is the whole reason generated artefacts are committed here.

## Consequences

Staleness replaces byte comparison, since LLM output is non-deterministic: the gate checks that
recorded inputs still match the tree and that no cited fact's evidence has moved. A page whose
cited fact was deleted goes `void` and is not rendered at all, rather than rendering shorter. See
`docs/plan-game-bible.md` § 12.
