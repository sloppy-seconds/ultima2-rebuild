#!/usr/bin/env python3
"""The bot's policy layer, seeded with the one rule P8 asked for.

There was no policy layer before this — `bot/` was scripts. The first rule it needs is the one
`docs/goal-spec.md` P8 names: a bot will attack an invulnerable guard forever, because nothing
tells it to stop.

Two things about the shape of this rule are worth keeping in mind:

* **The engine answers, the policy does not decide.** The observation carries `targets`, four
  values in N,E,S,W order, computed by `u2_monsters_at_attackable` / `u2_monsters_at` in C.
  A Python copy of asm/23E6's slot scan would be a second hand-maintained version of a rule the
  tree already has, which is the failure this project keeps naming. So this module reads a
  verdict; it never derives one from raw slots.
* **P8's stated tell is refuted and this does not use it.** The walkthrough says invulnerable
  guards say "Amusing! No answer", and that string does not exist in the DOS binary (checked
  2026-11-08, whole file, case-insensitive). A policy written to grep the message tap for it
  would never fire. What survives is the phenomenon, and the mechanism is slot 0: asm/23E6
  scans slots 31..1 and never examines slot 0.

The rule is therefore not "recognise a magic sentence" but "an attack that the engine says
cannot land is not retried", which is also the more general rule: a wasted attack is a wasted
turn whatever caused it.
"""

# Observation `targets` values, in N,E,S,W order.
EMPTY = 0
ATTACKABLE = 1
INVULNERABLE = -1

DIRECTIONS = ("north", "east", "south", "west")


class Policy:
    """Remembers which cells have already failed, so a bot cannot loop on one of them."""

    def __init__(self):
        # Keyed by (sector, x, y) rather than (x, y): the same cell in a different sector is a
        # different creature, and a set of bare coordinates would carry a refusal across a
        # sector change and silently make a fresh guard unattackable.
        self.refused = set()

    @staticmethod
    def target(obs, direction):
        """The engine's verdict for the neighbour in `direction`, or None if unknown."""
        t = obs.get("targets")
        if not t or len(t) != 4:
            return None
        return t[DIRECTIONS.index(direction)]

    def should_attack(self, obs, direction, where=None):
        """True only if an attack here can land AND has not already failed.

        `where` is the (sector, x, y) the refusal should be filed under. It is optional because
        the first call — the one that discovers the refusal — is usually made from an
        observation that already names the sector; pass it when the caller knows better.
        """
        v = self.target(obs, direction)
        if v is None:
            # No verdict is not a permission. Attacking on a missing field is how a bot ends up
            # spending turns on a rule it never actually checked.
            return False
        if v == INVULNERABLE:
            if where is not None:
                self.refused.add(where)
            return False
        if v != ATTACKABLE:
            return False
        if where is not None and where in self.refused:
            return False
        return True

    def mark_refused(self, where):
        """File a refusal seen outside this module, e.g. from a MISS in the message."""
        self.refused.add(where)

    def clear(self):
        self.refused.clear()


if __name__ == "__main__":
    # A tiny selftest so the module can be run, not just imported.
    fails = []

    def ck(c, m):
        if not c:
            fails.append(m)

    p = Policy()
    ck(p.should_attack({"targets": [1, 0, 0, 0]}, "north") is True, "attackable north")
    ck(p.should_attack({"targets": [1, 0, 0, 0]}, "east") is False, "empty east")
    ck(p.should_attack({"targets": [0, 0, 0, -1]}, "west", (40, 1, 2)) is False,
       "invulnerable west")
    ck((40, 1, 2) in p.refused, "the refusal was filed")
    # The same cell, once the creature is gone, must be attackable again: a refusal that never
    # expires would strand the bot next to a guard that has since moved on.
    p2 = Policy()
    ck(p2.should_attack({"targets": [1, 0, 0, 0]}, "north", (40, 5, 5)) is True, "fresh cell")
    p2.mark_refused((40, 5, 5))
    ck(p2.should_attack({"targets": [1, 0, 0, 0]}, "north", (40, 5, 5)) is False,
       "refused cell stays refused")
    p2.refused.discard((40, 5, 5))
    ck(p2.should_attack({"targets": [1, 0, 0, 0]}, "north", (40, 5, 5)) is True,
       "clearing the refusal restores the attack")
    ck(p.should_attack({}, "north") is False, "no verdict is not permission")
    ck(p.should_attack({"targets": [0, 0]}, "north") is False, "short array is not permission")
    print("policy selftest: %d failures" % len(fails))
    for f in fails:
        print("  FAIL " + f)
    raise SystemExit(1 if fails else 0)
