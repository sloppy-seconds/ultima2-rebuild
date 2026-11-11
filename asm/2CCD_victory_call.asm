# victory_call — the only call site of the victory sequence
# name-evidence: `call L_0074a0` (the victory sequence, at the start of asm/72A0) followed by
#           `jmp L_000c33`. Six bytes, and it has no caller anywhere in asm/: it is reached only
#           by FALL-THROUGH from asm/2CB5_combat_kill_minax_gate when the killed creature's
#           type is 0x40. That is why every static pass missed it -- there is no call edge to
#           find, exactly the asm/2D07 failure one routine earlier in the same address range.
# RESOLVED 2026-11-09: renamed from sub_2ccd. The body is:
#             2ECD  call L_0074a0     ; MINAX IS DEAD!! ... YOU HAVE SAVED THE UNIVERSE,
#                                     ; AND COMPLETED ULTIMA ][! SEEK
#                                     ; NOW TO CONQUER WICKED EXODUS,
#                                     ; FOUND IN ULTIMA ]I[-D ]II[-P!
#             2ED0  jmp L_000c33      ; which reads DS:26B and branches on it
#           The tail jump means the victory sequence does not return: control leaves this
#           routine for good, which is consistent with an ending rather than an event.
# func 2CCD..2CD2  len 0006 B  evidence: seed+reko
# confidence: medium (the bytes are solid; the callee's meaning is decoded statically and has
#           never executed in any capture)
#           ** CORRECTED 2026-11-12: it has executed.** Owner play session in the original
#           (captures/spice86/minax-fight-live/). The gate at asm/2CB5 fired on a killed
#           type-0x40 creature, this routine's `call L_0074a0` ran, and the whole ending
#           followed on screen -- `MINAX IS DEAD!!`, the progressive fade, the rocket flight,
#           `YOU HAVE SAVED THE UNIVERSE`. The confidence line above is left as written
#           because it records what was true when the header was cut; this line supersedes it.
#           Note the fall-through warning below is now load-bearing in a new way: the only
#           witness this project has of the fall-through firing is a person watching a screen,
#           because the trace window in the noVNC container rolls past a one-shot event in
#           seconds and this routine's addresses were absent from a dump taken immediately
#           after the kill. See the retention caveat in docs/spice86-capture.md.
# provenance: static, 2026-11-09. Discovered-but-not-executed: the strings it prints appear in
#           CFG blocks (branch successors Spice86 expanded statically) but in no
#           ExecutedInstructions. See AGENTS.md on CFG blocks not being proof of execution.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2ccd:
L_002ecd:
        call L_0074a0
L_002ed0:
        jmp L_000c33
