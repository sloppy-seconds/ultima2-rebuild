# 5EED_restore_regs_ret — a shared epilogue, not a routine: `pop %dx ; pop %cx ; pop %bx ; pop %ax
#           ; ret`. Four pops in that order is the matching exit for a four-push prologue, and the
#           caller does not return here so much as fall into the ret.
# name-evidence: body only. An epilogue has nothing to name it from but its own shape.
# provenance: static (decoded from the body 2026-11-23); never executed in any capture.
# confidence: high for what it is. It is reached from asm/5F08, whose pushes should match; that has
#           not been checked, and if they do not match, this is a second entry point instead.
# func 5EED..5EF1  len 0005 B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5eed:
L_0060ed:
        pop    %dx
L_0060ee:
        pop    %cx
L_0060ef:
        pop    %bx
L_0060f0:
        pop    %ax
L_0060f1:
        ret
