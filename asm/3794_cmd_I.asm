# cmd_I — command handler 'I' (IGNITE TORCH): one `e8`, with the operand and the text in
#           asm/3795 and the whole body in asm/37AE. Read together the command is:
#             print "IGNITE TORCH"
#             if [DS:4A] < 4            -> cmd_done, no effect at all
#             if [DS:64] == 0           -> print CR "NONE OWNED!", cmd_done
#             else packed-decimal [DS:64] -= 1 ; [DS:26] = 0x96 ; cmd_done
# name-evidence: the jump-table entry for 'I', and the text `IGNITE TORCH` in the fragment this
#           one-byte range cuts off. The site-kind gate is the same `cmp $0x4` DESCEND and KLIMB
#           use, and [DS:26] = 0x96 is the same constant the LIGHT spell writes in asm/3139, so
#           a torch and that spell share one light counter rather than running in parallel.
# func 3794..3794  len 001 B  evidence: seed+reko (jump table cs:0730 entry I)
# provenance: live:airport (+4 other harvests), static
#           entry executed in 17 of 265 harvests; derived by tools/provenance_from_traces.py
# confidence: high (handler role proven; split is a map-boundary artifact)
# notes: stop-trunc — do not "fix" to `call`; operand bytes are outside range
#           The emptiness of the overworld arm is the behaviour, not a stub: between the gate and
#           the return there are three instructions and one store, and none of them can touch a
#           mechanic the player is standing in. Do not "improve" it by letting a torch be lit
#           outdoors.
cmd_I:
        .byte 0xe8             # forced-kept: stop-trunc call   0x51df
