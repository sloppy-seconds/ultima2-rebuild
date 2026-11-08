# time_door_set_era — commits a time-door transit: writes the destination era into DS:49 and
#           rebuilds the world for it.
# name-evidence: the only writer of DS:49 in the tree apart from asm/7030_space_collision.asm
#           (the rocket, a different axis) and asm/6078 (a dungeon depth test). Both branches of
#           asm/26B7 / asm/26C3 land here with the target era in AL.
# func 26C6..26CB  len 0006 B  evidence: seed+reko
# calls: L_002490
# provenance: static (bytes read in full, 2026-11-08). docs/moongates.md section 4 records DS:49
#           moving 02 -> 03 -> 02 live while DS:6D stayed 00, which is this store firing and is
#           what settled the era-vs-world dispute on the same byte.
# confidence: high
# notes: reached by a `jb` from asm/26B7_time_door_target_era.asm or by fall-through through
#        asm/26C3_time_door_target_era_up.asm. The call target L_002490 is what reloads the sector
#        for the new era; asm/26CC_time_door_place_player.asm then puts the player on the gate cell
#        of the era just written.
#        Labels kept as L_<addr> on purpose (CP3 anchors); sub_26c6: is body.
sub_26c6:
L_0028c6:
        mov    %al,0x49
L_0028c9:
        call L_002490
