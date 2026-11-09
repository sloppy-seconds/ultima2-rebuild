# sub_2ce5 — mechanical clean (bulk wave); behavior not yet reviewed
# func 2CE5..2CE8  len 0004 B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# RESOLVED 2026-11-09: this is the kill-drop DISPATCH, and it is the reason asm/2D07
#           looked orphaned. Body is `cmp $0xfc,%al ; jne L_002f22` -- 0xFC is the THIEF
#           type (asm/0ADB spawn arms, type 0xFC with HP 0x20), so a thief falls through
#           into asm/2CE9 and every other type jumps to the type table at asm/2D22. Both
#           arms end in a tail `jmp L_002f8c` into combat_kill_reward, which is why the
#           reward executes more often than the thief arm does (17 vs 8 across the
#           captured flows). Renamed from sub_2ce5 on the strength of this.
# execution evidence: captures/spice86/baseline-fight, play-test-live, play-test --
#           0x2d07 appears in ExecutedInstructions 8 times, reached by FALL-THROUGH from
#           0x2d05. There is no `call` to asm/2D07 anywhere because none was ever needed,
#           which is exactly what every static pass over this tree was looking for.
sub_2ce5:
L_002ee5:
        cmp    $0xfc,%al
L_002ee7:
        jne L_002f22
