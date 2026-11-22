#!/usr/bin/env python3
"""Emit a synthetic content directory for the CP5 fight gate.

Everything written here is engine-authored filler — the same rule monsters_check.c
and world_check.c follow, so that a fight can be gated in CI without touching the
player's own game files. The bytes are shaped like the real ones because the engine
reads them the same way: a 4096-byte mapx, a 256-byte monx, and a 384-byte PLAYER
record at saves/<slot>/PLAYER whose byte i is DS[0x36+i] (docs/save-format.md).

Usage: cp5_fight_seed.py DIR
"""
import os
import sys

# The seeders share one offset source: state.h, read at runtime by u2_seed.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402

GRASS = 0x08          # measured passable on foot (docs/passability-findings.md F10)
SLOT = 32             # creature slots per array; monx columns are 32 bytes apart


def main(out_dir: str) -> int:
    content = os.path.join(out_dir, "content")
    save = os.path.join(out_dir, "save")
    os.makedirs(content, exist_ok=True)
    os.makedirs(save, exist_ok=True)

    # --- mapx00: all grass, sector 00 being the one the save names -------------
    with open(os.path.join(content, "mapx00"), "wb") as f:
        f.write(bytes([GRASS]) * 4096)

    # --- monx00: one creature at (3,0), the cell east of the player's entry ----
    # Columns: x 0..31, y 32..63, hp 64..95, type 96..127, kind 128..159,
    # role 160..191 (docs/monster-spawn.md § 6). Role 1 is the full AI pass
    # (§ 14), so the creature is awake from the first turn.
    mon = bytearray(256)
    mon[0 + 0] = 3            # x of slot 0
    mon[32 + 0] = 0           # y of slot 0
    mon[64 + 0] = 5           # hp — one hit from this character kills outright
    mon[96 + 0] = 0x68        # type: the Fighter art, 0x68 >> 2 == 0x1A
    mon[128 + 0] = 0x70       # kind
    mon[160 + 0] = 1          # role: active
    with open(os.path.join(content, "monx00"), "wb") as f:
        f.write(bytes(mon))

    # --- PLAYER: 384 bytes, file[i] == DS[0x36+i] -----------------------------
    # Built through u2_seed so the addresses come from state.h rather than from this file.
    # The values are load-bearing, not decoration: exp and gold are both 9999 so a kill
    # wraps the packed-decimal counter past its ceiling, and agility 0x80 displays as 128
    # because asm/2E3D rolls (rand>>1) < agi and that roll maxes at 127 — so a hit is
    # certain and the gate cannot flake on a miss.
    p = u2_seed.PlayerRecord()
    # Six characters, not seven: the seeder this replaced wrote b"GATEMAX"[:6], and a
    # migration that claims to be a move keeps the bytes it claims to move. The field in
    # state.h is 16 bytes, so "GATEMAX" would be legal — it is simply not what was there.
    p.set_name("GATEMA")
    p.set_at(0, 0)                       # sector 00; the second digit is the site kind
    p.set_intent("hp", 9997)
    p.set_intent("food", 999999)
    p.set_intent("exp", 9999)
    p.set_intent("gold", 9999)
    p.set_raw("entry_x", 2)              # the cell the player starts on
    p.set_raw("entry_y", 0)
    p.set_intent("readied", 1)           # weapon index 1: damage = (8*w + str)/4
    p.set_stat("agi", 0x80)
    p.set_stat("str", 0x40)              # plenty for a one-hit kill
    # saves/<slot>/PLAYER: the record lives inside its slot directory, which is
    # also where this seed's dirty sectors will be written. engine/src/core/slot.h
    # is the authority for the shape; a seed that spelled the old flat path would
    # leave the engine seeding a fresh blank and every gate below reading a
    # character that is not the one it authored.
    rec = os.path.join(save, "PLAYER", "PLAYER")
    os.makedirs(os.path.dirname(rec), exist_ok=True)
    p.write(rec)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: cp5_fight_seed.py DIR", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
