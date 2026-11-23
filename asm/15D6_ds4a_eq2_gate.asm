# 15D6_ds4a_eq2_gate — reads DS:4A and branches unless it equals 2. The sector number is
#           DS:49*10 + DS:4A, so DS:4A is the ones digit; what a ones digit of 2 selects is not
#           decided in this file.
# name-evidence: body only — `mov 0x4a,%al ; cmp $0x2,%al ; jne L_0017e1`.
# provenance: static (decoded from the body 2026-11-23); never executed in any capture.
# confidence: high for the comparison, none for what it gates. The name says the byte it reads and
#           the value it compares, which is all the bytes support.
# func 15D6..15DD  len 0008 B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_15d6:
L_0017d6:
        nop
L_0017d7:
        mov    0x4a,%al
L_0017da:
        cmp    $0x2,%al
L_0017dc:
        jne L_0017e1
