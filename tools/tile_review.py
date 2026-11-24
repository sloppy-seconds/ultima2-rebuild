#!/usr/bin/env python3
"""
tile_review.py — a per-tile decision sheet: original vs cleanroom, with evidence.

Why this exists
---------------
Naming a tile from a description is how arts `13` and `14` ended up called signpost and
spire when they are the plane and the rocket. The shape was described accurately and the
noun was guessed, and the guess was baked into generated art and a legal provenance record
before anyone looked. The fix is not a better guess: it is putting the drawing in front of
the person who can make the call, next to the evidence, for all 64 entries at once.

Each row shows:
  * the art index, its map byte, and the class from disasm/tile-art.tsv
  * what the SPEC asks the substitute to read as (docs/tile-art-spec.md § 7)
  * what the ORIGINAL actually looks like (tile-art.tsv description + notes)
  * a 2x2 grid of the cleanroom substitute and a 2x2 grid of the extracted original

The 2x2 repeat is deliberate and is not decoration: a single tile cannot show a seam.
Ground tiles in particular must tile without visible gutters, and that only becomes
visible when four of them sit together.

Verdicts are recorded in the browser (localStorage) and exported as markdown, so the
output of a review pass is a list an agent can act on rather than a conversation.

Data sources are the same ones tools/map_viewer.py uses. Original PNGs come from
tools/tile_atlas.py and live under tmp/ (game content, never committed).

Usage:
    python3 tools/tile_atlas.py --out tmp/tiles        # if tiles are missing
    python3 tools/tile_review.py -o tmp/tile-review.html
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import re
import sys

SPEC_SECTIONS = ("Ground", "Structures", "Monster sprites", "Other figures",
                 "Blocks and borders")


def load_tiles(tiledir):
    """Extracted original art: 16x16 PNGs, one per art index. Optional."""
    out = {}
    for n in range(64):
        p = os.path.join(tiledir, "tile_%02X.png" % n)
        if os.path.exists(p):
            out[n] = "data:image/png;base64," + base64.b64encode(open(p, "rb").read()).decode()
    return out


def load_cleanroom(dirpath, palette="engine"):
    """Cleanroom substitute SVGs. The committed files carry a viewBox but no
    width/height, which is correct for inline use but not as a CSS background, so
    the attributes are injected here at embed time.

    palette="engine" remaps the SVG's bright spec hexes onto the RGB the engine
    actually displays (core/frame.c), which is also what tools/tile_atlas.py used for
    the originals. Without this the cleanroom pane looks brighter than the original
    pane for a purely cosmetic reason, and that difference leaks into judgements about
    shape. tiles2h.py packs colour indices, not RGB, so this is preview only and never
    touches the bytes the engine draws.
    """
    REMAP = {"#55FFFF": "#00AAAA", "#FF55FF": "#AA00AA"}
    out = {}
    for n in range(64):
        p = os.path.join(dirpath, "tile_%02X.svg" % n)
        if not os.path.exists(p):
            sys.exit("missing cleanroom tile %s" % p)
        svg = open(p).read()
        if "width=" not in svg.split(">")[0]:
            svg = svg.replace("<svg ", '<svg width="16" height="16" ', 1)
        if palette == "engine":
            for a, b in REMAP.items():
                svg = svg.replace(a, b)
        out[n] = "data:image/svg+xml;base64," + base64.b64encode(svg.encode()).decode()
    return out


def load_tsv(path):
    """disasm/tile-art.tsv: id, hex, kind, description, glyph, notes."""
    art = {}
    for line in open(path):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        p = line.split("\t")
        # Indexed from 0: the file used to carry a phantom empty first column, and this
        # function had silently adapted by starting at p[1]. Both moved together.
        if len(p) < 4 or p[0].strip() == "id":
            continue
        art[int(p[0].strip(), 16)] = {
            "byte": p[1].strip(), "kind": p[2].strip(), "desc": p[3].strip(),
            "glyph": p[4].strip() if len(p) > 4 else "",
            "note": p[5].strip() if len(p) > 5 else "",
        }
    return art


def load_spec(path):
    """docs/tile-art-spec.md § 7: the name and 'must read as' the implementer was given.

    Parsed rather than hand-copied on purpose — a second hand-maintained copy of the
    spec would drift from it and then argue with it.
    """
    text = open(path).read()
    try:
        body = text[text.index("## 7. Per-tile specification"):text.index("## 8.")]
    except ValueError:
        sys.exit("cannot find § 7 in %s" % path)
    spec = {}
    for sec in SPEC_SECTIONS:
        m = re.search(r"### %s\n(.*?)(?=\n### |\Z)" % re.escape(sec), body, re.S)
        if not m:
            continue
        for row in re.findall(r"^\| `([0-9A-Fa-f]{2})` \| `0x([0-9A-Fa-f]{2})` \| ([^|]+)\| ([^|]+)\|",
                              m.group(1), re.M):
            a = int(row[0], 16)
            spec[a] = {"cls": sec.lower(), "name": row[2].strip(), "reads": row[3].strip(),
                       "byte": "0x" + row[1].upper()}
    # Letters A-Z use a paired art|letter layout.
    lm = re.search(r"### Letters A.*?\n(.*?)(?=\n### |\Z)", body, re.S)
    if lm:
        # Three art|letter pairs share pipes on one row, so the trailing pipe must be
        # a lookahead: consuming it makes find() skip every middle column (28-2F, I-P).
        for row in re.findall(r"\| `([0-9A-Fa-f]{2})` \| ([A-Z]) (?=\|)", lm.group(1)):
            spec[int(row[0], 16)] = {"cls": "letter", "name": "letter %s" % row[1],
                                     "reads": "the capital letter %s" % row[1], "byte": ""}
    return spec


HTML = r"""<!doctype html>
<html><head><meta charset="utf-8"><title>Ultima II tile review</title><style>
:root{color-scheme:dark}
body{font:13px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace;background:#111;color:#ddd;
     margin:0;padding:14px}
h1{font-size:15px;margin:0 0 4px}
a{color:#8cf}
#bar{position:sticky;top:0;background:#111;border-bottom:1px solid #333;padding:8px 0;
     margin-bottom:10px;display:flex;gap:14px;align-items:center;flex-wrap:wrap;z-index:5}
#bar label{color:#8fa}
select,input[type=number]{background:#1b1b1b;color:#ddd;border:1px solid #444;padding:4px;font:inherit}
button{background:#1b1b1b;color:#ddd;border:1px solid #555;padding:4px 9px;font:inherit;cursor:pointer}
button:hover{border-color:#8cf}
#grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(430px,1fr));gap:10px}
.card{border:1px solid #333;background:#161616;padding:9px}
.card.flag{border-color:#a70}
.card.fix{border-color:#d44}
.card.ok{border-color:#3a5}
.head{display:flex;gap:8px;align-items:baseline;margin-bottom:6px;flex-wrap:wrap}
.head b{color:#ffd479;font-size:14px}
.head .byte{color:#8fa}
.head .kind{color:#888;border:1px solid #444;padding:0 5px;font-size:11px}
.arts{display:flex;gap:10px;margin:6px 0}
.pane{text-align:center}
.pane b{display:block;color:#8fa;font-weight:normal;font-size:11px;margin-bottom:3px}
.sq{border:1px solid #555;background-color:#000;background-repeat:repeat;
    image-rendering:pixelated;display:inline-block}
.txt{font-size:12px;margin:3px 0}
.txt span{color:#777}
.spec{color:#cfe6b8}
.evid{color:#e6d3a8}
.note{color:#9ab;font-size:11px;margin-top:3px;white-space:pre-wrap}
.verdict{display:flex;gap:6px;margin-top:7px;align-items:center}
.verdict button{font-size:11px;padding:2px 7px}
.verdict button.sel{background:#2a3f57;border-color:#8cf;color:#fff}
.verdict input{flex:1;background:#1b1b1b;color:#ddd;border:1px solid #444;padding:3px;font:inherit}
#out{width:100%;height:180px;background:#0d0d0d;color:#cfe;border:1px solid #444;
     font:12px/1.4 ui-monospace,monospace;margin-top:8px;display:none}
#count{color:#888}
</style></head><body>
<div id="bar">
  <h1>Ultima II tile review</h1>
  <a href="map-viewer.html">sector map viewer</a>
  <label>class <select id="fcls"></select></label>
  <label>only <select id="fver">
    <option value="">all</option><option value="fix">needs fix</option>
    <option value="ok">looks right</option><option value="unsure">unsure</option>
    <option value="_">unjudged</option></select></label>
  <label>repeat <select id="rep"><option value="2">2x2</option><option value="3">3x3</option>
    <option value="1">1x1</option></select></label>
  <label>zoom <input id="zoom" type="number" min="2" max="10" value="4" style="width:52px"></label>
  <span id="palnote" style="color:#777">preview normalised to the engine palette</span>
  <button id="exp">export verdicts</button>
  <button id="clr">clear</button>
  <span id="count"></span>
</div>
<div id="grid"></div>
<textarea id="out" readonly></textarea>
<script>
const ORIG=%ORIG%, CLEAN=%CLEAN%, TSV=%TSV%, SPEC=%SPEC%;
const KEY='u2-tile-verdicts';
let V=JSON.parse(localStorage.getItem(KEY)||'{}');
const $=id=>document.getElementById(id);
const clsOf=a=>SPEC[a]?SPEC[a].cls:(TSV[a]?TSV[a].kind:'other');

function sq(uri,z,rep){
  const d=document.createElement('div');d.className='sq';
  d.style.width=d.style.height=(16*z*rep)+'px';
  if(uri){d.style.backgroundImage='url("'+uri+'")';d.style.backgroundSize=(16*z)+'px '+(16*z)+'px';}
  else{d.style.background='#300';d.title='art not extracted';}
  return d;
}
function card(a){
  const t=TSV[a]||{},s=SPEC[a]||{},v=V[a]||{};
  const c=document.createElement('div');
  c.className='card'+(v.ver?' '+v.ver:'');
  const h=document.createElement('div');h.className='head';
  // The map byte is art<<2 (art = map_byte>>2). tile-art.tsv's 'hex' column duplicates
  // the art index rather than carrying the byte, so it must not be used as one -- doing
  // so labelled art 03 as byte 03 when the tile the engine actually reads is 0x0C.
  const byte='0x'+(((a<<2)&0xFF).toString(16).toUpperCase().padStart(2,'0'));
  h.innerHTML='<b>art '+a.toString(16).toUpperCase().padStart(2,'0')+'</b>'
    +'<span class="byte">byte '+byte+'</span>'
    +'<span class="kind">'+(t.kind||clsOf(a))+'</span>';
  c.appendChild(h);
  const arts=document.createElement('div');arts.className='arts';
  const p1=document.createElement('div');p1.className='pane';
  p1.innerHTML='<b>cleanroom substitute</b>';p1.appendChild(sq(a in CLEAN?CLEAN[a]:null,z(),r()));
  const p2=document.createElement('div');p2.className='pane';
  p2.innerHTML='<b>extracted original</b>';p2.appendChild(sq(a in ORIG?ORIG[a]:null,z(),r()));
  arts.appendChild(p1);arts.appendChild(p2);c.appendChild(arts);
  const d1=document.createElement('div');d1.className='txt spec';
  d1.innerHTML='<span>spec asks it to read as:</span> '+(s.name?'<b>'+s.name+'</b> — '+s.reads:'<i>not in spec § 7</i>');
  c.appendChild(d1);
  const d2=document.createElement('div');d2.className='txt evid';
  d2.innerHTML='<span>original looks like:</span> '+(t.desc||'<i>no description</i>');
  c.appendChild(d2);
  if(t.note){const n=document.createElement('div');n.className='note';n.textContent='evidence: '+t.note;c.appendChild(n);}
  const vd=document.createElement('div');vd.className='verdict';
  [['ok','right'],['fix','needs fix'],['unsure','unsure']].forEach(([k,lab])=>{
    const b=document.createElement('button');b.textContent=lab;
    if(v.ver===k)b.className='sel';
    b.onclick=()=>{V[a]=V[a]||{};V[a].ver=(V[a].ver===k?'':k);save();render();};
    vd.appendChild(b);
  });
  const inp=document.createElement('input');inp.placeholder='what is wrong / what to change';
  inp.value=v.note||'';
  inp.onchange=()=>{V[a]=V[a]||{};V[a].note=inp.value;save();};
  vd.appendChild(inp);c.appendChild(vd);
  return c;
}
const z=()=>Math.max(2,Math.min(10,+$('zoom').value||4));
const r=()=>+$('rep').value||2;
function save(){localStorage.setItem(KEY,JSON.stringify(V));}
function render(){
  const g=$('grid');g.innerHTML='';
  const fc=$('fcls').value,fv=$('fver').value;
  let n=0;
  for(let a=0;a<64;a++){
    if(fc&&clsOf(a)!==fc)continue;
    const v=V[a]||{};
    if(fv==='_'&&v.ver)continue;
    if(fv&&fv!=='_'&&v.ver!==fv)continue;
    g.appendChild(card(a));n++;
  }
  const judged=Object.keys(V).filter(k=>V[k].ver).length;
  $('count').textContent=n+' shown · '+judged+'/64 judged';
}
function fillCls(){
  const s=$('fcls'),seen={};
  for(let a=0;a<64;a++)seen[clsOf(a)]=1;
  s.innerHTML='<option value="">all</option>'+Object.keys(seen).sort()
    .map(k=>'<option value="'+k+'">'+k+'</option>').join('');
}
$('exp').onclick=()=>{
  const rows=[];
  for(let a=0;a<64;a++){const v=V[a]||{};if(!v.ver&&!v.note)continue;
    const t=TSV[a]||{},s=SPEC[a]||{};
    rows.push('| `'+a.toString(16).toUpperCase().padStart(2,'0')+'` | '+(t.byte||'')+
      ' | '+(s.name||'')+ ' | '+(v.ver||'')+ ' | '+(v.note||'')+ ' |');}
  const md=rows.length? ('## Tile verdicts\n\n| art | byte | spec name | verdict | note |\n| --- | --- | --- | --- | --- |\n'
        +rows.join('\n')+'\n') : '## Tile verdicts\n\n_nothing recorded yet_\n';
  const o=$('out');o.value=md;o.style.display='block';o.select();
};
$('clr').onclick=()=>{if(confirm('clear all verdicts?')){V={};save();render();}};
['fcls','fver'].forEach(id=>$(id).onchange=render);
$('rep').onchange=render;$('zoom').onchange=render;
fillCls();render();
</script></body></html>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tiles', default='tmp/tiles', help='extracted original art (PNG)')
    ap.add_argument('--cleanroom', default='engine/assets/tiles/cleanroom/tiles')
    ap.add_argument('--art', default='disasm/tile-art.tsv')
    ap.add_argument('--palette', default='engine', choices=('engine', 'spec'),
                    help='engine = normalise the cleanroom preview onto the RGB the '
                         'engine actually displays (default; matches the originals)')
    ap.add_argument('--spec', default='docs/tile-art-spec.md')
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()

    low = os.path.abspath(a.out).lower()
    if '/original/' in low or '/references/' in low:
        sys.exit('refusing to write into original/ or references/')

    orig = load_tiles(a.tiles)
    clean = load_cleanroom(a.cleanroom, a.palette)
    tsv = load_tsv(a.art)
    spec = load_spec(a.spec)

    html = (HTML.replace('%ORIG%', json.dumps([orig.get(i, '') for i in range(64)]))
                .replace('%CLEAN%', json.dumps([clean[i] for i in range(64)]))
                .replace('%TSV%', json.dumps(tsv))
                .replace('%SPEC%', json.dumps(spec)))
    with open(a.out, 'w') as f:
        f.write(html)

    nospec = [i for i in range(64) if i not in spec]
    notsv = [i for i in range(64) if i not in tsv]
    print('wrote %s (%.1f MB)' % (a.out, len(html) / 1e6))
    print('  cleanroom art: %d/64 SVG' % len(clean))
    print('  original art:  %d/64 PNG%s' % (len(orig), '' if len(orig) == 64
          else '  (not extracted - run tools/tile_atlas.py --out %s)' % a.tiles))
    print('  tsv descriptions: %d/64' % len(tsv))
    print('  spec § 7 entries: %d/64' % len(spec))
    if nospec:
        print('  no spec row for: %s' % ' '.join('%02X' % i for i in nospec))
    if notsv:
        print('  no tsv row for:  %s' % ' '.join('%02X' % i for i in notsv))


main()
