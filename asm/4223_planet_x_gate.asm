# 4223_planet_x_gate — reads DS:6D and branches unless it equals 9, and 9 is PLANET X. DS:6D is the
#           world byte (asm/7030 is its only writer and what it writes fixes the meaning), and the
#           world index is measured: names at exe 0x072b9, hyperjump table at exe 0x07212, giving
#           0 EARTH, 1 MERCURY ... 8 PLUTO, 9 PLANET X. Earth takes index 0, so the planets are
#           shifted one below the counting order a player would use.
# name-evidence: body — `mov 0x6d,%al ; cmp $0x9,%al ; jne L_004446` — joined to the world index in
#           disasm/census/world.tsv. The DS:20 index it also builds into DI is not used by the
#           compared read, which is why this file reads as a fragment rather than a whole routine.
# provenance: static (decoded from the body 2026-11-23); never executed in any capture.
# confidence: high for the comparison and for 9 = PLANET X. Not observed executing, so what the
#           gate does on Planet X is still unread.
# func 4223..4231  len 000F B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_4223:
L_004423:
        mov    $0x0,%bh
L_004425:
        mov    0x20,%bl
L_004429:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_00442b:
        mov    0x6d,%al
L_00442e:
        cmp    $0x9,%al
L_004430:
        jne L_004446
