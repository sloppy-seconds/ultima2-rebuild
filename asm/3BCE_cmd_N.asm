# cmd_N — command handler 'N': prints "NEGATE TIME" then falls through
# func 3BCE..3BDC  len 015 B  evidence: seed+reko (jump table cs:0730 entry N)
# provenance: live:cmd-nr-live, static
#           entry executed in 2 of 235 harvests; derived by tools/provenance_from_traces.py
#           (first execution on record 2026-10-29, captures/spice86/cmd-nr-live; before that this
#           was the one command letter never seen running in any harvest)
# confidence: high (message literal; fallthrough into next fragment)
# name-evidence: the byte it gates on is DS:E3, and DS:E3 is not a charge counter of its own.
#           items_held is 16 slots from DS:D6, so DS:E3 is items_held[13], and the name table at
#           file 0x501D calls slot 13 STRANGE COIN. NEGATE TIME therefore spends a strange coin:
#           no coin -> " HOW?" + "YOU'RE NOT EINSTEIN"; a coin -> fall into asm/3C04, which
#           decrements the count in packed decimal and stores 0x14 into DS:26A. Both branches are
#           on a frame in the capture named above, and the seeded value of 0x01 that looked like a
#           mystery charge was maxout handing out one of every item.
# confidence: high (both branches observed executing, with memory read either side)
cmd_N:
        call L_0051df          # str_out — prints the inline string below
        db "NEGATE TIME",0     # string
