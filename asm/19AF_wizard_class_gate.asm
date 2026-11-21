# wizard_class_gate — the magic shoppe's entry: only a Wizard may buy wizard spells
# func 19AF..19B2  len 0004 B  evidence: seed+reko
# calls: L_00527a
# name-evidence: this file is the entry point and the fragment after it carries the test —
#           runtime 19B3 is `mov 0x47,%al ; cmp $0x2,%al ; je L_001bd7`, and L_001bd7 is the
#           magic shoppe menu (the `MUSERREF OLDUM!` / `1-LIGHT ... 4-M.M., 5-BLINK, 6-KILL`
#           catalogue inside asm/19D7). Falling through reaches
#           asm/19BA_not_a_wizard_msg. [DS:47] is the class byte, so this is the wizard half
#           of the pair whose cleric half is asm/1841_cleric_class_gate; § 4.7 of
#           docs/archive/handoff-commerce-transact.md listed the tested field as still unnamed and it
#           is DS:47.
# note: one of the eight destinations of asm/1590_transact_shop_selector — the west/south
#           cell of a non-town site. The `call L_00527a` here is the same screen-preparation
#           call the cleric entry makes two instructions later.
# provenance: static
#           entry never appears in ExecutedInstructions of any harvest; derived by
#           tools/provenance_from_traces.py. The magic shoppe behind it has never been reached
#           in a capture either — the gate is read, not observed.
# confidence: high for the entry and the call; the class test itself lives in the next tiling
#           fragment and is cited above rather than here
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_19af:
L_001baf:
        nop
L_001bb0:
        call L_00527a
