# speaker_stop — lowers the PC-speaker gate, restoring port 0x61 from the saved byte at cs:5c90
# name-evidence: in 0x61, and with the saved value, out 0x61 — the inverse of asm/5CB2. Confirmed live:
#           the emulator's Timer2GateEnabled falls at this routine, and the on-to-off span is the
#           audible window of a tone (73 Hz thud ~65,000 cycles; refusal buzz ~835,000 cycles).
# provenance: captures/spice86/step-sound-20261117, static
# confidence: medium
# func 5CE4..5CEC  len 0009 B  evidence: seed+call+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5ce4:
L_005ee4:
        push   %ax
L_005ee5:
        mov    %cs:0x5c90,%al
L_005ee9:
        out    %al,$0x61
L_005eeb:
        pop    %ax
L_005eec:
        ret
