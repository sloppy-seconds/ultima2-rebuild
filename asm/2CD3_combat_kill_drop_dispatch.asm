# sub_2ce5 — mechanical clean (bulk wave); behavior not yet reviewed
# name-evidence: four type compares (0x60, 0xFC, 0xF0, 0xF8) each tail-jumping to
#           combat_kill_reward at L_002f8c -- which is what makes them one routine
#           rather than four helpers
# func 2CD3..2D21  len 004F B  evidence: seed+reko+live (span is the merged dispatch, four arms;
#           the Phase 2 map's own rows for the absorbed fragments are gone)
# provenance: static:bytes (whole dispatch read from the listing) + live:baseline-fight, play-test-live, play-test -- 0x2d07 is in ExecutedInstructions of all three, reached by fall-through from 0x2d05.
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# RESOLVED 2026-11-09: this is the kill-drop DISPATCH, and it is the reason asm/2D07
#           looked orphaned. Body is `cmp $0xfc,%al ; jne L_002f22` -- 0xFC is the THIEF
#           type (asm/0ADB spawn arms, type 0xFC with HP 0x20), so a thief falls through
#           into asm/2CE9 and every other type jumps to the type table at asm/2D22. Both
#           arms end in a tail `jmp L_002f8c` into combat_kill_reward, which is why the
#           reward executes more often than the thief arm does (17 vs 8 across the
#           captured flows). Renamed from sub_2ce5 on the strength of this.
# execution evidence: captures/spice86/baseline-fight, play-test-live, play-test --
#           0x2d07 appears in ExecutedInstructions 8 times, reached by FALL-THROUGH from
#           0x2d05. There is no `call` to asm/2D07 anywhere because none was ever needed,
#           which is exactly what every static pass over this tree was looking for.
# MERGED 2026-11-09: this file is asm/2CE5 + 2CE9 + 2CF2 + 2CF6 + 2D00 + 2D03 + 2D07 +
#           2D17, which the Phase 2 map had chopped into eight fragments of 3-16 bytes.
#           The chop is why the routine was unreadable for as long as it was: the entry
#           into the item grant is a FALL-THROUGH from asm/2D03's `je`, so no call edge
#           exists for any tool to find, and each fragment looked like an unrelated stub.
#           The tiling is contiguous and byte-preserving; assemble_clean.sh is the proof.
#
# Full executed path (captures/spice86/baseline-fight, play-test-live, play-test):
#   2CE5  cmp $0xfc,%al ; jne L_002f22   thief? else the type table at asm/2D22
#   2CE9  save the creature slot to DS:1F; call rand
#   2CF2  cmp $0x40,%al ; jae skip       25% gate on the whole thief reward
#   2CF6  tools += 1   (DS:66, packed decimal, no clamp)
#   2D00  call rand again -- a SECOND draw, so the joint distribution is 25% x 1/15
#   2D03  and $0xf,%al ; je skip         slot index 0..15; index 0 excluded
#   2D07  items_held[idx] += 1  (DS:D6+idx, packed decimal, no clamp, wraps 99->00)
#   2D17  restore %di from DS:1F ; jmp L_002f8c (combat_kill_reward)
#
# Slot 0 is the RING, which asm/3C2E pays for gold instead, so the `je` at 2D05 keeping
# it out of the random pool is deliberate and not an off-by-one. Slots 1..15 are equally
# likely, which makes this the only source in the binary for the ANKH at DS:DD and
# therefore the only route to boarding the rocket for a character that did not start
# with one.
#
# confidence: high. Bytes proven by check_func.py over the merged span; the dispatch, the
#           two rolls, the slot mask and the tail jump are all read from the listing, and
#           the whole path appears in ExecutedInstructions.
# EXTENDED 2026-11-09: the merge now also covers asm/2CD3 and asm/2CD8, which are the FIRST
#           arm of this chain and were still chopped off from it. The dispatch is four arms
#           and starts at 2CD3, not 2CE5:
#             2CD3  cmp $0x60,%al ; jne L_002ee5   -> keys += 2   (asm/2CD8)
#             2CE5  cmp $0xfc,%al ; jne L_002f22   -> tools + a random item
#             2D22  cmp $0xf0 -> HELM + torches ; cmp $0xf8 -> WAND or STAFF
#             2D8B  nop                            -> everything else grants nothing
#           Each arm tail-jumps to combat_kill_reward at L_002f8c, which is what makes them
#           one routine rather than four helpers.
#
# Type 0x60 is the GUARD. state.h already records the type byte as "0x60 Guard, 0x68
# Merchant, 0xF0 Fighter" and notes it is NOT the art index, and the bestiary's gamercorner
# row for Guard carries the special "Drops 2 keys" -- which is exactly what asm/2CD8 does.
# Now also OBSERVED: 0x60 fills live mon_type slots in 105 of the 3055 captured DS dumps,
# 210 slot-occurrences, every one of them in sector 03 (DS:49=0, DS:4A=3) alongside 0x68 at
# the same count -- the castle town, which is where captures/spice86/castle-03-live is from
# and where Townsfolk belong.
sub_2cd3:
L_002ed3:
        nop
L_002ed4:
        cmp    $0x60,%al
L_002ed6:
        jne L_002ee5
sub_2cd8:
L_002ed8:
        clc
L_002ed9:
        mov    0x65,%al
L_002edc:
        adc    $0x2,%al
L_002ede:
        daa
L_002edf:
        mov    %al,0x65
L_002ee2:
        jmp L_002f8c
sub_2ce5:
L_002ee5:
        cmp    $0xfc,%al
L_002ee7:
        jne L_002f22
sub_2ce9:
L_002ee9:
        .byte 0x8b,0xdf  # forced-kept: gas re-encodes 'mov    %di,%bx' differently
L_002eeb:
        mov    %bl,0x1f
L_002eef:
        call L_005417
sub_2cf2:
L_002ef2:
        cmp    $0x40,%al
L_002ef4:
        jae L_002f00
sub_2cf6:
L_002ef6:
        clc
L_002ef7:
        mov    0x66,%al
L_002efa:
        adc    $0x1,%al
L_002efc:
        daa
L_002efd:
        mov    %al,0x66
sub_2d00:
L_002f00:
        call L_005417
sub_2d03:
L_002f03:
        and    $0xf,%al
L_002f05:
        je L_002f17
sub_2d07:
L_002f07:
        mov    $0x0,%ah
L_002f09:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_002f0b:
        clc
L_002f0c:
        mov    0xd6(%di),%al
L_002f10:
        adc    $0x1,%al
L_002f12:
        daa
L_002f13:
        mov    %al,0xd6(%di)
sub_2d17:
L_002f17:
        mov    $0x0,%bh
L_002f19:
        mov    0x1f,%bl
L_002f1d:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002f1f:
        jmp L_002f8c
L_002f21:
        nop
