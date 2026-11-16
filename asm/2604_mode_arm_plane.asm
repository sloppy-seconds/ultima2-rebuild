# sub_2604 — mechanical clean (bulk wave); behavior not yet reviewed
# func 2604..2618  len 0015 B  evidence: seed+reko
# calls: L_002804, L_002826, L_002864
# provenance: static (bytes) + live:flight-launched-live, flight-32-live — both dumps read
#           [DS:13] = 0x26, so the mode this arm gates on is observed. The remaining gap in
#           that line is now CLOSED: `captures/spice86/g43-plane-grounded/` walks a mounted
#           plane on the ground and observes the forest reject. Five steps: foot accepts
#           forest, foot steps back onto the pad, `B` sets [DS:13] = 0x26, the grounded plane
#           REFUSES the forest tile east of the pad, and the grounded plane ACCEPTS grass west
#           of it. That last step is the positive control — without it "refused forest" cannot
#           be told from "a grounded plane cannot move". So the forest reject is observed, not
#           inferred. What that capture deliberately does NOT claim is that a breakpoint proved
#           this routine ran: breakpoints at runtime 0x253B and 0x2607 never fired there, even
#           on steps that demonstrably moved, so bp_read at those addresses is not a reliable
#           probe of this path and the attribution rests on the decode plus the mode byte.
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# RENAMED 2026-10-29 from sub_2604, with the behaviour: the PLANE arm of asm/253B's
#           travel-mode dispatch. asm/2545 reaches L_002807 here when [DS:13] == 0x26.
#
#           The body is `call L_002826` (asm/2626, the mounted drain, which itself calls the
#           foot tail and so runs the whole ladder), reject if it returned 0xFF, then
#           `mov 0x29,%al ; and 0x7f ; cmp 0x6 ; je reject`, then accept.
#
#           So the plane's rule is the MOUNTED rule minus forest, and the mounted rule is
#           the foot ladder minus shallow water. It is NOT 'flies over anything except
#           forest', which is what F14 said when this file was read on its own without
#           following the call. The forest reject at runtime 0x2813 is the only tile this
#           arm adds by itself.
#
#           IMPORTANT SCOPE LIMIT, and it is the open question: this arm is the PLANE WALKED
#           ON THE GROUND. While airborne the player is inside asm/38B0's own flight loop,
#           which consults no passability routine at all and charges no food — [DS:13] stays
#           0x26 for both. So 'can the plane cross forest while airborne' is not a question
#           this ladder answers, and the owner's recollection ('plane on land can't traverse
#           forest') is consistent with the rule being the grounded one.
#           SCOPE LIMIT NOW OBSERVED, 2026-11-16: the grounded refusal is confirmed by
#           `captures/spice86/g43-plane-grounded/` (see the provenance line above), and the
#           airborne half was closed statically from asm/38B0 — landing is accepted on grass
#           0x08 only, and the flight loop consults no passability routine. The rule is
#           grounded-only, as the recollection said.
sub_2604:
L_002804:
        jmp L_002864
L_002806:
        nop
L_002807:
        call L_002826
L_00280a:
        cmp    $0xff,%al
L_00280c:
        je L_0027ff
L_00280e:
        mov    0x29,%al
L_002811:
        and    $0x7f,%al
L_002813:
        cmp    $0x6,%al
L_002815:
        je L_0027ff
L_002817:
        jmp L_002804
