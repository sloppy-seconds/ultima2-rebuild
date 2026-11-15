# site_floor_down — the decrement arm of the floor pair, and the only arm that can leave a site
# name-evidence: reached from two places and both are now known. asm/334D cmd_D jumps here when
#           the site kind is 4 (DESCEND in a tower goes down) and asm/37F1 cmd_K jumps here when
#           the kind is 5 (KLIMB in a dungeon goes up toward the surface). `decb 0x25` then `js`
#           is the whole rule: [DS:25] is a floor index and dropping below zero is how you get
#           out. The live witness is captures/spice86/d12-ladder-ds25, where DESCEND at floor 0
#           left the byte at 0xFF and put the player back on the world map. asm/3381 is the
#           mirror arm (`incb`, no bound check at all) and asm/3812 is the gate that picks.
# func 3823..38AF  len 008D B  evidence: reko; role read from the bytes 2026-10-29
# calls: L_000c33, L_002490, L_0051df (str_out), L_0052b3, L_0054ca, L_0055b5
# provenance: static bytes, corroborated live by captures/spice86/d12-ladder-ds25
# confidence: high for the floor arithmetic and the level message, both read directly off the
#           bytes, and the underflow outcome is observed.
#           LOW for the exit block at L_003a44. It restores the entry cell, clears the creature
#           slots through the [DS:272]/[DS:372] pair, zeroes the site kind, calls asm/2490, and
#           then branches on [DS:6D] between staging a cell from [DS:6A]/[DS:6B] and writing the
#           256-byte player record with an int-21h handle write. That last branch means leaving a
#           dungeon SAVES THE GAME. OBSERVED 2026-11-15 by captures/spice86/g82-site-autosave,
#           which had to force the world byte to see the other arm: standing on the sector-30
#           dungeon cell, `E` then `K` with DS:6D = 0x00 rewrote the run's `player` (food
#           800000 -> 799990) and left `mapx30` and `monx30` alone; the identical two keys with
#           DS:6D poked to 0x09 rewrote nothing. The exit itself is proven by state, not
#           inference: position returned to (25,5), sector to 30, and DS:25 underflowed to 0xff.
#           Two turns of PASS on Earth with food falling 130 points in memory wrote nothing, so
#           the write belongs to this transition and not to the tick. Dungeons are parked
#           (AGENTS.md § 3), so this block is still recorded and not modelled in the engine.
# notes: L_0055b5 renders one packed-decimal byte as two ASCII digits, high nibble first, and
#           what it is handed comes from `add $0x0,%al ; daa` rather than a stored BCD byte — so
#           a floor past 99 prints a wrapped pair instead of a third digit. Do not "simplify"
#           that to a decimal format.
# label: sub_3823 / L_003a23 kept as they are — labels are body (AGENTS.md § 3).
sub_3823:
L_003a23:
        nop
L_003a24:
        decb   0x25
L_003a28:
        js L_003a44
L_003a2a:
        call L_0051df
        db 0x8d,"TO LEVEL ",0x00        # string
L_003a38:
        mov    0x25,%al
L_003a3b:
        add    $0x0,%al
L_003a3d:
        daa
L_003a3e:
        call L_0055b5
L_003a41:
        jmp L_000c33
L_003a44:
        nop
L_003a45:
        mov    0x5a,%al
L_003a48:
        mov    %al,0x0
L_003a4b:
        mov    0x5b,%al
L_003a4e:
        mov    %al,0x1
L_003a51:
        mov    $0xfe,%bx
L_003a54:
        mov    $0xffff,%ax
L_003a57:
        mov    %ax,0x272(%bx)
L_003a5b:
        mov    %ax,0x372(%bx)
L_003a5f:
        sub    $0x2,%bx
L_003a62:
        jns L_003a57
L_003a64:
        mov    $0x0,%al
L_003a66:
        mov    %al,0x4a
L_003a69:
        call L_002490
L_003a6c:
        mov    0x5a,%al
L_003a6f:
        mov    %al,0x0
L_003a72:
        mov    0x5b,%al
L_003a75:
        mov    %al,0x1
L_003a78:
        mov    0x6d,%al
L_003a7b:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003a7d:
        je L_003a99
L_003a7f:
        mov    0x6a,%al
L_003a82:
        mov    %al,0x23
L_003a85:
        mov    0x6b,%al
L_003a88:
        mov    %al,0x24
L_003a8b:
        call L_0052b3
L_003a8e:
        mov    $0x50,%al
L_003a90:
        mov    0x6,%bx
L_003a94:
        mov    %al,(%bx,%si)
L_003a96:
        jmp L_000c33
L_003a99:
        lea    0x36,%dx
L_003a9d:
        mov    $0x28,%ah
L_003a9f:
        mov    $0x100,%cx
L_003aa2:
        call L_0054ca
L_003aa5:
        push   %ax
L_003aa6:
        dec    %sp
L_003aa7:
        inc    %cx
L_003aa8:
        pop    %cx
L_003aa9:
        inc    %bp
L_003aaa:
        push   %dx
L_003aab:
        and    %ah,(%bx,%si)
L_003aad:
        jmp L_000c33
