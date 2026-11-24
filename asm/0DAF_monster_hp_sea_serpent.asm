# monster_hp_sea_serpent — spawn arm: assigns monster type 0x2C and HP 64
# name-evidence: `mov $0x2c,%al` stored to [DI+0x197] and stamped into the map cell,
#           then `mov $0x40,%al` stored to [DI+0x177] (monster HP, proven by
#           asm/2E3D_combat_attack_flow). Type 0x2C >> 2 == art 0x0B, named Sea
#           serpent / Sea Monster by bitmap identity, third-party HP 64.
#           RESOLVED 2026-11-23, and the resolution came from this arm: art 0x0B is the sea
#           serpent, and the dispute in docs/bestiary.md § 8 was tile-art.tsv misclassifying it
#           as `object` (a doorway in a magenta field). Owner read the tile: the magenta speckles
#           are water and the white is a mostly submerged back, neck, head and open jaw. The HP
#           written here, 64, is the SPAWNED serpent's HP and matches the third-party figure
#           exactly; the serpents the game ships placed in harbour sectors 31/32/41 carry 255 in
#           monx. Two populations, two HP values, both correct — so this arm is what dissolved the
#           apparent conflict rather than a source being wrong.
# provenance: static, 2026-10-22; extracted by tools/monster_hp_dispatch.py
# confidence: high for the field and the value (bytes)
# label: kept as sub_0daf on purpose — labels are body (CP0)
# GUARD BYTE: this arm wants terrain 0x00 — water. It and the Frigate arm in
#   asm/0DC4 are the only two that do, and they are the only two sea creatures, which is the
#   strongest single corroboration that [DI+0x1B7] is the tile and not an index.
#   RUNTIME CONFIRMED 2026-10-28: with DS:60 forced so the chain saw 0x08, three sea serpents
#   appeared at mapx20 cells that all read 0x00. docs/monster-spawn.md 15.4.
# func 0DAF..0DC3  len 0015 B  evidence: seed+reko
# calls: L_000daf
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_0daf:
L_000faf:
        mov    $0x2c,%al
L_000fb1:
        mov    0x6,%bx
L_000fb5:
        mov    %al,(%bx,%si)
L_000fb7:
        mov    %al,0x197(%di)
L_000fbb:
        mov    $0x40,%al
L_000fbd:
        mov    %al,0x177(%di)
L_000fc1:
        jmp L_000daf
