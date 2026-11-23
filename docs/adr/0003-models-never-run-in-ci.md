# No model runs in CI; generation is agent-driven and CI gates the artefacts

The bible drafts prose with one model and judges it with another, and CI has no model endpoint and
no secret for one. We ruled that **models never run in CI**: a human-triggered agent step does the
drafting and judging, and commits the prose store plus a per-sentence verdict file, the judge
controls' output, and the hashes of the prompts and model pair it used. CI then gates those
artefacts deterministically — every sentence anchored, verdict present and consistent, controls
present, fresh and showing all three failures — without calling a model.

## Considered Options

Provisioning an API key as a CI secret was rejected: it makes every commit depend on a third-party
endpoint, and the repo already treats expensive derived output as generated locally, committed,
and gated on check-in. Running writer and judge only in interactive sessions with no committed
verdict was rejected as the real failure mode — an unjudged prose store is just a wiki.

## Consequences

"The judge was seen red" becomes a checkable artefact rather than a claim in prose, and changing
the judge model or prompt invalidates every verdict, so a pair change means re-judging every page.
See `docs/plan-game-bible.md` § 5 and § 11.
