# cleric_shrine — cleric shrine welcome/raise flow
# name-evidence: strings 'WELCOME ' / 'BROTHER '
# func 1869..19AE  len 0146 B  evidence: seed+reko
# strings: "WELCOME "; "BROTHER "; "SISTER "; "FOLLOW THE LIGHT, FRIEND!" (+4 more)
# calls: L_000c33, L_001a97, L_001b0f, L_002228, L_002252, L_0022ab, L_00514d, L_0051df (str_out) (+3 more)
# provenance: static
#           entry never appears in ExecutedInstructions of any of the 60 harvests; derived by tools/provenance_from_traces.py
# confidence: low (bytes proven by check_func.py)
# SISTER IS DEAD CODE IN THE ORIGINAL, closed 2026-11-15 statically (was needs-gameplay G80).
#           The branch at file 0x1a7b is real -- `cmp $0x1,%al ; je 0x1a8c`, and the `call str_out`
#           at 0x1a8c returns into "SISTER " at 0x1a8f while the fall-through call at 0x1a7d
#           returns into "BROTHER " at 0x1a80. But [DS:46] is written in exactly ONE place in the
#           whole binary: file 0x5951, `mov %al,0x46`, and the three bytes after it are
#           `cmp $0x4d,%al ; jne` -- `'M'` -- followed by str_out returning into "MALE". So the
#           byte holds the typed LETTER, never 1, and `cmp $0x1` cannot match. The shrine calls
#           every visitor BROTHER. Searched for every store encoding to DS:46 (A2/A3/C6 06/C7 06),
#           not just the asm text, because the text is what misled this project for a year: the
#           three readers are asm/1869, asm/4598 and asm/505F and none of them is the writer.
#           The engine is already faithful: status.c tests `sex == 'M'`, so commerce.c's
#           `sex == 1` test never fires either. Do NOT "fix" the engine to say SISTER -- that
#           would make it diverge from the bytes. The bug is the original's.
# note: the pristine references/ultima2/player is an all-zero slot template (DS:46 == 0), which
#           also never equals 1, so no shipped save can reach the branch either.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
cleric_shrine:
L_001a69:
        nop
L_001a6a:
        call L_0051df
        db "WELCOME ",0x00        # string
L_001a76:
        mov    0x46,%al
L_001a79:
        cmp    $0x1,%al
L_001a7b:
        je L_001a8c
L_001a7d:
        call L_0051df
        db "BROTHER ",0x00        # string
L_001a89:
        jmp L_001a97
L_001a8b:
        nop
L_001a8c:
        call L_0051df
        db "SISTER ",0x00        # string
L_001a97:
        call L_00527a
L_001a9a:
        call L_0051df
        db 0x8d,"1-LIGHT, 2-L.D., 3-L.U.,",0x8d,"4-PASS 5-SURFACE, 6-PRAYER.",0x8d,"YOUR INTEREST? ",0x00        # string
L_001ae3:
        call L_002228
L_001ae6:
        je L_001aef
L_001ae8:
        cmp    $0x7,%al
L_001aea:
        jae L_001aef
L_001aec:
        jmp L_001b0f
L_001aee:
        nop
L_001aef:
        call L_0051df
        db "FOLLOW THE LIGHT, FRIEND!",0x00        # string
L_001b0c:
        jmp L_000c33
L_001b0f:
        mov    %al,0x1f
L_001b12:
        call L_0051df
        db "FIVE ",0x00        # string
L_001b1b:
        mov    0x1f,%al
L_001b1e:
        clc
L_001b1f:
        adc    $0x24,%al
L_001b21:
        call L_00528c
L_001b24:
        call L_0051df
        db "S FOR ",0x00        # string
L_001b2e:
        mov    0x1f,%al
L_001b31:
        call L_0022ab
L_001b34:
        mov    0x27,%al
L_001b37:
        mov    $0x0,%bh
L_001b39:
        mov    0x28,%bl
L_001b3d:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_001b3f:
        call L_0053a5
L_001b42:
        call L_0051df
        db 0x8d,"YES, FRIEND? ",0x00        # string
L_001b54:
        call L_00514d
L_001b57:
        cmp    $0xff,%ah
L_001b5a:
        jne L_001b54
L_001b5c:
        cmp    $0x59,%al
L_001b5e:
        je L_001b7e
L_001b60:
        call L_0051df
        db "NO",0x8d,"I'M SORRY, GOOD DAY.",0x00        # string
L_001b7b:
        jmp L_000c33
L_001b7e:
        call L_0051df
        db "YES,",0x8d,"I WILL TAKE 5!",0x00        # string
L_001b95:
        call L_002252
L_001b98:
        mov    $0x0,%bh
L_001b9a:
        mov    0x1f,%bl
L_001b9e:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_001ba0:
        mov    0xb6(%di),%al
L_001ba4:
        clc
L_001ba5:
        adc    $0x5,%al
L_001ba7:
        daa
L_001ba8:
        mov    %al,0xb6(%di)
L_001bac:
        jmp L_000c33
