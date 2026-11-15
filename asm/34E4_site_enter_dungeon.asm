# site_enter_dungeon — the "-DUNGEON" arm: bank the sector you are leaving, then read the
#           dungeon's own mapx in over the same buffer
# name-evidence: read directly, 2026-11-15. The body is five moves and their ORDER is the
#           whole finding:
#                 0x36e4  DS:12 = 4 ; jmp cmd_done        (a separate short arm)
#                 0x36ec  call str_out -> "-DUNGEON"
#                 0x36f8  DS:5A = DS:00 ; DS:5B = DS:01   (park the outdoor position)
#                 0x3704  call the store gate (asm/22FD -> asm/2305)
#                 0x3707  DS:25 = 0 (site floor) ; DS:03 = 0 ; DS:02 = 1
#                 0x3714  DS:00 = DS:01 = 5               (arrival cell 5,5)
#                 0x371c  DS:4A = 5                       (ones digit 5 = dungeon)
#                 0x371f  call the mapx loader (asm/2290) ; jmp cmd_done
#           The store runs BEFORE DS:4A is reassigned, so what reaches disk is the sector
#           you were standing in and never the dungeon you are entering. The loader then
#           reads 0x1000 bytes at DS:472 — the same buffer asm/2305 writes from — so every
#           entry starts from the file. That ordering is what settles G81: a dungeon's own
#           mapx is read fresh on every entry and never written back, so an emptied chest
#           refills and nothing inside a site persists, without anyone having implemented a
#           rule to that effect. See docs/needs-gameplay.md G81 and the two checks named
#           "G81:" in engine/tests/site_check.c.
# func 34E4..3524  len 0041 B  evidence: seed+reko
# strings: "-DUNGEON"
# calls: L_000c33 (cmd_done), L_002490 (asm/2290, the mapx loader),
#           L_0024fd (asm/22FD, the Earth-gated store gate), L_0051df (str_out)
# renamed 2026-11-15 from sub_34e4 with the ordering above read out of the body. The
#           "-DUNGEON" literal was the only clue in the file and it was already recorded;
#           what was missing was reading the two calls as an order rather than a set.
# note: DS:5A/DS:5B as the parked outdoor coordinates is inferred from the two writes and
#           the arrival-cell overwrite immediately after them, not separately confirmed.
# confidence: high for the ordering and the two call targets (bytes); medium for DS:5A/5B
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_34e4:
L_0036e4:
        mov    $0x4,%al
L_0036e6:
        mov    %al,0x12
L_0036e9:
        jmp L_000c33
L_0036ec:
        call L_0051df
        db "-DUNGEON",0x00        # string
L_0036f8:
        .byte 0xa0,0x00,0x00  # forced-kept: gas re-encodes 'mov    0x0,%al' differently
L_0036fb:
        mov    %al,0x5a
L_0036fe:
        mov    0x1,%al
L_003701:
        mov    %al,0x5b
L_003704:
        call L_0024fd
L_003707:
        mov    $0x0,%al
L_003709:
        mov    %al,0x25
L_00370c:
        mov    %al,0x3
L_00370f:
        mov    $0x1,%al
L_003711:
        mov    %al,0x2
L_003714:
        mov    $0x5,%al
L_003716:
        mov    %al,0x0
L_003719:
        mov    %al,0x1
L_00371c:
        mov    %al,0x4a
L_00371f:
        call L_002490
L_003722:
        jmp L_000c33
