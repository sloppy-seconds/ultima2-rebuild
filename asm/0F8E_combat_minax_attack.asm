# combat_minax_attack — Minax boss attack event
# name-evidence: string 'MINAX CRIES: DIE FOOL!'
# corroboration 2026-10-22: Gamer Corner's Minax tile is pixel-identical to our art
#   0x10 (see bestiary/bestiary.tsv), and it describes "exactly 100 damage, range 3".
#   External claim; the 100-damage figure matches docs/spice86-live-findings.md and
#   needs a live re-check before it is treated as observed. Changes no name.
#   ** UPDATE 2026-11-09: the 100-damage claim is now corroborated by OUR OWN BYTES, not by
#   the wiki. The tail of this routine is a packed-decimal subtract on the HIGH byte of HP:
#     11BB  stc                       ; set CF
#     11BC  mov 0x51,%al              ; DS:51 = the high byte of the H.P. pair [0x51],[0x52]
#     11BF  cmc                       ; clear CF, so the sbb below subtracts exactly 1
#     11C0  sbb $0x1,%al
#     11C2  das                       ; decimal-adjust: this is "packed-decimal minus one"
#     11C3  cmc                       ; invert the borrow, so jb now means "still alive"
#     11C4  mov %al,0x51
#     11C7  jb L_0011cc               ; no borrow -> ret
#     11C9  jmp L_000e62              ; borrow -> the death tail (asm/0C62_death)
#   Subtracting one from the HUNDREDS byte is exactly 100 HP, and the borrow is what kills:
#   Minax does not zero HP, it decrements the high pair and lets the underflow run the death
#   path. So a character at 99 HP or below dies to one strike and a character at 9999 dies
#   after 100 strikes, which is why goal-spec's W4 threshold of 9900 is worth chasing -- it
#   is within one hundredth of the ceiling this instruction can address. Third-party claim
#   promoted to measured-by-bytes; the range-3 half is asm/1040's own `< 4` window.
#   ** OBSERVED 2026-11-12: this routine has now executed.** Owner play session in the
#   original (captures/spice86/minax-fight-live/); he read `MINAX CRIES: DIE FOOL!` on screen
#   and reported the strike landing from about three cells away, indoors. Until that session
#   both of this routine's strings were recorded in the corpus as discovered-but-not-executed,
#   so the 100-damage subtract above had never been seen to run against a live character. It
#   has now: HP fell from 9000 to 2336 during the fight, which is the high-byte decrement
#   firing roughly 67 times, and the character survived because the borrow only arrives when
#   the hundreds byte underflows -- exactly the behaviour the byte sequence predicts.
#   The two writes at 11AC/11AF zero DS:19 and DS:1A, which asm/29D2 uses as scratch loop
#   cursors for the creature sweep (it does `incb 0x1a ... cmp $0x40`), so this reads as
#   "reset the sweep before the strike". INFERRED, not proven.
# func 0F8E..0FCC  len 003F B  evidence: call+reko
# strings: "MINAX CRIES: DIE FOOL!"
# calls: L_000e62, L_0051df (str_out), L_00543e, L_0060f2
# provenance: static
#           entry never appears in ExecutedInstructions of any of the 60 harvests; derived by tools/provenance_from_traces.py
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
combat_minax_attack:
L_00118e:
        nop
L_00118f:
        call L_0051df
        db "MINAX CRIES: DIE FOOL!",0x8d,0x00        # string
L_0011aa:
        .byte 0xb0,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%al' differently
L_0011ac:
        .byte 0xa2,0x19,0x00  # forced-kept: gas re-encodes 'mov    %al,0x19' differently
L_0011af:
        mov    %al,0x1a
L_0011b2:
        call L_00543e
L_0011b5:
        call L_0060f2
L_0011b8:
        call L_00543e
L_0011bb:
        stc
L_0011bc:
        mov    0x51,%al
L_0011bf:
        cmc
L_0011c0:
        sbb    $0x1,%al
L_0011c2:
        das
L_0011c3:
        cmc
L_0011c4:
        mov    %al,0x51
L_0011c7:
        jb L_0011cc
L_0011c9:
        jmp L_000e62
L_0011cc:
        ret
