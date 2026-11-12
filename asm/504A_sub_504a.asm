# sub_504a — the text printer's column advance and right-margin wrap
# func 504A..5056  len 000D B  evidence: seed+reko
# confidence: medium (bytes proven by check_func.py; behaviour seen on frame)
# provenance: captures/spice86/g76-zwrap (Z status screen, maxout char, 2026-11-12);
#   engine mirror engine/src/core/seam.c char_print; gated by engine/tests/status_check.c
#   Z stat-line assertions (TORCHES-50, KEYS-20, ... ending at column 38).
# behaviour: this is the tail of the char printer (asm/5003..5057). asm/503D has
#   just written the glyph at the cursor via INT 10h AH=09; here the column is
#   advanced (incb 0x2e) and compared to the right margin DS:31 (mov 0x31,%al;
#   cmp 0x2e,%al; jb). The row wraps when the column EXCEEDS the margin, so the
#   glyph that lands at column == margin is kept and the next one starts the
#   following row. The Z status screen proves this: its wrapped item list
#   reconstructs whole words across every break (B|OWS, BOO|TS, GREEN GE|MS,
#   TRI-L|ITHIUMS) with no glyph lost, and its stat lines end exactly on the
#   margin column with the last glyph present. The Z screen's margin is 0x26=38
#   (asm/4568), an on-screen column, so the wrap never reaches the off-screen
#   column 40 where a glyph would be lost. See docs/needs-gameplay.md G76.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_504a:
L_00524a:
        incb   0x2e
L_00524e:
        mov    0x31,%al
L_005251:
        cmp    0x2e,%al
L_005255:
        jb L_00520d
