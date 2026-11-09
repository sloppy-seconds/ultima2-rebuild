# time_index_message — turns the numeric time index in %di into the period label
#           the game prints as "ANOS: <date>".
# name-evidence: five inline strings, file 0x3752 "ANOS: 2112 A.D.", 0x3768
#           "ANOS: LEGENDS!", 0x377D "ANOS: 9,000,000 B.C.", 0x3798
#           "ANOS: 1423 B.C.", 0x37AE "ANOS: 1990 A.D."
# THE TENS DIGIT IS AN ERA INDEX ON EARTH -- G45 RESOLVED 2026-10-29, see docs/moongates.md § 4.
#           The index here is DS:49, the sector TENS digit, and asm/7030_space_collision also
#           writes DS:49 -- from DS:6D, the world byte (0 Earth, 1..9 planets, 0x0A deep
#           space). That looked like one byte with two impossible meanings. It is one byte
#           with two axes, told apart by DS:6D:
#             * DS:6D == 0 (Earth): DS:49 is this routine's era index, 0..4. Settled by
#               watching a time door move DS:49 02 -> 03 -> 02 while DS:6D stayed 00
#               (captures/spice86/moongate-cycle).
#             * DS:6D != 0 (a planet): DS:49 is the world index, written by asm/7030 on
#               landing (captures/spice86/g39-rocket-flight: world 9 -> DS:49 = 0x09).
#           Both of the experiments G45 asked for have now been run, and this routine's five
#           arms are correct as stated: they label eras, and they are only reached on Earth.
#           TRAP, walked into more than once in this project: the argument "eras are 0..4 but
#           the mapx set runs to tens 9, and every tens group has one wilderness X0 plus its
#           sites, so tens must be a world" is INVALID. The root mapx list is the damage, not
#           the design. MS-DOS had no directories, so MAPG10 and mapx10 were the same name and
#           the floppy-to-CD merge overwrote the planet maps with Earth's era maps. The real
#           planet maps are MAPG/MONG/TLKG in original/updated/, shipped by the Galaxy patch
#           and unapplied in our pack. Earth and Planet X are mapx; Mercury..Pluto are MAPG,
#           and reaching them requires the rocket, not a time door.
#           Full record: docs/planet-maps-galaxy-patch.md.
# func 3546..3564  len 001F B  evidence: seed+reko
# dispatch (observed: the dec/je chain and each call target):
#   %di == 1 -> "ANOS: 9,000,000 B.C."  (call at file 0x377A)
#   %di == 2 -> "ANOS: 1423 B.C."       (call at file 0x3795)
#   %di == 3 -> "ANOS: 1990 A.D."       (call at file 0x37AB)
#   %di == 4 -> falls through to the call at file 0x374F: "ANOS: 2112 A.D."
#   "ANOS: LEGENDS!" is at file 0x3765 (asm/3565_sub_3565.asm, a stop-trunc
#   one-byte call) and is NOT targeted by this chain; it is reached from a branch
#   before 3546, outside this range.
#   CORRECTION 2026-10-28d on that last line: the branch before 3546 is now identified — it is
#   `or %di,%di ; je 3565` at runtime 3544, i.e. LEGENDS is the %di == 0 arm of the same era
#   dispatch, one instruction upstream. The five arms are 0/1/2/3/>=4, not 1/2/3/4-plus-default.
# notes: "ANOS" is the literal text, not a truncation - the byte before it is the
#        high half of the preceding call operand (0x1A), so the string begins at
#        the A. Whether it is intended as pseudo-Latin for "year" is not provable
#        from bytes.
#        The entry label still reads set_year_2112, which is now a poor name for a
#        five-way dispatch. It is left alone on purpose: the CP0 guard freezes asm
#        body content, and a label emits no bytes, so renaming it buys nothing and
#        breaks the guard. Naming lives in the filename and this header.
# calls: L_000c33 (return to command loop), L_0051df (str_out)
# apple2-twin: $777B in ULTIMA.II.OBJ, and it is the same dispatch, not a similar one. Hand-
#           decoded: `LDX $4E13` then `BEQ / DEX / BEQ / DEX / BEQ / DEX / BEQ`, five arms, each
#           a `JSR $4006` followed by its inline era string, and the fall-through (index >= 4)
#           prints 2112 A.D. — exactly this file's `dec %di ; je` chain with 2112 as the
#           fall-through. The order matches too: 0 LEGENDS, 1 9,000,000 B.C., 2 1423 B.C.,
#           3 1990 A.D., 4+ 2112 A.D. CORROBORATED, and it settles the part of G14 that
#           mattered: the five-way era split is original 1983 design, not a DOS-port invention.
#           What the Apple side adds is the SHAPE of the answer. $4E13 has exactly ONE writer
#           in the whole resident image, at $6BF8, and it is computed: `LDA $4E38 ; LSR A ;
#           CMP $4E13 ; BCC keep ; CLC ; ADC #$01 ; STA $4E13`. A value shifted right by one,
#           then plus one. That is the arithmetic that turns ten things into five — and $4E13
#           sits one byte below $4E14, which asm/2199 pins to DS:4A, the sector ONES digit.
#           tools/u2_edit.py records DS:49/4A as the sector's two decimal digits patched into
#           the mapx filename, so DS:49 is the TENS digit, and the mapping the ticket guessed
#           is the mapping the ancestor implements: tens 0-9, >>1, into five eras.
#           GRADE — RESOLVED 2026-10-28d, statically, no capture needed. The writer of %di was
#           found two instructions earlier, and it had been hidden by a mis-decode rather than
#           by absence. Runtime 353A..3545, decoded from original/ultimaii.exe file 0x373A:
#               353A  mov $0x0,%bh
#               353C  mov 0x49,%bl        ; BL = DS:49 = the sector TENS digit
#               3540  mov %bx,%di         ; DI = the tens digit, zero-extended via BH=0
#               3542  or %di,%di
#               3544  je 3565             ; tens == 0 -> "ANOS: LEGENDS!"
#               3546  <this dispatch>
#           So the index is the sector tens digit used DIRECTLY, not shifted. The ticket's
#           hypothesis was right about which byte and wrong about the arithmetic; the Apple side
#           is right about the arithmetic and its world is partitioned differently. Both halves
#           of the guess were load-bearing and only one survived, which is why this is recorded as
#           a resolution and not a confirmation.
# THE FULL ERA TABLE, now complete and each arm byte-verified:
#           DS:49 == 0  ->  runtime 3565  "ANOS: LEGENDS!"
#           DS:49 == 1  ->  runtime 357A  "ANOS: 9,000,000 B.C."
#           DS:49 == 2  ->  runtime 3595  "ANOS: 1423 B.C."
#           DS:49 == 3  ->  runtime 35AB  "ANOS: 1990 A.D."
#           DS:49 >= 4  ->  runtime 354F  "ANOS: 2112 A.D."   (dec/je fall-through)
#           The world is 5x5 sectors, so the tens digit runs 0..4 and yields exactly five eras.
#           That is why DOS needs no shift: five sectors, five digits, one-to-one. The Apple
#           build's `>>1 then +1` is the arithmetic of a ten-column world folded into five, and
#           the two builds therefore partition the same map differently — a real divergence, not
#           a decoding gap on either side.
# WHY THE WRITER WAS MISSED FOR SO LONG: asm/353A ends at 353C with `.byte 0x8a` marked
#           forced-kept, and asm/353D opens with `push %ds`. Both are wrong in the same way: the
#           instruction at 353C is the four bytes `8a 1e 49 00` = `mov 0x49,%bl`, and the clean
#           wave stopped after the opcode, then re-read the ModRM byte 0x1e as `push %ds` and the
#           displacement 0x49 as `dec %cx`. A four-byte instruction was sliced into three, and the
#           DS:49 read — the single byte G14 was hunting — sat inside the wreckage as a `push`.
#           The mis-decode is documented in asm/353A and asm/353D and in disasm/defects.md; the
#           body lines cannot be corrected here because a label and an instruction are both body
#           and CP0 md5s them.
# provenance: static
#           entry never appears in ExecutedInstructions of any of the 60 harvests; derived by tools/provenance_from_traces.py
# confidence: high for the dispatch and for the index's source (DS:49, read at runtime 353C and
#           moved to %di at 3540, both byte-verified from the original); medium for the era-to-
#           sector mapping in play, since no harvest has ever executed this entry and the world
#           is 5x5 by inference from the shipped mapx set rather than by a walk
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
set_year_2112:
L_003746:
        dec    %di
L_003747:
        je L_00377a
L_003749:
        dec    %di
L_00374a:
        je L_003795
L_00374c:
        dec    %di
L_00374d:
        je L_0037ab
L_00374f:
        call L_0051df
        db "ANOS: 2112 A.D.",0x00        # string
L_003762:
        jmp L_000c33
