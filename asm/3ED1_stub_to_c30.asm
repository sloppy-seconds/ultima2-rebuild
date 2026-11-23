# 3ED1_stub_to_c30 — a one-instruction stub: its whole body is `jmp L_000c30`.
# name-evidence: body only. There is nothing else to name it from.
# provenance: static (decoded from the body 2026-11-23); never executed in any capture.
# confidence: high for what it is. Reached from asm/3EAB, which is reached from cmd_O.
# func 3ED1..3ED3  len 0003 B  evidence: seed+reko
# calls: L_000c30
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_3ed1:
L_0040d1:
        jmp L_000c30
