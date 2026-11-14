#!/usr/bin/env bash
# Run the art-pipeline container with the repo mounted. Builds the image on
# first use. Everything the pipeline reads or writes is under the repo, so the
# generated header lands back on the host.
#
#   ./deploy/art-pipeline/run.sh                       # a shell in the image
#   ./deploy/art-pipeline/run.sh pngquant --version    # one tool, one shot
#   ./deploy/art-pipeline/run.sh python engine/tools/gen_dungeon_sprites.py concepts/dungeon2
#
# Rootless podman (no sudo), matching the rest of deploy/.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
IMAGE="${U2_ART_IMAGE:-u2-art-pipeline:latest}"

if ! podman image exists "$IMAGE"; then
  echo "run.sh: building $IMAGE (first use; downloads the model, a few minutes)" >&2
  podman build -t "$IMAGE" -f "$HERE/Containerfile" "$ROOT"
fi

# :z relabels the bind mount for SELinux (the host is Bazzite/Fedora).
exec podman run --rm -i \
  ${U2_ART_TTY:+-t} \
  -v "$ROOT:/work:z" \
  -w /work \
  "$IMAGE" "$@"
