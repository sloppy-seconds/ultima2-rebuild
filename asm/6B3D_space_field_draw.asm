# space_field_draw — redraws the 64-star space field, with a motion mode
# name-evidence: it walks two parallel 64-entry tables, `mov 0x4cd(%si),%al` for x and
#           `mov 0x50d(%si),%al` for y — and `0x50D - 0x4CD = 64`, exactly the star count
#           the engine measured independently off five capture frames (space_view.c
#           U2_SPACE_ORBIT_STARS). Each entry is recomputed relative to the ship at
#           DS:47B/DS:47C through the delta table at cs:0x6cac, written back into the
#           tables, and plotted through asm/4B76 (pixel_plot, colour 3). When the mode byte
#           says the star has moved, the OLD table position is first cleared through
#           asm/4BAA (the same address arithmetic with the mask inverted, colour 0), which
#           is why this is a motion routine and not just a scatter:
#                 mov 0x4c7,%al ; or %al,%al ; je L_006e14   ; mode 0 -> no erase
#                 ... plot old (DS:479,DS:47A) via L_004daa ...
#                 plot new via L_004d76 ; store new into the tables
#                 cmpb $0x2,0x4c7 ; jae L_006e52             ; mode >= 2 takes a further branch
#           So **DS:4C7 is the animation mode**: 0 = plot in place, 1 = erase-and-plot
#   CORRECTED 2026-11-16 -- **DS:4C7 IS NOT A MODE, IT IS A SWEEP COUNTER.** Read at this
#           routine's entry across 600 consecutive frames of a live hyperwarp
#           (`captures/spice86/g90-warp-table/full.txt`) its whole shape is:
#               128 -> 0      over 328 frames   (decrementing)
#               1 held         128 frames
#               2,4,6 ... 128  128 frames       (incrementing by 2)
#               128 held       ~81 frames
#           So it is a down-then-up sweep, not a mode and not a plain countdown. The two
#           branches the listing looks like a selector are:
#             * `or %al,%al ; je L_006e14` skips the ERASE, and it fires for **128
#               consecutive frames** -- the whole stretch where the sweep sits at 0. That
#               window is the warp's trail: 128 frames x 64 stars plotted with nothing
#               erased. Measured on screen at the peak of a warp: 6235 white px in 3892
#               distinct 1-px dots, radiating out from the ship. That is what this window
#               draws, and it is why the field climbs ~77 px -> ~6200 px and falls back.
#             * `cmpb $0x2,0x4c7 ; jae` is the normal path for the whole sweep except the
#               0 and 1 stretches, so the arm it selects is not a special mode either.
#           An earlier correction on this same line today said the erase was skipped "only
#           on the last frame or two". That was wrong too, and wrong in the direction that
#           hid the answer: it was sampled over 120 frames, which is only the first leg of
#           the sweep and never reaches 0. The full-shape read is what settled it.
#           What DS:4C8 holds is still open -- it read 0 at every one of the 120 stops in
#           the first read, so it is not the frame counter either.
#           (motion), >= 2 = the extra branch at L_006e3a. **Read 2026-11-15: the >= 2 branch
#           draws nothing.** The star is plotted at L_006e30 (`call L_004d76`) BEFORE the
#           `cmpb $0x2,0x4c7` at L_006e33, so both arms have already drawn the same pixel by
#           the time they diverge. What the branch selects is a side effect, and it is a
#           divider:
#                 mode < 2 : cs:0x6990 += cs:0x6992 ; mov $0x1,%cx ; call L_005ed2
#                 mode >= 2: decw cs:0x6994 ; jne skip ; call L_007442
#           L_005ed2 is file-offset notation for runtime 0x5CD2, which is asm/5CD2_speaker_freq
#           — the 8253 channel-2 latch. So mode 1 drives the speaker on every pass of the
#           star loop, and mode >= 2 instead counts a CS-side word down and fires asm/7242
#           only when it reaches zero. asm/7242 reloads that same word at its own tail, to
#           0xE0 on one path and 0x18 on the other, selected by `cmpb $0xff,%cs:0x6998`, and
#           its second path also calls the speaker. So the whole branch is a **rate-limited
#           tone**, not a second animation, and the reload values are the period.
#           The engine's `out->warping` is the same distinction, and the engine currently has
#           no sound on either arm.
#           **The streaks, and this is the part that was nearly missed.** The radiating lines
#           the owner describes — stars stretched into lines, like they are moving fast — are
#           not a second drawing routine and are not behind the >= 2 branch. They are what
#           mode 0 draws, by omission: `mov 0x4c7,%al ; or %al,%al ; je L_006e14` skips the
#           `call L_004daa` that clears the star's OLD table position, so in mode 0 the old
#           pixel stays lit while the moved star plots a new one further out. Nothing in the
#           warp path clears the screen between redraws — asm/6DFD calls this routine in a
#           bounded loop and the only writes to the pixels are individual plot and erase — so
#           successive mode-0 frames **accumulate into a trail radiating from the ship**. Mode
#           1 is the opposite: erase-then-plot, one clean pixel, no trail. So the trail is a
#           property of the mode byte and of the absence of a clear, and an engine that clears
#           its frame every iteration cannot produce it at any mode. See
#           `engine/src/game/space_view.c` `u2_space_scene_draw`, which does exactly that.
# renamed again 2026-11-15 (same day): the header used to list "what the mode >= 2 branch
#           draws" as undecoded. It draws nothing; the question was the wrong shape, and the
#           right question — what does the branch DO — was answerable statically in one read.
# func 6B3D..6CC3  len 0187 B  evidence: call+reko
# calls: L_004d76 (pixel_plot), L_004daa (pixel_erase, colour 0), L_005ed2, L_006d47,
#           L_006da0, L_006df6, L_006e5c, L_006e8c (+3 more)
# callers: asm/6999_space_status (the in-space command loop, i.e. the static orbit screen)
#           and asm/6CC4_space_ship_erase — and six times from asm/6DFD_hyperwarp_flow,
#           which is the only caller that sets DS:4C7 at all. asm/6999 never writes DS:4C7
#           or DS:4C8, so the static screen draws in whatever mode the last hyperwarp left.
# renamed 2026-11-15 from sub_6b3d. The mode byte, the two tables and the erase-before-plot
#           were read out of the body directly; the star count agreeing with the frame
#           measurement is the corroboration that makes the table reading more than a guess.
#           What is STILL not decoded: what the tone actually sounds like — the divisor
#           reaches asm/5CD2_speaker_freq through a value computed in asm/7242 from a masked
#           timer read, and no capture has sampled it.
#
#           **The cs:0x6cac table, decoded 2026-11-16 by a live CS read.** It is not in the
#           file image: `original/ultimaii.exe` at `0x6cac` and at `0x6cac-0x200` both land
#           mid-instruction, and so do the two known CS data words `cs:0x6990` and `cs:0x6998`,
#           which is what proves the CS data area here has no file backing rather than proving
#           the offset is wrong. Read live at `CS:0x6cac` (CS=0x17D, so linear 0x0847C) it is
#           `01 02 03 ... 10` then `01 02 ... 08` and then code resumes — **16 entries, value
#           = index + 1**. The mapping was pinned by two independent checks in the same read:
#           `cs:0x6990` came back `00 22` = 0x2200, the exact seed `asm/6DFD` writes, and
#           `cs:0x6998` came back `0xFF`, the value `asm/6B3D` itself compares against. The
#           bytes are identical at the title screen and in orbit, so the table is static.
#           The index is not a star number. Both sites build `%di` the same way — `xor $0xff,%al`
#           then four `clc; rcr $1,%al` then `mov $0x0,%ah; mov %ax,%di` — which is
#           `di = (x ^ 0xff) >> 4`, the high nibble of the complemented star x, so `di` is
#           0..15 and the table is exactly wide enough. And the value is not a motion delta
#           either: the two sites are `sbb` against `0x4c3` with a `cmc; jb` test, and `adc`
#           against the same `0x4c3` with the sum stored to `0x4c5`. So it is a **per-x-bucket
#           threshold and offset against a global counter at DS:4C3** — a stagger, and the
#           mechanism by which only part of the field updates on a given pass, which is what
#           makes the original's rays read as sparse dots rather than solid lines.
#           The consequence for the new x is direct: `L_006e1c` stores `0x4c5` into the table.
#           **Corrected 2026-11-16: an earlier note here said `new_x = old_x +
#           table_x[bucket(old_x)]`, and that was wrong on both counts.** The bucket is not
#           `bucket(old_x)`, it is `bucket(old_x - ship_x)`: `L_006d50` does `sbb 0x47b,%al`
#           (the ship's x) *before* the `xor $0xff` and the four `clc; rcr` (a logical shift,
#           not an arithmetic one), so `di = (~(old - ship)) >> 4`. And the sign depends on
#           which arm you are on, because `L_006d55 jb L_006d7f` branches when `old < ship`,
#           so the **fall-through is the `sbb` (subtract) arm** and the branch target is the
#           `adc` (add) arm with `di = ((old - ship) mod 256) >> 4`. Net effect: every star is
#           pulled **toward** the ship's column, by up to 16 when it is next to it and by 1
#           when it is at the far edge. That is the radial mechanism, and it is the opposite
#           sign to what was written here before.
#           The y path at `L_006da0` is the same shape with a second table and the same arm
#           swap (`L_006dae jb L_006dd5`).
#           **The y table question is now closed, and the answer is that the original reads
#           past its own table.** `cs:0x6cbc` live is `01 02 03 04 05 06 07 08` and then the
#           program's own code follows (`a0 7b 04 a2 79 04 a0 7c`) — eight entries, not
#           sixteen. And the bucket is never below 12 for a field seeded by `asm/6D3E`
#           (`y = rand & 0x7f`, ship at `0x40`): on the `>=` arm `d` is 0..63 so `(~d)>>4` is
#           12..15, and on the `<` arm the wrapped difference is 192..255 so `>>4` is also
#           12..15. **Every y step the original takes comes out of its own instructions**, at
#           the fixed values 121, 4, 160, 124 for buckets 12..15. It is a bug in the game,
#           and a deterministic one, because code bytes do not change.
#           **And the paths that look like refusals are not refusals.** `L_006d76
#           jmp L_006e8c` and `L_006df0 jmp L_006e8c` reload the star's old x and y, call
#           `L_006f68` — which is the RNG, advancing the CS-side state word at
#           `cs:0x6996/0x6997` — and store the returned byte into `0x4c5`. A star whose
#           arithmetic arm refuses therefore gets a **fresh random position**, which is why
#           the live table never converges: across 300 samples its x spread stays between 196
#           and 254 (mean 242). Without that arm a model of the four arithmetic arms alone
#           collapses into a band around the ship's column.
#           **One correction to how the mapping was pinned.** The `cs:0x6990 = 0x2200` and
#           `cs:0x6998 = 0xFF` readings that retired the skew question were taken on a session
#           left up by an earlier warp, so they were residue from `asm/6DFD` having run, not
#           fresh state. They still demonstrate the mapping — those are the values that routine
#           writes — but the independent confirmation on a never-warp'd session is that
#           `cs:0x6cac` reads the same 1..16 / 1..8 ramp there as it does in orbit, and that
#           `CS:0x6b3d` contains the routine's own bytes (`8a 84 cd 04` is `mov ah,[si+0x4cd]`).
#           On a fresh title screen both CS words read zero, which is what an unseeded
#           accumulator should do.
# confidence: medium-high for the mode byte and the tables; **measured-static for the >= 2
#           branch** (it is a divider into the speaker, not art); low for the audible result
# provenance: static — the mode byte, the two tables and the erase-before-plot were read out of
#           the body on 2026-11-15. The frame evidence for the star count is
#           captures/spice86/ frames 014/049/050/096/097 as measured in space_view.h, which is
#           corroboration of the count and not a claim that this routine executed.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_6b3d:
L_006d3d:
        nop
L_006d3e:
        call L_006f83
L_006d41:
        mov    $0x0,%bh
L_006d43:
        mov    $0x0,%bl
L_006d45:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_006d47:
        mov    0x4cd(%si),%al
L_006d4b:
        mov    %al,0x4c3
L_006d4e:
        stc
L_006d4f:
        cmc
L_006d50:
        sbb    0x47b,%al
L_006d54:
        cmc
L_006d55:
        jb L_006d7f
L_006d57:
        xor    $0xff,%al
L_006d59:
        clc
L_006d5a:
        rcr    $1,%al
L_006d5c:
        clc
L_006d5d:
        rcr    $1,%al
L_006d5f:
        clc
L_006d60:
        rcr    $1,%al
L_006d62:
        clc
L_006d63:
        rcr    $1,%al
L_006d65:
        mov    $0x0,%ah
L_006d67:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_006d69:
        mov    0x4c3,%al
L_006d6c:
        stc
L_006d6d:
        cmc
L_006d6e:
        sbb    %cs:0x6cac(%di),%al
L_006d73:
        cmc
L_006d74:
        jb L_006d79
L_006d76:
        jmp L_006e8c
L_006d79:
        mov    %al,0x4c5
L_006d7c:
        jmp L_006da0
L_006d7e:
        nop
L_006d7f:
        clc
L_006d80:
        rcr    $1,%al
L_006d82:
        clc
L_006d83:
        rcr    $1,%al
L_006d85:
        clc
L_006d86:
        rcr    $1,%al
L_006d88:
        clc
L_006d89:
        rcr    $1,%al
L_006d8b:
        mov    $0x0,%ah
L_006d8d:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_006d8f:
        mov    0x4c3,%al
L_006d92:
        clc
L_006d93:
        adc    %cs:0x6cac(%di),%al
L_006d98:
        mov    %al,0x4c5
L_006d9b:
        jae L_006da0
L_006d9d:
        jmp L_006e8c
L_006da0:
        mov    0x50d(%si),%al
L_006da4:
        mov    %al,0x4c4
L_006da7:
        stc
L_006da8:
        cmc
L_006da9:
        sbb    0x47c,%al
L_006dad:
        cmc
L_006dae:
        jb L_006dd5
L_006db0:
        xor    $0xff,%al
L_006db2:
        clc
L_006db3:
        rcr    $1,%al
L_006db5:
        clc
L_006db6:
        rcr    $1,%al
L_006db8:
        clc
L_006db9:
        rcr    $1,%al
L_006dbb:
        clc
L_006dbc:
        rcr    $1,%al
L_006dbe:
        mov    $0x0,%ah
L_006dc0:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_006dc2:
        mov    0x4c4,%al
L_006dc5:
        stc
L_006dc6:
        cmc
L_006dc7:
        sbb    %cs:0x6cbc(%di),%al
L_006dcc:
        cmc
L_006dcd:
        mov    %al,0x4c6
L_006dd0:
        jns L_006df6
L_006dd2:
        jmp L_006e8c
L_006dd5:
        clc
L_006dd6:
        rcr    $1,%al
L_006dd8:
        clc
L_006dd9:
        rcr    $1,%al
L_006ddb:
        clc
L_006ddc:
        rcr    $1,%al
L_006dde:
        clc
L_006ddf:
        rcr    $1,%al
L_006de1:
        mov    $0x0,%ah
L_006de3:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_006de5:
        mov    0x4c4,%al
L_006de8:
        clc
L_006de9:
        adc    %cs:0x6cbc(%di),%al
L_006dee:
        jns L_006df3
L_006df0:
        jmp L_006e8c
L_006df3:
        mov    %al,0x4c6
L_006df6:
        .byte 0x8b,0xde  # forced-kept: gas re-encodes 'mov    %si,%bx' differently
L_006df8:
        mov    %bl,0x4c9
L_006dfc:
        mov    0x4c7,%al
L_006dff:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_006e01:
        je L_006e14
L_006e03:
        mov    0x4cd(%si),%al
L_006e07:
        mov    %al,0x479
L_006e0a:
        mov    0x50d(%si),%al
L_006e0e:
        mov    %al,0x47a
L_006e11:
        call L_004daa
L_006e14:
        mov    $0x0,%bh
L_006e16:
        mov    0x4c9,%bl
L_006e1a:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_006e1c:
        mov    0x4c5,%al
L_006e1f:
        mov    %al,0x479
L_006e22:
        mov    %al,0x4cd(%si)
L_006e26:
        mov    0x4c6,%al
L_006e29:
        mov    %al,0x47a
L_006e2c:
        mov    %al,0x50d(%si)
L_006e30:
        call L_004d76
L_006e33:
        cmpb   $0x2,0x4c7
L_006e38:
        jae L_006e52
L_006e3a:
        mov    %cs:0x6990,%bx
L_006e3f:
        add    %cs:0x6992,%bx
L_006e44:
        mov    %bx,%cs:0x6990
L_006e49:
        mov    $0x1,%cx
L_006e4c:
        call L_005ed2
L_006e4f:
        jmp L_006e5c
L_006e51:
        nop
L_006e52:
        decw   %cs:0x6994
L_006e57:
        jne L_006e5c
L_006e59:
        call L_007442
L_006e5c:
        mov    $0x0,%bh
L_006e5e:
        mov    0x4c9,%bl
L_006e62:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_006e64:
        inc    %si
L_006e65:
        .byte 0x8b,0xde  # forced-kept: gas re-encodes 'mov    %si,%bx' differently
L_006e67:
        cmp    $0x40,%bl
L_006e6a:
        je L_006e76
L_006e6c:
        mov    0x4c7,%al
L_006e6f:
        cmp    $0x2,%al
L_006e71:
        jae L_006e73
L_006e73:
        jmp L_006d47
L_006e76:
        nop
L_006e77:
        mov    0x4c7,%al
L_006e7a:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_006e7c:
        je L_006e8b
L_006e7e:
        mov    $0x0,%ah
L_006e80:
        .byte 0x8b,0xc8  # forced-kept: gas re-encodes 'mov    %ax,%cx' differently
L_006e82:
        mov    $0x10,%bx
L_006e85:
        dec    %bx
L_006e86:
        jne L_006e85
L_006e88:
        dec    %cx
L_006e89:
        jne L_006e82
L_006e8b:
        ret
L_006e8c:
        nop
L_006e8d:
        mov    0x4cd(%si),%al
L_006e91:
        mov    %al,0x4c3
L_006e94:
        mov    0x50d(%si),%al
L_006e98:
        mov    %al,0x4c4
L_006e9b:
        call L_006f68
L_006e9e:
        mov    %al,0x4c5
L_006ea1:
        call L_006f68
L_006ea4:
        and    $0x7f,%al
L_006ea6:
        mov    %al,0x4c6
L_006ea9:
        jmp L_006df6
L_006eac:
        add    %ax,(%bp,%si)
L_006eae:
        add    (%si),%ax
L_006eb0:
        add    $0x706,%ax
L_006eb3:
        or     %cl,(%bx,%di)
L_006eb5:
        or     (%bp,%di),%cl
L_006eb7:
        or     $0xd,%al
L_006eb9:
        push   %cs
L_006eba:
        movups (%bx,%di),%xmm0
L_006ebd:
        add    (%bp,%di),%al
L_006ebf:
        add    $0x5,%al
L_006ec1:
        push   %es
L_006ec2:
        pop    %es
L_006ec3:
        .byte 0x08  # forced-kept: gas re-encodes 'or     %ah,0x47b(%bx,%si)' differently
