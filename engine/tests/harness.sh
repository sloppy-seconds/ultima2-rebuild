# tests/harness.sh — shared preamble for the shell checkpoints. Source it, do not
# run it:
#
#     . "$(dirname "$0")/harness.sh"
#
# It exists because every checkpoint re-declared the same four things: the compiler,
# the flag set, the working directory, and the list of engine sources its check
# needed. The first three belong to the Makefile, and the fourth went stale the
# moment a module gained a dependency — adding residency.c for #15 meant editing
# three scripts by hand, and a hand-maintained copy of a fact the build already
# knows is the defect this project keeps writing down.
#
# Provides:
#   U2_ROOT                 the engine directory; this file cds there
#   U2_CC, U2_CCFLAGS       the engine's own compiler and flag set
#   u2_lib                  ensure the current build dir's libu2.a exists and is current
#   u2_check OUT SRC...     compile SRC... and link them against libu2.a
#   u2_check_raw OUT SRC... compile SRC... with an explicit list and NO library
#
# u2_check_raw is not a convenience and not a step backwards. A check whose whole
# premise is "module A does not reach module B" cannot link the archive and still
# mean it — the linker would supply the very dependency the check claims is absent.
# Those checks name their sources, and the harness names the difference so the
# choice is visible at the call site instead of accidental.

# Resolve the engine root from this file's own location, not from $0: the scripts are
# invoked as ./tests/run_x.sh, from run_make targets, and from CI with a different
# cwd, and a preamble that gets that wrong silently checks the wrong tree.
: "${U2_HARNESS_DIR:=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
U2_ROOT="$(cd "$U2_HARNESS_DIR/.." && pwd)"
cd "$U2_ROOT" || exit 1

U2_CC="${U2_CC:-cc}"
# engine/Makefile is the only home for the flag set. A check held to a weaker bar
# than the code it checks is a check that passes for the wrong reason.
U2_CCFLAGS="${U2_CCFLAGS:-$(make -s ccflags)}"
# Where the archive lives. This MUST come from U2_OBJDIR, which the Makefile exports as $(BUILD):
# `test-asan` re-invokes make with BUILD=build/asan, so the archive that job builds is
# build/asan/libu2.a. Hardcoding `build` meant the sanitizer job linked whatever plain archive a
# dirty tree happened to leave behind -- locally that made an ASan run PASS while checking nothing
# under sanitizers -- and on CI's clean tree it failed as `cannot find -lu2`. Same root, two
# verdicts, neither one earned.
U2_LIBDIR="${U2_LIBDIR:-$U2_ROOT/${U2_OBJDIR:-build}}"
U2_LIB="$U2_LIBDIR/libu2.a"

u2_lib() {
    make -s lib || return 1
    # Fail here rather than at the linker. `ld: cannot find -lu2` inside a check program reads as a
    # defect in the code under test; the truth is that the build directory and the link path
    # disagreed, and that is a harness misconfiguration.
    [ -f "$U2_LIB" ] || { echo "harness: no libu2.a in $U2_LIBDIR (U2_OBJDIR=${U2_OBJDIR:-unset}); refusing to link a stale or foreign archive" >&2; return 1; }
}

# u2_check OUT SRC... — build SRC... against the engine library.
# The library is rebuilt through make, so a check can never link a stale archive
# after a source edit; make decides, by timestamp, exactly as the binary does.
u2_check() {
    local _out="$1"; shift
    u2_lib || return 1
    "$U2_CC" $U2_CCFLAGS -o "$_out" "$@" -L"$U2_LIBDIR" -lu2
}

# u2_check_raw OUT SRC... — build SRC... from an explicit list, no library.
# Use this only where the check's premise is the absence of a dependency.
u2_check_raw() {
    local _out="$1"; shift
    "$U2_CC" $U2_CCFLAGS -o "$_out" "$@"
}
