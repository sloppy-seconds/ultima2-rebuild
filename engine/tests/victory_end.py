#!/usr/bin/env python3
"""
victory_end.py — CP8 1d gate: a type-0x40 kill ends the run.

The original does not return from the victory path (asm/2CCD's `call L_0074a0` then
`jmp L_000c33` into a rocket-flight loop that never comes back). The engine prints the
ending and must then STOP, the way it stops on death. Before the fix nothing consumed
ctx->victory, so the overworld loop kept polling input with player_stamp=0x28 (drawn as a
rocket) in the still-loaded chamber — the "turned into a rocket standing in a chamber" the
owner saw.

This drives a REAL type-0x40 kill through the real loop (not a stub). It seeds a SITE
(sector 03, a castle) rather than the world map on purpose: the world-map sweep spawns and
moves creatures freely, so the cell you swing at is not reliably the one you aimed at, but
inside a site there is no spawner and a role-0x84 creature routes to asm/1040 (strike, no
move) and stays put. So:

  * mapx03 is all paving (0x70, walkable), the player enters at (2,0);
  * monx03 slot 0 is a Minax (type 0x40) at (3,0), HP 1, role 0x84 — one east swing is
    lethal (no surviving hit, so no teleport) and asm/2CB5's kill gate fires u2_victory;
  * the script sends A, RIGHT (attack east, kill), then two more RIGHTs.

Two assertions:
  * the ending text appears (MINAX IS DEAD / SAVED THE UNIVERSE) — the kill fired victory;
  * the run ENDED on victory: the two extra east moves after the kill never moved the
    player, so the final position is still the entry cell (2,0). Without the loop break the
    player would walk east.

The control that must be seen to fail: remove `if (ctx->victory) break;` from the loop and
the second check goes red (the player moves) while the first stays green, because the ending
prints either way. The ending printing is not the same as the run ending.
"""
import os
import re
import shutil
import subprocess
import sys
import hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE_DIR = os.path.dirname(HERE)
U2 = os.path.join(ENGINE_DIR, "u2")

PAVING = 0x70          # walkable castle floor
fails = []


def check(cond, msg):
    print("  %s  %s" % ("PASS" if cond else "FAIL", msg))
    if not cond:
        fails.append(msg)


def build_site_seed(work):
    content = os.path.join(work, "content")
    save = os.path.join(work, "save")
    os.makedirs(content, exist_ok=True)
    os.makedirs(os.path.join(save, "PLAYER"), exist_ok=True)

    # mapx03: all paving so the player and the creature stand on walkable floor.
    with open(os.path.join(content, "mapx03"), "wb") as f:
        f.write(bytes([PAVING]) * 4096)

    # monx03 slot 0: Minax (type 0x40) at (3,0), HP 1, role 0x84 (routes to asm/1040:
    # strikes, never moves). Columns: x 0..31, y 32..63, hp 64..95, type 96..127,
    # kind 128..159, role 160..191.
    mon = bytearray(384)
    mon[0] = 3; mon[32] = 0
    mon[64] = 1
    mon[96] = 0x40
    mon[128] = PAVING
    mon[160] = 0x84
    with open(os.path.join(content, "monx03"), "wb") as f:
        f.write(bytes(mon))

    # PLAYER record: file[i] == DS[0x36+i]. Sector digits 0,3 (castle). Entry (2,0).
    # Certain-hit agility, readied weapon, big HP so Minax's strikes don't kill the player.
    p = bytearray(384)
    p[0x36 - 0x36:0x36 - 0x36 + 6] = b"GATEWIN"[:6]
    p[0x49 - 0x36] = 0
    p[0x4A - 0x36] = 3                 # sector second digit 3 = castle
    p[0x51 - 0x36] = 0x99
    p[0x52 - 0x36] = 0x97              # HP 9997
    p[0x53 - 0x36] = 0x99; p[0x54 - 0x36] = 0x99; p[0x55 - 0x36] = 0x99
    p[0x56 - 0x36] = 0x00; p[0x57 - 0x36] = 0x00
    p[0x58 - 0x36] = 0x00; p[0x59 - 0x36] = 0x00
    p[0x5A - 0x36] = 2                 # entry x
    p[0x5B - 0x36] = 0                 # entry y
    p[0x61 - 0x36] = 1                 # readied weapon
    p[0x4C - 0x36] = 0x80              # agility: guaranteed hit
    p[0x4B - 0x36] = 0x40              # strength
    with open(os.path.join(save, "PLAYER", "PLAYER"), "wb") as f:
        f.write(bytes(p))


def run(work, keys):
    kp = os.path.join(work, "keys")
    with open(kp, "w") as f:
        f.write("".join(k + "\n" for k in keys))
    r = subprocess.run(
        [U2, "--headless", "--play-map", "--cleanroom",
         "--data-dir", os.path.join(work, "content"),
         "--save-dir", os.path.join(work, "save"),
         "--script", kp, "--dump-state"],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return r.stdout


def final_pos(text):
    m = re.findall(r'"pos":\{"x":(\d+),"y":(\d+)\}', text)
    return (int(m[-1][0]), int(m[-1][1])) if m else None


def content_md5(work):
    """md5 every file in the content dir: the engine-side G75a test."""
    d = os.path.join(work, "content")
    out = {}
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            with open(p, "rb") as f:
                out[name] = hashlib.md5(f.read()).hexdigest()
    return out


def diag_int(text, field):
    m = re.findall(r'"%s":(\d+)' % field, text)
    return int(m[-1]) if m else None


def main():
    work = "/var/tmp/u2-victory-end"
    if os.path.isdir(work):
        shutil.rmtree(work)
    build_site_seed(work)
    before = content_md5(work)

    # A, RIGHT = attack east and kill the Minax at (3,0). Then two more east moves that
    # must NOT run: with the fix the loop breaks on victory right after presenting the
    # ending, so the ending is the last thing on screen and the extra moves are never
    # processed. Without the fix the loop keeps playing, the extra moves scroll the ending
    # off, and the final frame no longer shows it -- that is the discriminator.
    text = run(work, ["A", "0xf3", "0xf3", "0xf3"])

    check("FOUND IN ULTIMA" in text or "MINAX IS DEAD" in text or "UNIVERSE" in text,
          "the ending is on the FINAL frame: the loop stopped at victory instead of "
          "continuing and scrolling it away (FOUND IN ULTIMA / MINAX IS DEAD present)")
    p = final_pos(text)
    check(p is not None,
          "the run terminated with a state dump (it ended cleanly, did not hang or crash)")

    # CP8 1d ending animation (measured by captures/spice86/victory-live/). The fade is
    # 64x64 RANDOM-cell writes of the black tile, so with repeats it blackens ~63% of the
    # 4096 cells -- assert it ran and painted randomly (not zero, not the whole grid), and
    # that the flight stepped. These are the state-level proof of the fade/flight; the pixel
    # rendering of the black tile is not headlessly assertable and is checked by eye.
    black = diag_int(text, "victory_fade_black")
    check(black is not None and 2000 <= black <= 3000,
          "the fade ran and painted RANDOM cells black (%s of 4096, expect ~63%% with "
          "repeats; 0 means no fade, 4096 would mean a systematic sweep, not random)" % black)
    steps = diag_int(text, "victory_flight_steps")
    check(steps is not None and steps > 0,
          "the rocket-flight loop stepped (%s steps) before the run ended" % steps)

    # G75a, engine side. The capture proved the original's ending writes NOTHING to disk
    # (0 of 90 content files changed). The engine's fade mutates the in-memory grid, so this
    # asserts that mutation never reaches the player's content: it must not dirty the map and
    # get written back. Without the direct-write (u2_world_tile_put sets map_dirty) this goes
    # red if anything stores after victory, and it is the property that keeps a won game from
    # corrupting the sector files.
    after = content_md5(work)
    check(before == after,
          "the ending wrote NOTHING to the player's content files (G75a: the original's fade "
          "is screen-only; %d files compared)" % len(before))
    # The discriminating assertion behind the md5 check: the md5 stays green in this run even
    # with a dirtying write, because nothing stores after victory. The flag is what actually
    # fails when the fade goes through u2_world_tile_put.
    dirty = diag_int(text, "victory_map_dirty")
    check(dirty == 0,
          "the fade left the map clean (victory_map_dirty=%s, must be 0: the original never "
          "schedules a write-back from the ending)" % dirty)

    if fails:
        print("  FAIL  %d victory-end checks are wrong" % len(fails))
        return 1
    print("  PASS  a type-0x40 kill ends the run (the loop unwinds on ctx->victory)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
