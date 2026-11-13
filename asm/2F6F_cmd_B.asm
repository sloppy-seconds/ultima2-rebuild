# cmd_B — command handler 'B': prints "BOARD" then falls
#           through into the following map fragment
# func 2F6F..2F77  len 009 B  evidence: seed+reko (jump table cs:0730 entry B)
# provenance: live:airport32 (+3 other harvests), static
#           entry executed in 15 of 258 harvests; derived by tools/provenance_from_traces.py
# confidence: high (handler role proven; message literal)
cmd_B:
        call L_0051df          # str_out — prints the inline string below
        db "BOARD",0        # string
