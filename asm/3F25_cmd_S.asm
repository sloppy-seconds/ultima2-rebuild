# cmd_S — command handler 'S': prints "STEAL DIRECT-" then falls through
# func 3F25..3F35  len 017 B  evidence: seed+reko (jump table cs:0730 entry S)
# provenance: live:cmd-letters2-20261025; static
#           entry executed in 3 of 274 harvests; confirmed by the capture of 2026-10-25. The
#           capture proves the entry ran and nothing more -- what the following direction did
#           with the aim is not established by these two harvests. Was recorded as never
#           executed against a 60-harvest corpus that has since grown.
# confidence: high
# name-evidence: read 2026-10-29, whole chain, from the bytes.
#           The fall-through is the aim gate: asm/3F36 is a single forced-keep `e8`, the opcode
#           of `call asm/2362`, the same gate cmd_A, cmd_U and cmd_T use. This is the second
#           time that storage choice hid a member of the family (see asm/405D), and it is why
#           this file used to say "falls through" and leave it there.
#           The ray is the same two cells as TRANSACT and nothing else: asm/3F37 puts the near
#           cell in DS:23/24, asm/3F59 requires a sign tile (high bit) or the counter tile
#           0x7C, and asm/3F5D steps once more and requires the merchant marker 0x68. So STEAL
#           aims at a shop, not at a person, and there is no creature scan anywhere in the
#           chain. The outcome table is in asm/3F5D_steal_attempt.
cmd_S:
        call L_0051df          # str_out — prints the inline string below
        db "STEAL DIRECT-",0   # string
