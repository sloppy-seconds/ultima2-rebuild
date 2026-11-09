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
# provenance: static, 2026-11-09. Discovered-but-not-executed: the strings it prints appear in
#           CFG blocks (branch successors Spice86 expanded statically) but in no
#           ExecutedInstructions. See AGENTS.md on CFG blocks not being proof of execution.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2ccd:
L_002ecd:
        call L_0074a0
L_002ed0:
        jmp L_000c33
