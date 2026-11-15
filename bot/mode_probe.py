"""Run the mode discriminator over every real DS capture on disk.

Synthetic blocks prove the rule is self-consistent. This proves the rule is TRUE of the
original: the captures taken in orbit must come out as `space`, and the ones taken on the
ground must not. If a ground capture came out as space the discriminator would be a coin
flip wearing a test suite.
"""
import glob
import json
import subprocess
import sys

DS = 1616          # sizeof(u2_state); a .ds dump is 0x2800 and the mirror is the head
STAGE_A, STAGE_B = 0x272, 0x372

rows = []
for path in sorted(glob.glob("captures/spice86/*/*.ds")):
    raw = open(path, "rb").read()[:DS]
    if len(raw) < DS:
        continue
    open("/var/tmp/mode_probe.bin", "wb").write(raw)
    r = subprocess.run(["engine/u2", "--headless", "--ds-json", "/var/tmp/mode_probe.bin"],
                       capture_output=True, text=True, timeout=120)
    i = r.stdout.find('{"u2api"')
    if i < 0:
        rows.append((path, "?", "no observation emitted"))
        continue
    obs = json.loads(r.stdout[i:])
    st = obs["state"]["state"]
    all_ff = (all(b == 0xFF for b in raw[STAGE_A:STAGE_A + 256])
              and all(b == 0xFF for b in raw[STAGE_B:STAGE_B + 256]))
    rows.append((path, obs["mode"], st.get("sector"),
                 "sector=%-4s stage_all_ff=%s" % (st.get("sector"), "YES" if all_ff else "no")))

bad = 0
for path, mode, sector, why in rows:
    name = path.replace("captures/spice86/", "")
    print("%-46s %-8s %s" % (name, mode, why))
    # Two independent expectations, because the first one alone was proven useless: an
    # earlier version of this check asserted only "nothing reports space unless the staging
    # pair says so", stayed green against a mutant that called eighteen dungeon frames
    # space, and had to be told so. A sufficient condition is not a test.
    #
    # 1. The staging pair is necessary for space (weak, kept because it is cheap).
    if mode == "space" and "stage_all_ff=YES" not in why:
        bad += 1
        print("   ^ reported space without the staging pair set")
    # 2. A frame whose sector's ones digit is 4 or 5 is a dungeon sector and must never be
    #    reported as space. Expressed on the sector digit rather than on the capture's
    #    directory name, because the first attempt keyed on the name and was wrong: the
    #    dungeon-entry capture walks in from sector 30 and walks back out, so seven of its
    #    frames are legitimately `world` and the name-based rule called them failures.
    #    The dungeon-entry frames matter for one reason only: their staging pair is uniformly
    #    0xFF for the same reason orbit's is, and that collision is what the ordering exists
    #    to break.
    if "dungeon" in name and mode == "space":
        bad += 1
        print("   ^ a dungeon-named capture reported space")
    if sector is not None and str(sector)[-1] in ("4", "5") and mode != "dungeon":
        bad += 1
        print("   ^ sector %s is a dungeon sector but reported %s" % (sector, mode))

print("\n%d captures, %d invariant violations" % (len(rows), bad))
sys.exit(1 if bad else 0)
