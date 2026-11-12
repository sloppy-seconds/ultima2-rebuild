# dungeon_move_commands — the four first-person movement commands, and the trap
# name-evidence: the five db strings inside it are 'ADVANCE', '-BLOCKED!', 'TURN LEFT',
#           'TURN RIGHT' and 'RETREAT', and each is preceded by a `call L_0051df` and
#           followed by the facing arithmetic below it. Renamed 2026-10-29 when CP7 read
#           the region as code rather than as the mis-decoded data the bulk wave saw.
#
# THE SHAPE, per command (runtime addresses are this file's own, +0x200 for file offsets):
#   ADVANCE  0x286F  al=[DS:15]; if (al & 0x0f) -> '-BLOCKED!'; if (al == 0x80) ->
#                    '-BLOCKED!'; [DS:00] = ([DS:00]+[DS:02]) & 0x3f; same for [DS:01]/
#                    [DS:03]; then one rand against [DS:25] and on `jb` the TRAP.
#   TRAP     0x28B1  [DS:26] = 0 (the torch goes out FIRST), 'ARGH! A TRAP!', then
#                    [DS:66] == 0 -> jmp L_000e62 (the DEATH handler, see below)
#                    else 'ESCAPED! BY USE OF TOOLS!' and one packed-decimal tool spent.
#                    CORRECTED 2026-11-13: L_000e62 is sub_0c62 (runtime 0x0e62 = file
#                    0x0c62), the death handler that zeroes DS:51..59 and prints ' IS DEAD!'
#                    (screen-proven: captures/spice86/cp5-weak-fight/death1.png). There is NO
#                    damage amount and no partial hit: a trap with zero tools is INSTANT DEATH.
#                    The earlier "damage tail, amount undecoded" reading was wrong — the jump
#                    target is the same one the food borrow-outs and the force field use.
#   TURN LEFT  0x2B13  if ([DS:03]) { [DS:02]=[DS:03]; [DS:03]=0 } else { [DS:03]=-[DS:02];
#                    [DS:02]=0 }  — a quarter turn counter-clockwise on a SIGNED pair.
#   TURN RIGHT 0x2B45  the mirror: if ([DS:02]) { [DS:03]=[DS:02]; [DS:02]=0 } else
#                    { [DS:02]=-[DS:03]; [DS:03]=0 }.
#   RETREAT  0x2B78  al=[DS:16]; if (al & 0x8f) -> '-BLOCKED!'; then step backwards.
#
# Two things this settles that prose could not. First, the facing is [DS:02]/[DS:03] and
# it is signed: the turns negate it, so an unsigned reader sees every westward facing as
# 0xFF. Second, ADVANCE and RETREAT gate on DIFFERENT masks — ADVANCE refuses a stamp or
# exactly 0x80, so a chest cell (0x40) is walkable and you GET it after stepping onto it,
# while RETREAT refuses anything with bit 7 or the low nibble set.
#
# The trap roll compares against the FLOOR byte, so floor 0 has no traps at all and the
# deeper the dungeon the more often one fires. That is the rule the bytes give; the rate
# has never been measured. The no-tools arm is NOT a damage roll (see the TRAP block above):
# it jumps straight to the death handler, so the only two outcomes are spend-a-tool or die.
#        One fact inside it is nonetheless worth carrying, because it is the only place the
#           game spends TOOLS: at runtime 0x28DC it prints 'ARGH! A TRAP!', reads DS:66, prints
#           'ESCAPED! BY USE OF TOOLS!' at 0x28E9, and at 0x2904..0x290C does
#           `stc ; cmc ; sbb $1,%al ; das ; cmc ; mov %al,0x66` — one packed-decimal tool spent
#           per trap. The counter is DS:66, which is NOT an items_held slot; it sits beside
#           torches (DS:64) and keys (DS:65), and the item name table at file 0x501D has no TOOLS
#           entry at all, which is how a scan scoped to the item array concluded the game had no
#           such thing. Owner recollection supplied the mechanic; the bytes confirm it. Static
#           grade: dungeons are parked (AGENTS.md § 3) and no capture has filmed this. The
#           increment side is asm/2CE5, the kill-drop dispatch: its thief arm adds 1 to
#           DS:66 on a 25% roll. That side is now LIVE evidence, not static — the increment
#           is reached by fall-through in captures/spice86/baseline-fight. See
#           docs/save-format.md § 5.
# func 277B..29D1  len 0257 B  evidence: seed+reko
# strings: "CMD: "; "CMD: "; "PASS"; "ADVANCE" (+7 more)
# calls: L_000911, L_000bfb, L_000c30, L_000c33, L_000e62, L_002a61, L_002b12, L_002b44 (+8 more)
# provenance: static, read in full 2026-10-29 for CP7 from the raw bytes of this span; the
#           ADVANCE/TURN behaviour is corroborated live by captures/spice86/tower-34 (arrows
#           print TURN LEFT / TURN RIGHT) and captures/spice86/dungeon-35-live (A prints
#           ADVANCE). The trap firing was observed 2026-11-13 (captures/spice86/g51-dungeon-A-attack:
#           'ARGH! A TRAP!' and DS:26 -> 0; the seed carried tools so it took the ESCAPED arm).
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_277b:
L_00297b:
        .byte 0xa2,0x70,0x00  # forced-kept: gas re-encodes 'mov    %al,0x70' differently
L_00297e:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_002980:
        .byte 0xb3,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bl' differently
L_002982:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_002984:
        .byte 0xb0,0xc0  # forced-kept: gas re-encodes 'mov    $0xc0,%al' differently
L_002986:
        .byte 0x8b,0x1e,0x06,0x00  # forced-kept: gas re-encodes 'mov    0x6,%bx' differently
L_00298a:
        .byte 0x88,0x00  # forced-kept: gas re-encodes 'mov    %al,(%bx,%si)' differently
L_00298c:
        .byte 0xa0,0x23,0x00  # forced-kept: gas re-encodes 'mov    0x23,%al' differently
L_00298f:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_002991:
        .byte 0x8a,0x1e,0x24,0x00  # forced-kept: gas re-encodes 'mov    0x24,%bl' differently
L_002995:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002997:
        .byte 0xc3  # forced-kept: gas re-encodes 'ret' differently
L_002998:
        .byte 0x1d,0x38,0x1f  # forced-kept: gas re-encodes 'sbb    $0x1f38,%ax' differently
L_00299b:
        .byte 0x38,0x21  # forced-kept: gas re-encodes 'cmp    %ah,(%bx,%di)' differently
L_00299d:
        .byte 0x38,0x23  # forced-kept: gas re-encodes 'cmp    %ah,(%bp,%di)' differently
L_00299f:
        .byte 0x38,0x22  # forced-kept: gas re-encodes 'cmp    %ah,(%bp,%si)' differently
L_0029a1:
        .byte 0x08,0x2f  # forced-kept: gas re-encodes 'or     %ch,(%bx)' differently
L_0029a3:
        .byte 0x1c,0x24  # forced-kept: gas re-encodes 'sbb    $0x24,%al' differently
L_0029a5:
        .byte 0x38,0x14  # forced-kept: gas re-encodes 'cmp    %dl,(%si)' differently
L_0029a7:
        .byte 0x25,0x10,0x10  # forced-kept: gas re-encodes 'and    $0x1010,%ax' differently
L_0029aa:
        .byte 0x30,0x18  # forced-kept: gas re-encodes 'xor    %bl,(%bx,%si)' differently
L_0029ac:
        .byte 0x2a,0x18  # forced-kept: gas re-encodes 'sub    (%bx,%si),%bl' differently
L_0029ae:
        .byte 0x13,0x34  # forced-kept: gas re-encodes 'adc    (%si),%si' differently
L_0029b0:
        .byte 0x38,0x34  # forced-kept: gas re-encodes 'cmp    %dh,(%si)' differently
L_0029b2:
        .byte 0x1c,0x0c  # forced-kept: gas re-encodes 'sbb    $0xc,%al' differently
L_0029b4:
        .byte 0x21,0x17  # forced-kept: gas re-encodes 'and    %dx,(%bx)' differently
L_0029b6:
        .byte 0x14,0x34  # forced-kept: gas re-encodes 'adc    $0x34,%al' differently
L_0029b8:
        .byte 0x12,0x17  # forced-kept: gas re-encodes 'adc    (%bx),%dl' differently
L_0029ba:
        .byte 0x32,0x22  # forced-kept: gas re-encodes 'xor    (%bp,%si),%ah' differently
L_0029bc:
        .byte 0x34,0x18  # forced-kept: gas re-encodes 'xor    $0x18,%al' differently
L_0029be:
        .byte 0x08,0x0b  # forced-kept: gas re-encodes 'or     %cl,(%bp,%di)' differently
L_0029c0:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_0029c1:
        .byte 0xa0,0x6d,0x02  # forced-kept: gas re-encodes 'mov    0x26d,%al' differently
L_0029c4:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_0029c6:
        je L_0029f1
L_0029c8:
        call L_0051df
        db "CMD: ",0x00        # string
L_0029d1:
        .byte 0xb0,0x14  # forced-kept: gas re-encodes 'mov    $0x14,%al' differently
L_0029d3:
        .byte 0xa2,0x27,0x00  # forced-kept: gas re-encodes 'mov    %al,0x27' differently
L_0029d6:
        call L_0051df
        db "Z",0x00        # string
L_0029db:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_0029dd:
        .byte 0xb3,0xff  # forced-kept: gas re-encodes 'mov    $0xff,%bl' differently
L_0029df:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0029e1:
        .byte 0x4f  # forced-kept: gas re-encodes 'dec    %di' differently
L_0029e2:
        jne L_0029e1
L_0029e4:
        decb   0x27
L_0029e8:
        jne L_0029d6
L_0029ea:
        decb   0x26d
L_0029ee:
        jmp L_000c33
L_0029f1:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_0029f2:
        call L_0051df
        db "CMD: ",0x00        # string
L_0029fb:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_0029fd:
        .byte 0xb3,0xff  # forced-kept: gas re-encodes 'mov    $0xff,%bl' differently
L_0029ff:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002a01:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_002a03:
        .byte 0xb3,0x02  # forced-kept: gas re-encodes 'mov    $0x2,%bl' differently
L_002a05:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_002a07:
        .byte 0xbb,0x00,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bx' differently
L_002a0a:
        call L_000911
L_002a0d:
        call L_00514d
L_002a10:
        .byte 0x80,0xfc,0xff  # forced-kept: gas re-encodes 'cmp    $0xff,%ah' differently
L_002a13:
        je L_002a32
L_002a15:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_002a16:
        .byte 0x58  # forced-kept: gas re-encodes 'pop    %ax' differently
L_002a17:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_002a18:
        .byte 0x58  # forced-kept: gas re-encodes 'pop    %ax' differently
L_002a19:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_002a1a:
        .byte 0x58  # forced-kept: gas re-encodes 'pop    %ax' differently
L_002a1b:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_002a1c:
        .byte 0x58  # forced-kept: gas re-encodes 'pop    %ax' differently
L_002a1d:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_002a1e:
        .byte 0x58  # forced-kept: gas re-encodes 'pop    %ax' differently
L_002a1f:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_002a20:
        .byte 0x58  # forced-kept: gas re-encodes 'pop    %ax' differently
L_002a21:
        .byte 0x4f  # forced-kept: gas re-encodes 'dec    %di' differently
L_002a22:
        jne L_002a07
L_002a24:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_002a25:
        jne L_002a07
L_002a27:
        call L_0051df
        db "PASS",0x00        # string
L_002a2f:
        jmp L_000c33
L_002a32:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002a33:
        .byte 0x3a,0x06,0x6e,0x02  # forced-kept: gas re-encodes 'cmp    0x26e,%al' differently
L_002a37:
        jne L_002a3c
L_002a39:
        jmp L_002a61
L_002a3b:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002a3c:
        .byte 0x3a,0x06,0x71,0x02  # forced-kept: gas re-encodes 'cmp    0x271,%al' differently
L_002a40:
        jne L_002a45
L_002a42:
        jmp L_002b12
L_002a45:
        .byte 0x3a,0x06,0x70,0x02  # forced-kept: gas re-encodes 'cmp    0x270,%al' differently
L_002a49:
        jne L_002a4e
L_002a4b:
        jmp L_002b44
L_002a4e:
        .byte 0x3a,0x06,0x6f,0x02  # forced-kept: gas re-encodes 'cmp    0x26f,%al' differently
L_002a52:
        jne L_002a57
L_002a54:
        jmp L_002b77
L_002a57:
        .byte 0x3c,0x20  # forced-kept: gas re-encodes 'cmp    $0x20,%al' differently
L_002a59:
        jne L_002a5e
L_002a5b:
        jmp L_002bbc
L_002a5e:
        jmp L_000bfb
L_002a61:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002a62:
        call L_0051df
        db "ADVANCE",0x00        # string
L_002a6d:
        .byte 0xa0,0x15,0x00  # forced-kept: gas re-encodes 'mov    0x15,%al' differently
L_002a70:
        .byte 0x24,0x0f  # forced-kept: gas re-encodes 'and    $0xf,%al' differently
L_002a72:
        je L_002a84
L_002a74:
        call L_0051df
        db "-BLOCKED!",0x00        # string
L_002a81:
        jmp L_000c30
L_002a84:
        .byte 0xa0,0x15,0x00  # forced-kept: gas re-encodes 'mov    0x15,%al' differently
L_002a87:
        .byte 0x3c,0x80  # forced-kept: gas re-encodes 'cmp    $0x80,%al' differently
L_002a89:
        je L_002a74
L_002a8b:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_002a8c:
        .byte 0xa0,0x00,0x00  # forced-kept: gas re-encodes 'mov    0x0,%al' differently
L_002a8f:
        .byte 0x12,0x06,0x02,0x00  # forced-kept: gas re-encodes 'adc    0x2,%al' differently
L_002a93:
        .byte 0x24,0x3f  # forced-kept: gas re-encodes 'and    $0x3f,%al' differently
L_002a95:
        .byte 0xa2,0x00,0x00  # forced-kept: gas re-encodes 'mov    %al,0x0' differently
L_002a98:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_002a99:
        .byte 0xa0,0x01,0x00  # forced-kept: gas re-encodes 'mov    0x1,%al' differently
L_002a9c:
        .byte 0x12,0x06,0x03,0x00  # forced-kept: gas re-encodes 'adc    0x3,%al' differently
L_002aa0:
        .byte 0x24,0x3f  # forced-kept: gas re-encodes 'and    $0x3f,%al' differently
L_002aa2:
        .byte 0xa2,0x01,0x00  # forced-kept: gas re-encodes 'mov    %al,0x1' differently
L_002aa5:
        call L_005417
L_002aa8:
        .byte 0x3a,0x06,0x25,0x00  # forced-kept: gas re-encodes 'cmp    0x25,%al' differently
L_002aac:
        jb L_002ab1
L_002aae:
        jmp L_000c33
L_002ab1:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002ab2:
        call L_004d4e
L_002ab5:
        .byte 0xb0,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%al' differently
L_002ab7:
        .byte 0xa2,0x26,0x00  # forced-kept: gas re-encodes 'mov    %al,0x26' differently
L_002aba:
        call L_0051df
        db 0x8d,"ARGH! A TRAP!",0x8d,0x00        # string
L_002acd:
        call L_00603b
L_002ad0:
        call L_0060f2
L_002ad3:
        call L_0060f2
L_002ad6:
        call L_0060f2
L_002ad9:
        call L_0060f2
L_002adc:
        .byte 0xa0,0x66,0x00  # forced-kept: gas re-encodes 'mov    0x66,%al' differently
L_002adf:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_002ae1:
        jne L_002ae6
L_002ae3:
        jmp L_000e62
L_002ae6:
        call L_0051df
        db "ESCAPED! BY USE OF TOOLS!",0x00        # string
L_002b03:
        .byte 0xf9  # forced-kept: gas re-encodes 'stc' differently
L_002b04:
        .byte 0xa0,0x66,0x00  # forced-kept: gas re-encodes 'mov    0x66,%al' differently
L_002b07:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002b08:
        .byte 0x1c,0x01  # forced-kept: gas re-encodes 'sbb    $0x1,%al' differently
L_002b0a:
        .byte 0x2f  # forced-kept: gas re-encodes 'das' differently
L_002b0b:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002b0c:
        .byte 0xa2,0x66,0x00  # forced-kept: gas re-encodes 'mov    %al,0x66' differently
L_002b0f:
        jmp L_000c33
L_002b12:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002b13:
        call L_0051df
        db "TURN LEFT",0x00        # string
L_002b20:
        .byte 0xa0,0x03,0x00  # forced-kept: gas re-encodes 'mov    0x3,%al' differently
L_002b23:
        .byte 0x3c,0x00  # forced-kept: gas re-encodes 'cmp    $0x0,%al' differently
L_002b25:
        je L_002b32
L_002b27:
        .byte 0xa2,0x02,0x00  # forced-kept: gas re-encodes 'mov    %al,0x2' differently
L_002b2a:
        .byte 0xb0,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%al' differently
L_002b2c:
        .byte 0xa2,0x03,0x00  # forced-kept: gas re-encodes 'mov    %al,0x3' differently
L_002b2f:
        jmp L_000c33
L_002b32:
        .byte 0xf9  # forced-kept: gas re-encodes 'stc' differently
L_002b33:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002b34:
        .byte 0x1a,0x06,0x02,0x00  # forced-kept: gas re-encodes 'sbb    0x2,%al' differently
L_002b38:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002b39:
        .byte 0xa2,0x03,0x00  # forced-kept: gas re-encodes 'mov    %al,0x3' differently
L_002b3c:
        .byte 0xb0,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%al' differently
L_002b3e:
        .byte 0xa2,0x02,0x00  # forced-kept: gas re-encodes 'mov    %al,0x2' differently
L_002b41:
        jmp L_000c33
L_002b44:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002b45:
        call L_0051df
        db "TURN RIGHT",0x00        # string
L_002b53:
        .byte 0xa0,0x02,0x00  # forced-kept: gas re-encodes 'mov    0x2,%al' differently
L_002b56:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_002b58:
        je L_002b65
L_002b5a:
        .byte 0xa2,0x03,0x00  # forced-kept: gas re-encodes 'mov    %al,0x3' differently
L_002b5d:
        .byte 0xb0,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%al' differently
L_002b5f:
        .byte 0xa2,0x02,0x00  # forced-kept: gas re-encodes 'mov    %al,0x2' differently
L_002b62:
        jmp L_000c33
L_002b65:
        .byte 0xf9  # forced-kept: gas re-encodes 'stc' differently
L_002b66:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002b67:
        .byte 0x1a,0x06,0x03,0x00  # forced-kept: gas re-encodes 'sbb    0x3,%al' differently
L_002b6b:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002b6c:
        .byte 0xa2,0x02,0x00  # forced-kept: gas re-encodes 'mov    %al,0x2' differently
L_002b6f:
        .byte 0xb0,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%al' differently
L_002b71:
        .byte 0xa2,0x03,0x00  # forced-kept: gas re-encodes 'mov    %al,0x3' differently
L_002b74:
        jmp L_000c33
L_002b77:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002b78:
        call L_0051df
        db "RETREAT",0x00        # string
L_002b83:
        .byte 0xa0,0x16,0x00  # forced-kept: gas re-encodes 'mov    0x16,%al' differently
L_002b86:
        .byte 0x24,0x8f  # forced-kept: gas re-encodes 'and    $0x8f,%al' differently
L_002b88:
        je L_002b9a
L_002b8a:
        call L_0051df
        db "-BLOCKED!",0x00        # string
L_002b97:
        jmp L_000c30
L_002b9a:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002b9b:
        .byte 0xa0,0x00,0x00  # forced-kept: gas re-encodes 'mov    0x0,%al' differently
L_002b9e:
        .byte 0xf9  # forced-kept: gas re-encodes 'stc' differently
L_002b9f:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002ba0:
        .byte 0x1a,0x06,0x02,0x00  # forced-kept: gas re-encodes 'sbb    0x2,%al' differently
L_002ba4:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002ba5:
        .byte 0x24,0x3f  # forced-kept: gas re-encodes 'and    $0x3f,%al' differently
L_002ba7:
        .byte 0xa2,0x00,0x00  # forced-kept: gas re-encodes 'mov    %al,0x0' differently
L_002baa:
        .byte 0xa0,0x01,0x00  # forced-kept: gas re-encodes 'mov    0x1,%al' differently
L_002bad:
        .byte 0xf9  # forced-kept: gas re-encodes 'stc' differently
L_002bae:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002baf:
        .byte 0x1a,0x06,0x03,0x00  # forced-kept: gas re-encodes 'sbb    0x3,%al' differently
L_002bb3:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_002bb4:
        .byte 0x24,0x3f  # forced-kept: gas re-encodes 'and    $0x3f,%al' differently
L_002bb6:
        .byte 0xa2,0x01,0x00  # forced-kept: gas re-encodes 'mov    %al,0x1' differently
L_002bb9:
        jmp L_000c33
L_002bbc:
        call L_0051df
        db "PASS",0x00        # string
L_002bc4:
        jmp L_000c33
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00        # tail-pad
