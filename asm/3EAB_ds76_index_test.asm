# 3EAB_ds76_index_test — builds an index from DS:1F into a byte table based at DS:76 and tests the
#           byte for zero: `mov $0,%bh ; mov 0x1f,%bl ; mov %bx,%di ; mov 0x76(%di),%al ;
#           or %al,%al ; jne`. The three middle instructions are forced-keep because gas re-encodes
#           them differently, which is the usual sign of a 16-bit base+index form.
# name-evidence: body only. No string, no call, nothing but the addressing mode.
# provenance: static (decoded from the body 2026-11-23); never executed in any capture.
# confidence: high for the addressing, none for what the table holds. It is reached from cmd_O, so
#           it is part of that command's flow, and asm/3ED1 next door is a stub out of it.
# func 3EAB..3EBA  len 0010 B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_3eab:
L_0040ab:
        mov    $0x0,%bh
L_0040ad:
        mov    0x1f,%bl
L_0040b1:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0040b3:
        .byte 0x8a,0x85,0x76,0x00  # forced-kept: gas re-encodes 'mov    0x76(%di),%al' differently
L_0040b7:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_0040b9:
        jne L_0040d4
