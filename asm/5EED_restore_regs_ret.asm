# 5EED_restore_regs_ret — a shared epilogue, not a routine: `pop %dx ; pop %cx ; pop %bx ; pop %ax
#           ; ret`. There is a second, byte-identical copy of this sequence at 0x6108, which is
#           where the traffic actually goes: 0x6108 has executed in the corpus and this copy has
#           not, and the code following 0x6108 inside asm/5F08 has never executed either. So the
#           pop-quad is an exit that many routines return through, and asm/5F08's nominal entry at
#           0x6108 is a boundary artefact, not a routine start.
# name-evidence: body only — four pops in LIFO order against a `ret`. An epilogue has nothing to
#           name it from but its own shape.
# provenance: static (decoded from the body 2026-11-23); this copy never executed in any capture.
# confidence: high for what it is. The two `call L_005eed` sites in asm/5F08 at 0x6163 and 0x6188
#           do NOT match it — they load DX/CX/BX from registers instead of pushing four words, and
#           neither site has ever executed. That is left as a gap rather than explained: a call into
#           an epilogue is not a call this corpus can account for.
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
