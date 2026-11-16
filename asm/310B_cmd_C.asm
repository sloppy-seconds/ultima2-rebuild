# cmd_C — command handler 'C' (CAST): prints "CAST-" and jumps to the cast flow at
#           runtime 0x3153. That is the whole handler — three instructions.
# CORRECTION 2026-10-25, and it removes a question that was about to cost a capture.
#           The previous version of this header said CAST "sets ES to video (0xB800) then
#           0xBA00 calling sub_3339 between, runs a timed wait, prints PICXXX". Two things
#           are wrong about that, and they are wrong independently:
#           1. That block is UNREACHABLE from cmd_C. The `jmp L_003353` is unconditional and
#              lands at runtime 0x3153, past it, and nothing anywhere else jumps into it —
#              no call, no branch-table slot, no fall-through, because the jmp sits in front
#              of it. It is dead bytes inside this file's declared span.
#           2. `PICXXX  ` is not printed. There is no str_out call before it; it is bare
#              inline data. With picdng / picdra / picmin / picout / picspa / pictwn sitting
#              in the pack as sidecar files, PICXXX reads as a picture FILENAME template, not
#              a message. Nobody should cite it as game text again.
#           Note the label base, because it is what made the old header wrong: L_003353 is a
#           FILE offset, so the target is runtime 0x3153, not 0x3353. Reading it as a runtime
#           address puts the jump inside cmd_D's "DESCEND" string, which is nonsense and was
#           presumably why the block got described as part of CAST's flow.
# WHAT CAST ACTUALLY DOES (asm/3139_cast_spell_flow.asm from runtime 0x3153): print the
#           readied spell's NAME from pool[0x24 + DS:63], require DS:D7 + DS:D8 non-zero
#           (NEED WAND OR STAFF!), require DS:B6[DS:63] non-zero (NO SPELL!), decrement that
#           count by one in packed decimal, require DS:4A >= 4 (else -FAILED!), then dispatch
#           on DS:63. No key read, no direction, no cursor, anywhere in that path.
#           This settles the open question the 2026-10-25 capture left: the arrow sent after
#           `CAST-` was never consumed by CAST, so the walk loop took it as movement and the
#           EAST--INVALID MOVE! was the ordinary step path. The seed was on the world map,
#           where DS:4A == 0 and -FAILED! is the only outcome the gates allow.
# func 310B..3138  len 046 B  evidence: seed+reko (jump table cs:0730 entry C)
# provenance: live:cmd-letters2-20261025; static
#           entry executed in 10 of 272 harvests; confirmed by the capture of 2026-10-25.
#           The capture's open question (was the aim gate entered?) is CLOSED 2026-10-25
#           without a re-run: there is no aim gate. See above.
#           Was recorded as never executed against a 60-harvest corpus that has since grown.
# confidence: high for the handler and the gate order (all read directly off the bytes);
#           the per-spell EFFECTS are dungeon-side and dungeons are parked (AGENTS.md § 3)
# notes: slice mis-decoded the mov/call chain (cmp $0xb890...); bytes identical
cmd_C:
        call L_0051df          # str_out — prints the inline string below
        db "CAST-",0           # string
        jmp    L_003353
        nop
        mov    $0xb800,%ax     # video segment
        call L_003339
        mov    $0xba00,%ax     # game data segment
        call L_003339
        mov    $0x28,%ah
        mov    $0x4000,%cx
        mov    $0x0,%dx
        call L_0054ca          # timed wait
        db "PICXXX  "          # string (no NUL: continues into next fragment)
        jmp L_000c33           # return to dispatcher
