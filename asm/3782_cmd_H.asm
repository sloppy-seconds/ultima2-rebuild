# cmd_H — command handler 'H': prints "HYPER WHAT?" then returns to the
#           dispatcher. Trailing 0xe8 stop-truncs into the next fragment (3794).
# func 3782..3793  len 018 B  evidence: seed+reko (jump table cs:0730 entry H)
# provenance: live:cmd-letters-20261025; static
#           entry executed in 2 of 258 harvests; derived from ExecutedInstructions in
#           captures/spice86/*/spice86dumpExecutionFlow.json. Was recorded as never executed
#           against a 60-harvest corpus; the corpus grew and this line was not refreshed, and
#           the capture on 2026-10-25 confirmed it directly (CMD log reads `HYPER WHAT?`).
# confidence: high
cmd_H:
        call L_0051df          # str_out — prints the inline string below
        db "HYPER WHAT?",0     # string
        jmp L_000c33           # return to dispatcher
