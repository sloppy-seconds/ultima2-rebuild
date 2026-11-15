# cmd_M — command handler 'M': prints "MAGIC SPELL READY #", reads one decimal digit,
#           stores it at DS:63 as the readied spell slot, prints "SPELL READY => " followed
#           by that spell's NAME, and returns to the dispatcher.
# CORRECTION 2026-10-25: the old header guessed "(call sub_508c = numeric out?)" and the
#           inline comment called `adc $0x24,%al` an "ASCII digit bias for numeric out".
#           It is neither. asm/508C is the INDEXED NAME POOL printer — it takes the entry
#           index in AL — and 0x24 is the SPELLS group base, the same constant cmd_C uses two
#           instructions after printing the spell name. So M echoes the NAME of the spell it
#           just readied: `MAGIC SPELL READY #3` then `SPELL READY => UP LADDER`. Nothing
#           numeric is printed, and 0x24 is not a digit bias. Getting this wrong would put a
#           number where the game puts a word, which is the kind of small confident mistake
#           this tree keeps having to take back.
# func 3B92..3BCD  len 060 B  evidence: seed+reko (jump table cs:0730 entry M)
# provenance: live:cmd-letters2-20261025; static
#           entry executed in 8 of 263 harvests; confirmed by the capture of 2026-10-25 (CMD
#           log reads `MAGIC SPELL READY #` and then waits for a spell number). Was recorded as
#           never executed against a 60-harvest corpus that has since grown.
# confidence: high for the handler's own five steps; the digit reader's own DOUBT list in
#           asm/2028_read_decimal_digit.asm still stands and is inherited by this handler
# notes: slice mis-decoded 0x3da8 as add+jl (swallowed the call opcode); true
#        read is NUL terminator + call 0x2228. Trailing 0xe8 stop-truncs into
#        next fragment (3BCE).
cmd_M:
        call L_0051df          # str_out — prints the inline string below
        db "MAGIC SPELL READY #",0   # string
        call L_002228
        mov    %al,0x63
        call L_0051df          # str_out — prints the inline string below
        db "SPELL READY => ",0 # string
        mov    0x63,%al
        clc
        adc    $0x24,%al       # ASCII digit bias for numeric out
        call L_00528c
        jmp L_000c33           # return to dispatcher
