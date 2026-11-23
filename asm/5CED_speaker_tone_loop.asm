# 5CED_speaker_tone_loop — the PC speaker. `mov $0xb6,%al ; out al,0x43` loads counter 2, then the
#           loop toggles the speaker gate: `out al,0x42`, `in al,0x61`, `or $0x3,al`, `out al,0x61`
#           with `dec cx / jne` as the delay, and a final `out al,0x61` to silence it. 0xB6 is the
#           usual 0x43 control word for counter 2, which is the speaker counter, so the port set is
#           not a judgement call.
# name-evidence: the port numbers in the body — 0x42 and 0x43 are the timer-2 counters and 0x61 is
#           the port-B speaker gate. Same seam asm/speaker_tone marks elsewhere in the tree.
# addresses: filename is the runtime offset; the `L_` labels are file offsets, and the body lives at
#           file 0x5EED. There is at least one other copy of this byte pattern in the binary, so a
#           search for the pattern finds more than this routine.
# provenance: static (decoded from the body 2026-11-23).
# confidence: high for what it drives; the exact pitch and duration depend on the caller's CX, which
#           is not decided in this file.
# func 5CED..5D08  len 001C B  evidence: call
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5ced:
L_005eed:
        mov    $0xb6,%al
L_005eef:
        out    %al,$0x43
L_005ef1:
        .byte 0x8b,0xc3  # forced-kept: gas re-encodes 'mov    %bx,%ax' differently
L_005ef3:
        out    %al,$0x42
L_005ef5:
        .byte 0x8a,0xc4  # forced-kept: gas re-encodes 'mov    %ah,%al' differently
L_005ef7:
        out    %al,$0x42
L_005ef9:
        in     $0x61,%al
L_005efb:
        .byte 0x8a,0xe0  # forced-kept: gas re-encodes 'mov    %al,%ah' differently
L_005efd:
        or     $0x3,%al
L_005eff:
        out    %al,$0x61
L_005f01:
        dec    %cx
L_005f02:
        jne L_005f01
L_005f04:
        .byte 0x8a,0xc4  # forced-kept: gas re-encodes 'mov    %ah,%al' differently
L_005f06:
        out    %al,$0x61
L_005f08:
        ret
