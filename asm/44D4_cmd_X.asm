# cmd_X — command handler 'X' (X-IT / destroy item): "X-IT", " WHAT?",
#           "-NOT HERE!" paths.
# func 44D4..4541  len 006E B  evidence: seed+reko (jump table cs:0730 entry X)
# provenance: live:airport32 (+2 other harvests), static
#           entry executed in 4 of 263 harvests; derived by tools/provenance_from_traces.py
# confidence: medium (flow + messages proven by bytes; NPC/quest semantics inferred)
# notes: re-decoded with tools/decode_str.py (str_out-aware); gas re-encode diffs
#        kept as .byte (disp8/disp16 and reg-op form choices)
# ---------------------------------------------------------------------------
# THIS IS THE DISMOUNT. The vehicle handoff's § 6 asked how the travel mode ever gets
#   cleared and found no evidence; it is here, and it is a letter the project already
#   treats as proven-executing. Read against asm/2FAD (which BOARDs), the pair is exactly
#   symmetric:
#
#     'X-IT' ; if [DS:13] >= 0x78 -> ' WHAT?' (on foot there is nothing to destroy)
#     look at the tile under the player: [DS:14] == 0x04 (grass) -> allowed
#                              [DS:14] == 0x00 (water) AND [DS:13] == 0x24 -> allowed
#                              anything else -> '-NOT HERE!'
#     al = [DS:13] ; add al,al ; write it through the cell pointer DS:06
#     [DS:13] = 2*[DS:47] + 0x78
#
#   Doubling the mode byte is the inverse of BOARD's shift right: 0x22->0x44 horse,
#   0x24->0x48 ship, 0x26->0x4C plane, 0x28->0x50 rocket. So X-IT puts the vehicle tile
#   BACK under the player and returns them to foot, which is the other half of the
#   'boarding consumes the tile' rule in asm/2FAD — the tile is not gone, it is carried.
#   The water clause is the ship's own case: you may only drop the ship where you are
#   standing on water, which is what the ship arm left behind when you boarded.
#   Grade: bytes for every step above. Not observed: no harvest has an X-IT on a vehicle
#   (the three harvests that ran cmd_X were the cmd-letters sweeps, which press every
#   letter on a foot character in a grass sector). WIRED 2026-10-29: the engine implements
#   it as cmd_X in engine/src/game/commands.c. tests/vehicle_dismount.py (CP5 V4) covers the
#   reachable arms end to end; site_check.c covers the "-NOT HERE!" arm and the class-
#   dependent foot stamp, neither of which a scripted foot start can reach.
#   NB the last line is the same expression as asm/080D's: the on-foot stamp is
#   class-dependent, 0x78 + 2*pclass, not the constant 0x78 every capture happens to show.
cmd_X:
cmd_X:
L_0046d4:
        call L_0051df
        db "X-IT",0x00        # string
L_0046dc:
        mov    0x13,%al
L_0046df:
        cmp    $0x78,%al
L_0046e1:
        jb L_0046f0
L_0046e3:
        call L_0051df
        db " WHAT?",0x00        # string
L_0046ed:
        jmp L_000c30
L_0046f0:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_0046f1:
        .byte 0xa0,0x00,0x00  # forced-kept: gas re-encodes 'mov    0x0,%al' differently
L_0046f4:
        .byte 0xa2,0x23,0x00  # forced-kept: gas re-encodes 'mov    %al,0x23' differently
L_0046f7:
        mov    0x1,%al
L_0046fa:
        mov    %al,0x24
L_0046fd:
        call L_0052b3
L_004700:
        mov    0x14,%al
L_004703:
        cmp    $0x4,%al
L_004705:
        je L_004723
L_004707:
        cmp    $0x0,%al
L_004709:
        jne L_004712
L_00470b:
        mov    0x13,%al
L_00470e:
        cmp    $0x24,%al
L_004710:
        je L_004723
L_004712:
        call L_0051df
        db "-NOT HERE!",0x00        # string
L_004720:
        jmp L_000c30
L_004723:
        mov    0x13,%al
L_004726:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_004728:
        mov    $0x0,%bh
L_00472a:
        mov    $0x0,%bl
L_00472c:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_00472e:
        mov    0x6,%bx
L_004732:
        mov    %al,(%bx,%si)
L_004734:
        mov    0x47,%al
L_004737:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_004739:
        clc
L_00473a:
        adc    $0x78,%al
L_00473c:
        mov    %al,0x13
L_00473f:
        jmp L_000c33
