# cmd_U — command handler 'U': prints "UNLOCK DIRECTION-" then falls through
# The prompt is real and it is a RACE, not a menu. The fall-through calls the direction
#   reader at runtime 2370..2392, which polls for a key with AH == 0xFF (a BIOS extended
#   scancode) and matches AL against the four arrow scancodes held in DS:0x26E..0x271; on
#   success it prints the direction and sets the DS:19/DS:1A deltas, and on counter wrap it
#   prints PASS and leaves DS:19 at zero, which asm/4309 then refuses. Observed live
#   2026-10-28: `U` then a one-second pause then an arrow gave "UNLOCK DIRECTION-Pass" and
#   the arrow was eaten as an ordinary move; `U` and the arrow in one burst with a 0.05 s gap
#   unlocked the door. An agent driving this must send the pair together.
# func 42F1..4305  len 021 B  evidence: seed+reko (jump table cs:0730 entry U)
# provenance: live:airport32 (+2 other harvests), static
#           entry executed in 7 of 271 harvests; derived by tools/provenance_from_traces.py
# confidence: high
cmd_U:
        call L_0051df          # str_out — prints the inline string below
        db "UNLOCK DIRECTION-",0    # string
