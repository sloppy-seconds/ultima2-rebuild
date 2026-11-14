# art-pipeline container

The concept-art → dungeon-sprite toolchain, containerised so the heavy tools
live in an image instead of the host, and so the pipeline is a reproducible,
shareable artifact.

## Why

The right tools for the sprite steps are not the ones we hand-rolled:

- **Background removal** wants a subject-segmentation model, not a colour
  flood-fill (a flood-fill cannot remove *enclosed* background — the white
  between a creature's legs, the pocket above a raised weapon).
- **Palette quantisation** wants a perceptual quantiser with alpha and dithering
  and a shared-palette mode, not a hand-built k-means.

Both pull large runtimes (onnxruntime, opencv, a ~170 MB model). Installing them
into the host's system Python is the wrong move on an immutable Bazzite host and
for a build-time step. So they live here.

## What is in the image

| tool | for | licence |
| --- | --- | --- |
| `rembg` (u2net) | background removal / alpha matte | MIT (runtime onnxruntime MIT) |
| `pngquant` | shared-palette quantise + dither + alpha | GPL-3 (CLI only, never shipped) |
| ImageMagick 7 | trim / resize / gravity-fit / pixel ops | Apache-like |
| optipng | lossless PNG shrink | zlib |
| numpy / scipy / scikit-image / opencv / Pillow | glue | permissive |

None of it is linked into the engine or committed as a binary. It is a build-time
toolchain, exactly like ImageMagick or `jwasm` used from a shell. The pipeline
*scripts* stay in the repo (`engine/tools/`), so a tool-usage change is a git
commit, not an image rebuild.

## Build

From the project root (rootless podman, no sudo):

```bash
podman build -t u2-art-pipeline -f deploy/art-pipeline/Containerfile .
```

The first build downloads the u2net model and bakes it into the image, so later
runs need no network.

## Run

`run.sh` builds on first use and bind-mounts the repo at `/work`:

```bash
./deploy/art-pipeline/run.sh                       # a shell in the image
./deploy/art-pipeline/run.sh pngquant --version    # one tool, one shot
./deploy/art-pipeline/run.sh magick -version
./deploy/art-pipeline/run.sh python engine/tools/gen_dungeon_sprites.py concepts/dungeon2
```

Because the repo is mounted read-write, anything the pipeline writes (e.g.
`engine/src/game/dungeon_sprites_gen.h`) lands back on the host.

## Status

The image is built and the tools are verified present. Rewiring
`gen_dungeon_sprites.py` to call `rembg` + `pngquant` instead of the hand-rolled
matte and quantiser is the next step; until then the generator still runs on the
host's numpy/Pillow and this image simply provides the better tools.
