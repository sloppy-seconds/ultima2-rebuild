# cmd_W — command handler 'W' (WEAR ARMOUR): armour menu "WEAR ARMOUR:\r1-CLOTH,
#           2-LEATHER, 3-CHAIN,\r4-PLATE, 5-REFLECT, 6-POWER,\rWHICH? \0";
#           errors " NOT OWNED!" and " <-THOU ART NOT\rSTRONG ENOUGH TO WEAR!";
#           success prints " READY."
# func 43F3..44D3  len 00E1 B  evidence: seed+reko (jump table cs:0730 entry W)
# provenance: live: cmd_W executed in captures/spice86/wonly/, observed 2026-10-26.
#           entry executed in 3 of 235 harvests; derived by tools/provenance_from_traces.py
#           The menu at 0x43F3/0x4445 and the success path at 0x44C0 are all present in that
#           harvest's ExecutedInstructions. The capture drove the whole success branch: with
#           DS:97 poked to 01 (own CLOTH) the digit `1` printed " READY." and DS:62 went 06 -> 01,
#           which is `mov 0x1f,%al ; mov %al,0x62` at 0x46CB landing exactly as decoded. The
#           NOT OWNED (0x4472) and strength-refusal (0x4493) branches were NOT taken and remain
#           static-only. Previously this line read "never appears in any of the 154 harvests", and
#           the D9 gate is what forced the correction rather than a silent edit.
#           All three exits tail-jump to L_000c33, the status tick — independent corroboration of
#           the input-path tick finding recorded in docs/phase4-state.md the same day.
# confidence: medium (flow + messages proven by bytes; NPC/quest semantics inferred)
# notes: re-decoded with tools/decode_str.py (str_out-aware); gas re-encode diffs
#        kept as .byte (disp8/disp16 and reg-op form choices)
cmd_W:
cmd_W:
L_0045f3:
        call L_0051df
        db "WEAR ARMOUR:",0x0d,"1-CLOTH, 2-LEATHER, 3-CHAIN,",0x0d,"4-PLATE, 5-REFLECT, 6-POWER,",0x0d,"WHICH? ",0x00        # string
L_004645:
        call L_002228
L_004648:
        mov    %al,0x1f
L_00464b:
        cmp    $0x7,%al
L_00464d:
        jb L_004654
L_00464f:
        mov    $0x0,%al
L_004651:
        mov    %al,0x1f
L_004654:
        nop
L_004655:
        clc
L_004656:
        adc    $0x1d,%al
L_004658:
        call L_00528c
L_00465b:
        mov    $0x0,%bh
L_00465d:
        mov    0x1f,%bl
L_004661:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_004663:
        mov    0x96(%di),%al
L_004667:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_004669:
        jne L_004684
L_00466b:
        mov    0x1f,%al
L_00466e:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_004670:
        je L_004684
L_004672:
        call L_0051df
        db " NOT OWNED!",0x00        # string
L_004681:
        jmp L_000c33
L_004684:
        mov    0x1f,%al
L_004687:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_004689:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_00468b:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_00468d:
        cmp    0x4b,%al
L_004691:
        jb L_0046c0
L_004693:
        call L_0051df
        db " <-THOU ART NOT",0x0d,"STRONG ENOUGH TO WEAR!",0x00        # string
L_0046bd:
        jmp L_000c33
L_0046c0:
        call L_0051df
        db " READY.",0x00        # string
L_0046cb:
        mov    0x1f,%al
L_0046ce:
        mov    %al,0x62
L_0046d1:
        jmp L_000c33
