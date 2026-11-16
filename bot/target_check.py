#!/usr/bin/env python3
"""target_check.py — the gate for the two-target adapter. Needs no emulator.

Four things are pinned here, and the fourth is the reason the file exists:

1. `--ds-json` reads the bytes it claims to. Spot-checked against documented DS offsets
   rather than re-derived — a full reimplementation in Python is exactly the second copy of
   the decode this design exists to prevent.
2. The decode is deterministic: same bytes, same observation.
3. EngineTarget drives end to end and the observation has the shape policy is written
   against.
4. A modifier key is REFUSED loudly. This is the mutation-shaped check: the failure it
   prevents is a bot reporting "the game ignored Ctrl-Y" when the key was never delivered,
   which is a real false negative this project already paid a session for.
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ENGINE = os.path.join(REPO, "engine")
sys.path.insert(0, HERE)
from target import U2_BIN, DS_LEN, Spice86Target, EngineTarget   # noqa: E402

checks = 0
fails = []


def ok(cond, msg):
    global checks
    if cond:
        checks += 1
    else:
        fails.append(msg)
        print("  NOT OK  %s" % msg)


def ds_blob(fields):
    """A DS mirror with a few fields set, everything else zero."""
    b = bytearray(DS_LEN)
    for off, val in fields.items():
        b[off] = val
    return b


def decode(blob):
    path = "/var/tmp/u2-target-check.bin"
    with open(path, "wb") as f:
        f.write(bytes(blob))
    p = subprocess.run([U2_BIN, "--headless", "--ds-json", path], cwd=ENGINE,
                       capture_output=True, text=True, timeout=60)
    for line in p.stdout.splitlines():
        i = line.find('{"u2api"')
        if i >= 0:
            return json.loads(line[i:])
    raise AssertionError("--ds-json gave nothing: %r %r" % (p.stdout[-200:], p.stderr[-200:]))


# --- 1. the decode reads the documented offsets --------------------------------------
# Offsets are the original's DS, cited in engine/src/core/state.h. Only the ones a bot
# acts on first: position, sector pair, name, and the packed-decimal counters.
blob = ds_blob({0x00: 25, 0x01: 7})
for i, ch in enumerate(b"BOTTEST"):
    blob[0x36 + i] = ch
blob[0x49] = 0x04; blob[0x4A] = 0x00          # sector digits: 40, wilderness
blob[0x51] = 0x42; blob[0x52] = 0x00          # hp, packed decimal
blob[0x58] = 0x12; blob[0x59] = 0x34          # gold, packed decimal

obs = decode(blob)
inner = obs["state"]["state"]
ok(obs["mode"] == "world", "a blob with a name must report mode=world, got %r" % obs["mode"])
ok(inner["pos"] == {"x": 25, "y": 7}, "pos must come from DS:00/01, got %r" % inner["pos"])
ok(obs["char"]["name"] == "BOTTEST", "name must come from DS:36, got %r" % obs["char"]["name"])
ok(str(inner["sector"]) in ("4", "04", "40"),
   "sector must reflect DS:49/4A, got %r" % inner["sector"])
ok(obs["char"]["hp"] == 4200, "hp must decode packed decimal, got %r" % obs["char"]["hp"])
ok(obs["char"]["gold"] == 1234, "gold must decode packed decimal, got %r" % obs["char"]["gold"])

# --- 2. determinism: one decoder cannot disagree with itself -------------------------
ok(decode(bytearray(blob)) == obs, "the same bytes must decode identically twice")

# --- 3. a title-screen blob is not a world ------------------------------------------
title = decode(bytes(DS_LEN))
ok(title["mode"] == "title", "an empty blob must report title, got %r" % title["mode"])

# --- 4. the refusal, which is the check that matters ---------------------------------
# No emulator needed: resolve_key rejects the shape before anything is sent. Note this calls
# the real resolver — an earlier draft monkeypatched _send and then asserted the patch had
# refused, which is a check that can only ever pass.
st = None
try:
    import importlib.util as _il
    _spec = _il.spec_from_file_location("st_tools", os.path.join(REPO, "tools", "spice86_type.py"))
    st = _il.module_from_spec(_spec); _spec.loader.exec_module(st)
except Exception as ex:                        # noqa: BLE001
    ok(False, "could not load spice86_type to test refusal: %s" % ex)
if st:
    for bad in ("ctrl-Right", "alt-9"):
        try:
            st.resolve_key(bad)
            ok(False, "%s must be refused, not silently downgraded" % bad)
        except SystemExit:
            ok(True, "%s is refused rather than downgraded" % bad)
    # And the deliverable ones resolve, so the refusal above is not just a broken resolver.
    for good, (scan, asc) in {"ctrl-y": (0x15, 0x19), "ctrl-s": (0x1F, 0x13)}.items():
        got = st.resolve_key(good)
        ok(tuple(got[-2:]) == (scan, asc), "%s must resolve to (%02x,%02x), got %r"
           % (good, scan, asc, got))

# And the adapter surfaces a failed send instead of reporting a refusal by the game.
class _Boom:
    returncode = 1
    stdout = ""
    stderr = "ring write refused"

_real = subprocess.run
subprocess.run = lambda *a, **k: _Boom()
try:
    try:
        Spice86Target._send(Spice86Target("x"), "a")
        ok(False, "a failed key send must raise, not look like a game refusal")
    except RuntimeError:
        ok(True, "a failed key send raises rather than masquerading as a refused move")
finally:
    subprocess.run = _real

# --- 5. EngineTarget end to end ------------------------------------------------------
# save_dir goes under /var/tmp on purpose. The engine writes a save record where it is told
# to, and the first run of this check wrote one to ./saves/ at the repo root — which CP3 then
# failed, because CP3's job is to assert no generated artefact is left untracked in the tree.
# A gate that dirties the tree it is gating is worse than a slow one: it makes the next
# unrelated run red.
try:
    save_dir = os.path.join("/var/tmp", "u2-bot-check-saves")
    os.makedirs(save_dir, exist_ok=True)
    e = EngineTarget(save_dir=save_dir).start()
    o = e.create("BOTTEST", stats=[10, 10, 10, 10, 10, 40])
    ok(o["mode"] in ("world", "created"), "engine create returned mode %r" % o["mode"])
    k = e.key("Down")
    ok("state" in k and "char" in k, "engine observation must carry state and char")
    ok(k["state"]["state"]["pos"] is not None, "engine observation must carry pos")
    ok(set(o) >= set(["mode", "digest", "char", "state"]),
       "engine observation missing top-level keys: %s" % sorted(set(o)))
    ok(set(o) == set(decode(bytearray(blob))),
       "both targets must emit the same top-level keys: %s vs %s"
       % (sorted(o), sorted(decode(bytearray(blob)))))
    e.close()
except Exception as ex:                       # noqa: BLE001 - report, do not crash the gate
    ok(False, "EngineTarget end-to-end failed: %s" % ex)

# --- the grid index, without an emulator ----------------------------------------
# tile_at takes a grid argument precisely so this can run offline. The live run proved the
# OFFSET (tile_at(3,10) on a real sector 41 returns the pad tile 0x4c); what this pins is the
# arithmetic, which a live run proves only for the one cell it happens to look at. A stride
# slip or a dropped mask would be invisible at (3,10) and obvious here.
try:
    from target import Spice86Target, GRID_OFF, GRID_EDGE

    ok(GRID_OFF == 0x1800 and GRID_EDGE == 64,
       "grid is DS:0x1800 with edge 64 (got %#x / %d)" % (GRID_OFF, GRID_EDGE))
    probe = bytes(range(256)) * 16          # 4096 bytes, byte i is i & 0xff
    t0 = Spice86Target.__new__(Spice86Target)   # no session: tile_at only touches `grid`
    ok(t0.tile_at(3, 10, grid=probe) == ((10 * 64) + 3) & 0xff,
       "(3,10) indexes row-major at stride 64")
    ok(t0.tile_at(67, 74, grid=probe) == t0.tile_at(3, 10, grid=probe),
       "coordinates wrap by & 63, so (67,74) is (3,10)")
    ok(t0.tile_at(-1, -1, grid=probe) == t0.tile_at(63, 63, grid=probe),
       "negative coordinates wrap to the far corner, not off the end")
    ok(t0.tile_at(0, 0, grid=probe) == 0 and t0.tile_at(63, 63, grid=probe) == (63 * 64 + 63) & 0xff,
       "both corners index inside the 4096-byte grid")
except Exception as ex:                       # noqa: BLE001
    ok(False, "grid index checks failed: %s" % ex)

# --- P8: "stop attacking this", expressed as data --------------------------------
# The original has no invulnerability flag. asm/23E6 scans slots 31..1 and never looks at
# slot 0, so a creature parked there is unhittable by melee, talk and cannon while still
# drawing, still blocking the cell and still running its AI. The engine's own predicate is
# the only faithful source, so the observation carries the answer rather than the bot
# reimplementing a slot scan in Python.
#
# Per neighbour in N,E,S,W: 1 attackable, 0 empty, -1 present but NOT targetable.
try:
    def p8_blob(extra):
        d = {0x00: 10, 0x01: 10, 0x49: 4, 0x4A: 0}   # sector 40, Earth wilderness
        for i, c in enumerate(b"BOT"):
            d[0x36 + i] = c                          # a nameless record reads as title
        d.update(extra)
        return ds_blob(d)

    def mon(slot, x, y, hp=20, typ=0x60):
        return {0x137 + slot: x, 0x157 + slot: y, 0x177 + slot: hp, 0x197 + slot: typ}

    def both(a, b):
        # dict(a, **b) would be the idiom but `**` needs string keys; these are offsets.
        m = dict(a)
        m.update(b)
        return m

    def tg(extra):
        o = decode(p8_blob(extra))
        return o.get("targets")

    ok(tg({}) == [0, 0, 0, 0], "empty world reports no targets (got %s)" % tg({}))
    ok(tg(mon(5, 10, 9)) == [1, 0, 0, 0],
       "a slot-5 creature to the north is attackable (got %s)" % tg(mon(5, 10, 9)))
    ok(tg(mon(0, 10, 9)) == [-1, 0, 0, 0],
       "the SAME creature in slot 0 is present but NOT attackable -- this is the P8 "
       "rule and the value a bot must not spend an attack on (got %s)" % tg(mon(0, 10, 9)))
    ok(tg(both(mon(0, 11, 10), mon(7, 9, 10))) == [0, -1, 0, 1],
       "slot 0 east and slot 7 west report both, in the right cells (got %s)"
       % tg(both(mon(0, 11, 10), mon(7, 9, 10))))
    # Slot 0 only makes a cell unhittable when it is the only thing there: asm/23E6 returns
    # the first match scanning down, so a lower slot at the same cell is still targetable.
    ok(tg(both(mon(0, 10, 11), mon(3, 10, 11))) == [0, 0, 1, 0],
       "slot 0 sharing a cell with slot 3 does NOT make the cell unhittable (got %s)"
       % tg(both(mon(0, 10, 11), mon(3, 10, 11))))
    # The two scans exclude on DIFFERENT fields, and that asymmetry is measured, not
    # incidental: asm/23E6 tests mon_type, so a zero-hp but still-typed slot is attackable
    # here, while the step/block scan skips hp 0. Asserting this the wrong way round is what a
    # first draft of this check did, which is how the asymmetry stays honest.
    ok(tg(mon(5, 10, 9, hp=0)) == [1, 0, 0, 0],
       "a zero-hp but still-typed slot IS attackable -- the attack scan tests type, not hp "
       "(got %s)" % tg(mon(5, 10, 9, hp=0)))
    ok(tg(mon(5, 10, 9, hp=0, typ=0)) == [0, 0, 0, 0],
       "a slot empty on BOTH fields is nothing at all (got %s)" % tg(mon(5, 10, 9, hp=0, typ=0)))
except Exception as ex:                       # noqa: BLE001
    ok(False, "P8 target-eligibility checks failed: %s" % ex)

if fails:
    print("  FAIL  %d checks passed, %d failed" % (checks, len(fails)))
    sys.exit(1)
print("  ok  %d checks: one decoder, two targets, undeliverable keys refused" % checks)
