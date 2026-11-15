#!/usr/bin/env python3
"""Proof the adapter works: the bot plays the ORIGINAL, not the engine.

Not a test — a demonstration with output, run once to establish the path is real.
The gated version is bot/target_check.py.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from target import make                                    # noqa: E402

t = make("original")
obs = t.state()
c = obs["char"]
print("target      :", t.name)
print("mode        :", obs["mode"])
print("character   :", c["name"], "| hp", c["hp"], "| gold", c["gold"])
print("sector      :", t.sector(obs), " (second digit 0 = wilderness)")
print("position    :", t.pos(obs))
print()

for step in range(3):
    before = obs
    obs = t.key("Down")
    print("  Down #%d -> pos %s  food=%s  moved=%s"
          % (step + 1, t.pos(obs), t.fields(obs)["food"], t.moved(before, obs)))

# A sweep, because a character parked on a coast will refuse every Down and that is the
# game being correct. Showing all four makes "did not move" mean something.
for d in ("Up", "Left", "Right", "Down"):
    before = obs
    obs = t.key(d)
    print("  %-5s  -> pos %s  food=%s  moved=%s"
          % (d, t.pos(obs), t.fields(obs)["food"], t.moved(before, obs)))

try:
    obs = t.key("ctrl-y")
    print("  Ctrl-Y   -> pos %s" % (t.pos(obs),))
except (SystemExit, RuntimeError) as e:
    # Expected today: Spice86's send_keyboard_key applies Shift and ignores Ctrl, and the
    # BIOS ring is emulator-owned, so a modifier is refused rather than silently downgraded.
    # Shown rather than swallowed, because "the game ignored it" and "it was never sent" look
    # identical downstream and only the adapter can tell them apart.
    print("  Ctrl-Y   -> refused by the adapter:", str(e)[:90], "...")
t.close()
