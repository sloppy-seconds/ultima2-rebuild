# time_door_target_era_up — the upward branch of the time-door destination rule: `target = k + 1`.
#           Reached only when the fall-through from asm/26B7_time_door_target_era.asm is taken,
#           i.e. when the gate index is NOT below the current era.
# name-evidence: `clc; adc $0x1,%al` immediately after `cmp DS:49,%al; jb` in the preceding
#           fragment. Together the three fragments say `target = k if k < era else k + 1`, and
#           that single expression is what reproduces both live transits and every published rule
#           in tools/gate_graph.py.
# func 26C3..26C5  len 0003 B  evidence: seed+reko
# provenance: static (bytes read in full, 2026-11-08), corroborated by the two live transits of
#           captures/spice86/moongate-cycle and by the Codex gate graph.
# confidence: high
# notes: three bytes, and without them the rule is wrong for every gate that goes forward in time,
#        which is most of them. Read on its own this fragment is an unattributable increment; the
#        `clc` is load-bearing because `adc` would otherwise add the incoming carry flag.
#        Labels kept as L_<addr> on purpose (CP3 anchors); sub_26c3: is body.
sub_26c3:
L_0028c3:
        clc
L_0028c4:
        adc    $0x1,%al
