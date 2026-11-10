# speed_delay_lookup — read one of the 20 machine-scaled delay slots into %bx
# CORRECTION 2026-11-10, measured: the value is DISCARDED at every call site that can be read.
#           The header below claims the slots are key-wait timeouts. That was inference from the
#           shape of the sites, and three of them have now been read end to end:
#             asm/38B0_cmd_L runtime 39B1 — `mov $0x1c,%bx ; call L_000911` then `call L_00514d`.
#                   key_poll (asm/4F4D) pushes %bx at entry and pops it at runtime 4FDD and never
#                   reads it; its only internal %bx is `mov $0x6,%bx` for the arrow table.
#             asm/2557_mode_arm_horse — the lookup sits inside a fixed nested busy loop (di from
#                   0xFF, inner si from 0x20) and the next instruction is `dec %di`.
#             asm/421A — `mov $0x4,%bx ; call L_000911` after a di/si busy delay, and the flush at
#                   asm/4AAD_flush_staged_cells.asm also saves and restores %bx without reading it.
#           So: the rescale machinery, DS:74, the F7/F8 adjustment and the 20 slots are all real and
#           measured, and nothing in this binary has yet been shown to consume a slot. Do not wire a
#           slot to a timeout, an animation rate, or anything else, on the strength of this header's
#           original claim. The descent animation's rate is the render's cost, not a slot — see
#           captures/spice86/dive-sweep-rate/README.md.
# name-evidence: `mov 0x580(%bx),%bx` — %bx goes in as a byte offset into the word table at
#           DS:580 and comes out as the value stored there. The table is written by the rescale
#           routine at asm/06ED..0710 (`mov %ax,0x580(%bx)`), which fills offsets 0x00..0x26 in
#           steps of 2, i.e. exactly 20 slots. Callers set %bx to a small even constant first —
#           observed 0x00, 0x08, 0x0a, 0x14, 0x16, 0x18 — which is an index, not a value.
# WHY IT IS WORTH A NAME: this is the single read path for the whole speed system, and it is
#           called from FOURTEEN routines (asm/022E, 0696, 08C8, 2557, 2587, 2593, 277B,
#           38B0_cmd_L, 421A, 5F08, 6999_space_status, 6DFD_hyperwarp_flow, 7030_space_collision,
#           72A0). Whatever the game does at animation pace — the hyperwarp countdown, the space
#           status crawl, key-wait timeouts — it takes its timing from here.
# what the value is used as: a KEY-WAIT TIMEOUT, not an animation delay. The tell is what
#           follows the call at the sites that can be read: `call L_000911` then a wait whose
#           result is tested with `cmp $0xff,%ah` -- AH=0xFF is this project's own "no key"
#           marker, and the original's INT 16h AH=01h poll uses ZF the same way. So the slots
#           bound how long the game waits for input before moving on, which is exactly why the
#           measured values span 47 to 64,581: a title screen may linger and a collision check
#           may not.
# SLOT USAGE, from the 14 callers (bx is a BYTE offset; slot = bx/2):
#           bx=0x00  slot 0   (47)     asm/08C8, asm/277B (277B then `cmp $0xff,%ah`)
#           bx=0x02  slot 1   (5786)   asm/38B0_cmd_L — a SECOND wait in the same handler, after
#                                       the first one at bx=0x1c; the `mov $0x2,%bx` is a
#                                       forced-keep, which is why the 2026-10-28d pass missed it
#           bx=0x04  slot 2   (64581)  asm/421A
#           bx=0x06  slot 3   (10090)  asm/72A0 — hidden because asm/72A0 renders its body as
#                                       `db` (branch targets land inside the runs), so the
#                                       `mov $0x6,%bx` is bytes, not a mnemonic
#           bx=0x08  slot 4   (23)     asm/022E   [value corrected 2026-10-28e: the 2026-10-28d
#                                       pass wrote 10090 here, which is slot 3's and slot 7's
#                                       value; slot 4 is 23 in the measured table]
#           bx=0x0a  slot 5   (23)     asm/0696
#           bx=0x0e  slot 7   (10090)  asm/72A0 — same reason as slot 3
#           bx=0x10  slot 8   (78)     asm/2557 (then `dec %di`), asm/2587
#           bx=0x12  slot 9   (32290)  asm/2593
#           bx=0x14  slot 10  (3784)   asm/6999_space_status, asm/7030_space_collision,
#                                       asm/6DFD_hyperwarp_flow (one of its six waits)
#           bx=0x16  slot 11  (2522)   asm/6DFD_hyperwarp_flow (three of its waits)
#           bx=0x18  slot 12  (4540)   asm/6DFD_hyperwarp_flow (one of its six waits)
#           bx=0x1a  slot 13  (4036)   asm/6DFD_hyperwarp_flow (two waits)
#           bx=0x1c  slot 14  (16145)  asm/38B0_cmd_L (two waits)
#           bx=0x1e  slot 15  (7)      asm/5F08 — **and asm/5F08 WRITES this slot before reading
#                                       it**: `mulw 0x74 ; divw 0x554` twice (the same speed
#                                       scaling asm/06ED does), guarded by
#                                       `cmpw $0x1800,0x74 ; jb`, then `div` by 5000, then
#                                       `mov %ax,0x580(%bx)`. So slot 15 is not a table entry at
#                                       all at that point in play; it is a runtime parameter that
#                                       happens to live in the table. Found 2026-10-28e.
#           slots 6, 16, 17, 18, 19 have no caller found yet (slots 1, 3, 7, 12, 13 and 15 were
#           found 2026-10-28e; 12 and 13 are in asm/6DFD, which has six waits and was only
#           partly read before).
#           WHY A GREP MISSES CALLERS: several of the slots the 2026-10-28d pass left unknown are
#           now known, and none of them was findable by looking for `mov $N,%bx` — one is a
#           forced-keep `.byte 0xbb,0x02,0x00` and two are `db 0xbb,...` inside asm/72A0. A
#           slot census has to decode the bytes at each call site, not grep for a mnemonic.
# provenance: static, decoded 2026-10-28d from the listing; bytes proven by tools/check_func.py after the merge. The
#           DS:74 value itself is live: 0x3A17, static across 103 samples in
#           captures/spice86/g30-clockcal/.
# confidence: high for the mechanism and for the timeout reading of the value; the per-slot meaning of each of the 20
#           offsets is PARTIALLY known (see SLOT USAGE above) and the reason each site chose
#           its length is not
# label: kept as sub_0711 on purpose — labels are body and CP0 md5s them
# func 0711..0714  len 0004 B  evidence: seed+call+reko
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_0711:
L_000911:
        mov    0x580(%bx),%bx
