# cmd_L — command handler 'L' (LAUNCH): rocket/plane launch sequences;
#           "LAUNCH WHAT?", "LAUNCH--ROCKET"/"LAUNCH--PLANE" with
#           "(PRESS ANY KEY)", direction words NORTH/SOUTH/EAST/WEST echoed
#           with CR + "CMD: ", plane landing ("LAND PLANE.", "LAND--NOT
#           HERE!!!!!"). Largest handler; many local branches.
# func 38B0..3B91  len 02E2 B  evidence: seed+reko (jump table cs:0730 entry L)
# provenance: live:airport32 (+2 other harvests), static
#           entry executed in 12 of 259 harvests; derived by tools/provenance_from_traces.py
# confidence: medium (flow + messages proven by bytes; NPC/quest semantics inferred)
# notes: re-decoded with tools/decode_str.py (str_out-aware); gas re-encode diffs
#        kept as .byte (disp8/disp16 and reg-op form choices)
# ---------------------------------------------------------------------------
# FLOW, read end to end 2026-10-29. The handler branches on [DS:13] alone: 0x26 plane at
#   0x3BFA, 0x28 rocket at 0x3AD2, everything else 'LAUNCH WHAT?'. Horse and ship have no
#   launch arm at all.
#
# PLANE (0x3BFA). Prints 'LAUNCH--PLANE' BEFORE the gate, then `mov 0xe1,%al` — DS:E1 is
#   items_held[11] = BRASS BUTTON (slot names: docs/spice86-live-findings.md § 11) — and
#   with none it says 'FUNNY
#   THIS PLANE IS' / 'MISSING A BRASS BUTTON!'. Launched, it initialises a heading at
#   DS:21=0, DS:22=0xFF (north) and enters a loop of its own that is NOT the walk loop:
#   poll the key non-blocking, apply the heading delta with both coordinates masked and
#   0x3F, redraw, one cell per iteration whether or not a key arrived. Arrows change the
#   heading and echo the direction word; 'L' (0x4C) tries to land; any other key just lets
#   the iteration run. Landing is accepted on tile 0x08 grass ('LAND PLANE.') and nowhere
#   else ('LAND--NOT HERE!!!!!' and the plane keeps moving). Crossing x==0 or y==0 while
#   [DS:4A] is non-zero leaves the site through the sector reload at asm/243E.
#   THE LOOP CONSULTS NO PASSABILITY ROUTINE AND CHARGES NO FOOD. That is the answer to the
#   handoff's W6 question shape: the airborne state is this loop, and asm/2604's forest rule
#   governs the plane walked on the ground after landing. Whether the airborne loop should
#   have any tile rule at all is now a byte-level no — it has none — but the owner's
#   recollection is about the ground case and is consistent.
#
# ROCKET (0x3AD2). 'LAUNCH--ROCKET', then DS:E5 = items_held[15] = TRI-LITHIUM; without it
#   'A METALLIC VOICE SAYS:' / 'SHIP INCAPABLE OF LAUNCH!' (note this metallic-voice line is
#   a DIFFERENT string from cmd_B's 'A METALIC VOICE COMMANDS:' — different spelling, verb
#   and file; they must never be merged). Launched: 'PREPARE FOR LAUNCH!', then if [DS:6D]
#   and [DS:4A] are both zero it records the launch cell at DS:6A/6B with DS:69=1 and calls
#   the save body at asm/24FD — launching a rocket SAVES THE GAME. Then two 256-byte tables
#   at DS:272 and DS:372 are filled with 0xFF, position goes to (0,0), DS:12 = 0xFF, and
#   '(PRESS ANY KEY)' prints with no leading newline glyph of its own.
#   The flight is not steerable: y += 2 every iteration, and x += 1 whenever the new y is a
#   multiple of 8 (asm/3B9F: `and $7,%al ; jne`). It stops on any key, and the tile it
#   stopped on decides everything — tile 0x08 grass and it lives, anything else jumps to
#   runtime 0x0E62, which is the death tail. So a rocket is aimed by timing, not steering.
#
#   THE FLIGHT IS ANIMATED, and the animation is this routine's render, not a separate one.
#   Per iteration the loop is: march, `call L_005cb1`, `mov $0x1c,%bx ; call L_000911`,
#   `call L_00514d` (key_poll), `cmp $0xff,%ah ; jne` back to the march. L_005cb1 is
#   asm/5AB1_map_render — the whole sector render: viewport origin, stage the visible 20x10
#   window from mapx, flush it to the CGA planes, copy the underlay. So one march step is one
#   frame, the ship the player watches is the stamp that render draws, and the frame rate is
#   the render's cost: 492,074 emulated cycles per step measured, about 103 ms and 9.7 steps/s
#   at 4.77 MHz, so a full north-to-south sweep (32 steps, x advancing 8) takes about 3.3 s.
#   Evidence and the observed position sequence:
#   captures/spice86/dive-sweep-rate/README.md (21-dive-a.png and 22-dive-b.png are the same
#   dive 0.4 s apart; 23-land-death.png is the fatal landing). The speed-delay lookup in the
#   middle of the loop is NOT a timeout — key_poll and the flush both save and restore %bx and
#   neither reads it, so the value is loaded and discarded. See asm/0711_speed_delay_lookup.
#   The 0xFF fill of DS:272/DS:372 described above is the underlay clear that makes the first
#   staged pass draw all 200 cells; it is not a blank screen.
#
# THE PARKED-ROCKET TRIPLE, DS:69 / DS:6A / DS:6B. Decoded 2026-11-03 by sweeping every
#   reader and writer in the tree; these three bytes are inside the save record (DS:36..135)
#   and that is the whole point of them.
#     DS:69  flag: a rocket is standing at DS:6A/DS:6B and must be re-staged
#     DS:6A  that cell's x
#     DS:6B  that cell's y
#   WRITERS, both in this routine:
#     L_003b52  launch: DS:6B := y, DS:6A := x, DS:69 := 1, then `call L_0024fd`. Observed
#               live as DS:69/6A/6B = 01 01 0a after launching from (1,10) in sector 40.
#     L_003bd7  a SURVIVED dive: `mov 0x6d,%al ; or %al,%al ; jne` -- on EARTH it clears
#               DS:69 to 0; off Earth it records the landing cell and sets DS:69 to 1.
#   READERS, three, all doing the identical stamp:
#     asm/080D  the 'P' load path. Sets position from DS:5A/5B, then `mov 0x69,%al ;
#               or %al,%al ; je` and on the not-taken arm falls into asm/0839.
#     asm/0839  DS:23 := DS:6A ; DS:24 := DS:6B ; call L_0052b3 ; mov $0x50,%al ; stamp.
#     asm/245A  byte-for-byte the same stamp, second entry point.
#     asm/3823  the site-exit block, and it too branches on DS:6D before restaging.
#   0x50 IS THE ROCKET TILE: mapx41 at the three authored pads (33,3) (38,3) (43,3) is
#   0x50, and the live grid cell DS:18E6 went 0x50 -> 0x08 the moment the rocket lifted off
#   (captures/spice86/g39-rocket-flight). The engine already models the pairing: see
#   engine/src/game/overworld.c, "0x28->0x50 rocket".
#   WHY THIS EXPLAINS THE AUTO-SAVE: a rocket left on a planet has no authored pad, so the
#   only thing that puts it back is the save record. The launch saves because the parked
#   state has to survive a restart -- the save is not incidental to launching, it is the
#   persistence mechanism. Earth needs none of that, which is exactly why the dive clears
#   the flag there instead of setting it.
#
# GRADES. Bytes for every branch above. OBSERVED (tools/cfg_printed.py over the harvests,
#   which requires the call instruction itself to have executed): 'LAUNCH--PLANE' and 'LAND
#   PLANE.' in flight-launched-live and airport32, 'LAND--NOT HERE!!!!!' in airport32, and
#   ' FRIGATE' in shipcap3. The DS census over every harvest memory dump agrees: [DS:13] is
#   0x24 in shipcap3 and 0x26 in flight-32-live and flight-launched-live, 0x78 in 150
#   dumps, 0x22 once (the stepcost forced poke) and NEVER 0x28. So the plane branch is
#   observed end to end and the ship arm is observed. THE ROCKET WAS ENTERED ON
#   2026-11-03 (captures/spice86/g39-rocket-flight), and every rocket rule in this
#   header is now OBSERVED rather than inferred-from-bytes: DS:13 = 0x28 twice with a
#   frame each time, the launch flight, the auto-flight-south, and the crash-on-non-grass
#   rule. The auto-flight-south rule written above from bytes alone predicted BOTH
#   landings to the cell -- (7,62) in run 1 and (5,44) in run 2 -- from y += 2 with
#   x += 1 on multiples of 8 starting at (0,6). That is the strongest confirmation in
#   this project so far of a static decode by live play.
#   Still inferred after that session: the crash arm's survival case. Both observed
#   landings were on a lethal tile (0x00 water, then 0x0c forest) and both ran the death
#   tail at runtime 0x0E62; landing on 0x08 grass is predicted safe and has NOT been
#   tested. And the plane's airborne flag (W6) remains open.
#
# CORRECTION 2026-11-03, and it matters for the engine: THE SOUTHWARD FLIGHT IS THE
#   DIVE, NOT THE LAUNCH. The static reading above attached the auto-flight-south loop to
#   the launch, because asm/38B0's rocket branch contains it and nothing had ever been
#   inside the rocket to tell the two apart. Live says otherwise. Launching from the pad
#   spent one TRI-LITHIUM and left the position byte pair at (38,3) with the view going to
#   the starfield; the position only began marching after a SECOND L in orbit, which first
#   printed 'CMD: DIVE' / 'CMD: LANDING REQUESTED!' / 'YOU ARE ORBITING <world>.' /
#   '(PRESS ANY KEY)'. So the sequence is B (board) -> L (launch to orbit) -> optionally H
#   (hyperwarp between worlds) -> L (dive) -> the unsteerable southward march -> tile verdict.
#   Note also the guard on the launch block, [DS:6D]==0 AND [DS:4A]==0: it needs Earth AND a
#   wilderness sector, and both observed launches were from sector 41 (ones digit 1), so
#   that block -- the return-point triple and the save call -- has still never executed.
#   engine/src/game/commands.c:launch_rocket currently runs the descent at launch and so
#   is wrong on this point; it needs splitting into launch-to-orbit and dive.
#   Also measured, from the same dumps: shipcap3 boarded the frigate and its DS:E2 still
#   reads 01 afterwards, so boarding CHECKS the gating item and does not spend it. One
#   sample, and it assumes the starting count was 1.
cmd_L:
cmd_L:
L_003ab0:
        mov    0x13,%al
L_003ab3:
        cmp    $0x26,%al
L_003ab5:
        jne L_003aba
L_003ab7:
        jmp L_003bfa
L_003aba:
        nop
L_003abb:
        cmp    $0x28,%al
L_003abd:
        je L_003ad2
L_003abf:
        call L_0051df
        db "LAUNCH WHAT?",0x00        # string
L_003acf:
        jmp L_000c33
L_003ad2:
        call L_0051df
        db "LAUNCH--ROCKET",0x00        # string
L_003ae4:
        .byte 0xa0,0xe5,0x00  # forced-kept: gas re-encodes 'mov    0xe5,%al' differently
L_003ae7:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003ae9:
        jne L_003b23
L_003aeb:
        call L_0051df
        db 0x8d,"A METALLIC VOICE SAYS:",0x8d,"SHIP INCAPABLE OF LAUNCH!",0x00        # string
L_003b20:
        jmp L_000c33
L_003b23:
        call L_0051df
        db 0x8d,"PREPARE FOR LAUNCH!",0x00        # string
L_003b3b:
        mov    0x6d,%al
L_003b3e:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003b40:
        jne L_003b5d
L_003b42:
        mov    0x4a,%al
L_003b45:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003b47:
        jne L_003b5d
L_003b49:
        mov    0x0,%al
L_003b4c:
        mov    %al,0x6a
L_003b4f:
        mov    0x1,%al
L_003b52:
        mov    %al,0x6b
L_003b55:
        mov    $0x1,%al
L_003b57:
        mov    %al,0x69
L_003b5a:
        call L_0024fd
L_003b5d:
        nop
L_003b5e:
        call L_006b99
L_003b61:
        call L_002490
L_003b64:
        mov    $0xff,%di
L_003b67:
        mov    $0xff,%al
L_003b69:
        mov    %al,0x272(%di)
L_003b6d:
        mov    %al,0x372(%di)
L_003b71:
        dec    %di
L_003b72:
        jns L_003b69
L_003b74:
        movb   $0x0,0x0
L_003b79:
        movb   $0x0,0x1
L_003b7e:
        mov    %al,0x12
L_003b81:
        call L_0051df
        db "(PRESS ANY KEY)",0x00        # string
L_003b94:
        mov    0x1,%al
L_003b97:
        clc
L_003b98:
        adc    $0x2,%al
L_003b9a:
        and    $0x3f,%al
L_003b9c:
        mov    %al,0x1
L_003b9f:
        and    $0x7,%al
L_003ba1:
        jne L_003bae
L_003ba3:
        mov    0x0,%al
L_003ba6:
        clc
L_003ba7:
        adc    $0x1,%al
L_003ba9:
        and    $0x3f,%al
L_003bab:
        mov    %al,0x0
L_003bae:
        call L_005cb1
L_003bb1:
        mov    $0x1c,%bx
L_003bb4:
        call L_000911
L_003bb7:
        call L_00514d
L_003bba:
        cmp    $0xff,%ah
L_003bbd:
        jne L_003b94
L_003bbf:
        mov    0x0,%al
L_003bc2:
        mov    %al,0x23
L_003bc5:
        mov    0x1,%al
L_003bc8:
        mov    %al,0x24
L_003bcb:
        call L_0052b3
L_003bce:
        and    $0x7f,%al
L_003bd0:
        cmp    $0x8,%al
L_003bd2:
        je L_003bd7
L_003bd4:
        jmp L_000e62
L_003bd7:
        mov    0x6d,%al
L_003bda:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003bdc:
        jne L_003be6
L_003bde:
        mov    $0x0,%al
L_003be0:
        mov    %al,0x69
L_003be3:
        jmp L_000c33
L_003be6:
        mov    0x0,%al
L_003be9:
        mov    %al,0x6a
L_003bec:
        mov    0x1,%al
L_003bef:
        mov    %al,0x6b
L_003bf2:
        mov    $0x1,%al
L_003bf4:
        mov    %al,0x69
L_003bf7:
        jmp L_000c33
L_003bfa:
        call L_0051df
        db "LAUNCH--PLANE",0x00        # string
L_003c0b:
        .byte 0xa0,0xe1,0x00  # forced-kept: gas re-encodes 'mov    0xe1,%al' differently
L_003c0e:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003c10:
        jne L_003c45
L_003c12:
        call L_0051df
        db 0x8d,"FUNNY THIS PLANE IS",0x8d,"MISSING A BRASS BUTTON!",0x00        # string
L_003c42:
        jmp L_000c30
L_003c45:
        nop
L_003c46:
        mov    $0x0,%al
L_003c48:
        mov    %al,0x21
L_003c4b:
        mov    $0xff,%al
L_003c4d:
        mov    %al,0x22
L_003c50:
        call L_0051df
        db 0x0d,"CMD: ",0x00        # string
L_003c5a:
        .byte 0xc6,0x06,0x4e,0x05,0xff  # forced-kept: gas re-encodes 'movb   $0xff,0x54e' differently
L_003c5f:
        call L_0060c7
L_003c62:
        .byte 0xbb,0x02,0x00  # forced-kept: gas re-encodes 'mov    $0x2,%bx' differently
L_003c65:
        call L_000911
L_003c68:
        call L_00514d
L_003c6b:
        cmp    $0xff,%ah
L_003c6e:
        jne L_003c9b
L_003c70:
        cmp    0x26e,%al
L_003c74:
        jne L_003c79
L_003c76:
        jmp L_003cdb
L_003c78:
        nop
L_003c79:
        cmp    0x26f,%al
L_003c7d:
        jne L_003c82
L_003c7f:
        jmp L_003cf6
L_003c81:
        nop
L_003c82:
        cmp    0x270,%al
L_003c86:
        jne L_003c8b
L_003c88:
        jmp L_003d11
L_003c8b:
        cmp    0x271,%al
L_003c8f:
        jne L_003c94
L_003c91:
        jmp L_003d2c
L_003c94:
        cmp    $0x4c,%al
L_003c96:
        jne L_003c9b
L_003c98:
        jmp L_003d47
L_003c9b:
        clc
L_003c9c:
        mov    0x0,%al
L_003c9f:
        adc    0x21,%al
L_003ca3:
        and    $0x3f,%al
L_003ca5:
        mov    %al,0x0
L_003ca8:
        clc
L_003ca9:
        mov    0x1,%al
L_003cac:
        adc    0x22,%al
L_003cb0:
        and    $0x3f,%al
L_003cb2:
        mov    %al,0x1
L_003cb5:
        mov    0x4a,%al
L_003cb8:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003cba:
        je L_003cd5
L_003cbc:
        mov    0x0,%al
L_003cbf:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003cc1:
        je L_003ccd
L_003cc3:
        mov    0x1,%al
L_003cc6:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003cc8:
        je L_003ccd
L_003cca:
        jmp L_003cd5
L_003ccc:
        nop
L_003ccd:
        movb   $0x0,0x54e
L_003cd2:
        jmp L_00263e
L_003cd5:
        nop
L_003cd6:
        call L_005cb1
L_003cd9:
        jmp L_003c5f
L_003cdb:
        mov    $0x0,%al
L_003cdd:
        mov    %al,0x21
L_003ce0:
        mov    $0xff,%al
L_003ce2:
        mov    %al,0x22
L_003ce5:
        call L_0051df
        db "NORTH",0x0d,"CMD: ",0x00        # string
L_003cf4:
        jmp L_003c9b
L_003cf6:
        mov    $0x0,%al
L_003cf8:
        mov    %al,0x21
L_003cfb:
        mov    $0x1,%al
L_003cfd:
        mov    %al,0x22
L_003d00:
        call L_0051df
        db "SOUTH",0x0d,"CMD: ",0x00        # string
L_003d0f:
        jmp L_003c9b
L_003d11:
        mov    $0x1,%al
L_003d13:
        mov    %al,0x21
L_003d16:
        mov    $0x0,%al
L_003d18:
        mov    %al,0x22
L_003d1b:
        call L_0051df
        db "EAST",0x0d,"CMD: ",0x00        # string
L_003d29:
        jmp L_003c9b
L_003d2c:
        mov    $0xff,%al
L_003d2e:
        mov    %al,0x21
L_003d31:
        mov    $0x0,%al
L_003d33:
        mov    %al,0x22
L_003d36:
        call L_0051df
        db "WEST",0x0d,"CMD: ",0x00        # string
L_003d44:
        jmp L_003c9b
L_003d47:
        mov    0x0,%al
L_003d4a:
        mov    %al,0x23
L_003d4d:
        mov    0x1,%al
L_003d50:
        mov    %al,0x24
L_003d53:
        call L_0052b3
L_003d56:
        cmp    $0x8,%al
L_003d58:
        jne L_003d71
L_003d5a:
        call L_0051df
        db "LAND PLANE.",0x00        # string
L_003d69:
        movb   $0x0,0x54e
L_003d6e:
        jmp L_000c33
L_003d71:
        nop
L_003d72:
        call L_0051df
        db "LAND--NOT HERE!!!!!",0x0d,"CMD: ",0x00        # string
L_003d8f:
        jmp L_003c9b
