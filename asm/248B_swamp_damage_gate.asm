# swamp_damage_gate — the mode test that decides whether entering a tile costs 5 HP
# name-evidence: `mov al,[DS:29]; and al,0x7F; cmp al,0x02; jne L_0026b1`. DS:29 holds the
#           mode the walk arm handed to asm/2423, and the mode is map_byte >> 1, so this
#           fires for map byte 0x04 and for nothing else among the common tiles:
#           0x00->0x00 water, 0x04->0x02 SWAMP, 0x08->0x04 grass, 0x0C->0x06, 0x10->0x08
#           mountain. Confirmed live 2026-11-02: [DS:18] read 0x82 while standing on the
#           damaging tile, and 0x82 & 0x7F == 0x02.
#           The route in is asm/2423 itself: `mov al,[DS:4A]; cmp al,0x00; je L_00268b`,
#           so the whole effect chain is gated on being on the world map, not inside a
#           town, tower or dungeon. Fall-through on a match goes to asm/2495 swamp_damage;
#           a miss falls to asm/24B1, the force-field gate. This is one ladder of tile
#           effects, not two unrelated routines.
#           Renamed 2026-11-02 from sub_248b. See docs/archive/handoff-tile-effects.md.
# provenance: static read of the asm/2423 span 2026-11-02. The mode value was confirmed
#           live the same day: [DS:18] read 0x82 while standing on the damaging tile in
#           sector 20, and 0x82 & 0x7F == 0x02. The map_byte >> 1 identity is not new
#           here — asm/5B71_mode_table_copy already recorded `0x5C >> 1 == 0x2e`.
# func 248B..2494  len 000A B  evidence: seed+reko
# confidence: medium (bytes proven by check_func.py; the meaning is observed live)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_248b:
L_00268b:
        nop
L_00268c:
        mov    0x29,%al
L_00268f:
        and    $0x7f,%al
L_002691:
        cmp    $0x2,%al
L_002693:
        jne L_0026b1
