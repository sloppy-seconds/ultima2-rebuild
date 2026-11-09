# space_collision — space navigation, and the writer of the world byte DS:6D
# name-evidence: string 'U HIT THE SUN!'; also 'YOU ARE IN DEEP SPACE.', 'YOU ARE ORBITING ',
#           'LANDING REQUESTED!', 'REQUEST DENIED!', 'HYPERWARP ENGAGED!', 'SHIP OFF COURSE!'
# THE WORLD BYTE: this is the only writer of DS:6D in the tree, and what it writes fixes the
#           meaning of the byte (G44, 2026-10-29):
#             * `mov $0xa,%al ; mov %al,0x6d` immediately before 'YOU ARE IN DEEP SPACE.'
#               -- so 0x0A is deep space.
#             * a second `mov %al,0x6d` beside 'YOU ARE ORBITING ' -- the byte names a world.
#             * the dispatch on it is `cmp $0x0` (Earth), `$0x1`, `$0x2`, ... and `cmp $0xa`
#               inside the landing path, which answers 'REQUEST DENIED!': you cannot land in
#               deep space.
#             * `mov 0x6d,%al ; mov %al,0x49` copies it into DS:49, the sector TENS digit.
#               The world you are on and the high digit of the sector number are the same
#               value, which is how MAPXNN reaches a planet's sectors at all.
#           G45 RESOLVED (2026-10-29, confirmed from the landing side 2026-11-03). DS:49 is
#           one byte carrying two axes that are distinguished by DS:6D, the world byte:
#             * ON EARTH (DS:6D == 0) the tens digit is the ERA axis, 0..4.
#               Observed: a time-door transit moved DS:49 02 -> 03 -> 02 while DS:6D stayed
#               00 (captures/spice86/moongate-cycle). asm/3546 is the reader of that axis.
#             * OFF EARTH the tens digit is the WORLD axis, copied from DS:6D here.
#               Observed: landing on world 9 wrote DS:49 = 0x09 (sector 90); landing on
#               Earth wrote DS:49 = 0x04 (sector 40) (captures/spice86/g39-rocket-flight).
#           So the two readings are not competing meanings for one situation; they are the
#           two situations, and reading this routine next to asm/3546 could never decide it
#           because neither routine is wrong.
#           TRAP, and this project has walked into it more than once: DO NOT infer the sector
#           model from the root mapx file list. The root set is the DAMAGE, not the design.
#           MS-DOS had no directories, so MAPG10 and mapx10 were the same name, and the
#           floppy-to-CD folder merge kept Earth's era maps and overwrote the planet maps.
#           The observation "every tens group has exactly one wilderness X0 plus its sites"
#           therefore looks like ten worlds and proves nothing of the kind -- it is what five
#           Earth eras plus a name collision looks like from the outside. Earth and Planet X
#           are mapx; Mercury..Pluto are MAPG, shipped in original/updated/ by the Galaxy
#           patch. Reaching Mercury requires space travel, not a time door.
#           Full record: docs/planet-maps-galaxy-patch.md, docs/moongates.md § 4.
# WORLD NAMES, from this routine's dispatch on DS:6D (cmp $0x0 .. cmp $0x8):
#           0 EARTH   1 MERCURY  2 VENUS   3 MARS    4 JUPITER
#           5 SATURN  6 URANUS   7 NEPTUNE 8 PLUTO   9 'X'
#           0x0A is deep space and has no name of its own; it prints 'YOU ARE IN DEEP
#           SPACE.' instead. The name for 9 is NOT a literal in this file: the chain
#           falls through to `jmp L_00737a`, which is past this routine's declared end
#           (0x7241) and past the highest offset ever executed (0x729B), so the world-9
#           name is still an open decode even though 'X' was seen on screen.
# LANDING SECTOR, computed not random:
#           world == 0:  mov $0x0,%al ; mov %al,0x4a
#                        mov $0x4,%al ; mov %al,0x49      -> sector 40
#           world != 0:  mov $0x0,%al ; mov %al,0x4a
#                        mov 0x6d,%al ; mov %al,0x49      -> sector world*10 + 0
#           'REQUEST DENIED!' fires on exactly one condition, `cmp $0xa,%al` against
#           DS:6D: you cannot land from deep space. Any orbiting world can be landed on.
#           The ones digit is always forced to 0, so a rocket always arrives in the
#           WILDERNESS sector of its world, never in a site.
#           asm/3DFA refuses the save when this byte is non-zero, with the message
#           'ONLY ON EARTH!'. asm/6999_space_status reads it as an index.
# func 7030..7241  len 0212 B  evidence: call+reko
# strings: "U HIT THE SUN!"; "t&"; "Oy"; "YOU ARE IN DEEP SPACE." (+14 more)
# calls: L_000911, L_0051df (str_out), L_006ec4, L_00737a, L_007384, L_007489
# provenance: observed 2026-11-03 -- captures/spice86/g39-rocket-flight/
#           spice86dumpExecutionFlow.json (segment 017D), entry present in
#           ExecutedInstructions. Both DS:6D writes are on record: 0x0A with
#           'YOU ARE IN DEEP SPACE.' (24-arrive.png) and 0x09 with 'YOU ARE ORBITING X.'
#           (26-h999-after.png), each read back from live DS in the same second.
#           TRACE WINDOW CAVEAT: the dump was taken at 15:36:51 and the dive landed at
#           ~15:43, so the ORBIT and HYPERWARP arms are traced and the LANDING path is
#           not. The landing sector arithmetic and the 'REQUEST DENIED!' test are graded
#           observed from live DS readback (DS:49 = 0x09, sector 90) plus frames, not
#           from ExecutedInstructions. Do not cite this trace for the landing path.
# confidence: high for the world byte, the name list 0..8, the deep-space refusal and
#           the landing sector arithmetic; low for the world-9 name routine, which has
#           never executed.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
space_collision:
L_007230:
        nop
L_007231:
        mov    0x4ca,%al
L_007234:
        cmp    $0x4,%al
L_007236:
        jne L_007263
L_007238:
        mov    0x4cb,%al
L_00723b:
        cmp    $0x4,%al
L_00723d:
        jne L_007263
L_00723f:
        mov    0x4cc,%al
L_007242:
        cmp    $0x4,%al
L_007244:
        jne L_007263
L_007246:
        call L_0051df
L_007249:
        lea    0x4f59(%di),%cx
L_00724d:
        db "U HIT THE SUN!",0x8d,0x00,0xe8        # string
L_00725e:
        out    %al,(%dx)
L_00725f:
        fucompp
L_007261:
        adc    %di,%dx
L_007263:
        nop
L_007264:
        mov    $0x0,%bh
L_007266:
        mov    $0x9,%bl
L_007268:
        .byte 0x8b,0xfb  # forced-kept: mov    %bx,%di
L_00726a:
        mov    %cs:0x7012(%di),%al
L_00726f:
        cmp    0x4ca,%al
L_007273:
        jne L_00728b
L_007275:
        mov    %cs:0x701c(%di),%al
L_00727a:
        cmp    0x4cb,%al
L_00727e:
        jne L_00728b
L_007280:
        mov    %cs:0x7026(%di),%al
L_007285:
        cmp    0x4cc,%al
L_007289:
        db "t&"        # string
L_00728b:
        db "Oy",0xdc        # string
L_00728e:
        call L_0051df
L_007291:
        db "YOU ARE IN DEEP SPACE.",0x8d,0x00        # string
L_0072a9:
        mov    $0xa,%al
L_0072ab:
        mov    %al,0x6d
L_0072ae:
        jmp L_007384
L_0072b1:
        .byte 0x8b,0xc7  # forced-kept: mov    %di,%ax
L_0072b3:
        mov    %al,0x6d
L_0072b6:
        call L_0051df
L_0072b9:
        db "YOU ARE ORBITING ",0x00        # string
L_0072cb:
        mov    0x6d,%al
L_0072ce:
        cmp    $0x0,%al
L_0072d0:
        je L_0072f5
L_0072d2:
        cmp    $0x1,%al
L_0072d4:
        je L_007303
L_0072d6:
        cmp    $0x2,%al
L_0072d8:
        je L_007313
L_0072da:
        cmp    $0x3,%al
L_0072dc:
        je L_007321
L_0072de:
        cmp    $0x4,%al
L_0072e0:
        je L_00732e
L_0072e2:
        cmp    $0x5,%al
L_0072e4:
        je L_00733e
L_0072e6:
        cmp    $0x6,%al
L_0072e8:
        je L_00734d
L_0072ea:
        cmp    $0x7,%al
L_0072ec:
        je L_00735c
L_0072ee:
        cmp    $0x8,%al
L_0072f0:
        je L_00736c
L_0072f2:
        jmp L_00737a
L_0072f5:
        call L_0051df
L_0072f8:
        db "EARTH.",0x8d,0x00        # string
L_007300:
        jmp L_007384
L_007303:
        call L_0051df
L_007306:
        db "MERCURY.",0x8d,0x00        # string
L_007310:
        jmp L_007384
L_007312:
        nop
L_007313:
        call L_0051df
L_007316:
        db "VENUS.",0x8d,0x00        # string
L_00731e:
        jmp L_007384
L_007320:
        nop
L_007321:
        call L_0051df
L_007324:
        db "MARS.",0x8d,0x00        # string
L_00732b:
        jmp L_007384
L_00732d:
        nop
L_00732e:
        call L_0051df
L_007331:
        db "JUPITER.",0x8d,0x00        # string
L_00733b:
        jmp L_007384
L_00733d:
        nop
L_00733e:
        call L_0051df
L_007341:
        db "SATURN.",0x8d,0x00        # string
L_00734a:
        jmp L_007384
L_00734c:
        nop
L_00734d:
        call L_0051df
L_007350:
        db "URANUS.",0x8d,0x00        # string
L_007359:
        jmp L_007384
L_00735b:
        nop
L_00735c:
        call L_0051df
L_00735f:
        db "NEPTUNE.",0x8d,0x00        # string
L_007369:
        jmp L_007384
L_00736b:
        nop
L_00736c:
        call L_0051df
L_00736f:
        db "PLUTO.",0x8d,0x00        # string
L_007377:
        jmp L_007384
L_007379:
        nop
L_00737a:
        call L_0051df
L_00737d:
        pop    %ax
L_00737e:
        lea    %cs:(%bx,%si),%ax
L_007381:
        jmp L_007384
L_007383:
        nop
L_007384:
        ret
L_007385:
        call L_006ec4
L_007388:
        mov    $0x80,%al
L_00738a:
        mov    %al,0x47b
L_00738d:
        mov    $0x40,%al
L_00738f:
        mov    %al,0x47c
L_007392:
        call L_0051df
L_007395:
        db "LANDING REQUESTED!",0x8d,0x00,0xe8        # string
L_0073aa:
        test   %bh,%dh
L_0073ac:
        mov    0x6d,%al
L_0073af:
        cmp    $0xa,%al
L_0073b1:
        jne L_0073ca
L_0073b3:
        call L_0051df
L_0073b6:
        db "REQUEST DENIED!",0x8d,0x00,0xe9        # string
L_0073c8:
        scas   %es:(%di),%ax
L_0073c9:
        clc
L_0073ca:
        nop
L_0073cb:
        cmp    $0x0,%al
L_0073cd:
        jne L_007407
L_0073cf:
        .byte 0xeb,0x17  # forced-kept: jmp    0x73e8
L_0073d1:
        fisubrs (%bx,%si)
L_0073d3:
        db "INSERT PLAYER DISK)",0x8d,0x00        # string
L_0073e8:
        db         # string
        db 0xe8        # string
L_0073e9:
        push   %dx
L_0073ea:
        stc
L_0073eb:
        mov    $0x14,%bx
L_0073ee:
        call L_000911
L_0073f1:
        .byte 0xeb,0x06  # forced-kept: jmp    0x73f9
L_0073f3:
        fnstsw (%di)
L_0073f5:
        .byte 0x1b,0xff  # forced-kept: sbb    %di,%di
L_0073f7:
        jne L_0073e8
L_0073f9:
        mov    $0x0,%al
L_0073fb:
        mov    %al,0x4a
L_0073fe:
        mov    $0x4,%al
L_007400:
        mov    %al,0x49
L_007403:
        call L_007489
L_007406:
        ret
L_007407:
        .byte 0xeb,0x19  # forced-kept: jmp    0x7422
L_007409:
        .byte 0xdd,0x28  # forced-kept: (bad) (%bx,%si)
L_00740b:
        db "INSERT GALACTIC DISK)",0x8d,0x00        # string
L_007422:
        db         # string
        db 0xe8        # string
L_007423:
        sbb    %bh,%cl
L_007425:
        mov    $0x14,%bx
L_007428:
        call L_000911
L_00742b:
        .byte 0xeb,0x06  # forced-kept: jmp    0x7433
L_00742d:
        fnstsw (%di)
L_00742f:
        .byte 0x1b,0xff  # forced-kept: sbb    %di,%di
L_007431:
        jne L_007422
L_007433:
        mov    $0x0,%al
L_007435:
        mov    %al,0x4a
L_007438:
        mov    0x6d,%al
L_00743b:
        mov    %al,0x49
L_00743e:
        call L_007489
L_007441:
        ret
