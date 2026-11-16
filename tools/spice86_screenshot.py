#!/usr/bin/env python3
"""spice86_screenshot.py — grab the emulator screen from a HEADLESS session.

Why: the game renders in CGA graphics mode, so reading 0xB8000 as a text page returns
garbage (this happened, and the garbage was nearly read as a result). The Spice86 MCP
endpoint exposes a `screenshot` tool that returns PNG base64 inline and works without a
display — see docs/spice86-capture.md, "screenshot, headless | MCP screenshot | no".

Reuses the MCP streamable-HTTP handshake already implemented in spice86_type.py rather
than duplicating it.

Usage:
    python3 tools/spice86_screenshot.py out.png
    python3 tools/spice86_screenshot.py --list-tools
"""
import argparse
import base64
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from spice86_type import MCP, Mcp  # noqa: E402


def extract_png(result):
    """Pull PNG bytes out of a tools/call result, however it is wrapped.

    The vendored Spice86 MCP returns `screenshot` under
    `result.content[]` as an image part, and also exposes structuredContent with
    Width/Height/Format/MimeType/FilePath. Only the base64 image part is useful here.
    """
    if result is None:
        return None
    res = result.get('result', result) if isinstance(result, dict) else result
    blobs = []
    if isinstance(res, dict):
        for c in res.get('content', []) or []:
            if isinstance(c, dict):
                if c.get('type') == 'image' and c.get('data'):
                    blobs.append(c['data'])
                elif c.get('type') == 'text' and c.get('text'):
                    blobs.append(c['text'])
        if not blobs and res.get('data'):
            blobs.append(res['data'])
    elif isinstance(res, str):
        blobs.append(res)
    for b in blobs:
        if not isinstance(b, str):
            continue
        s = b.strip()
        m = re.search(r'data:image/png;base64,([A-Za-z0-9+/=]+)', s)
        if m:
            s = m.group(1)
        try:
            raw = base64.b64decode(s, validate=False)
        except Exception:
            continue
        if raw[:8] == b'\x89PNG\r\n\x1a\n':
            return raw
    return None


def tool_names(client):
    """Full tool list. Do NOT truncate the JSON before scanning it: this endpoint
    exposes 63 tools, and a 4000-char slice showed only 4 and made `screenshot`
    look absent. That misreading cost a detour into hand-decoding the VGA buffer."""
    t = client.tools()
    res = t.get('result', t) if isinstance(t, dict) else t
    return [x['name'] for x in res.get('tools', [])]


def read_video_state(client):
    r = client.call('read_video_state', {})
    return (r or {}).get('result', {}).get('structuredContent', r)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('out', nargs='?', help='path to write the PNG to')
    ap.add_argument('--mcp-port', type=int, default=None,
                    help="default: the live session's recorded port (see tools/s86_ports.py)")
    ap.add_argument('--list-tools', action='store_true')
    a = ap.parse_args()
    if a.mcp_port is None:
        import s86_ports          # sys.path already carries tools/ from the header
        a.mcp_port = s86_ports.resolve("mcp")

    c = Mcp(a.mcp_port)
    c.open()
    if a.list_tools:
        names = tool_names(c)
        print('%d tools' % len(names))
        for n in names:
            print('  ' + n)
        return
    if not a.out:
        ap.error('need an output path, or --list-tools')

    r = c.call('screenshot', {})
    png = extract_png(r)
    if png is None:
        print('screenshot tool returned no PNG; raw follows (first 600 chars):')
        print(json.dumps(r)[:600])
        sys.exit(1)
    with open(a.out, 'wb') as f:
        f.write(png)
    print('wrote %s (%d bytes PNG)' % (a.out, len(png)))


if __name__ == '__main__':
    main()
