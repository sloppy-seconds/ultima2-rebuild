#!/usr/bin/env bash
# apple2_ghidra.sh — the whole Apple II 6502 pipeline in one command: extract the disks,
# build the flat image, scan the seeds, import into Ghidra, decompile, print checksums.
#
# This exists because every stage of it was run by hand at least once and none of the output
# was reproducible: the seed scan lived only in a deleted /var/tmp file, and the decompiler
# output everyone quoted was four uncheckable artifacts. With this script, `disasm/apple2-*`
# (committed: addresses, sizes, verdicts) can be regenerated and diffed against the bulk
# output in decompiled/apple2-6502/ (gitignored: derived from copyrighted bytes).
#
#   tools/apple2_ghidra.sh                      # fresh run into /var/tmp
#   tools/apple2_ghidra.sh --out DIR --keep     # keep the work dir
#   tools/apple2_ghidra.sh --bulk               # also write the .c/.tsv next to the committed index
#
# The checksums printed at the end are the diff: if u2_6502.bin and u2_6502_seeds.txt match
# the values in docs/archive/handoff-apple2-crosswalk.md T0, the C on disk is the C that index came from.
#
# Exit is non-zero if any stage fails or if the seed count does not match the committed list.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT="/var/tmp/u2-apple2-6502"
BULK=0
KEEP=0
SKIP_EXTRACT=0
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --bulk) BULK=1; shift ;;
        --keep) KEEP=1; shift ;;
        --skip-extract) SKIP_EXTRACT=1; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown flag $1" >&2; exit 2 ;;
    esac
done

CP2="$ROOT/tools/ciderpress2/cp2"
DSK="references/apple/Ultima II (4am crack)/Ultima II (4am crack) disk 1 - program.dsk"
EXDIR="$ROOT/decompiled/apple2"
WORK="$OUT"
mkdir -p "$WORK"

if [ ! -x "$CP2" ]; then
    echo "missing $CP2 — see docs/apple2-reference.md § 2 (vendored, gitignored, GPL-3.0)" >&2
    exit 1
fi
if [ ! -f "$DSK" ]; then
    echo "missing $DSK — references/apple/ is gitignored game content" >&2
    exit 1
fi

echo "== 1/5 extract disk 1"
if [ "$SKIP_EXTRACT" = 1 ] && [ -d "$EXDIR" ]; then
    echo "   skipped (--skip-extract): $EXDIR"
else
    mkdir -p "$EXDIR"
    "$CP2" extract --exdir="$EXDIR" "$DSK" >/dev/null
    echo "   -> $EXDIR ($(find "$EXDIR" -maxdepth 1 -type f | wc -l) files)"
fi

echo "== 2/5 flat image"
python3 tools/apple2_image.py -d "$EXDIR" -o "$WORK/u2_6502.bin" | sed 's/^/   /'

echo "== 3/5 seeds"
python3 tools/apple2_seeds.py -i "$WORK/u2_6502.bin" -d "$EXDIR" -o "$WORK/u2_6502_seeds.txt" | sed 's/^/   /'
python3 tools/apple2_seeds.py -i "$WORK/u2_6502.bin" -d "$EXDIR" --selftest | sed 's/^/   /'
if [ -f "$ROOT/disasm/apple2-seeds.txt" ]; then
    if diff -q <(grep -v '^#' "$ROOT/disasm/apple2-seeds.txt") "$WORK/u2_6502_seeds.txt" >/dev/null; then
        echo "   matches disasm/apple2-seeds.txt"
    else
        echo "   DIFFERS from disasm/apple2-seeds.txt — the committed index is stale" >&2
        diff <(grep -v '^#' "$ROOT/disasm/apple2-seeds.txt") "$WORK/u2_6502_seeds.txt" | head -20 >&2
        exit 1
    fi
fi

echo "== 4/5 Ghidra import + decompile"
source tools/env.sh
PROJ="$WORK/proj"; rm -rf "$PROJ"; mkdir -p "$PROJ"
"$GHIDRA_HOME/support/analyzeHeadless" "$PROJ" u2apple -deleteProject \
    -import "$WORK/u2_6502.bin" \
    -loader BinaryLoader -loader-baseAddr 0x4000 \
    -processor "6502:LE:16:default" \
    -scriptPath "$ROOT/tools/ghidra" \
    -postScript Seed6502.java "seeds=$WORK/u2_6502_seeds.txt" \
    -postScript DecompileAll.java "out=$WORK/u2_6502.c" "index=$WORK/u2_6502.tsv" 2>&1 \
    | grep -Ev '^(  |$)' | tail -20 | sed 's/^/   /'

echo "== 5/5 checksums"
if [ "$BULK" = 1 ]; then
    mkdir -p "$ROOT/decompiled/apple2-6502"
    cp "$WORK/u2_6502.bin" "$WORK/u2_6502_seeds.txt" "$WORK/u2_6502.c" "$WORK/u2_6502.tsv" \
       "$ROOT/decompiled/apple2-6502/"
    echo "   bulk output copied to decompiled/apple2-6502/"
fi
( cd "$WORK" && md5sum u2_6502.bin u2_6502_seeds.txt u2_6502.c u2_6502.tsv | sed 's/^/   /' )
echo "   functions: $(( $(wc -l < "$WORK/u2_6502.tsv") - 1 ))"

[ "$KEEP" = 1 ] && echo "work dir kept: $WORK"
echo "done"
