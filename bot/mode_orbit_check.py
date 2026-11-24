"""The discriminator, run against the three states that defined it.

This is the check that matters, and it exists because the synthetic tests passed while the
rule was wrong. mode_check.c asserted that the staging pair outranks the dungeon digit and
was green; the dungeon-entry corpus then showed eighteen dungeon frames reading as space.
Synthetic blocks prove a rule is self-consistent. Only the captures prove it is true.

w6-orbit-dive is the capture that established the pad / orbit / landed table
(docs/handoff-space-view.md § 3a). It recorded six `--peek` ranges of the DS segment at each
of three moments, which is enough to rebuild a 1536-byte mirror; the last 80 bytes of the
1616-byte struct are padded with zeros and nothing the mode rule reads lives there.

Expected, straight from the README's own table:
  PAD    sector 41, staging holds the village map          -> site
  ORBIT  sector 41, staging uniformly 0xFF, DS:12 still 04 -> space
  LANDED sector 40, staging holds sector 40's map          -> world

Note PAD and ORBIT differ in the sector by nothing at all. That is the entire difficulty, and
the reason a mode report that only looks at the settlement digit is not merely imprecise but
wrong in a way a bot would act on.
"""
import json
import os
import re
import subprocess
import sys

DS_LEN = 1616
CAP = "captures/spice86/w6-orbit-dive"
CASES = [("PAD.txt", "site"), ("ORBIT.txt", "space"), ("LANDED.txt", "world")]

# Exit 77 is automake's "cannot run"; check_all.sh counts it as a skip. This needs the
# built engine binary and one gitignored capture directory, and a traceback when either is
# absent is not the same verdict as a failure.
if not os.path.exists("engine/u2"):
    print("SKIP: engine/u2 is not built -- run 'make -C engine' first")
    sys.exit(77)
if not os.path.isdir(CAP):
    print("SKIP: %s is not present (captures/ is gitignored)" % CAP)
    sys.exit(77)


def rebuild(path):
    """Reassemble the DS mirror from the `DS:<hexoff> = <hex bytes>` snapshot lines."""
    buf = bytearray(0x600)
    for line in open(path):
        m = re.match(r"DS:([0-9A-Fa-f]+) = ((?:[0-9A-Fa-f]{2} ?)+)", line.strip())
        if not m:
            continue
        off = int(m.group(1), 16)
        data = bytes(int(t, 16) for t in m.group(2).split())
        if off + len(data) > len(buf):
            buf.extend(bytes(off + len(data) - len(buf)))
        buf[off:off + len(data)] = data
    if len(buf) < DS_LEN:
        buf.extend(bytes(DS_LEN - len(buf)))     # tail beyond the recorded ranges
    return bytes(buf[:DS_LEN])


def mode_of(blob):
    open("/var/tmp/w6blob.bin", "wb").write(blob)
    r = subprocess.run(["engine/u2", "--headless", "--ds-json", "/var/tmp/w6blob.bin"],
                       capture_output=True, text=True, timeout=120)
    i = r.stdout.find('{"u2api"')
    if i < 0:
        return None, None, r.stderr[:100]
    obs = json.loads(r.stdout[i:])
    return obs["mode"], obs["state"]["state"].get("sector"), ""


fails = 0
for name, want in CASES:
    blob = rebuild(f"{CAP}/{name}")
    got, sector, err = mode_of(blob)
    staged = all(b == 0xFF for b in blob[0x272:0x472])
    if got != want:
        fails += 1
        print("  FAIL %-7s -> %-8s want %-6s sector=%s staging_all_ff=%s %s"
              % (name[:-4], got, want, sector, "YES" if staged else "no", err))
    else:
        print("  ok   %-7s -> %-8s sector=%-4s staging_all_ff=%s"
              % (name[:-4], got, sector, "YES" if staged else "no"))

# The three must not all answer the same thing. A rule that returned one value for all three
# frames would pass three individual assertions if the expectations were copied wrong, so
# assert the distinctness the capture is named for.
modes = [mode_of(rebuild(f"{CAP}/{n}"))[0] for n, _ in CASES]
if len(set(modes)) != 3:
    fails += 1
    print("  FAIL the three states collapsed to %s" % modes)
else:
    print("  ok   three distinct answers for three distinct states: %s" % modes)

print("\n%d failure(s)" % fails)
sys.exit(1 if fails else 0)
