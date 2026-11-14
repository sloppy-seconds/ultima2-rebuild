#!/usr/bin/env python3
"""Generate dungeon_sprites_gen.h from the approved dungeon concept art.

Pipeline per concept (docs/concept-dungeon-brief.md stage 2, owner ruling
2026-11-14: 256-colour dungeon palette + 128px sprite source):
  matte the background to transparent,
  tight-crop to the figure and fit it to the sprite square anchored to the floor,
  quantize the opaque pixels to a shared sub-palette of N colours, which become
  frame indices 16..15+N (0..15 stay the EGA table so the status panel is intact),
  bake 4 torch-falloff shade levels as a palette index map (darken each palette
  colour, re-find the nearest in-palette index) rather than 4 full copies.

The two steps that have proper tools use them, when present (they are in the
art-pipeline container, deploy/art-pipeline/):
  * background removal -> rembg (u2net subject segmentation). A colour flood-fill
    cannot remove *enclosed* background (the white between a creature's legs, the
    pocket above a raised weapon); a segmentation model can.
  * the shared palette -> pngquant (libimagequant), a perceptual quantiser.
    This pngquant build cannot apply an external palette, so pngquant builds the
    palette from a montage of every sprite's opaque pixels and the per-sprite
    index map is a nearest-map into it (at 240 colours dithering is negligible).
Both fall back to the older in-file implementations when the tool is absent, so
the generator still runs on a bare host; run it in the container to get the good
tools. See deploy/art-pipeline/README.md.

Cleanroom: reads only concepts/ (our own generated art). Never original/,
references/, bestiary/cache/, or any screenshot.
"""
import os, sys, shutil, subprocess, tempfile
from collections import deque
from PIL import Image
import numpy as np

EDGE = 128
PAL_N = 240            # frame indices 16..255
TRANSPARENT = 0xFF
SHADES = [1.0, 0.62, 0.40, 0.24]   # level 0 = full bright, then falloff
WORK = 512             # matte working resolution (rembg's net is 320; 512 keeps edges crisp)

# concept file stem -> C symbol emitted (SP_MON slot mapping lives in dungeon_assets).
# The symbol must be a valid C identifier, so hyphenated stems get underscores.
SPRITES = [
    ("orc",             "orc"),
    ("ghost",           "ghost"),
    ("carrion-creeper", "carrion_creeper"),
    ("viper-emerald",   "viper_emerald"),
    ("gremlin",         "gremlin"),
    ("daemon-minotaur", "daemon_minotaur"),
    ("balron",          "balron"),
]

# ------------------------------------------------- optional proper tools ----
try:
    import rembg
    _REMBG = None
    def _rembg_session():
        global _REMBG
        if _REMBG is None:
            _REMBG = rembg.new_session("u2net")
        return _REMBG
    HAVE_REMBG = True
except Exception:
    HAVE_REMBG = False

HAVE_PNGQUANT = shutil.which("pngquant") is not None


def matte_rembg(im):
    """Subject-segmentation matte: True = opaque. Removes enclosed background."""
    rgba = rembg.remove(im.convert("RGB"), session=_rembg_session())
    return np.asarray(rgba)[..., 3] > 128


def matte_flood(im, tol=40):
    """Fallback: flood the near-uniform background in from the border. Cannot
    remove enclosed background, so it is strictly worse than rembg."""
    arr = np.asarray(im.convert('RGB')).astype(np.int32)
    H, W, _ = arr.shape
    corner = arr[0, 0]
    close = (np.abs(arr - corner).sum(-1) < tol)
    seen = np.zeros((H, W), bool)
    dq = deque()
    for x in range(W):
        for y in (0, H - 1):
            if close[y, x] and not seen[y, x]:
                seen[y, x] = True; dq.append((y, x))
    for y in range(H):
        for x in (0, W - 1):
            if close[y, x] and not seen[y, x]:
                seen[y, x] = True; dq.append((y, x))
    while dq:
        y, x = dq.popleft()
        for ny, nx in ((y-1,x),(y+1,x),(y,x-1),(y,x+1)):
            if 0 <= ny < H and 0 <= nx < W and close[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True; dq.append((ny, nx))
    return ~seen     # True = opaque


def load_sprite(src_dir, stem):
    """Matte at working resolution, tight-crop to the figure, then fit it to fill
    the EDGE square anchored to the floor. Filling the square is what makes a
    close creature read as close: a figure that only occupies the middle of the
    sprite looks far away no matter the distance."""
    path = os.path.join(src_dir, f"{stem}_00001_.png")
    work = Image.open(path).convert('RGB').resize((WORK, WORK), Image.LANCZOS)
    alpha = matte_rembg(work) if HAVE_REMBG else matte_flood(work)
    if not HAVE_REMBG:
        # the flood matte leaves a blended fringe; erode 1px to eat the halo
        a = alpha.copy()
        a[1:-1, 1:-1] &= (alpha[:-2, 1:-1] & alpha[2:, 1:-1] &
                          alpha[1:-1, :-2] & alpha[1:-1, 2:])
        alpha = a
    ys, xs = np.where(alpha)
    if len(ys) == 0:
        return np.zeros((EDGE, EDGE, 3), 'uint8'), np.zeros((EDGE, EDGE), bool)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    crop = work.crop((int(x0), int(y0), int(x1), int(y1))).convert('RGBA')
    carr = np.asarray(crop).copy()
    carr[..., 3] = np.where(alpha[y0:y1, x0:x1], 255, 0)
    crop = Image.fromarray(carr)
    cw, ch = crop.size
    scale = EDGE / float(max(cw, ch))
    nw, nh = max(1, int(round(cw * scale))), max(1, int(round(ch * scale)))
    crop = crop.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new('RGBA', (EDGE, EDGE), (0, 0, 0, 0))
    canvas.paste(crop, ((EDGE - nw) // 2, EDGE - nh), crop)   # bottom-centre
    out = np.asarray(canvas)
    return out[..., :3].copy(), out[..., 3] > 128


def _montage(pts):
    """Lay a flat array of RGB pixels into a square image (padded with the mean
    colour) so a quantiser can see them all at once."""
    M = len(pts)
    side = int(np.ceil(np.sqrt(M)))
    pad = np.tile(pts.mean(0).astype('uint8'), (side * side, 1))
    pad[:M] = pts
    return Image.fromarray(pad.reshape(side, side, 3), 'RGB')


def _palette_pngquant(pts):
    """Build the shared palette with libimagequant. This pngquant build cannot
    apply an external palette, so we take only its palette and map into it."""
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 'montage.png')
        out = os.path.join(d, 'pal.png')
        _montage(pts).save(src)
        subprocess.run(["pngquant", "--force", "--speed", "1", str(PAL_N),
                        "--output", out, src], check=True)
        pal = np.array(Image.open(out).getpalette(), dtype=np.int32)
    if len(pal) < PAL_N * 3:
        pal = np.concatenate([pal, np.zeros(PAL_N * 3 - len(pal), np.int32)])
    return pal[:PAL_N * 3].reshape(PAL_N, 3)


def _palette_pil(pts):
    """Fallback palette: PIL median-cut over the union of all sprites."""
    img = _montage(pts)
    palimg = img.quantize(colors=PAL_N, method=Image.Quantize.MEDIANCUT,
                          dither=Image.Dither.NONE)
    pal = np.array(palimg.getpalette(), dtype=np.int32)
    if len(pal) < PAL_N * 3:
        pal = np.concatenate([pal, np.zeros(PAL_N * 3 - len(pal), np.int32)])
    return pal[:PAL_N * 3].reshape(PAL_N, 3)


def build_palette(sprites):
    pts = np.concatenate([rgb[alpha] for rgb, alpha in sprites], 0).astype('uint8')
    return _palette_pngquant(pts) if HAVE_PNGQUANT else _palette_pil(pts)


def nearest_idx(arr, pal):
    d = ((arr[:, None, :] - pal[None, :, :]) ** 2).sum(-1)
    return d.argmin(1)


def shade_map(pal):
    sm = np.zeros((4, PAL_N), dtype=np.uint8)
    for L, f in enumerate(SHADES):
        dark = np.clip(pal * f, 0, 255).astype(np.int32)
        sm[L] = nearest_idx(dark, pal).astype(np.uint8)
    return sm


def emit_header(out_path, names, sprites, pal, sm):
    lines = []
    lines.append("/* dungeon_sprites_gen.h -- GENERATED by engine/tools/gen_dungeon_sprites.py.")
    lines.append(" * Do not edit by hand. Source: concepts/dungeon2/ (our own concept art).")
    lines.append(" * Frame indices 0..15 stay the EGA table; 16..%d carry this sprite art. */" % (15+PAL_N))
    lines.append("#ifndef DUNGEON_SPRITES_GEN_H")
    lines.append("#define DUNGEON_SPRITES_GEN_H")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#define DG_SPR_EDGE %d" % EDGE)
    lines.append("#define DG_SPR_PAL_N %d" % PAL_N)
    lines.append("#define DG_SPR_TRANSPARENT 0xFFu")
    lines.append("")
    lines.append("/* palette for frame indices 16..%d */" % (15+PAL_N))
    lines.append("static const uint8_t dg_sprite_pal[%d][3] = {" % PAL_N)
    for i in range(PAL_N):
        lines.append("  {%d,%d,%d}," % (pal[i,0], pal[i,1], pal[i,2]))
    lines.append("};")
    lines.append("")
    lines.append("/* shade[level][palette_offset] -> shaded palette_offset */")
    lines.append("static const uint8_t dg_sprite_shade[4][%d] = {" % PAL_N)
    for L in range(4):
        row = ",".join(str(int(v)) for v in sm[L])
        lines.append("  {" + row + "},")
    lines.append("};")
    lines.append("")
    for name, (rgb, alpha) in zip(names, sprites):
        idx = np.full((EDGE, EDGE), TRANSPARENT, dtype=np.uint8)
        flat = rgb[alpha]
        idx[alpha] = (nearest_idx(flat, pal) + 16).astype(np.uint8)
        lines.append("/* %s: base indices, value 16..%d or 0xFF transparent */" % (name, 15+PAL_N))
        lines.append("static const uint8_t dg_spr_%s[DG_SPR_EDGE*DG_SPR_EDGE] = {" % name)
        flat = idx.reshape(-1)
        for y in range(EDGE):
            row = ",".join("0x%02X" % v for v in flat[y*EDGE:(y+1)*EDGE])
            lines.append("  " + row + ",")
        lines.append("};")
        lines.append("")
    lines.append("#endif")
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    src_dir = sys.argv[1] if len(sys.argv) > 1 else 'concepts/dungeon2'
    out_path = sys.argv[2] if len(sys.argv) > 2 else 'engine/src/game/dungeon_sprites_gen.h'
    prev_dir = os.environ.get("DG_PREVIEW_DIR", "tmp")
    print(f"matte: {'rembg' if HAVE_REMBG else 'flood (fallback)'}"
          f"   palette: {'pngquant' if HAVE_PNGQUANT else 'PIL median-cut (fallback)'}")
    names = [sym for _, sym in SPRITES]        # emitted C symbol suffix
    stems = [stem for stem, _ in SPRITES]      # concept filename stem
    sprites = [load_sprite(src_dir, stem) for stem in stems]
    pal = build_palette(sprites)
    sm = shade_map(pal)
    emit_header(out_path, names, sprites, pal, sm)
    # preview: composite each base sprite on a dark wall for eyeballing
    os.makedirs(prev_dir, exist_ok=True)
    for name, (rgb, alpha) in zip(names, sprites):
        idx = np.full((EDGE, EDGE), TRANSPARENT, dtype=np.uint8)
        idx[alpha] = (nearest_idx(rgb[alpha], pal) + 16).astype(np.uint8)
        canvas = np.full((EDGE, EDGE, 3), 52, np.uint8)
        opaque = idx != TRANSPARENT
        canvas[opaque] = pal[idx[opaque] - 16]
        Image.fromarray(canvas).save(f'{prev_dir}/gen_{name}.png')
    print(f"wrote {out_path}  ({len(names)} sprites, palette {PAL_N}, edge {EDGE})")


if __name__ == '__main__':
    main()
