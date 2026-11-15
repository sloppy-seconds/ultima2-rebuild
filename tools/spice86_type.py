#!/usr/bin/env python3
"""spice86_type.py — inject keystrokes into a HEADLESS Spice86 session.

Why this exists: tools/spice86_keys.py sends keys with xdotool/XTEST, which needs a
focused window. An agent shell on this host has no DISPLAY, no WAYLAND_DISPLAY and no
Xvfb, so headed input is unavailable and every capture looked blocked on a human.

It is not blocked, because the game reads the keyboard the way a real 1983 machine
did. docs/pass-message-rendering.md records that **INT 16h is used at exactly one
site**, asm/4F4D_key_poll (AH=01h to test, AH=00h to consume). INT 16 reads the BIOS
typeahead ring buffer, not a window. So a key is just bytes in memory:

    0040:001E  head   (word, offset within the BDA segment)
    0040:001C  tail   (word)
    0040:001E  ring   16 entries of (scancode, ascii), wrap at 0040:003E

Writing a pair at tail and advancing tail is a key press. Verified: a headless run
parked at CS:F000:003D (the BIOS wait-for-key loop) with head == tail == 0x041E,
i.e. an empty buffer waiting to be filled.

Writes go through Spice86's own MCP endpoint (--McpHttpPort), which is the only
write path; the HTTP API is read-only. This speaks streamable-HTTP MCP directly
(initialize -> notifications/initialized -> tools/call, with
`Accept: application/json, text/event-stream` and the session id header) rather than
registering the server with an agent harness, so it works from any shell, any agent,
and CI with no per-host configuration.

The same MCP session is the only way to build a controlled scenario: `--poke`
writes a byte sequence at a real-segment address and reads it straight back, and
`--peek` reads any address without writing. Both exist so a capture can *force* a
value instead of waiting for the game to produce one — see docs/monster-spawn.md
§ 15.4 for the spawn selector, where the whole measurement is "set DS:60, take one
step, see which arm ran".

Usage:
    python3 tools/spice86_type.py --list                 # what does the API offer?
    python3 tools/spice86_type.py --keys Escape,p        # ESC then P (play)
    python3 tools/spice86_type.py --probe                # show buffer + CS:IP, no write
    python3 tools/spice86_type.py --keys p --delay 0.4
    python3 tools/spice86_type.py --peek 8be:60:1        # read DS:60
    python3 tools/spice86_type.py --poke 8be:60=00       # DS:60 = 0, read back
    python3 tools/spice86_type.py --poke 8be:60=00 --keys Right
    python3 tools/spice86_type.py --keys ctrl-y          # a MODIFIED key (see below)
    python3 tools/spice86_type.py --keymap ctrl-y,alt-x  # show the encoding, send nothing

MODIFIED KEYS. The MCP `send_keyboard_key` tool takes a single PcKeyboardKey and has no
modifier concept at all, so `ctrl-y` cannot be expressed through it — it would arrive as a
plain `Y`. A modified key has to go in the way a real BIOS put it in: as a (scancode, ascii)
pair in the typeahead ring, because that is where the modifier already left its mark. Ctrl+A..Z
is ascii 0x01..0x1A with the letter's own scancode, Alt+letter is ascii 0x00, and Shift+letter
is the uppercase ascii. So `--keys ctrl-y` enqueues (0x15, 0x18) and nothing else. `--keymap`
prints that resolution without touching a running emulator, which is what makes the table
testable at all.
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.request

HTTP = 20180
MCP = 20181
DS = 0x08BE                      # proven in docs/spice86-capture.md
BDA = 0x0040
OFF_HEAD = 0x001A
OFF_TAIL = 0x001C
RING_START = 0x001E
RING_END = 0x003E                # exclusive; 16 entries of 2 bytes

# name -> PcKeyboardKey spelling (SDL's enum, case-insensitive per the tool desc).
# Only what the title screen and driving need so far; single alphanumerics are
# handled generically and do not need an entry here.
KEYS = {
    'escape': 'Escape', 'esc': 'Escape',
    # PcKeyboardKey spells it 'Enter'; 'Return' is rejected as invalid (found
    # 2026-11-02 driving character creation — every Enter sent through this
    # tool before that silently failed).
    'return': 'Enter', 'enter': 'Enter',
    'space': 'Space',
    'up': 'Up', 'down': 'Down', 'left': 'Left', 'right': 'Right',
}


def letter(name):
    """Resolve a friendly key name to a PcKeyboardKey spelling."""
    if len(name) == 1 and name.isalnum():
        return name.upper()
    k = KEYS.get(name.lower())
    if k is None:
        raise SystemExit('unknown key %r — known: %s (single letters/digits always work)'
                         % (name, ', '.join(sorted(set(KEYS)))))
    return k


# BIOS make-codes, set 1, for every key a modifier can legally sit on. These are the
# keyboard controller's own codes, not SDL's, and they are what the typeahead ring stores.
# Letters are laid out as the physical rows put them, which is why they are not contiguous.
SCAN_AZ = {
    'Q': 0x10, 'W': 0x11, 'E': 0x12, 'R': 0x13, 'T': 0x14, 'Y': 0x15, 'U': 0x16,
    'I': 0x17, 'O': 0x18, 'P': 0x19,
    'A': 0x1E, 'S': 0x1F, 'D': 0x20, 'F': 0x21, 'G': 0x22, 'H': 0x23, 'J': 0x24,
    'K': 0x25, 'L': 0x26,
    'Z': 0x2C, 'X': 0x2D, 'C': 0x2E, 'V': 0x2F, 'B': 0x30, 'N': 0x31, 'M': 0x32,
}
SCAN_09 = {d: i for i, d in enumerate('1234567890', start=0x02)}


def resolve_key(name):
    """Resolve a key spec into how it must be injected.

    Returns ('mcp', spelling) for a plain key, or ('bios', scancode, ascii) for a modified
    one. The split is not cosmetic: the MCP tool cannot express a modifier, so anything with
    one MUST go through the ring or it silently arrives unmodified — a Ctrl-Y that lands as
    'Y' is the exact plausible-wrong outcome this returns instead of.

    Accepted: 'y', 'Y', 'ctrl-y', 'ctrl-Y', 'ctrlshift-y', 'alt-x', 'shift-y'.
    """
    mods, base = [], name.strip()
    while '-' in base:
        head, rest = base.split('-', 1)
        h = head.lower()
        if h in ('ctrl', 'control', 'alt', 'shift'):
            mods.append(h if h != 'control' else 'ctrl')
            base = rest
        else:
            break
    if not mods:
        return ('mcp', letter(name))

    base = base.upper()
    if len(base) != 1 or base not in SCAN_AZ:
        raise SystemExit(f"{name!r}: modifiers are supported for letters only "
                         f"(ctrl-y, alt-x, shift-y)")
    scan = SCAN_AZ[base]
    if 'ctrl' in mods:
        # Ctrl+letter is ascii 0x01..0x1A, the convention the BIOS ring stores and the
        # game's own command parser compares against.
        return ('bios', scan, ord(base) - ord('A') + 1)
    if 'alt' in mods:
        # Alt leaves the scancode and a null ascii; the modifier lives in the BDA flag byte.
        return ('bios', scan, 0x00)
    if 'shift' in mods:
        return ('bios', scan, ord(base))
    raise SystemExit(f"{name!r}: unhandled modifier")


# ---------------------------------------------------------------- HTTP read side
def read_mem(seg, off, n, port=HTTP):
    phys = seg * 16 + off
    url = 'http://127.0.0.1:%d/api/memory/%d/range/%d' % (port, phys, n)
    with urllib.request.urlopen(url, timeout=10) as r:
        return bytes(json.load(r)['values'])


def status(port=HTTP):
    with urllib.request.urlopen('http://127.0.0.1:%d/api/status' % port,
                                timeout=10) as r:
        return json.load(r)


def keyboard_state(port=HTTP):
    bda = read_mem(BDA, 0x00, 0x40, port)
    head = int.from_bytes(bda[OFF_HEAD:OFF_HEAD + 2], 'little')
    tail = int.from_bytes(bda[OFF_TAIL:OFF_TAIL + 2], 'little')
    return head, tail, bda[RING_START:RING_END]


# ---------------------------------------------------------------- MCP write side
class Mcp:
    """Minimal streamable-HTTP MCP client: handshake once, then call tools."""

    def __init__(self, port=MCP):
        self.url = 'http://127.0.0.1:%d/mcp' % port
        self.session = None
        self._id = 0

    def _post(self, method, params=None, notify=False):
        self._id += 1
        body = {'jsonrpc': '2.0', 'method': method}
        if params is not None:
            body['params'] = params
        if not notify:
            body['id'] = self._id
        req = urllib.request.Request(self.url, data=json.dumps(body).encode(),
                                     headers={
                                         'Content-Type': 'application/json',
                                         'Accept': 'application/json, '
                                                   'text/event-stream',
                                         **({'Mcp-Session-Id': self.session}
                                            if self.session else {})})
        with urllib.request.urlopen(req, timeout=30) as r:
            sid = r.headers.get('Mcp-Session-Id')
            if sid:
                self.session = sid
            raw = r.read().decode('utf-8', 'replace')
        if notify:
            return None
        # The reply is either bare JSON or an SSE stream; pull the last data line.
        for line in raw.splitlines():
            if line.startswith('data:'):
                raw = line[5:].strip()
                break
        try:
            return json.loads(raw)
        except ValueError:
            return {'raw': raw[:400]}

    def open(self):
        r = self._post('initialize', {
            'protocolVersion': '2024-11-05', 'capabilities': {},
            'clientInfo': {'name': 'spice86_type', 'version': '1'}})
        self._post('notifications/initialized', notify=True)
        return r

    def tools(self):
        return self._post('tools/list')

    def call(self, name, args):
        return self._post('tools/call', {'name': name, 'arguments': args})


def write_mem(mcp, seg, off, data_hex):
    """write_memory takes segment/offset/data(hex string); try the shapes we know."""
    for args in ({'segment': seg, 'offset': off, 'data': data_hex},
                 {'Segment': seg, 'Offset': off, 'Data': data_hex}):
        r = mcp.call('write_memory', args)
        if r and 'error' not in r and not (r.get('result') or {}).get('isError'):
            return r
    return r


# ---------------------------------------------------------------- the injection
def send_key(mcp, keyname):
    """Full press: down then up, which is what the tool's description requires."""
    down = mcp.call('send_keyboard_key', {'key': keyname, 'isPressed': True})
    up = mcp.call('send_keyboard_key', {'key': keyname, 'isPressed': False})
    for r in (down, up):
        if r and 'error' in r:
            return r
    return up


def enqueue_bios(mcp, port, scan, asc):
    """Fallback: poke the BIOS typeahead ring directly (see module docstring)."""
    head, tail, _ = keyboard_state(port)
    if not (RING_START <= tail < RING_END):
        tail = RING_START
    write_mem(mcp, BDA, tail, '%02x%02x' % (scan, asc))
    nxt = tail + 2
    if nxt >= RING_END:
        nxt = RING_START
    write_mem(mcp, BDA, OFF_TAIL, '%04x' % nxt)
    return tail


def parse_addr(spec, allow_len):
    """'8be:60:1' or '8be:60=00' -> (seg, off, n_or_bytes). Rejects silently-wrong input.

    Hex throughout, because every other address in this project is hex and a
    decimal segment here would read a different segment than the one being poked.
    """
    try:
        if allow_len and '=' in spec:
            left, data = spec.split('=', 1)
            seg, off = (int(p, 16) for p in left.split(':', 1))
            if not data or len(data) % 2:
                raise ValueError('data must be whole hex bytes')
            return seg, off, bytes.fromhex(data)
        parts = spec.split(':')
        seg, off = int(parts[0], 16), int(parts[1], 16)
        n = int(parts[2], 16) if len(parts) > 2 else 1
        return seg, off, n
    except (ValueError, IndexError) as e:
        raise SystemExit(f'bad address {spec!r}: use SEG:OFF[:LEN] or SEG:OFF=HEX ({e})')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--http-port', type=int, default=HTTP)
    ap.add_argument('--mcp-port', type=int, default=MCP)
    ap.add_argument('--peek', action='append', metavar='SEG:OFF[:LEN]',
                    help='read memory (hex addr), repeatable; works without the MCP port')
    ap.add_argument('--poke', action='append', metavar='SEG:OFF=HEX',
                    help='write hex bytes at a real-segment address, repeatable; '
                         'always prints the read-back, because a write that did not '
                         'land is the failure mode this project keeps hitting')
    ap.add_argument('--keys', help='comma/space separated: Escape,p,ctrl-y '
                                   '(ctrl-/alt-/shift- prefixes go through the BIOS ring)')
    ap.add_argument('--keymap', metavar='SPEC',
                    help='resolve key specs and print the encoding; sends nothing and '
                         'needs no running emulator')
    ap.add_argument('--selftest', action='store_true',
                    help='check the modifier table against known control-byte conventions '
                         'and exit; needs no running emulator')

    ap.add_argument('--delay', type=float, default=0.6,
                    help='seconds between keys (the game drains pending keys)')
    ap.add_argument('--list', action='store_true', help='list MCP tools and exit')
    ap.add_argument('--probe', action='store_true', help='show state, write nothing')
    a = ap.parse_args()

    # Resolving a key must be checkable without a running emulator, or the modifier table is
    # the kind of code that is only ever exercised by the one capture it was written for.
    # Resolving a key must be checkable without a running emulator, or the modifier table is
    # the kind of code that is only ever exercised by the one capture it was written for.
    if a.selftest:
        # Externally verifiable conventions, not this file's opinions: Ctrl-A..Z is 0x01..0x1A
        # (Ctrl-Z = 0x1A is the DOS end-of-file byte), Ctrl-S/Ctrl-Q are XOFF/XON at 0x13/0x11,
        # and the scancodes are the keyboard controller's own make-codes. A wrong byte here
        # does not error, it silently sends a different command to the game.
        expect = [('ctrl-a', 0x1E, 0x01), ('ctrl-z', 0x2C, 0x1A), ('ctrl-y', 0x15, 0x19),
                  ('ctrl-s', 0x1F, 0x13), ('ctrl-q', 0x10, 0x11), ('ctrl-r', 0x13, 0x12),
                  ('alt-x', 0x2D, 0x00), ('shift-y', 0x15, 0x59)]
        bad = 0
        for spec, scan, asc in expect:
            got = resolve_key(spec)
            ok = got == ('bios', scan, asc)
            bad += not ok
            print('  %-4s %-10s want scan=0x%02X ascii=0x%02X got %s'
                  % ('ok' if ok else 'BAD', spec, scan, asc, got[1:]))
        plain = resolve_key('y')
        if plain != ('mcp', 'Y'):
            bad += 1
            print('  BAD  plain y must stay on the MCP path, got %s' % (plain,))
        try:
            resolve_key('ctrl-Right')
            bad += 1
            print('  BAD  ctrl-Right should be refused, not silently downgraded to Right')
        except SystemExit:
            print('  ok   ctrl-Right refused rather than silently sent as Right')
        print('SPICE86_TYPE SELFTEST: %s (%d cases)' % ('PASS' if not bad else 'FAIL', len(expect) + 2))
        return 1 if bad else 0

    if a.keymap:
        for spec in [k for k in a.keymap.replace(',', ' ').split() if k]:
            r = resolve_key(spec)
            if r[0] == 'bios':
                print('%-14s -> bios ring  scan=0x%02X ascii=0x%02X' % (spec, r[1], r[2]))
            else:
                print('%-14s -> mcp key    %s' % (spec, r[1]))
        return

    st = status(a.http_port)
    print('CS:%04X IP:%04X cycles=%d' % (st['cs'], st['ip'], st['cycles']))
    head, tail, ring = keyboard_state(a.http_port)
    print('BDA head=0x%04X tail=0x%04X ring=%s'
          % (head, tail, ring[:8].hex(' ')))
    print('DS:36 name=%r' % read_mem(DS, 0x36, 16, a.http_port))
    if a.probe:
        return

    for spec in (a.peek or []):
        seg, off, n = parse_addr(spec, allow_len=False)
        print('peek %04X:%04X len %d = %s'
              % (seg, off, n, read_mem(seg, off, n, a.http_port).hex(' ')))
    if a.peek and not a.poke and not a.keys:
        return

    mcp = Mcp(a.mcp_port)
    init = mcp.open()
    if a.list:
        tl = mcp.tools()
        tools = (tl or {}).get('result', {}).get('tools', [])
        print('\nMCP session=%s, %d tools:' % (mcp.session, len(tools)))
        for t in tools:
            req = (t.get('inputSchema') or {}).get('required', [])
            print('  %-22s %s' % (t.get('name'), ','.join(req)))
        return

    names = [k for k in (a.keys or '').replace(',', ' ').split() if k]
    if not names and not a.poke:
        raise SystemExit('nothing to do (use --keys, --poke or --probe)')

    for spec in (a.poke or []):
        seg, off, data = parse_addr(spec, allow_len=True)
        r = write_mem(mcp, seg, off, data.hex())
        bad = (r or {}).get('error') or (r or {}).get('result', {}).get('isError')
        back = read_mem(seg, off, len(data), a.http_port)
        print('poke %04X:%04X = %s -> read back %s %s'
              % (seg, off, data.hex(' '), back.hex(' '),
                 'MISMATCH' if back != data else ('FAILED %s' % bad if bad else 'ok')))
        if back != data:
            raise SystemExit('poke did not land — refusing to continue on a write that lied')

    for name in names:
        r_key = resolve_key(name)
        if r_key[0] == 'bios':
            _, scan, asc = r_key
            slot = enqueue_bios(mcp, a.http_port, scan, asc)
            print('sent %-8s as bios scan=0x%02X ascii=0x%02X at 0x%04X'
                  % (name, scan, asc, slot))
        else:
            r = send_key(mcp, r_key[1])
            bad = (r or {}).get('error') or (r or {}).get('result', {}).get('isError')
            print('sent %-8s as %-8s %s'
                  % (name, r_key[1], 'FAILED %s' % bad if bad else 'ok'))
        time.sleep(a.delay)
        s2 = status(a.http_port)
        h2, t2, _ = keyboard_state(a.http_port)
        print('   -> CS:%04X IP:%04X  head=0x%04X tail=0x%04X'
              % (s2['cs'], s2['ip'], h2, t2))
    print('DS:36 name=%r' % read_mem(DS, 0x36, 16, a.http_port))


if __name__ == '__main__':
    sys.exit(main())
