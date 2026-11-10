# cmd_K — command handler 'K' (KLIMB): one `e8`, with everything else in the fragments after
#           it. Read together, and with asm/3812, asm/3823 and asm/3381, the command is:
#             print "KLIMB"
#             if [DS:4A] not in {4,5}   -> print "-WHAT?", cmd_done
#             if [DS:14] & 0x10 == 0    -> print "-WHAT?", cmd_done
#             if [DS:4A] == 4           -> [DS:25] += 1  (asm/3381, no bound check)
#             else                      -> [DS:25] -= 1  (asm/3823, and js exits the site)
#             print CR " TO LEVEL " + [DS:25] as two packed-decimal digits
# name-evidence: the jump-table entry for 'K' and the text `KLIMB` in asm/37F2. The 0x10 cell
#           feature is the UP ladder, the exact mirror of the 0x20 DESCEND tests in asm/334D,
#           and the two commands swap which way the floor moves depending on the site kind —
#           tower climbs up, dungeon climbs toward the surface. That symmetry is what makes the
#           pair readable; neither one is decodable alone.
# func 37F1..37F1  len 001 B  evidence: seed+reko (jump table cs:0730 entry K)
# provenance: live:airport32 (+2 other harvests), static
#           entry executed in 3 of 226 harvests; derived by tools/provenance_from_traces.py
# confidence: high (handler role proven; split is a map-boundary artifact)
# notes: stop-trunc — do not "fix" to `call`; operand bytes are outside range
#           The spelling is the binary's, not ours: KLIMB with a K, in the same file as the
#           `-WHAT?` refusal. Same convention as the name pool's `KLIMB` entry, if one exists —
#           check rather than normalise it to CLIMB.
cmd_K:
        .byte 0xe8             # forced-kept: stop-trunc call   0x51df
