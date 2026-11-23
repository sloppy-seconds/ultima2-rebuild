# 4B4E_dual_text_video_helper — runs one helper twice, once with 0xB800 and once with 0xBA00 in AX:
#           `push %ax ; mov $0xb800,%ax ; call L_004d5d ; mov $0xba00,%ax ; call L_004d5d ; pop %ax
#           ; ret`. Those are the colour and monochrome text segments, so the shape is "do this to
#           both text adapters", and the helper at L_004d5d is where the work actually happens.
# name-evidence: body only, plus the two segment constants, which are not a judgement call — they
#           are the only two text-mode frame buffers an MDA/CGA machine has.
# provenance: static (decoded from the body 2026-11-23); never executed in any capture.
# confidence: high for the two segments and the double call; the effect depends on asm/4D5D, which
#           is a separate question. Note ES is not set here, unlike asm/310B_cmd_C which loads
#           0xB800 into ES directly.
# func 4B4E..4B5C  len 000F B  evidence: call+reko
# calls: L_004d5d
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_4b4e:
L_004d4e:
        push   %ax
L_004d4f:
        mov    $0xb800,%ax
L_004d52:
        call L_004d5d
L_004d55:
        mov    $0xba00,%ax
L_004d58:
        call L_004d5d
L_004d5b:
        pop    %ax
L_004d5c:
        ret
