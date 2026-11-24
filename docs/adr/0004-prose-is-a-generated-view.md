# Prose is a generated view; humans never edit it

The bible's prose is model-written, committed, and anchored to fact ids, and we ruled the
generator is its **only writer** — the same rule ADR 0002 sets for generated tables. A human who
wants different wording changes the fact or the prompt, never the sentence. This is what makes the
epic's central claim true rather than aspirational: a corrected decode invalidates the prose,
because the prose is a view over facts and not a text someone maintains.

**Amended 2026-11-23, for the two things this rule did not cover.** The owner's objection was that prose is how a quirk gets surfaced at all, and that is correct — but it pointed at a different gap than it looked like. The loop already generates prose and already judges it: a writer drafts from facts, a critic checks each sentence against the facts it cites, and failures land in a dropped file. So the critic existed. What it could not do is validate an insight that was never a fact, and a critic with nothing to check against degrades into judging style while looking like it judged truth.

Two additions, both of which preserve the invalidation guarantee rather than trading it away:

**Quirks are facts, not prose.** An observation like "no stat add site has a ceiling guard, so stats wrap to 00" is a `rule` with a named evidence site, not a flourish. The kind enum already covers it. What was missing is a way to *find* such claims, so the amendment adds an **observation-mining pass** that derives candidate facts from the index tables — a field written and never read, a guard that never fires, a claimed edge never observed executing, a cap that does not cap — each arriving pre-cited and graded by its own evidence. This is the answer to "how do we surface interesting things": not looser prose, and not hand-written asides that are claims nobody can check, but machine-findable tensions between facts, at scale.

**An editorial lane exists, and it is visibly ungraded.** Voice — "this is the game's strangest shop" — cannot be fact-backed and should not pretend to. It lives in a separate lane, never in the same sentence as a graded claim, marked as not derived from the store. The critic reviews it for the one thing it can be reviewed for: that it contains no checkable claim.

The rule's enemy was never prose. It was a second hand-maintained copy of *facts*, and neither addition is one. "A human who wants different wording changes the fact or the prompt, never the sentence" still holds for every checkable claim in the store.

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

The critic gains one check with the amendment: a sentence containing a checkable claim must cite a
fact that supports it, and an editorial sentence must contain none. Both failures go to the dropped
file like any other. **This widens what the critic must police, so it must not be built until the
judge's existing controls pass their intended questions** — all three currently fail them, and
expanding an unproven judge is how fluent wrong prose gets manufactured faster.
