# func 154E..155C  len 000F B  evidence: seed+call+reko
# calls: L_001764
# tlkx_record_printer — print the Nth record out of the sector's loaded dialogue buffer
# name-evidence: read directly, 2026-10-29. The body is
#             154E  push %es ; si = AL ; di = [DS:476] ; es = ds
#             1564  al = es:[di] ; cmp 0 ; je 156E        -- scan to a NUL
#             156B  inc di ; jmp 1564
#             156E  dec si ; je 1573 ; else jmp 156B      -- count NULs until SI reaches 0
#             1573  inc di ; al = es:[di] ; cmp 0 ; je 1580
#             157B  call L_0055f4 (emit one char) ; jmp 1573
#             1580  pop %es ; ret
#           [DS:476] is the monx/tlkx load address — asm/22D5 is the loader that patches the
#           sector's two decimal digits into a filename and reads 0x100 bytes at [DS:476],
#           the same shape asm/2290 uses for mapx. The buffer is the sector's tlkxNN file:
#           high-bit-set ASCII, NUL between records, `<NAME> <VERB>: <LINE>` per record
#           (tools/tlkx_decode.py is the reference decode), and it starts with a NUL, so
#           record 1 is the first non-empty one and AL selects it directly.
#           The caller is asm/4209, which arrives with AL = [slot+0x1D7] - 0x80. That is the
#           selection rule docs/archive/handoff-commerce-transact.md § 4.5 recorded as "completely
#           unknown": the role byte IS the tlkx record index, high bit set as the Apple II
#           convention. Observed 2026-10-29 with a breakpoint at this entry: a role-0x82
#           Jester in sector 31 entered with AL=0x02 and DI=0x11 (slot 17), and tlkx31
#           record 2 is `RONALL MC DONALL SINGS: ...`.
# note: asm/155D is a SECOND ENTRY POINT into the same body, not a second routine: it enters
#           at 155C with ES=CS and DI=[DS:27] instead of ES=DS and DI=[DS:476], which is how
#           the tavern's barkeep chatter table (asm/16BD) is read out of the code segment.
#           That is why asm/155D's header calls itself a prologue of a routine running to
#           about 0x1581 — it is the same routine, entered one instruction later.
# provenance: live:food-buy, g23-food2, jester3, play-vnc2, play-vnc2-live, spell-slot-map
#           entry executed in 6 of the harvests that carry a flow dump; derived from
#           ExecutedInstructions, and stopped at this address with a live register dump on
#           2026-10-29 (captures/spice86/g23-food-live).
# confidence: high (bytes proven by check_func.py; the walk, the buffer and the index are all
#           read, and one live entry agrees)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_154e:
L_00174e:
        nop
L_00174f:
        push   %es
L_001750:
        mov    $0x0,%ah
L_001752:
        .byte 0x8b,0xf0  # forced-kept: gas re-encodes 'mov    %ax,%si' differently
L_001754:
        mov    0x476,%di
L_001758:
        push   %ds
L_001759:
        pop    %es
L_00175a:
        jmp L_001764
L_00175c:
        nop
