# cmd_E — command handler 'E': prints "ENTER" then falls
#           through into the following map fragment
# func 3389..3391  len 009 B  evidence: seed+reko (jump table cs:0730 entry E)
# provenance: live:airport (+4 other harvests), static
#           entry executed in 24 of 228 harvests; derived by tools/provenance_from_traces.py
# confidence: high (handler role proven; message literal)
cmd_E:
        call L_0051df          # str_out — prints the inline string below
        db "ENTER",0        # string
