# time_door_target_era — the first half of the time-door destination rule: compute the target era
#           from the gate index, and take the downward branch.
# name-evidence: `k = DS:6E >> 1` then `cmp DS:49,%al; jb` is the whole routine's decision, and
#           it is the only place in the tree where the rotation byte and the era byte are compared.
#           The rule it implements is checked mechanically by tools/gate_graph.py against two live
#           transits and the Codex gate graph; see docs/moongates.md section 3b.
# func 26B7..26C2  len 000C B  evidence: seed+reko
# provenance: static (bytes read in full, 2026-11-08). The two transits it governs that were
#           watched live are era2 gate1 -> era1 and era2 gate2 -> era3, from
#           captures/spice86/moongate-cycle.
# confidence: high (three instructions, read end to end, and the derived graph reproduces every
#           observation and every published rule it is checked against)
# notes: THIS FILE AND asm/26C3_time_door_target_era_up.asm AND asm/26C6_time_door_set_era.asm
#        ARE ONE ROUTINE, 26B7..26CB, cut into three by the Phase 2 map. Read alone this fragment
#        looks like a bare bounds test and says nothing; the meaning is in the fall-through. Same
#        failure as cmd_C / cmd_D / cmd_K, each of which was halved by the map and read as a stub.
#        Labels kept as L_<addr> on purpose (CP3 anchors); sub_26b7: is body.
sub_26b7:
L_0028b7:
        mov    0x6e,%al
L_0028ba:
        clc
L_0028bb:
        rcr    $1,%al
L_0028bd:
        cmp    0x49,%al
L_0028c1:
        jb L_0028c6
