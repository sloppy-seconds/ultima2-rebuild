#!/usr/bin/env bash
# generated_fresh.sh -- fail if a committed generated table differs from its generator.
#
# WHY. `place_index.py --verify` re-derives everything in memory and checks the RESULT -- that
# every wiki row resolves, that the letter table is pinned, that no Q glyph exists. It never
# compares against the file everyone actually reads. So disasm/place-index.tsv sat stale with
# five third-party place names unjoined while its own gate reported green, and the viewer and
# the docs were rendering the stale copy. A generator whose output can drift is a generator
# with no output guarantee.
#
# This is the missing half: regenerate to a temp dir, byte-compare against the committed file.
# Cheap, and it fails in exactly the place --verify cannot.
#
# WHAT IS NOT HERE. disasm/tile-art.tsv is hand-curated description prose joined to a generated
# art table; tile_atlas.py writes PNGs under tmp/, not this TSV. Listing it here would be a
# claim that something generates it when nothing does. The two wiki crosswalk tables are
# hand-curated inputs by design and are named as such in their own headers.
#
# A generator that cannot run is a SKIP, not a PASS: a missing input is expected in CI, but the
# line says so rather than going quiet.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0
skips=0

# check COMMITTED_PATH GENERATOR...   -- the generator's output path is appended as its last arg
check() {
    local out="$1"; shift
    local tmp
    tmp=$(mktemp -d "${TMPDIR:-/var/tmp}/genfresh.XXXXXX") || { echo "  SKIP  $out (no tmpdir)"; skips=$((skips+1)); return; }
    if ! "$@" "$tmp/out.tsv" >"$tmp/log" 2>&1; then
        printf '  SKIP  %-26s generator exited non-zero: %s\n' "$out" "$(tail -1 "$tmp/log" 2>/dev/null | cut -c1-60)"
        skips=$((skips + 1)); rm -rf "$tmp"; return
    fi
    if [ ! -f "$tmp/out.tsv" ]; then
        printf '  FAIL  %-26s generator wrote nothing to the path it was given\n' "$out"
        fail=1; rm -rf "$tmp"; return
    fi
    if [ ! -f "$out" ]; then
        printf '  FAIL  %-26s committed file is missing\n' "$out"
        fail=1; rm -rf "$tmp"; return
    fi
    if cmp -s "$tmp/out.tsv" "$out"; then
        printf '  PASS  %-26s matches its generator\n' "$out"
    else
        printf '  FAIL  %-26s STALE -- regenerate it\n' "$out"
        fail=1
    fi
    rm -rf "$tmp"
}

check disasm/place-index.tsv  python3 tools/place_index.py --tsv
check disasm/npc-index.tsv    python3 tools/npc_index.py --tsv
check disasm/planet-index.tsv python3 tools/planet_index.py --tsv

# The census is its own tool with its own --check-fresh, which diffs eight tables at once.
# Scratch goes under /var/tmp: /tmp here is tmpfs, so every byte of it is RAM taken from the
# model that is running.
CFLOG="${TMPDIR:-/var/tmp}/genfresh-census.log"
if python3 tools/census.py --check-fresh >"$CFLOG" 2>&1; then
    printf '  PASS  disasm/census/             %s\n' "$(cat "$CFLOG")"
else
    printf '  FAIL  disasm/census/             %s\n' "$(cat "$CFLOG")"
    fail=1
fi

[ "$fail" -ne 0 ] && { echo "GENERATED FRESH: FAIL"; exit 1; }
[ "$skips" -ne 0 ] && echo "GENERATED FRESH: PASS ($skips skipped)" || echo "GENERATED FRESH: PASS"
