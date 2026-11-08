#!/usr/bin/env python3
"""u2api.py — a client for the engine's play API. See docs/play-api.md and engine/src/core/api.h.

The protocol is one JSON object per line in each direction and the engine speaks first, so a
caller could drive it with two pipes. This wrapper exists because a hand-rolled reader of a
line protocol is where protocol bugs hide: it would be easy for a test to silently accept a
truncated line, which is hazard 1 of the plan. Every line this client reads is parsed or the
client raises, so a corrupt stream is a failure rather than a None.

    from u2api import Session
    with Session(engine="./u2", data_dir="original", save_dir="work/save") as s:
        s.create(name="BRITISH", sex="M", race=1, cls=1, stats=[10,10,10,10,10,40])
        obs = s.key("P")
        print(obs["mode"], obs["char"]["stats"])

`strict` is on by default and it is the point: a line that does not parse is an error, not a
skipped line.
"""
import json
import os
import subprocess
import sys


class ApiError(Exception):
    pass


class Session:
    def __init__(self, engine="./u2", data_dir=None, save_dir=None, extra=(),
                 timeout=30, env=None):
        argv = [engine, "--headless", "--api"]
        if data_dir:
            argv += ["--data-dir", data_dir]
        if save_dir:
            argv += ["--save-dir", save_dir]
        argv += list(extra)
        self.timeout = timeout
        self.proc = subprocess.Popen(
            argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, env=env,
        )
        self.lines_seen = 0
        self._expect("ready")

    # -- transport ---------------------------------------------------------
    def _send(self, obj):
        self.proc.stdin.write(json.dumps(obj) + "\n")
        self.proc.stdin.flush()

    def _read(self):
        line = self.proc.stdout.readline()
        if not line:
            raise ApiError("engine closed the stream (exit %s)" % self.proc.poll())
        self.lines_seen += 1
        line = line.rstrip("\n")
        if not line:
            raise ApiError("line %d was empty; every protocol line must be one JSON object"
                           % self.lines_seen)
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as e:
            raise ApiError("line %d is not JSON (%s): %r" % (self.lines_seen, e, line[:120]))
        if obj.get("u2api") != 1:
            raise ApiError("line %d is not an API message: %r" % (self.lines_seen, line[:120]))
        return obj

    def _expect(self, event):
        """Read until an event that is not an error. Errors are raised, not returned:
        a caller that ignores them would keep playing a game that refused it."""
        while True:
            obj = self._read()
            if obj["event"] == "error":
                raise ApiError("%s: %s" % (obj.get("code"), obj.get("message")))
            if obj["event"] == event:
                return obj.get("obs", {})
            if obj["event"] == "end":
                raise ApiError("engine ended (%s) while waiting for %s"
                               % (obj.get("reason"), event))
            # Any other event (created, await) is progress; keep reading for what we asked.

    # -- commands ----------------------------------------------------------
    def create(self, name, sex="M", race=1, cls=1, stats=None):
        if stats is None:
            raise ApiError("stats are required: the API does not roll for you")
        self._send({"cmd": "create", "name": name, "sex": sex,
                    "race": race, "class": cls, "stats": list(stats)})
        return self._expect("created")

    def key(self, token):
        self._send({"cmd": "key", "key": token})
        return self._expect("await")

    def state(self):
        self._send({"cmd": "state"})
        return self._expect("await")

    def quit(self):
        self._send({"cmd": "quit"})
        try:
            return self._expect("end")
        finally:
            self.close()

    def close(self):
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=self.timeout)
        except Exception:
            self.proc.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False


def main(argv):
    """A smoke conversation, printed as JSON, so `make api` has something to run."""
    here = os.path.dirname(os.path.abspath(__file__))
    engine = os.environ.get("U2", os.path.join(here, "..", "u2"))
    data = os.environ.get("DATA_DIR")
    save = os.environ.get("SAVE_DIR")
    with Session(engine=engine, data_dir=data, save_dir=save) as s:
        obs = s.create(name="BRITISH", sex="M", race=1, cls=1,
                       stats=[10, 10, 10, 10, 10, 40])
        print(json.dumps({"event": "created", "char": obs["char"]}))
        for tok in ("P", "U", "U"):
            obs = s.key(tok)
            print(json.dumps({"event": "key", "key": tok, "mode": obs["mode"],
                              "digest": obs["digest"], "message": obs["message"][:60]}))
        s.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
