# speaker_start — raises the PC-speaker gate and latches the PIT mode
# name-evidence: out 0x43 (PIT mode/control), two out 0x42 (channel-2 latch), then in 0x61 / or / out
#           0x61 — port 0x61 bits 0-1 are the speaker gate and the PIT gate. Confirmed live: polling
#           the emulator's PC-speaker state showed Timer2GateEnabled go true at this routine and false
#           at asm/5CE4, and an idle run with no input produced no stop here at all.
# provenance: captures/spice86/step-sound-20261117, static
# confidence: medium
# func 5CB2..5CCB  len 001A B  evidence: seed+call+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5cb2:
L_005eb2:
        push   %ax
L_005eb3:
        mov    $0xb6,%al
L_005eb5:
        out    %al,$0x43
L_005eb7:
        .byte 0x8b,0xc3  # forced-kept: gas re-encodes 'mov    %bx,%ax' differently
L_005eb9:
        out    %al,$0x42
L_005ebb:
        .byte 0x8a,0xc4  # forced-kept: gas re-encodes 'mov    %ah,%al' differently
L_005ebd:
        out    %al,$0x42
L_005ebf:
        in     $0x61,%al
L_005ec1:
        mov    %al,%cs:0x5c90
L_005ec5:
        cmpb   $0x0,0x54d
L_005eca:
        jne L_005ed0
