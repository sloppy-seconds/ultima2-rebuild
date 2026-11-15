# cmd_A — command handler 'A': prints "ATTACK--" then falls into sub_2b9b
# func 2B8F..2B9A  len 012 B  evidence: seed+reko (jump table cs:0730 entry A)
# provenance: live:airport (+14 other harvests), static
#           entry executed in 35 of 263 harvests; derived by tools/provenance_from_traces.py
# confidence: medium (handler role proven by jump table; string content literal)
cmd_A:
        call L_0051df          # str_out — prints the inline string below
        db "ATTACK--",0        # string
