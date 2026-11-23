# 17E0_ah_ff_sentinel_check — tests AH against 0xFF and leaves if it is not that value. 0xFF is
#           used elsewhere in this binary as an absent / no-value sentinel, so this reads as a
#           presence check on a caller-supplied argument rather than arithmetic.
# name-evidence: body only — `cmp $0xff,%ah ; jne L_0019dd`. Two instructions, no string, no call.
# provenance: static (decoded from the body 2026-11-23); never executed in any capture.
# confidence: low. The comparison is certain, the meaning of the sentinel is inferred from how the
#           rest of the binary uses 0xFF, and no caller has ever been observed here.
# func 17E0..17E4  len 0005 B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_17e0:
L_0019e0:
        cmp    $0xff,%ah
L_0019e3:
        jne L_0019dd
