# cmd_V — command handler 'V' (VIEW): "VIEW WHAT?"; with magical helm shows
#           "VIEW\x8dWITH MAGICAL HELM!" variant.
# func 437E..43F2  len 0075 B  evidence: seed+reko (jump table cs:0730 entry V)
# provenance: live: cmd_V executed in captures/spice86/wvcap/ and wvcap-live/, observed 2026-10-26.
#           entry executed in 8 of 285 harvests; derived by tools/provenance_from_traces.py
#           Entry 0x437E is present in both harvests' ExecutedInstructions, and so is 0x4396 — the
#           MAGICAL HELM branch, not just the refusal.
#           OBSERVED ON SCREEN 2026-10-26 (captures/spice86/vhelm/, frame v_helm_view.png). With
#           DS:DB read as 01 BEFORE the press and DS:4A = 00, pressing V replaced the whole play
#           field with a **world map** — continents in white on black, filling the screen — with the
#           CMD window beneath reading `CMD: VIEW` / `WITH MAGICAL HELM!`. DS:DB read 00 afterwards,
#           so the charge is spent by the view, not by a later action. A subsequent press with the
#           counter at zero takes the 0x4585 refusal and prints VIEW WHAT?.
#           RETRACTION: an earlier version of this header said the render was "not captured" and
#           offered a whole-SECTOR view as consistent with the code shape. It is not a sector view.
#           The frame refutes it, and the mistake was reading memory and a cut-off text fragment and
#           then writing the inference down as though it had been watched. Memory says what changed;
#           only the screen says what the player saw. Those two disagreed here.
#           RETRACTION, 2026-11-11 — the sentence below was wrong and is kept only so nobody
#           quotes it. It said the map's source was unidentified and "is not the sector grid".
#           It IS the sector grid: three sectors were drawn and each matched its own mapx file,
#           including mapx00, whose landmass is the oval that this header earlier described as
#           continents. See captures/spice86/vhelm-sector-map-live/ for the frames and the
#           measurements. The mistake was the one AGENTS § 3 names: a memory trace and a
#           cut-off string, written down as though it had been watched.
#           WHAT THE MAP IS (measured 2026-11-11, captures/spice86/vhelm-sector-map-live):
#           the current sector's own map, one cell per 8x4 real pixels, 64x64 cells = 512x256.
#           Water is black; land is a dither whose density rises with the art index — mean
#           luminance in sector 30 was art 0 → 9.6, art 1 → 50, art 2 → 47, art 3 → 76,
#           art 4 → 114. The gate `cmp $0x4,%al; jae` on DS:4A is the site-kind digit, so
#           wilderness passes too (all three frames were wilderness, site kind 0) and only
#           towers and dungeons are refused.
#           STILL NOT DECODED: how the map is produced. The body sets DS:23/24 to 0x20,0x20, calls
#           L_002bd2, waits on L_00514d until AH == 0xFF, then clears two 256-byte tables at 0x272
#           and 0x372 and tail-jumps to the tick. The wait-for-key and the two table clears match a
#           modal overlay that is torn down on a keypress, which the frame agrees with, and
#           asm/29D2's body walks a mod-64 grid and calls the cell stamper at L_0052b3, which is
#           consistent with staging the whole sector. What is still untraced is the per-art dither
#           density: no table has been found for it, so the engine renders the densities measured
#           above rather than claiming the mechanism that produces them.
#           The gate is `cmp $0x4,%al; jae` on DS:4A, i.e. the sector's second decimal digit must
#           be below 4. That is NOT the same axis as "towns and castles", which the engine derives
#           from the tile under the player (site_kind). Recorded as an open discrepancy rather than
#           resolved in favour of the recollection.
# confidence: medium (flow + messages proven by bytes; NPC/quest semantics inferred)
# notes: re-decoded with tools/decode_str.py (str_out-aware); gas re-encode diffs
#        kept as .byte (disp8/disp16 and reg-op form choices)
cmd_V:
cmd_V:
L_00457e:
        .byte 0xa0,0xdb,0x00  # forced-kept: gas re-encodes 'mov    0xdb,%al' differently
L_004581:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_004583:
        jne L_004596
L_004585:
        call L_0051df
        db "VIEW WHAT?",0x00        # string
L_004593:
        jmp L_000c33
L_004596:
        .byte 0xa0,0x4a,0x00  # forced-kept: gas re-encodes 'mov    0x4a,%al' differently
L_004599:
        .byte 0x3c,0x04  # forced-kept: gas re-encodes 'cmp    $0x4,%al' differently
L_00459b:
        jae L_004585
L_00459d:
        call L_0051df
        db "VIEW",0x8d,"WITH MAGICAL HELM!",0x00        # string
L_0045b8:
        .byte 0xf9  # forced-kept: gas re-encodes 'stc' differently
L_0045b9:
        .byte 0xa0,0xdb,0x00  # forced-kept: gas re-encodes 'mov    0xdb,%al' differently
L_0045bc:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_0045bd:
        .byte 0x1c,0x01  # forced-kept: gas re-encodes 'sbb    $0x1,%al' differently
L_0045bf:
        .byte 0x2f  # forced-kept: gas re-encodes 'das' differently
L_0045c0:
        .byte 0xf5  # forced-kept: gas re-encodes 'cmc' differently
L_0045c1:
        .byte 0xa2,0xdb,0x00  # forced-kept: gas re-encodes 'mov    %al,0xdb' differently
L_0045c4:
        .byte 0xb0,0x20  # forced-kept: gas re-encodes 'mov    $0x20,%al' differently
L_0045c6:
        .byte 0xa2,0x23,0x00  # forced-kept: gas re-encodes 'mov    %al,0x23' differently
L_0045c9:
        .byte 0xa2,0x24,0x00  # forced-kept: gas re-encodes 'mov    %al,0x24' differently
L_0045cc:
        call L_002bd2
L_0045cf:
        call L_00514d
L_0045d2:
        .byte 0x80,0xfc,0xff  # forced-kept: gas re-encodes 'cmp    $0xff,%ah' differently
L_0045d5:
        jne L_0045cf
L_0045d7:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_0045d9:
        .byte 0xb3,0xff  # forced-kept: gas re-encodes 'mov    $0xff,%bl' differently
L_0045db:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0045dd:
        .byte 0xb0,0xff  # forced-kept: gas re-encodes 'mov    $0xff,%al' differently
L_0045df:
        .byte 0x88,0x85,0x72,0x02  # forced-kept: gas re-encodes 'mov    %al,0x272(%di)' differently
L_0045e3:
        .byte 0x88,0x85,0x72,0x03  # forced-kept: gas re-encodes 'mov    %al,0x372(%di)' differently
L_0045e7:
        .byte 0x4f  # forced-kept: gas re-encodes 'dec    %di' differently
L_0045e8:
        jne L_0045df
L_0045ea:
        .byte 0xa2,0x72,0x02  # forced-kept: gas re-encodes 'mov    %al,0x272' differently
L_0045ed:
        .byte 0xa2,0x72,0x03  # forced-kept: gas re-encodes 'mov    %al,0x372' differently
L_0045f0:
        jmp L_000c33
