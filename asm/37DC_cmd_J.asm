# cmd_J — command handler 'J': prints "JUMP...WHEE..." then returns to the
#           dispatcher (jmp 0xc33). Trailing 0xe8 is a stop-trunc call opcode
#           whose operand lives in the next map fragment (37F1).
# func 37DC..37F0  len 021 B  evidence: seed+reko (jump table cs:0730 entry J)
# provenance: live:cmd-letters-20261025; static
#           entry executed in 2 of 283 harvests; confirmed by the capture of 2026-10-25 (CMD
#           log reads `JUMP...WHEE...`). Was recorded as never executed against a 60-harvest
#           corpus that has since grown without this line being refreshed.
# confidence: high
# INTENTIONAL, not unfinished: the owner reports the shipped manual describing J as letting the
#           character "jump up and down" to release frustration, with no effect on obstacles,
#           terrain or any mechanic. The bytes agree and are stronger — three instructions, no
#           write of any kind, so there is nothing here to touch a mechanic even by accident.
#           Recorded in docs/third-party-claims.md § 1. Do not "fix" the missing jump.
cmd_J:
        call L_0051df          # str_out — prints the inline string below
        db "JUMP...WHEE...",0   # string
        jmp L_000c33           # return to dispatcher
