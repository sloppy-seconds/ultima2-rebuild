# cmd_Q — command handler 'Q': prints "QUIT OR SAVE GAME." then falls through
# name-evidence: string 'QUIT OR SAVE GAME.' at file 0x3FCA; the jump table cs:0730 entry Q
# THE CHAIN (objdump of the bytes 2026-10-29; the body extends past the func line into the
# two routines it falls through, so the guard tests were not visible from this file alone):
#
#   3FDD  mov al,[DS:4A] ; or al,al ; je 3FFA   DS:4A != 0 -> 'ONLY OUTDOORS!'  (in a site)
#   3FFA  mov al,[DS:6D] ; or al,al ; je 4017   DS:6D != 0 -> 'ONLY ON EARTH!'  (asm/3DFA)
#   4017  mov al,[DS:13] ; cmp $0x78 ; jae 4033 DS:13 < 0x78 -> 'ONLY ON FOOT!' (in a vehicle)
#   4033  'ONE MOMENT PLEASE!' ; call asm/22FD ; jmp L_000c33                    -> the save
#
# So the save has three guards and an order, and the order is observable: a character indoors,
# holding boots and aboard a vehicle still gets ONLY OUTDOORS!, because DS:4A is tested first.
# The engine implements all three in u2_overworld_save() behind u2_save_policy; enhanced mode
# drops them and saves anywhere (owner, 2026-10-29). Gate: tests/save_gate.py, CP5 V3.
# Note asm/22FD is called on this path, which is consistent with it being a DS:6D test and not
# the sector store it used to be described as.
# func 3DC7..3DDC  len 022 B  evidence: seed+reko (jump table cs:0730 entry Q)
# provenance: live:play-test (+2 other harvests), static
#           entry executed in 9 of 291 harvests; derived by tools/provenance_from_traces.py
#           the guard chain above is from the raw bytes and from
#           captures/spice86/save-wilderness, where both refusals and the accepted save were
#           screenshotted
# confidence: high
cmd_Q:
        call L_0051df          # str_out — prints the inline string below
        db "QUIT OR SAVE GAME.",0   # string
