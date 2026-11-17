#!/usr/bin/env python3
"""g91_ab.py — the controlled A/B behind docs/needs-gameplay.md G91.

The question, in the form a measurement can answer: over the same scripted session in
the same sector from the same seed, does the original ever leave a creature footprint
in the saved sector with no creature standing on it, and if not, which engine path
does?

Both sides are driven by the SAME key script and sampled at the SAME checkpoints. A
checkpoint is "N moves, then Q to save, then read the world". The artefact that is
measured is the file each program wrote, not what either one believes about itself:
the saved `mapxNN` is diffed cell by cell against the release map, and every differing
cell is tagged with whether a live creature is on it. A differing cell with a creature
under it is a footprint, which is normal and is what the original has too. A differing
cell with nothing under it is a ghost, and that is the bug.

Why checkpoints and not a final diff: the endpoint is already known (original 0 bare,
engine 9) and it names no cause. A timeline says at which move a ghost appeared and
which creature slot vacated it, which is the difference between "the move path is
missing a restore" and "the overlay is feeding itself" and "the kill path leaves a
corpse on purpose".

Two backends, one script:

  engine   the portable engine under --api, headless, no emulator
  original the real binary under Spice86, headless, keys via the MCP endpoint

and a `compare` mode that puts the two timelines side by side.

Usage:
  python3 tools/g91_ab.py engine   --scratch /var/tmp/g91 [--sector 20] [--rounds 12]
  python3 tools/g91_ab.py original --scratch /var/tmp/g91 [--sector 20] [--rounds 12]
  python3 tools/g91_ab.py compare  --engine A.jsonl --original B.jsonl
  python3 tools/g91_ab.py --selftest

Scratch lives under /var/tmp by default: /tmp is tmpfs here and a run copy is RAM.

Why there is no `site` result: sector/map travel was the leading hypothesis for where a ghost
comes from, and it is refuted structurally rather than by sampling. **A transition writes no
sector file at all**, in either program. `site_reload()` syncs the outgoing sector in memory
and writes nothing unless `--keep-site-state` is set, which is off by default; the original was
measured the same way on 2026-10-29 across several town entries and exits, with `mapx31`,
`monx31`, `mapx30` and `monx30` byte-identical to pristine and only `player` changed, which
`engine/tests/site_state.py` gates. The only writer of a sector file is the save path, so
travel cannot bake a stamp.

What travel does is make a stamp permanent, and that is the half of the hypothesis that held.
`site_reload()` calls `u2_world_load`, which reads the save overlay ahead of content, so a bare
stamp in the overlay arrives as authored terrain in `map_base` and is recreated by every
restamp and rewritten by every save. A stamp under its own creature is cleaned at that moment
by `clear_spawn_cells` reading the creature's terrain byte; a bare one has no creature to clean
it. That is how nine cells survived many sessions on a shipped tree.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP_W = 64
MAP_H = 64

# The default walk: a square, so a creature that starts near the player keeps being
# passed, and a four-move cycle means the player returns to where they began and any
# cell they left stamped is visible as a ghost rather than hidden behind their own
# sprite.
WALK = ["Right", "Down", "Left", "Up"]

# DS offsets of the creature block, read over the emulator's HTTP API. Same offsets
# AGENTS.md quotes cold: 32 slots of x, y, hp, type.
DS_SEG = "8be"
DS_MON_X = 0x137
DS_MON_Y = 0x157
DS_MON_TYPE = 0x197


# ---------------------------------------------------------------- shared analysis

def read_map(path):
    with open(path, "rb") as fh:
        return fh.read()


def diff_cells(pristine, saved):
    """Cells the save changed, as (index, x, y, was, now)."""
    out = []
    for i in range(min(len(pristine), len(saved))):
        if pristine[i] != saved[i]:
            out.append((i, i % MAP_W, i // MAP_W, pristine[i], saved[i]))
    return out


def classify(pristine, saved, creatures):
    """Split the saved map's differences into footprints and ghosts.

    `creatures` is a list of (x, y) live positions. A cell is owned when a creature is
    on it now; anything else that differs from the release map is a ghost, which is the
    thing a new character walks past.

    CALLERS MUST ONLY CLASSIFY A FILE THAT WAS JUST WRITTEN. A sector file lags: if
    nothing dirties it the next save does not rewrite it, and comparing yesterday's
    bytes against today's creature positions invents a ghost at every cell a creature
    has left since. That is not hypothetical — the first version of this harness
    reported a ghost at every save that followed a creature move, and the file was
    simply still the previous save's. `fresh_since()` exists so a caller can prove it.
    """
    on = {(c[0], c[1]) for c in creatures}
    owned, ghosts = [], []
    for i, x, y, was, now in diff_cells(pristine, saved):
        (owned if (x, y) in on else ghosts).append(
            {"i": i, "x": x, "y": y, "was": was, "now": now})
    return owned, ghosts


def fresh_since(path, previous):
    """(bytes, changed) — the file now, and whether it differs from the last bytes seen.
    A save that rewrote nothing is not a new measurement."""
    try:
        with open(path, "rb") as fh:
            b = fh.read()
    except OSError:
        return previous, False
    return b, (b != previous)


def vacated(prev, now):
    """Slots that moved between two creature snapshots: {slot: (from, to)}.

    Correlating a ghost with the slot that left it is the whole reason for sampling per
    checkpoint instead of once at the end.
    """
    moves = {}
    for slot in range(max(len(prev), len(now))):
        a = prev[slot] if slot < len(prev) else None
        b = now[slot] if slot < len(now) else None
        if a and b and a != b:
            moves[slot] = (a, b)
    return moves


# -------------------------------------------------------------------- engine side

def engine_commands(sector, rounds):
    """create, then `rounds` squares of walking with a save at each corner set.

    The engine's API enters the world only through `create` (docs/play-api.md
    deviation 6: the title handoff lives in title.c's case 'C' and `load` does not
    pass through it), so every run starts a character. That is also the scenario the
    bug was reported from, so it is not a compromise.
    """
    cmds = [CREATE]
    for _ in range(rounds):
        for k in WALK:
            cmds.append({"cmd": "key", "key": k})
        cmds.append({"cmd": "key", "key": "Q"})
        cmds.append({"cmd": "state"})
    cmds.append({"cmd": "quit"})
    return cmds


CREATE = {"cmd": "create", "name": "G91", "sex": "M", "race": 1, "class": 1,
          "stats": [10, 10, 10, 10, 10, 40]}


def _toward(px, py, cx, cy):
    """One step toward a creature, preferring the larger axis. Crude on purpose: the
    point is contact, not navigation, and a bump is how the original starts a fight."""
    dx, dy = cx - px, cy - py
    if abs(dx) >= abs(dy):
        return "Right" if dx > 0 else "Left"
    return "Down" if dy > 0 else "Up"


def _obs(proc):
    """Read stdout until the next observation line, and return it (or None at EOF)."""
    while True:
        line = proc.stdout.readline()
        if not line:
            return None
        try:
            o = json.loads(line)
        except ValueError:
            continue
        if o.get("event") in ("await", "end", "error"):
            return o


def _send(proc, cmd):
    proc.stdin.write(json.dumps(cmd) + "\n")
    proc.stdin.flush()


def _creatures_of(o):
    st = (o.get("obs") or {}).get("state", {}).get("state") or {}
    raw = st.get("raw", {})
    xs = [int(v, 16) for v in raw.get("mon_x", "").split()]
    ys = [int(v, 16) for v in raw.get("mon_y", "").split()]
    ts = [int(v, 16) for v in raw.get("mon_type", "").split()]
    return [(x, y) for x, y, t in zip(xs, ys, ts) if t], st


def run_engine(scratch, sector, rounds, keep=False):
    """Removed: the square walk used to batch every command and read the sector file
    after the process exited, so every checkpoint scored the SAME final bytes against a
    different step's creature positions and reported ghosts that did not exist. A
    measurement that can only read the endpoint cannot date a write. `--policy square`
    now runs the same streaming driver as chase, with a fixed key cycle instead of a
    target."""
    sys.exit("run_engine is retired; use run_engine_play(policy=...)")


def _mode(o):
    return (o.get("obs") or {}).get("mode")


def _wait_world(proc, obs, limit=12):
    """Drain the title splash before driving anything.

    The splash emits its own observations on the script clock, and a key sent while it is
    still running is buffered and lands one step late in the world — which in a harness
    means every key is aimed at the cell the player left behind. Read until the mode says
    world, and say so if it never does rather than driving a title screen.
    """
    n = 0
    while _mode(obs) != "world" and n < limit:
        nxt = _obs(proc)
        if nxt is None:
            break
        obs = nxt
        n += 1
    return obs


def _pick_key(policy, obs, r, i, target):
    """The next key for a policy. Returns (key, note)."""
    creatures, st = _creatures_of(obs)
    pos = (st.get("pos") or {}).get("x"), (st.get("pos") or {}).get("y")
    mode = _mode(obs)
    if mode in ("dungeon", "site"):
        # Indoors, the way out is a command rather than a step: KLIMB at dungeon floor 0,
        # DESCEND out of a tower. Try both with a step between, which is what a player does.
        return (("K", "exit-attempt") if (r + i) % 3 == 0 else
                ("D", "exit-attempt") if (r + i) % 3 == 1 else
                (WALK[(r + i) % 4], "indoor-step")), None
    if policy == "chase" and creatures and None not in pos:
        near = min(creatures, key=lambda c: abs(c[0]-pos[0]) + abs(c[1]-pos[1]))
        return (_toward(*pos, *near), "chase"), None
    if policy in ("site", "kill") and target and None not in pos:
        if policy == "site" and pos == target:
            # Standing on the door. Entry is the ENTER command, not a step: the walk loop
            # refuses the door tile and asm/3389's dispatch reads the tile UNDER the player.
            return ("E", "enter-door"), None
        return (_toward(*pos, *target), f"toward-{target}"), None
    return (WALK[(r * 4 + i) % 4], "square"), None


def seed_content(scratch, sector, cell, tile):
    """A content tree with one cell of the sector map changed.

    Why this exists rather than a walk: the created character starts in a pocket in
    sector 20 — north and east are `INVALID MOVE!` from the start cell — and every door
    the census lists for that sector is 15+ cells away, so a scripted walk cannot reach
    one. The CP6 commerce gate solves the same problem the same way: build a content
    directory whose map bytes put the geometry where the test needs it, and let the
    engine's own rules do the rest. Nothing here invents a rule; it moves a door.

    Returns the directory to pass as --data-dir.
    """
    # Beside the scratch dir, not inside it: the run driver wipes the scratch tree to
    # start clean, and a content directory it deletes on the way is a content tree that
    # never existed by the time anything reads it.
    dst = os.path.join(scratch + "-content", "content")
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(REPO, "original"), dst)
    x, y = cell
    path = os.path.join(dst, f"mapx{sector}")
    with open(path, "r+b") as fh:
        fh.seek(y * MAP_W + x)
        fh.write(bytes([tile]))
    return dst


def run_engine_play(scratch, sector, rounds, stride=8, policy="chase", target=None,
                    data=None):
    """Stream commands and score every write the engine makes, whenever it makes it.

    Scoring is per key, not per save, because the writes that matter are not all saves:
    entering a site autosaves the sector being left, and that is the write the entry
    path makes *before* it reloads the sector from disk. A harness that only looks after
    an explicit `Q` never sees it.

    The ordering is still the whole point: the file is read after the engine has answered
    for the key that caused the write and before it is given another, and a file whose
    bytes did not change is skipped — nothing was written, so there is nothing to score.
    """
    save = os.path.join(scratch, "save")
    demos = os.path.join(scratch, "demos")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(save)
    os.makedirs(demos)
    exe = os.path.join(REPO, "engine", "u2")
    if not os.path.exists(exe):
        sys.exit("engine not built: run make -C engine first")
    data = data or os.path.join(REPO, "original")
    if not os.path.isdir(data):
        sys.exit("no content tree; the A/B needs the real sector")

    proc = subprocess.Popen(
        [exe, "--headless", "--api", "--data-dir", data,
         "--save-dir", save, "--demo-dir", demos],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    _send(proc, CREATE)
    obs = _wait_world(proc, _obs(proc))
    if _mode(obs) != "world":
        _send(proc, {"cmd": "quit"})
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
        sys.exit("the engine never reached the world; nothing was measured")
    pristine = read_map(os.path.join(data, f"mapx{sector}"))
    saved_path = os.path.join(save, "PLAYER", f"mapx{sector}")

    timeline, prev, step = [], [], 0
    last_bytes = None
    last_mode = _mode(obs)

    def score(note):
        nonlocal step, last_bytes, prev
        buf, changed = fresh_since(saved_path, last_bytes)
        if not changed:
            return
        last_bytes = buf
        creatures, _ = _creatures_of(obs)
        owned, ghosts = classify(pristine, buf, creatures)
        step += 1
        timeline.append({"side": "engine", "step": step, "policy": policy, "note": note,
                         "mode": last_mode, "creatures": creatures,
                         "owned": owned, "ghosts": ghosts,
                         "moved": {str(k): v for k, v in vacated(prev, creatures).items()},
                         "n_creatures": len(creatures)})
        prev = creatures

    for r in range(rounds):
        for i in range(stride):
            (key, note), _ = _pick_key(policy, obs, r, i, target)
            _send(proc, {"cmd": "key", "key": key})
            obs = _obs(proc)
            if obs is None:
                break
            m = _mode(obs)
            if m != last_mode:
                note = f"{note}-mode-{m}"
                last_mode = m
            score(note)
        _send(proc, {"cmd": "key", "key": "Q"})
        _send(proc, {"cmd": "state"})
        obs = _obs(proc)
        if obs is None:
            break
        last_mode = _mode(obs)
        score("save")
    _send(proc, {"cmd": "quit"})
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
    return timeline


# ------------------------------------------------------------------ original side

def seed_record(scratch, sector):
    """A record both binaries can load, made by the engine.

    The shipped `player` file is a blank: the original loads it and sits at the title
    with a zero-length name and position (0,0), so it is no seed at all. The engine's
    created character does load in the original — which is the same cross-load
    `tools/save_cross_load.py` exists to prove — and it puts both sides in the same
    sector at the same cell, which is what makes the two timelines comparable.
    """
    seed_save = os.path.join(scratch, "seed")
    run_engine_play(seed_save, sector, 1, stride=1, policy="square")
    rec = os.path.join(seed_save, "save", "PLAYER", "PLAYER")
    if not os.path.exists(rec):
        sys.exit("seeding the original needs a record the engine wrote; none appeared")
    return rec


def _creatures():
    xs, ys, ts = _peek(DS_MON_X, 32), _peek(DS_MON_Y, 32), _peek(DS_MON_TYPE, 32)
    return [(x, y) for x, y, t in zip(xs, ys, ts) if t]


def run_original(scratch, sector, rounds, stride=8, policy="chase"):
    """The real binary under Spice86, same script, same checkpoints.

    Three things this had to learn from a live run rather than from the docs:

    * The launcher's env names the run dir `S86_RUN_DIR`, and that copy is where the
      game writes; `original/` stays pristine.
    * Saving is `q` then `y`. The engine's Q writes on its own; the original asks.
    * The original does NOT rewrite the sector on every save — only when something
      dirtied it. A file that was not rewritten must not be scored against positions
      that have moved since, which is the same lag trap `classify()` documents.
    """
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch)
    seed = os.path.join(scratch, "player")
    shutil.copyfile(seed_record(os.path.join(scratch, "seedside"), sector), seed)

    start = subprocess.run(
        ["bash", os.path.join(REPO, "tools", "spice86_start.sh"),
         "--headless", "--auto-play", "--patch-arrows", "--player", seed,
         "--name", "g91ab"], capture_output=True, text=True, timeout=600)
    env_path = os.path.join(REPO, "tmp", ".spice86-session.env")
    if not os.path.exists(env_path):
        print(start.stdout, start.stderr, file=sys.stderr)
        sys.exit("spice86_start.sh produced no session env; the run did not start")
    env = dict(l.split("=", 1) for l in open(env_path).read().splitlines() if "=" in l)
    rundir = env.get("S86_RUN_DIR")
    if not rundir:
        sys.exit(f"session env names no run dir: {env}")

    pristine = read_map(os.path.join(REPO, "original", f"mapx{sector}"))
    timeline, prev, last_bytes = [], [], None
    try:
        for r in range(rounds):
            keys = []
            for i in range(stride):
                keys.append(WALK[(r * stride + i) % 4] if policy != "chase" else "Right")
            subprocess.run([sys.executable, os.path.join(REPO, "tools", "spice86_type.py"),
                            "--keys", ",".join(keys)],
                           capture_output=True, text=True, timeout=300)
            time.sleep(1.0)
            # Positions are only evidence about the write if nothing moved between the read
            # and the write, and `q` itself can hand the creatures a turn. Read on both
            # sides of the save and score the write only when they agree; a checkpoint that
            # cannot be paired is skipped, not guessed at.
            pre = _creatures()
            subprocess.run([sys.executable, os.path.join(REPO, "tools", "spice86_type.py"),
                            "--keys", "q,y"],
                           capture_output=True, text=True, timeout=300)
            time.sleep(1.0)
            creatures = _creatures()
            if pre != creatures:
                continue
            buf, changed = fresh_since(os.path.join(rundir, f"mapx{sector}"), last_bytes)
            if not changed:
                continue        # nothing was written; the old bytes are not a measurement
            last_bytes = buf
            owned, ghosts = classify(pristine, buf, creatures)
            timeline.append({"side": "original", "step": r + 1, "creatures": creatures,
                             "owned": owned, "ghosts": ghosts,
                             "moved": {str(k): v for k, v in vacated(prev, creatures).items()}})
            prev = creatures
    finally:
        subprocess.run(["bash", os.path.join(REPO, "tools", "spice86_stop.sh")],
                       capture_output=True, text=True, timeout=300)
    return timeline


_PEEK = [None]


def _peek(off, n):
    """Read n bytes of DS through the emulator's HTTP API."""
    out = subprocess.run(
        [sys.executable, os.path.join(REPO, "tools", "spice86_type.py"),
         "--peek", f"{DS_SEG}:{off:x}:{n}"],
        capture_output=True, text=True, timeout=120)
    txt = out.stdout.strip().split()
    return [int(v, 16) for v in txt[-n:]] if txt else [0] * n


# ----------------------------------------------------------------------- compare

def compare(engine, original):
    """Where the two timelines part company, in the order that matters."""
    lines = []
    e_last = engine[-1] if engine else None
    o_last = original[-1] if original else None
    if not e_last or not o_last:
        return ["compare: one side produced no checkpoints; nothing was measured"]
    lines.append(f"engine:    {len(engine)} checkpoints, "
                 f"{len(e_last['owned'])} footprints, {len(e_last['ghosts'])} ghosts")
    lines.append(f"original:  {len(original)} checkpoints, "
                 f"{len(o_last['owned'])} footprints, {len(o_last['ghosts'])} ghosts")
    first = next((c for c in engine if c["ghosts"]), None)
    if first:
        lines.append(f"engine's first ghost: checkpoint {first['step']} at "
                     + ", ".join(f"({g['x']},{g['y']})={g['now']:#04x}"
                                 for g in first["ghosts"][:4]))
        if first["moved"]:
            lines.append(f"  slots that moved on that checkpoint: {first['moved']}")
    else:
        lines.append("engine left no ghost on this script")
    ofirst = next((c for c in original if c["ghosts"]), None)
    if ofirst:
        lines.append(f"original ALSO left a ghost: checkpoint {ofirst['step']} — "
                     "bare footprints are normal and G91 is not a stamp bug")
    else:
        lines.append("original left no ghost on this script")
    # The self-feeding test: a checkpoint where the player did not move and no creature
    # moved, yet the ghost count rose. That is the overlay being read back as ground.
    for a, b in zip(engine, engine[1:]):
        if len(b["ghosts"]) > len(a["ghosts"]) and not b["moved"]:
            lines.append(f"  engine ghost count rose with NO creature moving at "
                         f"checkpoint {b['step']}: {len(a['ghosts'])} -> "
                         f"{len(b['ghosts'])} — the overlay is feeding itself")
            break
    return lines


# ---------------------------------------------------------------------- selftest

def selftest():
    """The control: classify() must be able to say ghost, and vacated() must name a slot.

    A gate that cannot go red is worse than no gate, and this one's failure mode is a
    silent empty list.
    """
    pristine = bytes([0x20] * (MAP_W * MAP_H))
    saved = bytearray(pristine)
    saved[3 * MAP_W + 5] = 0x30          # a creature is standing here
    saved[10 * MAP_W + 12] = 0xFC        # nothing is
    owned, ghosts = classify(pristine, bytes(saved), [(5, 3)])
    assert len(owned) == 1 and owned[0]["x"] == 5 and owned[0]["y"] == 3, owned
    assert len(ghosts) == 1 and ghosts[0]["x"] == 12 and ghosts[0]["y"] == 10, ghosts
    mv = vacated([(5, 3), (1, 1)], [(5, 4), (1, 1)])
    assert list(mv) == [0] and mv[0] == ((5, 3), (5, 4)), mv
    # And the negative half: identical maps classify to nothing at all.
    o2, g2 = classify(pristine, pristine, [])
    assert not o2 and not g2, (o2, g2)
    # And the pairing control, which is the bug this harness had: a file that was not
    # rewritten must not be scored against positions that have moved since.
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "mapx20")
        with open(p, "wb") as fh:
            fh.write(bytes(saved))
        b1, c1 = fresh_since(p, None)
        assert c1 and b1 == bytes(saved), c1
        b2, c2 = fresh_since(p, b1)
        assert not c2, "an unchanged file was scored as a new write"
        b3, c3 = fresh_since(os.path.join(td, "absent"), b1)
        assert not c3 and b3 == b1, c3
    print("selftest: PASS (footprint, ghost, vacated slot, the empty case, "
          "and the unchanged-file control)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("mode", nargs="?", choices=["engine", "original", "compare"])
    ap.add_argument("--scratch", default="/var/tmp/g91")
    ap.add_argument("--sector", type=int, default=20)
    ap.add_argument("--rounds", type=int, default=12)
    ap.add_argument("--out", default=None)
    ap.add_argument("--engine", default=None)
    ap.add_argument("--original", default=None)
    ap.add_argument("--policy", default="square",
                    choices=["square", "chase", "site", "kill"],
                    help="square: a fixed cycle. chase: walk into creatures. site: walk onto a "
                         "door and come back out — scores nothing by design, because a "
                         "transition writes no sector file (see the module docstring). "
                         "kill: chase and keep going.")
    ap.add_argument("--target", default=None,
                    help="site policy: the cell to walk to, as x,y (default 35,22, "
                         "the castle door in sector 20)")
    ap.add_argument("--stride", type=int, default=8, help="chase: moves between saves")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if not a.mode:
        ap.print_help()
        return 2

    if a.mode == "compare":
        need = [a.engine, a.original]
        if not all(need):
            sys.exit("compare needs --engine and --original jsonl files")
        load = lambda p: [json.loads(l) for l in open(p) if l.strip()]
        for line in compare(load(a.engine), load(a.original)):
            print(line)
        return 0

    tgt = None
    if a.target:
        xs, ys = a.target.split(",")
        tgt = (int(xs), int(ys))
    elif a.policy == "site":
        tgt = (35, 22)
    data = None
    if a.policy == "site" and not a.target:
        # A dungeon entrance one step east of the start cell, in a copy of the content
        # tree. `original/` is never written: this is the player's release data.
        data = seed_content(a.scratch, a.sector, (20, 21), 0x24)
        tgt = (20, 21)
        print("seeded: dungeon entrance at (20,21) in a copy of the content tree")
    tl = run_engine_play(a.scratch, a.sector, a.rounds, a.stride, a.policy, tgt, data) \
        if a.mode == "engine" else run_original(a.scratch, a.sector, a.rounds,
                                                a.stride, a.policy)
    out = a.out or os.path.join(a.scratch, f"{a.mode}.jsonl")
    with open(out, "w") as fh:
        for row in tl:
            fh.write(json.dumps(row) + "\n")
    if not tl:
        sys.exit(f"{a.mode}: no checkpoints — nothing was measured (this is a failure, "
                 "not an empty pass)")
    last = tl[-1]
    print(f"{a.mode}: {len(tl)} checkpoints -> {out}")
    print(f"  last: {len(last['owned'])} footprints, {len(last['ghosts'])} ghosts")
    for g in last["ghosts"][:12]:
        print(f"  ghost ({g['x']},{g['y']}) was {g['was']:#04x} now {g['now']:#04x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
