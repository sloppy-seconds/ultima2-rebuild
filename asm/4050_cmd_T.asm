# cmd_T — TRANSACT: aim a ray from the player at a vendor and see whether it will deal
#           RAY EXTENT CORRECTED 2026-10-29: this line said "one cell from the player" and that
#           is wrong. A live purchase in captures/spice86/shop-window-transport had the player at
#           (48,53) and the vendor standing on the 0x68 marker at (48,51), with the shop's sign
#           row (the "TRANSPORT" lettering, y=52) between them. The deal completed. So the ray
#           crosses the sign band and reaches at least two cells; how far it actually goes, and
#           whether the sign row is transparent to it by design or simply not a stop condition,
#           is NOT established. Do not write cmd_T to a one-cell test.
#           RAY EXTENT RESOLVED, later the same day, from the bytes rather than from another
#           observation. The span is one routine and it is exactly two steps:
#             405D  call L_002562   -- the aim gate asm/2362, see the note below
#             4060  call L_0025e6   -- cursor = player + delta, then the slot scan
#             406E  reject if [DS:4A] == 0
#             4075  if the scan left DI != 0 -> 40C0, the CREATURE path (talk to whoever is
#                   standing on the cursor cell: role bit 7 -> tlkx, else the street chain)
#             407C  map_tile_ptr(cursor) ; 407F `js 4085` skips the next test when the tile
#                   has its high bit set -- which is what a shop sign letter is
#             4081  reject unless the cell == 0x7C
#             4085  cursor += delta ; map_tile_ptr again
#             409F  reject unless that cell == 0x68
#             40A3  jmp 1590        -- SUCCESS: the vendor dispatcher
#           So: the near cell must be a sign letter (high bit) or 0x7C, and the far cell must
#           be the 0x68 marker. The sign row is transparent BY DESIGN — it is the accepted
#           near cell, not an obstacle the ray happens to ignore. Both live purchases sit on
#           this geometry: sector 32 (38,5) sign (38,4) marker (38,3), and sector 22 (48,53)
#           sign (48,52) marker (48,51). The grocery was reproduced from (55,21) with 0x7C at
#           (55,20) and the marker at (55,19), which is the cell the 2026-10-23 sweep stood
#           beside and missed.
#           This also retires a wrong blocker note. On 2026-10-29 an earlier pass concluded that no
#           0x68 marker had a walkable cell adjacent to it, from a scan that compared raw mapx tile
#           bytes against the stamped grid at DS:1800 as if they were one representation (they are
#           not: the same ground is 0x0c in mapx21 and 0x70 in the grid). The geometry was never
#           the obstacle; adjacency was the wrong expectation.
# name-evidence: prints 'TRANSACT-' then the direction; calls the cursor initialiser at
#           runtime 23E6; rejects via the shared printer asm/40A6 ('FUNNY, NO RESPONSE!');
#           on success tail-jumps to runtime 1590, the vendor dispatcher. Corroboration:
#           1590/1591/1594/1596 all appear in ExecutedInstructions of
#           captures/spice86/food-buy, and the shop opened live on 2026-10-23
#           (captures/spice86/armour-buy).
# func 4050..40A5  len 0056 B  evidence: seed+call+reko
# strings: "TRANSACT-"
# calls: L_0051df (str_out), L_0025e6 (cursor init + slot scan seed), L_0055f4 (emit),
#        L_0052b3 (map_tile_ptr) x2
# provenance: live:armour-buy, food-buy; static
#           entry executed in 38 of 282 harvests; derived from ExecutedInstructions. This line
#           previously carried the live citations but no executed count, and tools/cmd_handlers.py
#           parses only the `entry executed in N of M` form -- so TRANSACT was reported as never
#           exercised while 24 harvests already contained it. The crosswalk's `--` was a format
#           gap, not an absence of evidence. Confirmed again by the capture of 2026-10-25, whose
#           CMD log reads `TRANSACT--PASS` (the turn expired before a direction arrived).
# confidence: high for the control flow (linear disassembly of the whole span agrees with
#           the per-fragment bytes); see DOUBT for what the two paths actually match on.
#
# MERGED 2026-10-23 from 13 fragments (4050, 405D, 405E, 4060, 4063, 406E, 4075, 407C,
# 407F, 4081, 4085, 409F, 40A3) under the owner's split ruling, on the evidence of
# tools/fragment_splits.py plus a linear disassembly of the whole span. Body lines are
# unchanged and in address order; engine/tests/asm_body_guard.py proves it.
#
# HOW IT WORKS (observed)
#   4050  str_out "TRANSACT-"
#   4060  call 23E6        -- the seed. asm/23E6 does:
#                              DS:23 = DS:00 + DS:19      cursor = player_x + delta_x
#                              DS:24 = DS:01 + DS:1A      cursor = player_y + delta_y
#                              DI = 31                    top creature slot
#                            so DS:19/1A IS the direction delta, the cursor is one delta
#                            step from the player, and DI starts a downward scan of the
#                            32 creature slots.
#   4063  [DS:20] = BL ; emit 0x8D
#   406E  reject if site kind [DS:4A] == 0
#   4075  if BL != 0 -> 40C0   CREATURE path: checks [DI+0x1D7] (flag array) and
#                              [DI+0x197] (type array, compared against 0x6C)
#         if BL == 0 -> fall through  MAP-TILE path, i.e. the scan exhausted its slots
#   407C  map_tile_ptr(cursor) ; js 4085 skips the next test when the tile has its high
#         bit set
#   4081  reject unless the cell == 0x7C
#   4085  cursor += delta ; map_tile_ptr(cursor) again
#   409F  reject unless that cell == 0x68
#   40A3  jmp 1590        SUCCESS -- the vendor dispatcher
#
# DOUBT -- read this before citing it
#   * The two paths match on DIFFERENT namespaces and that is the trap here. 0x68 and
#     0x7C are **map grid cell values**; 0x6C is a **creature type byte** read from
#     [DI+0x197]. An earlier pass on 2026-10-23 wrote "0x68 is the Merchant type" and was
#     wrong -- it conflated the two. What is measured about 0x68 is a strong coincidence
#     and still not an identity: in mapx31 the byte 0x68 occurs at exactly the seven
#     monx31 Merchant spawn cells (7/7), which says the map marks where a transactor
#     stands, not that 0x68 means Merchant in general. Do not generalise it to other
#     sectors without checking.
#   * Which path a live talk takes is now established, and the DOUBT above about the scan
#     loop is closed: asm/23E6_find_creature_at_cursor scans slots 31 down to 1 for a
#     creature standing on the cursor cell and returns with DI = that slot, or DI = 0 when
#     the scan is exhausted. asm/4075's `cmp bl,0 ; jne 40C0` is therefore live in both
#     directions: a creature on the cursor takes the talk path, an empty cursor falls through
#     to the map-tile path. Slot 0 is never examined (`dec %di ; jne` exits on zero) and
#     monx31 slot 0 is a populated Guard — real gap or intentional reserve is still not
#     established.
#   * RESOLVED, what writes the delta: asm/405D calls asm/2362_aim_state_clear, the same aim
#     gate cmd_A, cmd_S, cmd_U and cmd_O use. It zeroes DS:1F/20/19/1A, polls for a key, and
#     on a direction key prints the direction word and does `decb/incb` on DS:19 (x) or
#     DS:1A (y) — NORTH is `fe 0e 1a 00`, SOUTH `fe 06 1a 00`, EAST `fe 06 19 00`, WEST
#     `fe 0e 19 00`, all read out of the file at 0x25B0..0x25E5. On timeout it prints PASS and
#     leaves via asm/0A30, which is the `CMD: TRANSACT-PASS` line. The reason this was missed
#     is that asm/405D stores the call as `.byte 0xe8 0x02 0xe3` — a forced-keep — so the
#     `# calls:` metadata never carried the edge and asm/2362's own caller list concluded from
#     that silence that cmd_T was not in the family. It is. The samples that all read (1,1)
#     were taken at teardown, after the creature-wander writers had overwritten the pair.
#   * The delta DS:19/1A read (1,1) in every live sample taken so far -- after T,Up,
#     T,Down, T,Left, T,Right AND after five movement keys that demonstrably moved the
#     player. So whatever writes the delta from a direction is none of what has been
#     sampled, and the ray was aiming diagonally down-right in every one of those tests.
#     Candidate writers not yet examined: asm/0B0F, asm/1086, asm/10AA, asm/10BF,
#     asm/152E. This is the open item, and it is static.
#   * Four exits, all tail jumps (1590, 0A33, 4209, 4249) and no `ret`. That is normal for
#     this game's command handlers -- see the same finding for 19D7's handlers in
#     docs/g22-split-proposal.md -- but it means the routine never returns to its caller,
#     so anything that expected a return value is not getting one.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
cmd_T:
        call L_0051df          # str_out — prints the inline string below
        db "TRANSACT-",0       # string
sub_405d:
L_00425d:
        .byte 0xe8  # forced-kept: stop-trunc call   0x2562
sub_405e:
L_00425e:
        .byte 0x02,0xe3  # forced-kept: gas re-encodes 'add    %bl,%ah' differently
sub_4060:
L_004260:
        call L_0025e6
sub_4063:
L_004263:
        .byte 0x8b,0xdf  # forced-kept: gas re-encodes 'mov    %di,%bx' differently
L_004265:
        mov    %bl,0x20
L_004269:
        mov    $0x8d,%al
L_00426b:
        call L_0055f4
sub_406e:
L_00426e:
        mov    0x4a,%al
L_004271:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_004273:
        je L_0042a6
sub_4075:
L_004275:
        .byte 0x8b,0xdf  # forced-kept: gas re-encodes 'mov    %di,%bx' differently
L_004277:
        cmp    $0x0,%bl
L_00427a:
        jne L_0042c0
sub_407c:
L_00427c:
        call L_0052b3
sub_407f:
L_00427f:
        js L_004285
sub_4081:
L_004281:
        cmp    $0x7c,%al
L_004283:
        jne L_0042a6
sub_4085:
L_004285:
        nop
L_004286:
        clc
L_004287:
        mov    0x23,%al
L_00428a:
        adc    0x19,%al
L_00428e:
        mov    %al,0x23
L_004291:
        clc
L_004292:
        mov    0x24,%al
L_004295:
        adc    0x1a,%al
L_004299:
        mov    %al,0x24
L_00429c:
        call L_0052b3
sub_409f:
L_00429f:
        cmp    $0x68,%al
L_0042a1:
        jne L_0042a6
sub_40a3:
L_0042a3:
        jmp L_001790
