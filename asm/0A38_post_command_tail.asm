# 0A38_post_command_tail — the tail every completed command falls through to
# name-evidence: reached only by fall-through from asm/0A33_cmd_done (no branch names
#           L_000c38 anywhere in the tree); clears DS:22/20/21, then `mov 0x4a,%al;
#           or %al,%al; je L_000c6d`.
# This is the answer to "what triggers a wandering spawn": not a step and not a clock
# tick, but the end of a command. See docs/monster-spawn.md § 15.2.
# DS:4A IS THE SECTOR'S SECOND DECIMAL DIGIT, not an unknown flag. docs/map-format.md and
# engine/src/core/state.h say DS:49/4A are the two digits patched into the MAPXFF filename,
# and a live read in sector 20 gave DS:49=0x02 DS:4A=0x00, which is 2 and 0. So this is not
# a gate, it is a dispatch on the low sector digit: digit 0 takes the sweep here, digits
# 1..5 branch away (asm/0A4A..0A5F jump to L_001213/L_001214/L_001215/L_00135f/L_00135e),
# and digits 6..9 fall through to the sweep at asm/0A6D from the last compare.
# NOT ESTABLISHED: what the 1..5 branches do, or why the sector digit selects on them.
# RESOLVED 2026-10-28 from the branch targets. Digits 1, 2 and 3 all enter the role-gated
#           creature sweep at asm/1013/1015 -- runtime 1213/1214/1215 land on the loop whose
#           body at asm/101C tests mon_type and then mon_role (docs/monster-spawn.md § 14).
#           Digits 4 and 5 land at runtime 135E/135F, which is inside asm/1151's span: the
#           dungeon flow. CORRECTED 2026-11-08: this line said "parked per AGENTS.md § 3",
#           which had already stopped being true -- the park was lifted 2026-10-29, asm/1151
#           was read in full the same day for G54, and the engine implements it as a second
#           AI in engine/src/game/dungeon.c. So the sweep this tail jumps to for digit 0
#           is the WORLD-MAP sweep, and it reads mon_role nowhere in asm/0A6D..0AB5: every live
#           slot in range gets the full pass. The engine had been applying the town gate here,
#           which froze the overworld -- owner-reported 2026-10-28 and fixed in
#           engine/src/game/ai.c as the gated_sweep test.
# label: kept as sub_0a38 on purpose (CP3 cross-reference anchor).
# func 0A38..0A49  len 0012 B  evidence: seed+reko
# provenance: static reassembly 2026-10-28 (docs/monster-spawn.md § 15.2), corroborated
#           by a stop at 17D:0CEE reached during an arrow-key walk in the same session
# confidence: high for the flow and for DS:49/4A's identity; high for the 1..3 arms (the
#           branch targets are bytes and the target loop is § 14's), and high for the 4/5
#           arms as of 2026-11-08 -- the earlier "unread" note predated asm/1151 being read
#           in full on 2026-10-29 for G54, and outlived it by ten days
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_0a38:
L_000c38:
        mov    $0x0,%al
L_000c3a:
        mov    %al,0x22
L_000c3d:
        mov    %al,0x20
L_000c40:
        mov    %al,0x21
L_000c43:
        mov    0x4a,%al
L_000c46:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_000c48:
        je L_000c6d
