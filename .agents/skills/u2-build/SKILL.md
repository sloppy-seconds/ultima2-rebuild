---
name: u2-build
description: Build this project's artifacts — the terminal engine binary, the single-file static-SDL Linux player, the Windows build, and substitute tile art — each in its own container. Use when asked to build, rebuild, package or release a binary, when the `engine build fresh` gate is red, or when a build is reported as impossible on this host.
---

# Building u2 artifacts

**Nothing with a platform dependency is built on the host.** That is the design, not a limitation.
If a note, a comment, or your own recollection says "SDL3 is not installed here so X cannot be
built", it is wrong: build it in the container. A claim of exactly this kind sat in
`docs/outstanding.md` for weeks justifying a red gate, and was false.

## What to run

| want | run |
| --- | --- |
| dev binary, tests, gates | `make -C engine` |
| full test suite | `make -C engine test` |
| every gate | `bash tools/check_all.sh` (add `--fast` to skip the slow proofs) |
| single-file Linux player | `./deploy/linuxbuild/build.sh` → `engine/u2-sdl-static` |
| Windows build | `make sdl-win`, or `deploy/winbuild/` |
| byte-exact asm reconstruction proof | `bash tools/assemble_clean.sh` |
| one function's proof | `python3 tools/check_func.py <SEG>` |

`make -C engine` on the host is correct and expected: the terminal build has no platform deps.
`make sdl` on the host is **not** a path — it probes for SDL3 dev headers via
`engine/tools/sdl_probe.sh` and this host deliberately has none.

## The two traps

- **`engine build fresh` refuses a dirty tree** with `fresh: run 'make clean' before rebuilding`.
  Run `make -C engine clean` and then the build. This deletes `engine/u2-sdl` and
  `engine/u2-sdl-static`; they are gitignored build output and the container rebuilds the static
  one in about a minute. A clean is cheap. Do not leave a stale binary in place to keep a gate
  quiet, and do not treat a missing SDL binary as something to work around.
- **`deploy/linuxbuild/build.sh` keys image freshness on the Containerfile's content hash**, not on
  whether an image exists. If you edited the Containerfile and the build reports a missing SDL3,
  that is a stale image: pass `--rebuild`.

## Verify a build you just did

`ldd engine/u2-sdl-static` must list only `libc`, `libm` and the vdso — no `libSDL3.so`. That
static link is the entire point of the container; if `libSDL3.so` appears, the artifact is wrong
even though the build exited 0.

Detail and provenance: `docs/tooling-procedures.md` § "Build, play, seed".
