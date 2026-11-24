# dungeon_monster_spawn — scans the 32 monster slots for an empty one and fills it
#           with a RANDOM monster whose HP is computed, not an immediate
# name-evidence: its only caller in the tree is asm/1151_dungeon_darkness, which
#           establishes the context. It loops `bl = 0x1f` down to 1 over the record
#           arrays, tests [DI+0x197] for empty, then calls the RNG (L_005417) three
#           times: x = (rand&7)*2|1 and y likewise, i.e. odd coordinates in 1..15,
#           which matches the dungeon coordinate range already recorded in
#           docs/spice86-live-findings.md and NOT the overworld range of 0..63.
#           A retry loop at runtime 0x16EA rejects values that collide with
#           [DI+0x1B7]. Then type and HP are DERIVED rather than stored:
#             clc; rcr $1,%al; adc $0x1,%al   -> [DI+0x197]  type
#             add %al,%al; adc 0x1B7(%di),%al; add %al,%al x3; or $0x10,%al
#                                          -> [DI+0x177]  HP
#           VALIDATED 2026-10-23 against 54/54 monster slots in the two dungeon
#           captures on disk (tools/dungeon_spawn_check.py). The formula is
#             [DI+0x1B7] = slot >> 1                -- a slot number, NOT a creature kind
#             type       = (rand & 7) + 1, 7 rejected, accepted iff 2*(type-1) <= slot>>1
#             HP         = ((2*type + (slot>>1)) * 8) | 0x10
#           So the `or $0x10` is part of the value, not a flag: the envelope it produces
#           reproduces the third-party dungeon HP table exactly, one lower, on both the
#           min ladder (16,48,...,208 vs 17,49,...,209) and the paired max ladder
#           (152,152,184,184,216,216,248 vs 153,153,185,185,217,217,249).
#           The guard makes the SLOT the difficulty ladder: type <= (slot>>1)/2 + 1, so
#           slot 4 admits types 1-2 and slot 31 admits all seven. Detail and grades in
#           docs/monster-spawn.md § 9.
#           NOTE the earlier header line here said [DI+0x1B7] was a sprite and then a
#           kind index; in a dungeon it is neither, it is slot>>1. The carry out of the
#           `adc 0x1B7(%di),%al` was 0 in all 54 observations, so its contribution is
#           untested rather than proven absent.
#           The stamp store at L_00171b reads `mov 0x8,%bx` and indexes (%bx,%si). DS:08/09
#           is not a fixed base: it is the cell pointer left by asm/1451_site_cell_open_on_floor,
#           which computes `AX = word[DS:472] + AL*256` then `offset = DS:24*16 + DS:23` and
#           stores the result back into DS:08/09. asm/144C_dungeon_cell_gate_tail is the line
#           that supplies AL -- `mov 0x1b7(%di),%al`, the CREATURE's floor byte, not the
#           player's DS:25. So the cell transform is
#             byte = word[DS:472] + (slot>>1)*256 + y*16 + x      (word[DS:472] = 0x1800)
#           and it is the same page arithmetic as asm/68C0_dungeon_grid_byte. Measured 27/27
#           against captures/spice86/dungeon-35-live on 2026-11-23: every monster's stamp lands
#           at that byte and carries that type. The earlier "no linear map of the tested shapes
#           reproduces the cell" was true of the shapes tested -- none of them used the
#           monster's own floor; they all used DS:25 or no page term at all.
# IMPORTANT: this is NOT the townsfolk path. Townsfolk have no HP assignment anywhere
#           in the monster-kind dispatch — see docs/bestiary.md § 6.
# provenance: static, 2026-10-22; formula validated live 2026-10-23 against
#           captures/spice86/dungeon-35-live and captures/spice86/airport32 by
#           tools/dungeon_spawn_check.py (54/54 slots)
# confidence: high for the formula and for [DI+0x1B7]=slot>>1 (bytes + 54/54 live);
#           high for the cell-address computation (asm/144C + asm/1451, and 27/27 measured)
# label: kept as sub_147f on purpose — labels are body (CP0)
# func 147F..152D  len 00AF B  evidence: call+reko
# calls: L_00164c, L_001694, L_005417
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_147f:
L_00167f:
        nop
L_001680:
        mov    $0x0,%bh
L_001682:
        mov    $0x1f,%bl
L_001684:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_001686:
        .byte 0x8b,0xdf  # forced-kept: gas re-encodes 'mov    %di,%bx' differently
L_001688:
        mov    %bl,0x1f
L_00168c:
        mov    0x197(%di),%al
L_001690:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_001692:
        je L_0016a0
L_001694:
        mov    $0x0,%bh
L_001696:
        mov    0x1f,%bl
L_00169a:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_00169c:
        dec    %di
L_00169d:
        jne L_001686
L_00169f:
        ret
L_0016a0:
        nop
L_0016a1:
        call L_005417
L_0016a4:
        mov    $0x0,%bh
L_0016a6:
        mov    0x1f,%bl
L_0016aa:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0016ac:
        and    $0x7,%al
L_0016ae:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_0016b0:
        or     $0x1,%al
L_0016b2:
        mov    %al,0x137(%di)
L_0016b6:
        mov    %al,0x23
L_0016b9:
        call L_005417
L_0016bc:
        mov    $0x0,%bh
L_0016be:
        mov    0x1f,%bl
L_0016c2:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0016c4:
        and    $0x7,%al
L_0016c6:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_0016c8:
        or     $0x1,%al
L_0016ca:
        mov    %al,0x157(%di)
L_0016ce:
        mov    %al,0x24
L_0016d1:
        .byte 0x8b,0xc7  # forced-kept: gas re-encodes 'mov    %di,%ax' differently
L_0016d3:
        clc
L_0016d4:
        rcr    $1,%al
L_0016d6:
        mov    %al,0x1b7(%di)
L_0016da:
        call L_00164c
L_0016dd:
        jne L_001694
L_0016df:
        call L_005417
L_0016e2:
        mov    $0x0,%bh
L_0016e4:
        mov    0x1f,%bl
L_0016e8:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0016ea:
        and    $0x7,%al
L_0016ec:
        cmp    $0x7,%al
L_0016ee:
        je L_0016fa
L_0016f0:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_0016f2:
        cmp    0x1b7(%di),%al
L_0016f6:
        je L_0016fc
L_0016f8:
        jb L_0016fc
L_0016fa:
        jmp L_001694
L_0016fc:
        nop
L_0016fd:
        clc
L_0016fe:
        rcr    $1,%al
L_001700:
        adc    $0x1,%al
L_001702:
        mov    %al,0x197(%di)
L_001706:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_001708:
        adc    0x1b7(%di),%al
L_00170c:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_00170e:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_001710:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_001712:
        or     $0x10,%al
L_001714:
        mov    %al,0x177(%di)
L_001718:
        call L_00164c
L_00171b:
        mov    0x8,%bx
L_00171f:
        mov    (%bx,%si),%al
L_001721:
        or     0x197(%di),%al
L_001725:
        mov    0x8,%bx
L_001729:
        mov    %al,(%bx,%si)
L_00172b:
        jmp L_001694
