# space_status_values — the readout's VALUE pass: four two-digit values at column 37, then the
#           scroll margin and the CMD cursor are set for the command loop
# name-evidence: read 2026-11-10 after an owner playtest found the hyperwarp prompt missing and
#           letters dropped from the HP/FOOD display. In address order:
#             L_0071a4  `mov $0x28,%al ; mov %al,0x31` -- right_margin := 0x28 for the draws.
#             L_0071ab  four times: cursor := (column 0x25 = 37, rows 0x14..0x17 = 20..23) via
#                       L_0053eb, then `mov DS:E5 / DS:4CA / DS:4CB / DS:4CC` and `call L_0055b5`,
#                       which prints the byte as TWO BCD digits. So the readout reads FUEL=08,
#                       not FUEL=8, and the labels' own two leading spaces put their visible text
#                       at column 32 -- one column left of the values' 37 is what makes the pair
#                       line up as `FUEL=08`.
#             L_0071fd  `mov $0x1f,%al ; mov %al,0x31` -- right_margin := 0x1f (31). This is the
#                       byte that keeps the readout whole: the row scroll (asm/5025,
#                       `CL=0 DL=DS:0x31`) runs from column 0 to the margin inclusive, so at 31
#                       it stops one column short of the readout's first visible glyph at 32.
#                       With any wider margin every CMD line that scrolls shaves a column off the
#                       readout, and after two scrolls `  FUEL=6` reads `YUEL=6`.
#             L_007202  cursor := (column 0, row 0x17 = 23) then `ret` -- the CMD prompt prints
#                       from the left edge of the bottom row. Leaving the cursor wherever the last
#                       positioned print put it makes the next `CMD: ` land past the margin, wrap,
#                       and scroll the pane once per prompt.
#           CALLERS: asm/6999's loop head (L_006c7a, before every `CMD: `) and asm/6DFD's
#           hyperwarp flow. It is a per-pass repaint, not an entry-only draw -- that is the
#           repair the frames show and no other routine in the tree does.
# provenance: static, decoded 2026-11-10 from file 0x71a3..0x7211 instruction by instruction;
#           the observable it explains is measured in captures/spice86/g39-rocket-flight
#           (22-right.png, 23-hyperspace.png) and dive-sweep-rate (21-dive-a.png)
# confidence: high for the geometry and the two margin writes; the BCD printer's digit count is
#           read from the frames (FUEL=08) as well as the call to L_0055b5
# func 6FA3..702F  len 008D B  evidence: call+reko
# calls: L_0053eb, L_0055b5
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_6fa3:
L_0071a3:
        nop
L_0071a4:
        mov    $0x28,%al
L_0071a6:
        mov    %al,0x31
L_0071a9:
        mov    $0x0,%bh
L_0071ab:
        mov    $0x25,%bl
L_0071ad:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0071af:
        mov    $0x0,%bh
L_0071b1:
        mov    $0x14,%bl
L_0071b3:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_0071b5:
        call L_0053eb
L_0071b8:
        mov    0xe5,%al
L_0071bb:
        call L_0055b5
L_0071be:
        mov    $0x0,%bh
L_0071c0:
        mov    $0x25,%bl
L_0071c2:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0071c4:
        mov    $0x0,%bh
L_0071c6:
        mov    $0x15,%bl
L_0071c8:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_0071ca:
        call L_0053eb
L_0071cd:
        mov    0x4ca,%al
L_0071d0:
        call L_0055b5
L_0071d3:
        mov    $0x0,%bh
L_0071d5:
        mov    $0x25,%bl
L_0071d7:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0071d9:
        mov    $0x0,%bh
L_0071db:
        mov    $0x16,%bl
L_0071dd:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_0071df:
        call L_0053eb
L_0071e2:
        mov    0x4cb,%al
L_0071e5:
        call L_0055b5
L_0071e8:
        mov    $0x0,%bh
L_0071ea:
        mov    $0x25,%bl
L_0071ec:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0071ee:
        mov    $0x0,%bh
L_0071f0:
        mov    $0x17,%bl
L_0071f2:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_0071f4:
        call L_0053eb
L_0071f7:
        mov    0x4cc,%al
L_0071fa:
        call L_0055b5
L_0071fd:
        mov    $0x1f,%al
L_0071ff:
        mov    %al,0x31
L_007202:
        mov    $0x0,%bh
L_007204:
        mov    $0x0,%bl
L_007206:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_007208:
        mov    $0x0,%bh
L_00720a:
        mov    $0x17,%bl
L_00720c:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_00720e:
        call L_0053eb
L_007211:
        ret
L_007212:
        push   %es
L_007213:
        add    $0x603,%ax
L_007216:
        add    %ax,(%bp,%si)
L_007218:
        or     %ax,(%si)
L_00721a:
        add    %cl,(%bx,%di)
L_00721c:
        push   %es
L_00721d:
        add    $0x3,%al
L_00721f:
        add    (%bp,%di),%al
L_007221:
        or     %al,(%si)
L_007223:
        add    %al,(%bx,%di)
L_007225:
        or     %ax,0x405
L_007229:
        add    (%si),%ax
L_00722b:
        add    $0x506,%ax
L_00722e:
        add    $0x9,%al
