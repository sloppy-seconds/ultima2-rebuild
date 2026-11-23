# 15A9_stub_to_15d6 — a stub: its whole body is `jmp L_0017d6`, which is the entry of asm/15D6.
#           Like asm/017D, the transfer is the entire function.
# name-evidence: body only — `jmp L_0017d6` and a `nop`. There is nothing else to name it from.
# provenance: static (decoded from the body 2026-11-23). Never executed in any capture;
#           index_xref --reconcile lists it among the callee-never-reached edges.
# confidence: high for what it is. There is no behaviour beyond the jump.
# func 15A9..15AB  len 0003 B  evidence: seed+reko
# calls: L_0017d6
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_15a9:
L_0017a9:
        jmp L_0017d6
L_0017ab:
        nop
