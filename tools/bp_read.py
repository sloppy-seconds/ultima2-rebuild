#!/usr/bin/env python3
"""bp_read.py — stop the CPU at a runtime address and dump guest memory there.

Why this exists
---------------
Several open questions are "what is the value of this operand at the instant the
game prints or charges it". Static reading cannot answer that (docs/commerce.md
§ 3 is the standing example: the price pair is written by a routine whose output
a static reader can only model), and polling cannot answer it either, because the
operand is scratch that the next caller overwrites.

The mechanism is Spice86's `monitor breakCsIp`, driven over the GDB remote
protocol by the same RSP class tools/reject_trap.py validated on 2026-10-22. This
module reuses that class rather than re-deriving it.

Two facts inherited from reject_trap.py and not renegotiated here:
  * `breakCsIp` parses its arguments as HEX while /api/status reports DECIMAL.
    The conversion goes through reject_trap.cs_ip_from_status, which asserts.
  * The trap is read-only with respect to guest memory: it never patches guest
    bytes, so it cannot damage the byte-exact comparison the project depends on.

Caller identification
---------------------
`--state`, `--ret` and `--callstack` ask the emulator, while the target is stopped,
who called us. They are **monitor commands on the GDB/RSP port** (20190); the HTTP
API has no equivalent — `GET /api/peekRet` answers an empty 200, which is not an
error and reads exactly like "no data".

  * `state`     -> registers, including SS:SP and DI
  * `peekRet`   -> the return address Spice86 finds on the emulated stack
  * `callStack` -> Spice86's whole tracked call stack (noisy, and its function
                   names are `unknown_CS_IP_linear`, so treat it as context only)

`--ret` also decodes the stack word at SS:SP itself and prints the two side by
side. They can disagree — `peekRet` reports what Spice86's call tracking believes,
the raw word reports what is in RAM — and a single number printed alone would hide
that. The offset is also converted to a **file offset** (`file = runtime + 0x200`,
see `tools/check_func.py` BIAS), because `asm/` files are named by runtime offset
and the two spaces have caused several false blockers.

**An RSP attach halts the VM.** Opening the gdb port pauses execution and it stays
paused when the socket closes without a `continue` — `/api/status` then reports
`isCpuRunning: true, isPaused: true` with a frozen cycle count, and every key sent
afterwards is swallowed. So this tool resumes on *every* exit path, including "no
stop within timeout", unless `--no-continue` was asked for.

Controls
--------
`--selftest` proves the mechanism against a live run in BOTH directions:
  positive  arm on the address the CPU is already executing -> must stop
  negative  arm on an address that can never execute        -> must NOT stop
A tool that can only ever report a hit is worthless here; the negative control is
what makes "no stop" mean something.

Usage
-----
    # arm, resume, and dump DS:27..2C when the tavern price print is reached
    python3 tools/bp_read.py --ip 17B7 --dump 27:6 --dump 4E:4 --timeout 90

    # who reaches the monster-spawn dispatch: registers, return address, call stack
    python3 tools/bp_read.py --ip 0D44 --cs 17D --state --ret --callstack --timeout 150

    # prove the tool can fail
    python3 tools/bp_read.py --selftest
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.request

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from reject_trap import RSP, DS_SEG, cs_ip_from_status  # noqa: E402

CS_DEFAULT = 0x017D          # the code segment the runtime CFG reports
FILE_BIAS = 0x200            # file offset = runtime offset + 0x200 (check_func.py BIAS)


def http_json(port: int, path: str):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=5) as r:
        return json.load(r)


def read_phys(port: int, phys: int, n: int) -> bytes:
    url = f"/api/memory/{phys}/range/{n}"
    data = http_json(port, url)
    if isinstance(data, dict):
        # the endpoint answers {"values": [...]}; older notes say "bytes"/"data".
        # Accept any of them but never silently return empty.
        for key in ('values', 'bytes', 'data'):
            if key in data:
                data = data[key]
                break
        else:
            raise SystemExit(f'unrecognised memory response keys: {list(data)}')
    return bytes(data)


def read_ds(port: int, off: int, n: int) -> bytes:
    return read_phys(port, DS_SEG * 16 + off, n)


def status(port: int):
    """/api/status, normalised. Flat decimal cs/ip -- NOT nested, NOT hex.

    reject_trap.py's whole warning about hex-vs-decimal lives here: this API reports
    decimal (cs=381 means 0x17D) while breakCsIp wants hex.
    """
    st = http_json(port, '/api/status')
    return {'cs': st['cs'], 'ip': st['ip'], 'paused': st.get('isPaused')}


def parse_ranges(specs):
    out = []
    for s in specs:
        a, _, n = s.partition(':')
        out.append((int(a, 16), int(n) if n else 1))
    return out


def wait_for_pause(port: int, target_ip: int, timeout: float, strict: bool = True):
    """Poll /api/status until the emulator is paused AT target_ip; return that status.

    isPaused alone is NOT proof of a breakpoint hit. Any HTTP memory read pauses the
    CPU for its duration (docs/spice86-capture.md, and the standing trap about keys
    lost during a read), and tools/spice86_type.py reads memory to verify its own key
    injection. So a concurrent key send produces real pauses at whatever address the
    CPU happened to be at -- which is how the first version of this tool reported a
    stop at the idle key-poll address while asking for 17B7.

    With strict=True a pause at any other address is skipped and polling continues.
    """
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = status(port)
        if st['paused'] and (not strict or st['ip'] == target_ip):
            return st
        time.sleep(0.02)
    return None


def arm(conn: RSP, cs_hex: str, ip_hex: str) -> bool:
    reply = conn.monitor(f"breakCsIp {cs_hex} {ip_hex}", 2.0)
    return "Breakpoint added" in conn.decode_monitor(reply)


def monitor(conn: RSP, cmd: str, drain: float = 8.0) -> str:
    """Send one monitor command against the halted target and return its text.

    Drains the socket first, then uses `monitor_collect`: the halt itself puts stop
    notifications on the link, and reading them as the reply is how the first version
    of this printed `state: S05` and then a call stack that was really a register dump.
    An empty reply is reported as such rather than passed on as a value.
    """
    stale = conn.drain_pending()
    if stale:
        print(f'  (discarded {len(stale)} unread RSP packet(s) before {cmd})')
    text = conn.decode_packets(conn.monitor_collect(cmd, timeout=drain))
    if not text:
        print(f'  {cmd}: NO REPLY (empty monitor response)')
    return text


# 'Cycles=... SS=0xBCE DS=0x8BE ...' -- the register dump is one flat line.
STATE_RE = re.compile(r'\b([A-Z]S|E[AXBCD]X|E[SIP][PI])=0x([0-9A-Fa-f]+)')


def parse_state(text: str) -> dict:
    return {k: int(v, 16) for k, v in STATE_RE.findall(text)}


# How to recognise each monitor reply by what it contains, because on a halted target
# the reply cannot be attributed to the command that is in flight (see collect_monitors).
MONITOR_SIGNATURES = {
    'state': re.compile(r'Cycles='),
    'peekRet': re.compile(r'^\s*[0-9A-Fa-f]{1,4}:[0-9A-Fa-f]{1,4}\s*$'),
    'callStack': re.compile(r'expected to return|From external interrupt'),
}


def collect_monitors(conn: RSP, cmds: list[str], timeout: float = 25.0) -> dict:
    """Send several monitor commands, then read until every one has answered.

    Why not send-then-read one at a time: **on a halted target Spice86's replies lag the
    command that produced them.** Traced 2026-10-28 at 17D:0CEE -- `qRcmd,state` answered
    nothing for 20 s; the register dump arrived only once a further packet was on the
    wire, and `peekRet`'s answer arrived after the `continue`. Read one at a time, every
    reply is attributed to the wrong command, which is how `callStack` came back holding
    a register dump and looked like plausible evidence.

    So: fire all the commands, then pump the link, nudging once a second (a halted target
    needs a reason to write), and file each packet under the first signature it matches.
    Attribution is by content, so the ordering cannot lie.
    """
    out: dict[str, str] = {}
    conn.drain_pending()
    for cmd in cmds:
        conn._send('qRcmd,' + cmd.encode().hex())
    t0 = time.time()
    last_nudge = t0
    while time.time() - t0 < timeout and len(out) < len({c.split()[0] for c in cmds}):
        p = conn._recv_packet(0.3)
        if p is None:
            if time.time() - last_nudge > 1.0:
                conn._send('?')
                conn._send('qRcmd,' + b'state'.hex())
                last_nudge = time.time()
            continue
        if re.fullmatch(r'[TS][0-9A-Fa-f]{2}', p) or p == 'OK':
            continue
        text = conn.decode_packets([p])
        for cmd in cmds:
            key = cmd.split()[0]
            sig = MONITOR_SIGNATURES.get(key)
            if sig and sig.search(text):
                out.setdefault(key, text)
    missing = [c for c in cmds if c.split()[0] not in out]
    if missing:
        print(f'  MONITOR NO REPLY for: {", ".join(missing)} '
              f'(got {len(out)} of {len(set(c.split()[0] for c in cmds))} in '
              f'{timeout:.0f}s)')
    return out


def report_caller(conn: RSP, a, want_state: bool, want_ret: bool, want_stack: int = 0):
    """Print registers / return address / raw stack, cross-checking each other.

    Every monitor command goes through `collect_monitors` in one batch: replies arrive
    out of order on a halted target, and a report that labels them by send order is
    worse than no report.
    """
    cmds = ['state']
    if want_ret:
        cmds.append('peekRet' + (f' {a.ret_type}' if a.ret_type else ''))
    got = collect_monitors(conn, cmds)
    text = got.get('state', '')
    if want_state:
        print(f'  state: {text}')
    regs = parse_state(text)
    if want_ret:
        rtext = got.get('peekRet', '').strip()
        print(f'  peekRet: {rtext or "(no reply)"}')
        m = re.fullmatch(r'\s*([0-9A-Fa-f]{1,4}):([0-9A-Fa-f]{1,4})\s*', rtext)
        if not m:
            print('  RET UNPARSED: peekRet did not answer with a single CS:IP pair')
        else:
            cs, ip = int(m.group(1), 16), int(m.group(2), 16)
            print(f'  peekRet -> file offset {ip + FILE_BIAS:04X} '
                  f'(runtime {cs:03X}:{ip:04X}; asm/ files are named by runtime offset)')
            # Independent derivation: a near call pushed the return IP on top of the
            # stack. If the enclosing routine pushed anything first this word is a
            # saved register and not the return address -- which is exactly why the two
            # numbers are printed side by side instead of one being trusted.
            if 'SS' in regs and 'ESP' in regs:
                sp = regs['ESP'] & 0xFFFF
                raw = read_phys(a.http_port, regs['SS'] * 16 + sp, 2)
                word = raw[0] | (raw[1] << 8)
                agree = 'AGREES' if word == ip else 'DISAGREES'
                print(f'  stack check: SS={regs["SS"]:03X} SP={sp:04X} -> word '
                      f'0x{word:04X}, peekRet {agree}')
            else:
                print('  stack check: skipped, no SS/ESP in the state dump')
    if want_stack:
        report_stack(conn, a, want_stack, regs)


def asm_index(root: str = '') -> list:
    """[(runtime_start, filename)] for every `asm/NNNN_name.asm`, sorted.

    Derived from the tree on demand, never cached by hand — `asm/` is the one lookup
    table (AGENTS.md § 3) and a second hand-maintained copy would drift and lie.
    """
    if not root:
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'asm')
    out = []
    try:
        names = os.listdir(root)
    except OSError:
        return out
    for n in names:
        stem = n.split('_')[0]
        if len(stem) == 4:
            try:
                out.append((int(stem, 16), n))
            except ValueError:
                pass
    out.sort()
    return out


def asm_owner(idx: list, runtime: int):
    """The asm file whose start is the greatest one <= runtime, or None.

    A start match is not proof the address belongs to that routine — the fragments in
    this region are 3-8 bytes and the real function is far larger — so callers get the
    name as a lead, not as a verdict.
    """
    best = None
    for start, name in idx:
        if start <= runtime:
            best = (start, name)
        else:
            break
    return best


def report_stack(conn: RSP, a, depth: int, regs: dict):
    """Dump the guest stack at SS:SP and show which offsets look like return addresses."""
    if not regs:
        regs = parse_state(monitor(conn, 'state'))
    if 'SS' not in regs or 'ESP' not in regs:
        print('  stack: skipped, no SS/ESP in the state dump')
        return
    sp = regs['ESP'] & 0xFFFF
    ss = regs['SS']
    raw = read_phys(a.http_port, ss * 16 + sp, depth)
    idx = asm_index()
    print(f'  stack at SS={ss:03X}:SP={sp:04X} ({depth} bytes, low address first):')
    for i in range(0, len(raw) - 1, 2):
        w = raw[i] | (raw[i + 1] << 8)
        note = ''
        if idx:
            own = asm_owner(idx, w)
            if own and w - own[0] < 0x40:
                note = f'  <- inside {own[1]} (+{w - own[0]:02X})'
        print(f'    +{i:02X}: {w:04X}   (file {w + FILE_BIAS:04X}){note}')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--http-port', type=int, default=None,
                    help='default: the live session\'s recorded port (see tools/s86_ports.py)')
    ap.add_argument('--gdb-port', type=int, default=20190)
    ap.add_argument('--ip', help='runtime offset (hex) to stop at, e.g. 17B7')
    ap.add_argument('--cs', type=lambda x: int(x, 16), default=CS_DEFAULT,
                    help='code segment (hex), default 17D')
    ap.add_argument('--dump', action='append', default=[],
                    help='DS range as OFF:LEN (hex), repeatable')
    ap.add_argument('--timeout', type=float, default=60.0)
    ap.add_argument('--no-continue', action='store_true',
                    help='arm but leave the target stopped (caller resumes)')
    ap.add_argument('--state', action='store_true',
                    help='while stopped, dump monitor state (registers, SS:SP, DI)')
    ap.add_argument('--ret', action='store_true',
                    help='while stopped, dump monitor peekRet (the return address) '
                         'and cross-check it against the word at SS:SP')
    ap.add_argument('--ret-type', default=None,
                    help='peekRet type: NEAR16 NEAR32 FAR16 FAR32 INTERRUPT '
                         'EXTERNAL_INTERRUPT MACHINE (default: emulator default)')
    ap.add_argument('--callstack', action='store_true',
                    help='while stopped, dump monitor callStack')
    ap.add_argument('--stack', type=int, default=0, metavar='N',
                    help='while stopped, dump N bytes of the guest stack at SS:SP as '
                         'words, each annotated with the asm/ routine that owns the '
                         'offset if one does. This is the fallback when peekRet is '
                         'useless, which it is on this binary: Spice86 call tracking '
                         'answers 017D:0000 because the program is not well-behaved '
                         'enough to track (jmp-based dispatch).')
    ap.add_argument('--samples', type=int, default=1, metavar='N',
                    help='collect N stops instead of one. breakCsIp is multi-shot and '
                         'cannot be removed, so one arm plus a resume loop is the cheap '
                         'way to sample a rare event -- a restart per sample was the old '
                         'assumption and it is wrong. Between samples the target runs, so '
                         'whatever drives the game (keys, a walk loop) has to be running '
                         'separately.')
    ap.add_argument('--post-dump', action='append', default=[],
                    help='DS range as OFF:LEN, read AFTER resuming rather than at the '
                         'stop. This is how you see what the stopped code went on to do: '
                         'the value at the break is the input, the post value is the '
                         'result. Repeatable.')
    ap.add_argument('--post-delay', type=float, default=0.6,
                    help='seconds after resume before --post-dump reads (default 0.6)')
    ap.add_argument('--selftest', action='store_true',
                    help='positive and negative control against a live run')
    a = ap.parse_args()
    if a.http_port is None:
        import s86_ports          # sys.path already carries tools/ from the header
        a.http_port = s86_ports.resolve("http")

    ranges = parse_ranges(a.dump)
    post_ranges = parse_ranges(a.post_dump)

    if a.selftest:
        st = status(a.http_port)
        cs_dec, ip_dec = st['cs'], st['ip']
        cs_hex, ip_hex = cs_ip_from_status(cs_dec, ip_dec)
        conn = RSP(a.gdb_port)
        print(f'selftest: CPU is at {cs_hex}:{ip_hex}')
        ok_pos = arm(conn, cs_hex, ip_hex)
        print(f'  positive control armed on the live address: {ok_pos}')
        conn.continue_()
        got = wait_for_pause(a.http_port, ip_dec, 10)
        print(f'  positive control actually stopped: {got is not None}'
              + (f' at {got["cs"]:04X}:{got["ip"]:04X}' if got else ''))
        if got:
            conn.continue_()
        conn.close()
        if not (ok_pos and got):
            print('SELFTEST FAIL: trap did not arm and stop on an executing address')
            return 1
        print('SELFTEST OK -- but this run is now POISONED: Spice86 has no command to')
        print('remove a breakpoint (monitor help lists only breakCycles/breakCsIp/')
        print('breakStop/callStack/peekRet/state/ramx/dumpAll), so the trap armed here')
        print('on the live idle address keeps firing. Restart the run before any')
        print('real capture.')
        return 0

    if not a.ip:
        print('nothing to do (use --ip or --selftest)', file=sys.stderr)
        return 2

    st = status(a.http_port)
    cs_hex, ip_hex = f'{a.cs:X}', a.ip.upper()
    conn = RSP(a.gdb_port)
    if not arm(conn, cs_hex, ip_hex):
        print(f'FAIL: could not arm breakCsIp {cs_hex} {ip_hex}', file=sys.stderr)
        conn.continue_()
        conn.close()
        return 1
    print(f'armed on {cs_hex}:{ip_hex}')

    if not a.no_continue:
        conn.continue_()

    st = wait_for_pause(a.http_port, int(ip_hex, 16), a.timeout)
    if st is None:
        print(f'NO STOP within {a.timeout:.0f}s at {cs_hex}:{ip_hex}')
        # Leaving the socket open after an attach freezes the run (see the docstring),
        # and a frozen run looks exactly like a game that is simply idle.
        conn.continue_()
        conn.close()
        return 3

    print(f'STOP at {st["cs"]:04X}:{st["ip"]:04X} '
          f'(decimal {st["cs"]}:{st["ip"]}, paused={st["paused"]})')

    def dump_all(tag: str, specs):
        for off, n in specs:
            raw = read_ds(a.http_port, off, n)
            print(f'  {tag} DS:{off:02X}..{off + n - 1:02X} = ' +
                  ' '.join(f'{b:02X}' for b in raw) +
                  f'   (as BCD pairs: ' +
                  ' '.join(f'{raw[i]:02d}' for i in range(0, len(raw), 2)) + ')')

    for sample in range(1, max(1, a.samples) + 1):
        if sample > 1:
            st = wait_for_pause(a.http_port, int(ip_hex, 16), a.timeout)
            if st is None:
                print(f'NO STOP within {a.timeout:.0f}s (after {sample - 1} sample(s))')
                break
            print(f'STOP {sample} at {st["cs"]:04X}:{st["ip"]:04X}')
        dump_all('at-stop', ranges)
        if a.ret or a.state or a.stack:
            # Registers per sample: the selector analysis needs AL and EDI at every stop,
            # not just the first. peekRet is useless on this binary, so ask it once.
            report_caller(conn, a, want_state=a.state,
                          want_ret=(a.ret and sample == 1), want_stack=a.stack)
        if a.callstack and sample == 1:
            # Spice86's tracked stack is degenerate on this binary (hundreds of repeated
            # `unknown_017D_4FDF_067AF` frames), so it is context, not evidence. Cap it.
            lines = collect_monitors(conn, ['callStack'], timeout=20.0) \
                .get('callStack', '').splitlines()
            print(f'  callStack ({len(lines)} lines):')
            for line in lines[:40]:
                print(f'    {line}')
            if len(lines) > 40:
                print(f'    ... {len(lines) - 40} more')
        conn.continue_()
        if post_ranges:
            time.sleep(a.post_delay)
            dump_all('post  ', post_ranges)
        if sample < a.samples:
            time.sleep(a.post_delay)

    conn.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
