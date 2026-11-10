#!/usr/bin/env python3
"""census_viewer.py — the tile census as a browsable review page.

Why: `disasm/census/tile.tsv` is the one lookup for what a tile IS, but a TSV is the wrong
surface for the question "which of these names are wrong". Naming a tile is a visual judgement,
and reading hex codes at a terminal is how wrong names survive. This renders every art index
with its image beside its name, grade and characteristics, so a human can scan it and say "that
one is a swamp, not shallow water" — which is exactly how art 0x01 got corrected.

It is a REVIEW surface, not a gate. It reads the generated census and never writes it; the
census has its own `--verify` and `--check-fresh`, and duplicating those here would be a second
definition of the same rules.

Usage:
    python3 tools/census_viewer.py --serve                 # http://host:8096/
    python3 tools/census_viewer.py -o /var/tmp/census.html
    python3 tools/census_viewer.py --serve --port 8096 --bind 127.0.0.1

Tile art comes from two places and both are shown side by side, because the page is used to
judge both: `tmp/tiles/` holds the extracted originals (gitignored, from the player's own copy)
and `engine/assets/tiles/cleanroom/tiles/` holds our substitute set.
"""

import argparse
import base64
import csv
import html
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CENSUS = os.path.join(ROOT, 'disasm', 'census', 'tile.tsv')

# Columns shown in the table, in order, with the header label. Anything not listed is still
# present in the row's data-* attributes so a reviewer can read it in the page source.
COLUMNS = [
    ('art', 'art'),
    ('map_byte', 'map'),
    ('gate_value', 'gate'),
    ('kind', 'kind'),
    ('role', 'role'),
    ('sign_part', 'sign part'),
    ('name', 'NAME'),
    ('name_grade', 'name grade'),
    ('walkable', 'walk'),
    ('entry_effect', 'on entry'),
    ('entry_message', 'message'),
    ('boardable_by', 'board'),
    ('creature_hp', 'hp'),
    ('creature_damage', 'dmg'),
    ('creature_special', 'special'),
    ('instances', 'count'),
    ('description', 'description (measured pixels)'),
    ('note', 'note'),
]


def read_census(path):
    """The census TSV, skipping the generator's comment lines.

    The first non-comment line is the header; the row before it is a JSON summary the census
    writes for freshness tooling, and treating either as data would add phantom rows.
    """
    with open(path) as fh:
        lines = [l for l in fh if not l.startswith('#')]
    return list(csv.DictReader(lines, delimiter='\t'))


def load_images(subdir, ext, optional=True):
    """Art as data URIs, keyed by art index. Same convention as tools/map_viewer.py."""
    out = {}
    d = os.path.join(ROOT, subdir)
    for n in range(64):
        p = os.path.join(d, 'tile_%02x.%s' % (n, ext))
        if not os.path.exists(p):
            p = os.path.join(d, 'tile_%02X.%s' % (n, ext))
        if not os.path.exists(p):
            if optional:
                return {}
            sys.exit('missing tile %s' % p)
        if ext == 'svg':
            svg = open(p, encoding='utf-8').read()
            if 'width=' not in svg.split('>', 1)[0]:
                # committed SVGs carry a viewBox but no intrinsic size, and an <img> will not
                # scale one that has none
                svg = svg.replace('<svg ', '<svg width="16" height="16" ', 1)
            data = svg.encode()
        else:
            data = open(p, 'rb').read()
        mime = 'image/svg+xml' if ext == 'svg' else 'image/png'
        out[n] = 'data:%s;base64,%s' % (mime, base64.b64encode(data).decode())
    return out


def esc(v):
    return html.escape(str(v if v is not None else ''))


def build(rows, orig, clean):
    cards = []
    for r in rows:
        try:
            ai = int(r['art'], 16)
        except (KeyError, ValueError):
            continue
        unnamed = not (r.get('name') or '').strip()
        cls = 'card unnamed' if unnamed else 'card'
        role = esc(r.get('role', ''))
        imgs = ''
        for src, label in ((orig, 'original'), (clean, 'cleanroom')):
            if ai in src:
                imgs += ('<figure><img src="%s" alt="%s" width="64" height="64">'
                         '<figcaption>%s</figcaption></figure>' % (src[ai], label, label))
        fields = ''.join(
            '<tr><th>%s</th><td>%s</td></tr>' % (esc(h), esc(r.get(k, '')))
            for k, h in COLUMNS if k not in ('art', 'map_byte', 'gate_value'))
        cards.append(
            '<div class="%s" data-role="%s" data-unnamed="%s" data-art="%s">'
            '<div class="hdr"><span class="art">%s</span>'
            '<span class="bytes">map %s · gate %s</span></div>'
            '<div class="imgs">%s</div>'
            '<table>%s</table></div>'
            % (cls, role, 'yes' if unnamed else 'no', esc(r.get('art', '')),
               esc(r.get('art', '')), esc(r.get('map_byte', '')),
               esc(r.get('gate_value', '')), imgs, fields))

    roles = sorted({r.get('role', '') for r in rows if r.get('role')})
    opts = ''.join('<option value="%s">%s</option>' % (esc(x), esc(x)) for x in roles)
    n_unnamed = sum(1 for r in rows if not (r.get('name') or '').strip())

    # Token substitution rather than %-formatting: the CSS below contains literal percent
    # signs (width:100%), which %-formatting reads as a broken conversion.
    return ("""<!doctype html><meta charset=utf-8>
<title>Ultima II tile census — review</title>
<style>
 body{background:#141414;color:#ddd;font:13px/1.45 system-ui,sans-serif;margin:0;padding:16px}
 h1{font-size:17px;margin:0 0 4px}
 .sub{color:#888;margin:0 0 12px}
 #bar{position:sticky;top:0;background:#141414;padding:10px 0;border-bottom:1px solid #333;
      margin-bottom:14px;z-index:2}
 #bar label{margin-right:14px;color:#aaa}
 select,input[type=search]{background:#222;color:#ddd;border:1px solid #444;padding:4px 6px}
 #grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(330px,1fr));gap:12px}
 .card{background:#1b1b1b;border:1px solid #333;padding:10px}
 .card.unnamed{border-color:#a33;background:#1f1616}
 .hdr{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:6px}
 .art{font:15px monospace;color:#ffd479}
 .bytes{color:#777;font:11px monospace}
 .imgs{display:flex;gap:10px;margin:6px 0 8px}
 figure{margin:0;text-align:center}
 figcaption{color:#666;font-size:10px;margin-top:2px}
 img{image-rendering:pixelated;background:#000;border:1px solid #333}
 table{border-collapse:collapse;width:100%;font-size:12px}
 th{text-align:left;color:#7d9;font-weight:normal;white-space:nowrap;vertical-align:top;
    padding:2px 8px 2px 0;width:96px}
 td{padding:2px 0;vertical-align:top}
 .hide{display:none}
 .miss{color:#e66}
</style>
<h1>Ultima II tile census</h1>
<p class="sub">@@TOTAL@@ art indices · <b class="miss">@@UNNAMED@@ unnamed</b> (red border) ·
generated by <code>tools/census.py</code>, read from <code>disasm/census/tile.tsv</code>.
Left image is the extracted original, right is our cleanroom substitute.</p>
<div id="bar">
 <label>role <select id="role"><option value="">all</option>@@OPTS@@</select></label>
 <label><input type="checkbox" id="only"> unnamed only</label>
 <label>search <input type="search" id="q" placeholder="name, description, note…"></label>
 <span id="count" style="color:#777"></span>
</div>
<div id="grid">@@CARDS@@</div>
<script>
const grid=[...document.querySelectorAll('.card')];
function filt(){
  const r=document.getElementById('role').value,
        u=document.getElementById('only').checked,
        q=document.getElementById('q').value.toLowerCase();
  let n=0;
  for(const c of grid){
    let ok=(!r||c.dataset.role===r)&&(!u||c.dataset.unnamed==='yes')&&
           (!q||c.textContent.toLowerCase().includes(q));
    c.classList.toggle('hide',!ok); if(ok)n++;
  }
  document.getElementById('count').textContent=n+' of '+grid.length+' shown';
}
for(const el of ['role','only','q'])
  document.getElementById(el).addEventListener('input',filt);
filt();
</script>
"""
        .replace('@@CARDS@@', '\n'.join(cards))
        .replace('@@OPTS@@', opts)
        .replace('@@TOTAL@@', str(len(rows)))
        .replace('@@UNNAMED@@', str(n_unnamed)))


def serve(page, bind, port):
    """Host the page and nothing else.

    The document is built in memory and returned for every path, so the server has no
    filesystem root. The obvious implementation is `python3 -m http.server` in the repo, and
    this repo contains the player's game files, extracted originals and per-launch run copies.
    A review page must not also be a LAN mirror of the game. Same rule as tools/map_viewer.py.
    """
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    body = page.encode('utf-8')

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def do_GET(self):
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    httpd = ThreadingHTTPServer((bind, port), Handler)
    print('serving the tile census on http://%s:%d/  (Ctrl-C to stop)' % (bind, port))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print('\nstopped')
    finally:
        httpd.server_close()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--census', default=CENSUS)
    ap.add_argument('--tiles', default='tmp/tiles', help='extracted original PNGs')
    ap.add_argument('--cleanroom', default='engine/assets/tiles/cleanroom/tiles')
    ap.add_argument('-o', '--out', help='write the page here (mutually exclusive with --serve)')
    ap.add_argument('--serve', action='store_true', help='host it instead of writing a file')
    ap.add_argument('--port', type=int, default=8096)
    ap.add_argument('--bind', default='0.0.0.0')
    a = ap.parse_args()

    if bool(a.serve) == bool(a.out):
        sys.exit('pick one: -o FILE to write it, or --serve to host it')
    if a.out:
        low = os.path.abspath(a.out).lower()
        if '/original/' in low or '/references/' in low:
            sys.exit('refusing to write into original/ or references/')

    if not os.path.exists(a.census):
        sys.exit('%s is missing; run python3 tools/census.py first' % a.census)

    rows = read_census(a.census)
    page = build(rows, load_images(a.tiles, 'png'), load_images(a.cleanroom, 'svg'))

    if a.out:
        with open(a.out, 'w') as fh:
            fh.write(page)
        print('wrote %s (%d KB, %d tiles)' % (a.out, len(page) // 1024, len(rows)))
    else:
        serve(page, a.bind, a.port)


if __name__ == '__main__':
    main()
