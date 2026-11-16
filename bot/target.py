"""bot/target.py — one bot, two executables.

A target is "something the bot can play": it takes a key and gives back an observation.
Two implementations, same shape:

    EngineTarget    the portable engine, over --api
    Spice86Target   the original ultimaii.exe, under Spice86

The point is not that a bot can play the original. The point is that the same policy code
runs against both and the difference is evidence — see docs/bot-driver.md § 5.

THE ONE RULE THIS FILE OBEYS: it does not decode the DS block.

The obvious implementation of Spice86Target.state() parses the segment in Python —
`hp = bcd(ds[0x51], ds[0x52])` and so on. That is a second hand-maintained copy of every
offset and every packed-decimal rule in engine/src/core/state.h, and the two drift silently
until a bot acts on a wrong field. So instead the bytes go to the engine:

    raw DS  ->  ./u2 --ds-json FILE  ->  the same JSON --api emits

One decoder, two targets. If they ever disagree about a byte they cannot, because there is
only one function that reads it.
"""

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE_DIR = os.path.join(REPO, "engine")
U2_BIN = os.environ.get("U2_BIN", os.path.join(ENGINE_DIR, "u2"))

# The original's DS segment, and the mirror's width. Both are load-bearing and both are
# stated rather than derived: DS base is what every capture in this project reads, and the
# width is sizeof(u2_state), which --ds-json refuses to guess about.
DS_SEG = 0x08BE
DS_LEN = 1616


def _load_spice86_tools():
    """Reuse tools/spice86_type.py rather than writing a second MCP client.

    That module already has the handshake, the read/write helpers, the key press/release
    pair and the self-tested key table. Duplicating any of it here would be a second copy
    of something that has already been gotten wrong once.
    """
    path = os.path.join(REPO, "tools", "spice86_type.py")
    spec = importlib.util.spec_from_file_location("u2_spice86_type", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Target:
    """The interface policy code is written against. Never branch on the implementation."""

    name = "target"

    def start(self):
        raise NotImplementedError

    def state(self) -> dict:
        raise NotImplementedError

    def key(self, token: str) -> dict:
        raise NotImplementedError

    def close(self):
        raise NotImplementedError

    # --- helpers both targets can offer, defined once -------------------------------
    #
    # The nesting is not cosmetic and it is not the adapter's invention: the API's `state`
    # block is the debug dump, and the DS fields live one level below it at `state.state`
    # (api.c keeps them apart because CP2's evidence is read through the dump and growing it
    # was judged a bigger decision than the bot's need). Policy should not have to know that,
    # so it is unwrapped once here and the names below are the bot's vocabulary.

    def fields(self, obs=None) -> dict:
        s = (obs or self.state())["state"]["state"]
        return s

    def pos(self, obs=None):
        p = self.fields(obs)["pos"]
        return p["x"], p["y"]

    def hp(self, obs=None) -> int:
        return self.fields(obs)["hp"]

    def sector(self, obs=None):
        f = self.fields(obs)
        return f["sector"], f["sector_second"]

    def same_place(self, a, b) -> bool:
        return self.pos(a) == self.pos(b) and self.sector(a) == self.sector(b)


class EngineTarget(Target):
    """The portable engine, driven the way the API was designed to be driven."""

    name = "engine"

    def __init__(self, engine=None, data_dir=None, save_dir=None):
        sys.path.insert(0, os.path.join(REPO, "engine", "tools"))
        import u2api                                    # noqa: E402
        self._u2api = u2api
        self._kw = {"engine": engine or U2_BIN}
        if data_dir:
            self._kw["data_dir"] = data_dir
        if save_dir:
            self._kw["save_dir"] = save_dir
        self.s = None

    def start(self):
        self.s = self._u2api.Session(**self._kw)
        return self

    def create(self, name, **kw):
        self.s.create(name, **kw)
        return self.state()

    def state(self):
        return self.s.state()

    def key(self, token):
        return self.s.key(token)

    def close(self):
        if self.s:
            self.s.close()
            self.s = None


class Spice86Target(Target):
    """The original, under Spice86.

    Requires a running emulator — start one with tools/spice86_start.sh and pass its ports.
    This deliberately does not launch or stop the emulator itself: the start script owns the
    run directory, the port pick, the state file and the harvest, and a bot that re-implements
    that loses the harvest silently, which is a hazard this project has already recorded.
    """

    name = "original"

    def __init__(self, http_port=None, mcp_port=None, ds_seg=DS_SEG, ds_len=DS_LEN):
        # Ports default to whatever the live session recorded, not to 20180/20181. A bot
        # that quietly talked to a stale emulator on the default ports would diff two
        # processes it believed were one.
        if http_port is None or mcp_port is None:
            import sys
            sys.path.insert(0, os.path.join(REPO, "tools"))
            import s86_ports
            http_port, mcp_port = s86_ports.resolve_pair(http_port, mcp_port)
        self.http_port = http_port
        self.mcp_port = mcp_port
        self.ds_seg = ds_seg
        self.ds_len = ds_len
        self._st = None
        self._mcp = None
        self._tmp = None

    def start(self):
        self._st = _load_spice86_tools()
        self._mcp = self._st.Mcp(self.mcp_port)
        self._mcp.open()
        fd, self._tmp = tempfile.mkstemp(prefix="u2ds-", suffix=".bin")
        os.close(fd)
        return self

    # --- observation ---------------------------------------------------------------

    def read_ds(self) -> bytes:
        phys = self.ds_seg * 16
        with urllib.request.urlopen(
                "http://127.0.0.1:%d/api/memory/%d/range/%d"
                % (self.http_port, phys, self.ds_len), timeout=10) as r:
            vals = json.load(r)["values"]
        if len(vals) != self.ds_len:
            raise RuntimeError("read %d DS bytes, expected %d" % (len(vals), self.ds_len))
        return bytes(vals)

    def state(self) -> dict:
        """Raw DS -> the engine's own decode. No field knowledge lives here on purpose."""
        with open(self._tmp, "wb") as f:
            f.write(self.read_ds())
        # --headless so the terminal renderer stays out of stdout. Without it the first
        # observation line arrives prefixed with the clear-screen escape sequence, which
        # broke a startswith() match on the very first run of this function: the JSON was
        # there and complete, and the parser called it missing. Parsed by substring below
        # as well, because a decoder that can be silenced by a flag should not also have to
        # be silenced to be readable.
        p = subprocess.run([U2_BIN, "--headless", "--ds-json", self._tmp], cwd=ENGINE_DIR,
                           capture_output=True, text=True, timeout=60)
        for line in p.stdout.splitlines():
            i = line.find('{"u2api"')
            if i >= 0:
                return json.loads(line[i:])
        raise RuntimeError("--ds-json produced no observation: %s %s"
                           % (p.stdout[-200:], p.stderr[-200:]))

    # --- input ---------------------------------------------------------------------

    def key(self, token, settle=0.6, timeout=12.0):
        """One key in, one observation out.

        Two properties of asm/4F4D make naive driving lossy, and both are handled here rather
        than left to the caller:

        1. asm/4F69..asm/4F74 drains the typeahead buffer and discards everything after the
           first key, so two keys inside one poll window lose the second — and a lost key is
           observationally identical to a refused move. So this sends exactly one key.
        2. A dropped key still returns ok. So the answer is read back from memory.

        "Settled" means two consecutive reads with the same digest, i.e. the world has gone
        quiescent. The first version of this waited for the digest to CHANGE, which was wrong
        in a way worth recording: the digest covers the RNG register, and the game rolls while
        it idles, so the digest moves constantly and the call returned before the game had
        even polled the key. It reported a character that never moved as though the key had
        been processed. Quiescence is the honest signal; a changing digest is noise.
        """
        self._send(token)
        deadline = time.time() + timeout
        last, obs = None, self.state()
        while time.time() < deadline:
            time.sleep(settle)
            prev = obs
            obs = self.state()
            if obs.get("digest") == prev.get("digest"):
                return obs
            last = prev
        return obs          # never quiesced: still the latest truth, and slow, and visible

    @staticmethod
    def moved(a, b) -> bool:
        """Did the world actually change between two observations?

        Separate from `key()` on purpose: "the key was delivered" and "the key did something"
        are different claims, and conflating them is how a bot decides a move was refused when
        it was really never sent. Compare position, sector and the three counters a step can
        move — not the digest, for the reason in key().
        """
        fa, fb = a["state"]["state"], b["state"]["state"]
        return (fa["pos"] != fb["pos"] or fa["sector"] != fb["sector"]
                or fa["sector_second"] != fb["sector_second"]
                or fa["hp"] != fb["hp"] or fa["food"] != fb["food"])

    def _send(self, token):
        """Route through spice86_type so the key table and the modifier rules stay in one place.

        The child's exit status is checked, and that is not a formality. spice86_type now
        refuses to write a modifier it cannot deliver (the BIOS ring is emulator-owned), and a
        caller that ignored the exit status would report "the game ignored Ctrl-Y" — the
        exact false negative that cost a whole session earlier. A send that failed and a key
        the game refused look identical downstream, so the adapter has to be the one that
        knows the difference.
        """
        r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "spice86_type.py"),
                            "--keys", token], cwd=REPO, capture_output=True, text=True,
                           timeout=120)
        if r.returncode != 0:
            raise RuntimeError("key %r was not delivered: %s"
                               % (token, (r.stderr or r.stdout).strip()[-200:]))

    def close(self):
        if self._tmp and os.path.exists(self._tmp):
            os.unlink(self._tmp)
            self._tmp = None


def make(which, **kw):
    if which == "engine":
        return EngineTarget(**kw).start()
    if which in ("original", "spice86"):
        return Spice86Target(**kw).start()
    raise SystemExit("unknown target %r (want engine | original)" % which)
