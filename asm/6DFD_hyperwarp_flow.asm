# hyperwarp_flow — hyperwarp engage + off-course handling
# OFF-COURSE GATE at L_007086, decoded and then confirmed twice by live play:
#       mov 0xe5,%al            TRI-LITHIUM count, packed BCD
#       clc ; rcr $1,%al ; jae  low bit clear -> skip off-course
#       clc ; rcr $1,%al ; jae  next bit clear -> skip off-course
#       'SHIP OFF COURSE!'
#       call L_006f68 ; and $0x7,%al ; mov %al,0x4ca   (and again for 4cb, 4cc)
#   So off-course fires iff (fuel & 3) == 3, evaluated on the POST-decrement count
#   (run 1: 8 -> 7, 7&3==3, dumped; run 2: 7 -> 6, 6&3==2, arrived where typed).
#   The coordinates are then rand & 7, i.e. 0..7, and asm/7030 sets DS:6D to 0x0A.
#   The owner's recollection was 'randomly it will dump you in deep space'. The
#   randomness is real but is only the COORDINATES; whether it happens at all is
#   arithmetic on the fuel stack, which is why it feels like luck in play.
#   A typed destination is written into DS:4CA/4CB/4CC BEFORE this gate, so on-course
#   keeps what you typed (9/9/9 honoured exactly) and off-course overwrites it.
#   Note the typed range is not bounded by the random range: 9 is accepted, and the
#   coordinate readout can therefore exceed 7.
# WARP ANIMATION, read 2026-11-15 and CORRECTED the same day. This routine is the only
#       caller that sets the space field's motion mode, so it is where the trigger and the
#       length live. Four bounded redraw loops, each `call L_006d3d`
#       (asm/6B3D_space_field_draw), then a speed-slot lookup, then a counter step:
#           L_007033  DS:4C8 = 0x80 (128 frames), DS:4C7 = 0   slot bx=0x16
#           L_007059  DS:4C8 = 0x80 (128 frames), DS:4C7 = 1   slot bx=0x16
#           L_00714e  DS:4C7 counts down to 2                  slot bx=0x18
#           L_00716a  DS:4C8 = 0x40 (64 frames)                slot bx=0x1a
#       plus `incb DS:4C7` steps between them that push the mode past 2, which is the
#       branch in asm/6B3D at L_006e3a.
#   **THESE ARE NOT DELAYS.** The first version of this note called bx=0x16/0x18/0x1a delays,
#       and that was wrong in the direction that matters for a port. asm/0711 loads a word
#       out of the rescaled table into %bx, and at five of this routine's six call sites the
#       very next instruction is `decb` / `incb` / `mov` on a counter — %bx is never read.
#       The values are computed and dropped, exactly as asm/0711_speed_delay_lookup's own
#       correction records for the other callers, and that header warns specifically not to
#       wire a slot to an animation rate on the strength of the old timeout reading. The
#       sixth site, L_007188, is genuinely different and IS a key wait:
#       `call L_000911 ; call L_00514d ; cmp $0xff,%ah`.
#   So the warp's frame rate is **the cost of drawing the field**, not a programmed pause —
#       the same conclusion captures/spice86/dive-sweep-rate reached for the descent, from
#       the same shape. A port that wants the animation visible has to supply the time the
#       CGA work used to cost, and that number is not in these bytes.
#   Trigger = hyperwarp engage, after the 'PREPARE FOR HYPERWARP!' line at L_00712f; the
#       static orbit screen in asm/6999 never touches DS:4C7, which is what makes the warp
#       distinguishable at all.
#   Still not decoded: what the mode >= 2 branch draws.
# func 6DFD..6FA2  len 01A6 B  evidence: call+reko
# strings: "HYPERWARP ENGAGED!"; "SHIP OFF COURSE!"; "NO FUEL!"; "HYPERWARP TO:" (+2 more)
# calls: L_000911, L_00514d, L_0051df (str_out), L_0055f4, L_006c79, L_006d3d, L_006ec4, L_006f5b (+6 more)
# provenance: observed 2026-11-03 -- captures/spice86/g39-rocket-flight/
#           spice86dumpExecutionFlow.json (segment 017D): this entry, the off-course
#           gate at 0x7086, the 'SHIP OFF COURSE!' print at 0x7093 and the random source
#           L_006f68 all appear in ExecutedInstructions. Frames 23-hyperspace.png
#           (armed), 24-arrive.png (deep space, 7/1/1), 26-h999-after.png (on course,
#           9/9/9, 'YOU ARE ORBITING X.').
# confidence: high for both arms of the gate, each observed on a different fuel count
#   READ END TO END 2026-11-23 -- the warp is THREE phases, and their counts and sweep
#           values are literal in the body. `DS:4C8` is the per-phase iteration counter,
#           `DS:4C7` the sweep value asm/6B3D reads:
#
#             print "HYPERWARP ENGAGED!"
#             cs:0x6990 = 0x2200 ; cs:0x6992 = 0xffff      (RNG seed and step)
#             A: 0x4C7 = 0 ; 128 x { call asm/6B3D ; delay 0x16 }
#                sweep 0 means asm/6B3D skips the erase, so these 128 passes accumulate --
#                this is the streak window, and it is 128 passes at 0, not a hold at 1.
#             B: 0x4C7 = 1 ; cs:0x6992 = 1 ; 128 x { call asm/6B3D ; delay 0x16 }
#                erase on, RNG step 1.
#             C: cs:0x6994 = 1 ; sweep += 2 each pass: draws at 2,4,...,126 (63 passes),
#                then 128 sets the sign bit and `jns` exits ; delay 0x18.
#             then the off-course gate on DS:E5 (tri-lithium): two `rcr $1` tests, and if
#                both bits are set it prints SHIP OFF COURSE! and randomises DS:4CA/4CB/4CC
#                with three `rand & 7`; then `call L_007230` (asm/7030) and `ret`.
#
#           CORRECTION TO THIS CORRECTION, same day: the paragraph below claimed nothing in
#           this routine decrements DS:4C7, and that was a partial read -- the same mistake
#           this file exists to document. The sweep DOES descend, in the 'H' arm, which is
#           the real hyperwarp command and sits AFTER L_0070c4. Full order:
#
#             'H' arm (L_0070c5): if DS:E5 == 0 print NO FUEL! and return to the orbit loop;
#               else DS:E5 = DS:E5 - 1 in packed decimal (`sbb $1 ; das`), erase the ship at
#               its old position, RESET THE SHIP to DS:47B=0x80, DS:47C=0x40, print
#               "HYPERWARP TO:" and read three single decimal digits -- `L_00717f` draws the
#               field, delays, polls, and accepts only 0x30..0x39, returning the digit minus
#               0x30 -- into DS:4CA, DS:4CB, DS:4CC. So the destination is TYPED, one digit
#               per axis, and each digit redraws the field while it waits.
#             print "PREPARE FOR HYPERWARP!"
#             PRE1: do { field draw ; delay 0x1a ; decb DS:4C7 } while (DS:4C7 != 2)
#                   -- the descend, and it descends TO 2, from whatever the last warp left.
#             PRE2: DS:4C8 = 0x40 ; 64 x { field draw ; delay 0x1a } at sweep 2.
#             then `call L_006ffd`, the three phases above, then back to the orbit loop.
#
#           So the engine's U2_SWEEP_DESCEND is in the right family and was right to exist;
#           what the bytes say and the constant does not is the target (2, not 1) and the
#           starting value: nothing initialises DS:4C7 before PRE1, so it descends from
#           whatever the previous warp left, which is 128 after the first warp and therefore
#           126 passes -- not a fixed 192. U2_SWEEP_TOP_HOLD still has no counterpart: 128 is
#           the value that ENDS phase C via the sign test and nothing holds it there. Both are
#           now decidable from the bytes, so this is engine work, not a capture question.
#
#           THE SUPERSEDED PARAGRAPH, kept because it is the record of a wrong claim: it said
#           the engine's sweep constants in space_view.h are
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
hyperwarp_flow:
L_006ffd:
        call L_0051df
        db "HYPERWARP ENGAGED!",0x8d,0x00        # string
L_007014:
        mov    $0x80,%al
L_007016:
        mov    %al,0x4c8
L_007019:
        .byte 0xb0,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%al' differently
L_00701b:
        mov    %al,0x4c7
L_00701e:
        call L_006f5b
L_007021:
        mov    $0x2200,%bx
L_007024:
        mov    %bx,%cs:0x6990
L_007029:
        movw   $0xffff,%cs:0x6992
L_007030:
        call L_007476
L_007033:
        call L_006d3d
L_007036:
        mov    $0x16,%bx
L_007039:
        call L_000911
L_00703c:
        decb   0x4c8
L_007040:
        jne L_007033
L_007042:
        mov    $0x80,%al
L_007044:
        mov    %al,0x4c8
L_007047:
        mov    $0x1,%al
L_007049:
        mov    %al,0x4c7
L_00704c:
        call L_0071a3
L_00704f:
        call L_006f5b
L_007052:
        movw   $0x1,%cs:0x6992
L_007059:
        call L_006d3d
L_00705c:
        mov    $0x16,%bx
L_00705f:
        call L_000911
L_007062:
        decb   0x4c8
L_007066:
        jne L_007059
L_007068:
        incb   0x4c7
L_00706c:
        movw   $0x1,%cs:0x6994
L_007073:
        call L_006d3d
L_007076:
        mov    $0x18,%bx
L_007079:
        call L_000911
L_00707c:
        incb   0x4c7
L_007080:
        incb   0x4c7
L_007084:
        jns L_007073
L_007086:
        mov    0xe5,%al
L_007089:
        clc
L_00708a:
        rcr    $1,%al
L_00708c:
        jae L_0070c0
L_00708e:
        clc
L_00708f:
        rcr    $1,%al
L_007091:
        jae L_0070c0
L_007093:
        call L_0051df
        db "SHIP OFF COURSE!",0x8d,0x00        # string
L_0070a8:
        call L_006f68
L_0070ab:
        and    $0x7,%al
L_0070ad:
        mov    %al,0x4ca
L_0070b0:
        call L_006f68
L_0070b3:
        and    $0x7,%al
L_0070b5:
        mov    %al,0x4cb
L_0070b8:
        call L_006f68
L_0070bb:
        and    $0x7,%al
L_0070bd:
        mov    %al,0x4cc
L_0070c0:
        nop
L_0070c1:
        call L_007230
L_0070c4:
        ret
L_0070c5:
        mov    0xe5,%al
L_0070c8:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_0070ca:
        jne L_0070dc
L_0070cc:
        call L_0051df
        db "NO FUEL!",0x8d,0x00        # string
L_0070d9:
        jmp L_006c79
L_0070dc:
        stc
L_0070dd:
        cmc
L_0070de:
        sbb    $0x1,%al
L_0070e0:
        das
L_0070e1:
        cmc
L_0070e2:
        mov    %al,0xe5
L_0070e5:
        call L_006ec4
L_0070e8:
        mov    $0x80,%al
L_0070ea:
        mov    %al,0x47b
L_0070ed:
        mov    $0x40,%al
L_0070ef:
        mov    %al,0x47c
L_0070f2:
        call L_0051df
        db "HYPERWARP TO:",0x8d,"XENO=",0x00        # string
L_007109:
        call L_00717f
L_00710c:
        mov    %al,0x4ca
L_00710f:
        call L_0051df
        db " YAKO=",0x00        # string
L_007119:
        call L_00717f
L_00711c:
        mov    %al,0x4cb
L_00711f:
        call L_0051df
        db " ZABO=",0x00        # string
L_007129:
        call L_00717f
L_00712c:
        mov    %al,0x4cc
L_00712f:
        call L_0051df
        db 0x8d,"PREPARE FOR HYPERWARP!",0x8d,0x00        # string
L_00714b:
        call L_006f5b
L_00714e:
        call L_006d3d
L_007151:
        mov    $0x1a,%bx
L_007154:
        call L_000911
L_007157:
        decb   0x4c7
L_00715b:
        mov    0x4c7,%al
L_00715e:
        cmp    $0x2,%al
L_007160:
        jne L_00714e
L_007162:
        mov    $0x40,%al
L_007164:
        mov    %al,0x4c8
L_007167:
        call L_006f5b
L_00716a:
        call L_006d3d
L_00716d:
        mov    $0x1a,%bx
L_007170:
        call L_000911
L_007173:
        decb   0x4c8
L_007177:
        jne L_00716a
L_007179:
        call L_006ffd
L_00717c:
        jmp L_006c79
L_00717f:
        call L_006d3d
L_007182:
        mov    $0x14,%bx
L_007185:
        call L_000911
L_007188:
        call L_00514d
L_00718b:
        cmp    $0xff,%ah
L_00718e:
        jne L_00717f
L_007190:
        cmp    $0x30,%al
L_007192:
        jb L_00717f
L_007194:
        cmp    $0x39,%al
L_007196:
        ja L_00717f
L_007198:
        push   %ax
L_007199:
        call L_0055f4
L_00719c:
        pop    %ax
L_00719d:
        stc
L_00719e:
        cmc
L_00719f:
        sbb    $0x30,%al
L_0071a1:
        cmc
L_0071a2:
        ret
