# engine/Makefile — portable C engine, CP0 skeleton.
#
# Hard rule from docs/phase4-plan.md §2: the terminal build needs nothing but
# a C11 compiler. No third-party libraries. The one generated header,
# content/gen_content.h, is committed rather than built, so a fresh clone needs
# nothing installed or downloaded to compile. SDL2 is a CP9 target and stays
# behind its own goal, because installing it is an owner decision.

CC       ?= cc
STD      ?= -std=c11
# -pedantic is on because the tree is clean under it, and it is the only thing
# that keeps it clean. What it was catching was not cosmetics: eight call sites
# passed a plain uint8_t (*)[3] where the parameter was const uint8_t (*)[3],
# which is not a valid qualification conversion before C23, and one _Static_assert
# on strcmp was a warning that had been mistaken for a check. The full reasoning
# is on the palette parameters in core/frame.h and on U2_CONFIG_THEMES_DIR in
# core/config.h.
WARN     := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
            -Wpointer-arith -Wwrite-strings -Wno-unused-parameter -pedantic
# -g1 rather than no debug info at all: it costs a few seconds of link and a little
# binary size, and it is the difference between a crash that names a line and a crash
# that names an address. -g0 would strip the backtraces the sanitizer target relies on.
OPT      ?= -O2 -g1
CPPFLAGS += -Isrc -Icontent
CFLAGS   += $(STD) $(WARN) $(OPT)
# The flag set every C translation unit in this project is compiled with, exported to
# the test scripts so a check program is never held to a weaker bar than the engine it
# checks. Before this, the ad-hoc `cc -std=c11 -Wall -Wextra -Werror` lines in
# engine/tests/ silently omitted -Wshadow, -Wmissing-prototypes, -Wstrict-prototypes
# and -pedantic, and 25 real defects were hiding in the check programs behind that.
# Scripts run by hand rather than through make fall back to `make -s ccflags`, so the
# list has exactly one home.
U2_CCFLAGS  = $(STD) $(WARN) $(CPPFLAGS)
export U2_CCFLAGS
# The compiler the check programs are built with, so `make CC=clang test` compiles the
# tests with clang too and not only the engine. A script run by hand falls back to cc.
U2_CC       = $(CC)
export U2_CC
# Where the built objects live, so a check program that links engine objects links the
# ones compiled with the same flags it was built with. `test-asan` points this at its
# own tree; a script run by hand falls back to the ordinary build directory.
U2_OBJDIR   = $(BUILD)
export U2_OBJDIR

# ---- build stamp ------------------------------------------------------------
#
# Every binary has to be able to answer "which build am I holding", because the
# whole SDL story in docs/sdl2-vs-sdl3.md is that a dev binary and a shipped
# binary can be built against different implementations of the same API and both
# run. Until --version could say so, that question was only answerable by reading
# the Makefile and guessing what the host had. The stamp is compile-time truth;
# --version also prints the runtime SDL version, and the two disagreeing is
# itself the signal.
#
# U2_VERSION is a variable rather than a constant because the release workflow can
# pass the tag it is building, and a binary that names its own tag is worth more
# than one that says "unreleased".
U2_VERSION ?= 0.1.0-unreleased

# STAMP <target> <sdl impl> <linkage> <compile-time sdl version> <artifact>
STAMP = -DU2_VERSION='"$(U2_VERSION)"' \
        -DU2_BUILD_TARGET='"$(1)"' \
        -DU2_SDL_IMPL='"$(2)"' \
        -DU2_SDL_LINKAGE='"$(3)"' \
        -DU2_SDL_VERSION='"$(4)"' \
        -DU2_ARTIFACT='"$(5)"'

STAMP_TERMINAL  = $(call STAMP,terminal,none,none,,dev)
STAMP_SDLSTATIC = $(call STAMP,sdl-static,sdl3,static,,release)
STAMP_SDLWIN    = $(call STAMP,sdl-win,sdl3,static,,release)

BIN      := u2
# Overridable so a sanitizer run can build into its own tree (see `test-asan`) without
# disturbing the objects the ordinary build and the `fresh` gate use.
BUILD    ?= build
CONTENT  := content
GEN      := $(CONTENT)/gen_content.h
# --- freshness stamps (see the `fresh` target) --------------------------------
# A stamp is the hash of everything that actually feeds a binary, written by the
# link rule that produced that binary. `fresh` grades each binary against its own
# stamp. The snippet lives here, not in the target, so a link rule and the gate
# cannot disagree about what the inputs are.
# `asm/` is deliberately absent: the build consumes it only through $(GEN), which
# the extractor writes and CP1's content-check proves against the exe, so an asm
# rename or a marked body correction cannot change a byte of any binary. mtime is
# absent for the same reason: a checkout, a stash or a rebase rewrites hundreds of
# mtimes without changing a byte.
STAMPDIR := $(BUILD)/stamps
# Makefile is one of the hashed inputs, so every object rule lists it as a
# prerequisite too. Without that the gate can go red on a flags change and `make`
# has no rule that fixes it, which leaves only `make clean` — a red the gate cannot
# clear is a red people learn to ignore, the exact failure this rewrite was for.
FRESH_HASH = { find src -type f \( -name '*.c' -o -name '*.h' \) -print0; \
                 printf '%s\0' $(GEN) Makefile; } | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-64
define WRITE_STAMP
	@mkdir -p $(STAMPDIR)
	@$(FRESH_HASH) > $(STAMPDIR)/stamp.$@.tmp && mv -f $(STAMPDIR)/stamp.$@.tmp $(STAMPDIR)/stamp.$@
endef
EXE      ?= ../original/ultimaii.exe
SRCS     := src/main.c \
            $(wildcard src/core/*.c) \
            $(wildcard src/game/*.c) \
            $(filter-out src/platform/time_win32.c,$(wildcard src/platform/*.c)) \
            $(wildcard src/debug/*.c)
# time_win32.c is excluded from every Linux build and re-added by the win
# targets; see WIN_SRCS. The portable default is the POSIX clock.
OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)
SDLOBJS  := $(patsubst %.c,$(BUILD)-sdl/%.o,$(SRCS))
SDL_DEPS := $(SDLOBJS:.o=.d)

.PHONY: all run headless test lib test-cp0 test-cp1 test-cp2 test-cp3 test-cp4 test-cp5 test-cp6 test-cp7 test-demo test-api clean content content-check licenses licenses-check compile-commands compile-commands-check fmtowns-map fmtowns-map-check dungeon-sprites dungeon-sprites-check sdl sdl-probe-check sdl-run sdl-win sdl-static play help fresh ccflags test-asan analyze

all: $(BIN)

# The content header is generated by the extractor and then COMMITTED, which is
# a deliberate exception to "build output never belongs in git" and is ruled in
# AGENTS.md § 0. The reason is that the two sources are not equal: the player's
# exe resolves all 220 strings, the asm/ fallback resolves 181 and leaves 38 as
# empty literals, and an empty literal prints as nothing. A build that
# regenerated this file on a machine without the exe — every CI runner, every
# release build — would therefore ship a game with a blank character-creation
# screen, and every test that greps the screen for text would pass vacuously.
#
# So there is deliberately NO rule to remake $(GEN). The build consumes the
# committed file and fails loudly if it is absent. Refresh it on purpose with
# `make content`, which refuses without the exe, and let `make content-check`
# prove the committed text still agrees with the byte-proven asm/ tree for
# every string asm can reach.
content:
	@test -f $(EXE) || { echo "make content needs a copy of the game at $(EXE)"; exit 1; }
	@mkdir -p $(CONTENT)
	python3 tools/extract/extractor.py --source exe --exe $(EXE) -o $(GEN)

content-check:
	@test -f $(GEN) || { echo "$(GEN) is missing and is committed content"; exit 1; }
	python3 tools/extract/extractor.py --source asm --verify -o $(GEN)

# The FM Towns art-index mapping the binary carries. Same discipline as the two
# above — committed, no build rule that quietly regenerates it — but unlike the
# text header its source is committed too, so the check is exact rather than
# best-effort and it can run anywhere.
FMTOWNS_MAP := $(CONTENT)/fmtowns_map.h

fmtowns-map:
	@mkdir -p $(CONTENT)
	python3 tools/fmtowns_map2h.py -o $(FMTOWNS_MAP)

fmtowns-map-check:
	@test -f $(FMTOWNS_MAP) || { echo "$(FMTOWNS_MAP) is missing and is committed content"; exit 1; }
	python3 tools/fmtowns_map2h.py --verify -o $(FMTOWNS_MAP)

# Dungeon sprite art, same discipline as the three above — the header is
# committed generated output — but the check is NOT a byte comparison, and the
# reason is in tools/check_dungeon_sprites.py: the generator uses rembg and
# pngquant when present and falls back to in-file implementations when absent,
# so emitted art differs by host and a byte-compare would fail on a clean
# checkout even when the committed header is exactly right. The check is
# therefore structural: the generator's SPRITES/EDGE/PAL_N tables against the
# header's emitted symbols and defines. Byte-exact comparison belongs in
# deploy/art-pipeline/, where the tools exist.
DUNGEON_SPRITES_H := src/game/dungeon_sprites_gen.h
DUNGEON_CONCEPTS  := ../concepts/dungeon2

dungeon-sprites:
	@mkdir -p $(BUILD)/preview
	DG_PREVIEW_DIR=$(BUILD)/preview python3 tools/gen_dungeon_sprites.py $(DUNGEON_CONCEPTS) $(DUNGEON_SPRITES_H)

dungeon-sprites-check:
	@test -f $(DUNGEON_SPRITES_H) || { echo "$(DUNGEON_SPRITES_H) is missing and is committed content"; exit 1; }
	@python3 tools/check_dungeon_sprites.py

# The licence record, same discipline as the content header: the generated file is
# committed, there is deliberately no build rule that regenerates it during a
# normal build, and `licenses-check` proves the committed copy still matches its
# sources. A silently short licence list looks exactly like a complete one, which
# is the only kind of licensing failure that would be easy to miss.
licenses:
	python3 tools/gen_licenses.py

licenses-check:
	@test -f src/core/licenses_data.h || { echo "src/core/licenses_data.h is missing and is committed output"; exit 1; }
	@python3 tools/gen_licenses.py --verify

# ---- clangd compilation database ---------------------------------------------
#
# Why this exists: clangd has no project model for a plain Makefile. Without a
# compilation database it guesses flags, cannot resolve -Isrc or -Icontent, and
# reports every project header as missing — measured on engine/src/game/commerce.c:
# 22 unresolved includes, after which textDocument/references answers "no
# references" for symbols that have dozens. A plausible wrong answer is worse than
# an error, so the database is generated from the Makefile's own variables rather
# than hand-written, and rather than left to `bear` or `compiledb`, which have to
# be remembered and re-run after every added file.
#
# Why generated in Make and not by a script: the build stamp embeds
# -DU2_VERSION='"..."'. Getting that through a shell loses the inner double quotes
# unless the flags travel by environment or by make's own writer, and a `sed`
# escape would be exactly the kind of transform that fails silently — a missed
# pattern returns the input unchanged and exits 0. $(file >) writes the text
# byte-for-byte with no shell in the way, and $(subst) performs the single JSON
# escape the stamp needs. No new tool file, no new dependency.
#
# Why it lives in build/ and is never committed: it carries absolute paths and this
# host's compiler. That is the opposite ruling to content/gen_content.h, and for
# the opposite reason — that one must survive a machine missing the inputs, this
# one must not, because a stale database is a lie about the build. `make clean`
# removes it with the rest of build/, and it regenerates in well under a second,
# so staleness is never worth reasoning about. compile-commands-check is the gate
# that keeps it honest.

CC_DB        ?= $(BUILD)/compile_commands.json
CC_DB_FLAGS  := $(CPPFLAGS) $(CFLAGS) $(STAMP_TERMINAL)
# JSON cannot contain a bare double quote and the stamp is full of them.
CC_DB_ESC    := $(subst ",\",$(CC_DB_FLAGS))
# Derived from CPPFLAGS rather than hardcoded: dropping an -I must fail loudly here
# rather than quietly produce a database that resolves half the headers.
CC_DB_IDIRS  := $(patsubst -I%,%,$(filter -I%,$(CPPFLAGS)))
comma := ,
cc_db_row = { "directory": "$(CURDIR)", "file": "$(CURDIR)/$(1)", "command": "$(CC) $(CC_DB_ESC) -c $(1) -o $(BUILD)/$(1:.c=.o)" }$(if $(filter $(1),$(lastword $(SRCS))),,$(comma))
CC_DB_TEXT   := [$(foreach s,$(SRCS),  $(call cc_db_row,$(s)))]
CC_DB_EMIT    = $(file >$(CC_DB),$(CC_DB_TEXT))

# The three checks below are the point as much as the output. Each one is a way
# this file could produce a database that parses and is still wrong.
compile-commands:
	@mkdir -p $(BUILD)
	@test -n "$(strip $(SRCS))" || { echo "compile_commands: empty source list — a database with no entries checks nothing and looks identical to a complete one"; exit 1; }
	@for d in $(CC_DB_IDIRS); do test -d "$$d" || { echo "compile_commands: -I$$d from CPPFLAGS does not exist"; exit 1; }; done
	@for s in $(SRCS); do test -f "$$s" || { echo "compile_commands: $$s is listed in SRCS but absent"; exit 1; }; done
	@$(CC_DB_EMIT) :
	@python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d, "empty database"; print("compile_commands.json: %d entries -> %s" % (len(d), sys.argv[1]))' $(CC_DB)

# Freshness gate, same shape as tools/census.py --check-fresh: regenerate to a
# throwaway path and diff against the real one. A generated file with no freshness
# check goes stale and keeps reporting healthy.
compile-commands-check:
	@test -f $(CC_DB) || { echo "compile-commands-check: $(CC_DB) is missing — run make compile-commands"; exit 1; }
	@$(MAKE) --no-print-directory CC_DB=$(BUILD)/compile_commands.check.json compile-commands >/dev/null
	@cmp -s $(CC_DB) $(BUILD)/compile_commands.check.json || { echo "compile-commands-check: $(CC_DB) is stale against the Makefile — run make compile-commands"; rm -f $(BUILD)/compile_commands.check.json; exit 1; }
	@rm -f $(BUILD)/compile_commands.check.json
	@echo "compile-commands-check: current"

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(WRITE_STAMP)

$(BUILD)/%.o: %.c $(GEN) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STAMP_TERMINAL) -MMD -MP -c $< -o $@

# --- static library for the test checks ---------------------------------------
# The C checks under tests/ used to name the engine sources they needed on their own
# command line, and every such list went stale the first time a module gained a
# dependency: adding residency.c for #15 meant editing three scripts by hand. A list
# that is wrong in the missing direction is a link error, which is at least loud, but
# it is still a second copy of a fact the build already knows.
#
# So the checks link this instead: the same objects the binary links, minus main.c.
# A static archive rather than a wildcard on the command line because a .a contributes
# only the objects a check actually references, which keeps a check from accidentally
# succeeding by dragging in a module it was supposed to be independent of.
#
# Checks that exist to PROVE independence (dungeon_art_neutral, anything that asserts
# one module does not reach another) must keep an explicit source list: linking the
# whole archive would make their premise unfalsifiable. See tests/harness.sh.
LIB_OBJS := $(filter-out $(BUILD)/src/main.o,$(OBJS))
LIB      := $(BUILD)/libu2.a

lib: $(LIB)

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	@rm -f $@
	ar rcs $@ $(LIB_OBJS)

# Interactive terminal play.
run: $(BIN)
	./$(BIN)

# The CP0 acceptance command, verbatim from the plan.
headless: $(BIN)
	./$(BIN) --headless --script tests/title.keys --dump-state

test: test-cp0 test-cp1 test-cp2 test-cp3 test-cp4 test-cp5 test-cp6 test-cp7 test-demo test-api

# --- sanitizer build ----------------------------------------------------------
# ASan + UBSan over the same suites `make test` runs. This is the one control that can
# see the class of defect the type system cannot: the engine's whole job is reading
# byte layouts it does not own (save records, mapx/monx/tlkx, theme directories), and
# the `fresh` gate's own header records a struct mismatch that surfaced as a hang with
# no error anywhere. Nothing in the suite would have caught that class.
#
# Objects go in $(BUILD)/asan so the ordinary build tree is untouched. The link rule
# still writes ./u2, so the recipe restores the ordinary binary afterwards whatever
# the suites decide — otherwise a red sanitizer run would leave the `fresh` gate
# reporting a stale binary the developer never asked for.
#
# Leak checking is off, and that is a recorded decision rather than an oversight: the
# engine allocates its content tables once and lets the process exit with them held,
# so LeakSanitizer reports a fixed set of non-defects and a gate that is always noisy
# is a gate that gets skipped. Memory errors, not garbage collection, are the target.
# ASan and UBSan run together because the CP suites are the expensive part, not the
# instrumentation.
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all \
            -fsanitize-address-use-after-scope
ASAN_OPTIONS  ?= detect_leaks=0:halt_on_error=1:abort_on_error=1
UBSAN_OPTIONS ?= print_stacktrace=1:halt_on_error=1

test-asan:
	@probe=$$(mktemp -d); printf 'int main(void){return 0;}\n' > $$probe/p.c; \
	 if $(CC) $(SANFLAGS) -o $$probe/p $$probe/p.c 2>$$probe/err; then rc=0; else rc=1; fi; \
	 if [ $$rc -ne 0 ]; then \
	   if [ "$${ASAN_REQUIRED:-0}" = 1 ]; then \
	     echo "test-asan: required here, but $(CC) cannot link the sanitizer runtime:"; \
	     sed 's/^/    /' $$probe/err; rm -rf $$probe; exit 1; \
	   fi; \
	   echo "SKIP test-asan: $(CC) has no sanitizer runtime (install libasan and libubsan)"; \
	   sed 's/^/    /' $$probe/err | head -2; rm -rf $$probe; exit 0; \
	 fi; rm -rf $$probe; \
	 echo "test-asan: $(SANFLAGS)"; \
	 status=0; \
	 ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS="$(UBSAN_OPTIONS)" \
	   $(MAKE) --no-print-directory BUILD=$(BUILD)/asan OPT="-O1 -g $(SANFLAGS)" \
	     LDFLAGS="$(SANFLAGS)" U2_CCFLAGS="$(U2_CCFLAGS) $(SANFLAGS)" test || status=$$?; \
	 $(MAKE) --no-print-directory -B $(BIN) >/dev/null 2>&1 || true; \
	 [ $$status -eq 0 ] && echo "test-asan: PASS"; exit $$status

# Re-record the real playthroughs and verify every one under three backends. Needs the
# gitignored game content AND the save slot those recordings started from, so it is not
# part of `test` and never runs in CI: a gate that skips silently when its input is absent
# is the defect AGENTS.md § 3 warns about, so this refuses with the reason instead.
# Scratch lives under /var/tmp; override with SCRATCH=.
#   SAVE_SOURCE=~/Games/u2/saves make drive    # everything in engine/demos
#   SAVE_SOURCE=... SRC_DEMOS=demos/x.demo make drive   # one recording
drive: $(BIN)
	@test -f ../original/mapx20 || { echo "drive: needs ../original (the player's own game files); nothing to record against."; exit 1; }
	SAVE_SOURCE="$${SAVE_SOURCE:-$(HOME)/Games/u2/saves}" bash tests/drive_and_validate.sh

# Every suite runs with stdin closed. An engine invocation that has no --script otherwise
# inherits whatever stdin the suite has, and under a human terminal that means it waits for
# input forever: found 2026-11-21, when run_cp4's `--headless --config config.ini` case hung
# eight minutes at 0% CPU in a tty and passed instantly under a pipe. CI pipes, so CI never
# saw it, which is the whole point — a gate that only works when a machine runs it is a gate
# that will be distrusted the first time a person runs it by hand.
test-demo: $(BIN)
	bash tests/run_demo.sh < /dev/null

test-api: $(BIN)
	bash tests/run_api.sh < /dev/null

# Drive the API from a shell the way a bot would. Needs nothing but the binary.
api: $(BIN)
	U2="$PWD/$(BIN)" python3 tools/u2api.py

test-cp0: $(BIN)
	bash tests/run_cp0.sh < /dev/null

test-cp1: $(BIN)
	bash tests/run_cp1.sh < /dev/null

test-cp2: $(BIN)
	bash tests/run_cp2.sh < /dev/null

test-cp3: $(BIN)
	bash tests/run_cp3.sh < /dev/null

test-cp4: $(BIN)
	bash tests/run_cp4.sh < /dev/null

# CP5 combat: src/game/combat.c implements C1/C2/C6 and is linked into the engine
# build (src/game/*.c), so the behavioural spec runs for real. The two fixture
# controls and the zero-check self-test run whether or not combat.c is present,
# so the spec cannot go soft on a revert. See tests/run_cp5.sh and
# docs/phase4-state.md § CP5.
test-cp5:
	bash tests/run_cp5.sh < /dev/null

# CP6 commerce: src/game/commerce.c is the TRANSACT ray, the asm/1590 vendor selector and
# the shops it reaches. The pure half (price walk, selector table, BCD charge) runs anywhere;
# the behavioural half drives ./u2 against a synthetic sector and SKIPs only when the content
# strings cannot be extracted, which is the same rule CP1 runs under. See tests/run_cp6.sh and
# docs/commerce.md.
test-cp6:
	bash tests/run_cp6.sh < /dev/null

# CP7 dungeons: game/dungeon.c is the interior sector — the paged grid, the four movement
# commands, the torch, the wireframe and the chest. The gate drives ./u2 against a synthetic
# dungeon written by tests/cp7_dungeon_seed.py and SKIPs only when the content strings cannot
# be extracted. See tests/run_cp7.sh and docs/phase4-state.md § CP7.
test-cp7:
	bash tests/run_cp7.sh < /dev/null

# CP9 placeholder. SDL must be approved and installed by the owner first;
# this target deliberately does not attempt any install.
# SDL is an owner-approved dependency (docs/phase4-plan.md § 6 stop-and-ask), so
# it never enters the default build: the objects are compiled a second time into
# a separate directory with -DU2_HAVE_SDL, and src/platform/*_sdl.c are empty
# without that define. `make` alone still needs nothing but a C compiler.
#
# Which SDL `make sdl` links is decided by tools/sdl_probe.sh. The project builds
# against SDL3 only (docs/sdl2-vs-sdl3.md § 9): the old behaviour of asking
# sdl2-config was what quietly produced a binary linked against sdl2-compat, the
# SDL2 API implemented over SDL3, which compiled and ran and was not what shipped.
# A host with only SDL2 now gets a refusal that names the mismatch.
# Lazy on purpose: `make test` and `make clean` must not pay for a probe they
# never use, and a recursive assignment is only expanded if an SDL target asks.
SDL_PROBE    = $(shell sh tools/sdl_probe.sh --quiet 2>/dev/null)
SDL_MODE     = $(patsubst mode=%,%,$(filter mode=%,$(SDL_PROBE)))
SDL_IMPL     = $(patsubst impl=%,%,$(filter impl=%,$(SDL_PROBE)))
SDL_LINKAGE  = $(patsubst linkage=%,%,$(filter linkage=%,$(SDL_PROBE)))
SDL_HOSTVER  = $(patsubst version=%,%,$(filter version=%,$(SDL_PROBE)))
SDL_ARTIFACT = $(patsubst artifact=%,%,$(filter artifact=%,$(SDL_PROBE)))

ifeq ($(SDL_MODE),static)
SDL_CFLAGS  = $(SDLSTAT_CFLAGS)
SDL_LIBS    = $(SDLSTAT_LIBS)
else
SDL_CFLAGS  = $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS    = $(shell pkg-config --libs sdl3 2>/dev/null)
endif

STAMP_SDL = $(call STAMP,sdl,$(SDL_IMPL),$(SDL_LINKAGE),$(SDL_HOSTVER),$(SDL_ARTIFACT))

SDLOBJ     := $(BUILD)-sdl
SDL_BIN    := u2-sdl

sdl: sdl-probe-check $(SDL_BIN)

# The refusals live here, not in the object rule, because an object rule is skipped
# whenever the objects happen to be up to date -- so a guard placed there fires on
# a clean tree and silently stops firing on a dirty one. That is exactly how the
# first draft of this behaved: `make sdl U2_STRICT_SDL=1` printed the dev-build
# note and exited 0 with everything already compiled. A phony prerequisite always
# runs.
sdl-probe-check:
	@test -n "$(SDL_CFLAGS)" || { echo "sdl: no usable SDL3 found (probe said mode=$(SDL_MODE))." \
	     "Options: deploy/linuxbuild/build.sh for the release artifact; install SDL3;" \
	     "or point SDLSTAT_PREFIX at a prefix containing lib/libSDL3.a."; exit 1; }
	@if [ "$(U2_STRICT_SDL)" = 1 ] && [ "$(SDL_ARTIFACT)" != release ]; then \
	    echo "sdl: REFUSING under U2_STRICT_SDL=1: this would link $(SDL_IMPL) ($(SDL_LINKAGE)),"; \
	    echo "sdl:   which is not the pinned static SDL3 the release ships."; \
	    echo "sdl:   Build it with deploy/linuxbuild/build.sh, or unset U2_STRICT_SDL"; \
	    echo "sdl:   if you only want a development binary."; \
	    exit 1; \
	fi

$(SDL_BIN): $(SDLOBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(SDL_LIBS)
	$(WRITE_STAMP)
	@echo "u2: built $(SDL_BIN) — target=sdl sdl=$(SDL_IMPL) linkage=$(SDL_LINKAGE) artifact=$(SDL_ARTIFACT)"
	@if [ "$(SDL_ARTIFACT)" = dev ]; then \
	    echo "u2: NOTE this is a DEV build, not the shipped artifact. Players get"; \
	    echo "u2:      u2-sdl-static from deploy/linuxbuild/build.sh (pinned static SDL3)."; \
	    echo "u2:      './$(SDL_BIN) --version' always repeats this."; \
	fi

$(SDLOBJ)/%.o: %.c $(GEN) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DU2_HAVE_SDL $(STAMP_SDL) $(SDL_CFLAGS) -MMD -MP -c $< -o $@

sdl-run: $(SDL_BIN)
	./$(SDL_BIN) --sdl --play-map --data-dir ../original --save-dir saves

# ---- Windows cross-build (make sdl-win) ------------------------------------------
#
# Static-SDL Windows binary, built with mingw-w64. The low-friction way to get
# the toolchain on Bazzite is the container in deploy/winbuild/ (no host layering
# on an immutable OS); build.sh runs exactly this target inside it. Override
# CROSS= for a different triple. Fedora's mingw64-sdl2 ships libsdl2.a, so the
# result is one .exe whose only DLL imports are stock Windows ones — that is the
# whole point of the static link, and it is what the macOS path will NOT get for
# free (Apple SDK, so that build is a CI macos runner, deferred by the owner
# 2026-11-02).
CROSS          ?= x86_64-w64-mingw32-
WIN_CC         := $(CROSS)gcc
WIN_PKGCONFIG  ?= pkg-config
SDLWIN_CFLAGS  ?= $(shell $(WIN_PKGCONFIG) --cflags sdl3 2>/dev/null)
# Drop SDL's main-rewrite define if a pkg-config still emits one: this build keeps
# the console subsystem (stderr diagnostics and the wine smoke test need it), so
# plain main() is the entry. SDL3 does not add this define, so the filter is a
# harmless holdover from the SDL2 days rather than a live requirement, and it
# stays because -Dmain=SDL_main trips -Werror=missing-prototypes if it ever
# reappears.
SDLWIN_CFLAGS  := $(filter-out -Dmain=SDL_main,$(SDLWIN_CFLAGS))
SDLWIN_LIBS    ?= $(shell $(WIN_PKGCONFIG) --static --libs sdl3 2>/dev/null)
# SDL's pkg-config adds -mwindows, which produces a GUI-subsystem PE. Keep the
# console subsystem instead: the engine writes its diagnostics to stderr, and a
# GUI-subsystem binary under wine loses them — which is exactly the evidence a
# cross-build needs when it fails to start. The comment above promising a console
# subsystem for the wine smoke test was aspirational until this line.
SDLWIN_LIBS    := $(filter-out -mwindows,$(SDLWIN_LIBS))
WINOBJ         := $(BUILD)-win
WIN_BIN        := u2-sdl.exe
# The clock is the one object that is not portable: mingw declares
# clock_gettime and nanosleep and then has no symbols to link them against, so
# the Windows tree swaps the backend rather than patching the POSIX one.
WIN_SRCS       := $(filter-out src/platform/time_posix.c,$(SRCS)) src/platform/time_win32.c
WINOBJS        := $(patsubst %.c,$(WINOBJ)/%.o,$(WIN_SRCS))
WIN_DEPS       := $(WINOBJS:.o=.d)

sdl-win: $(WIN_BIN)

$(WIN_BIN): $(WINOBJS)
	$(WIN_CC) -static -o $@ $^ $(SDLWIN_LIBS)

$(WINOBJ)/%.o: %.c $(GEN) Makefile
	@command -v $(WIN_CC) >/dev/null || { echo "sdl-win: $(WIN_CC) not found; use deploy/winbuild/build.sh"; exit 1; }
	@test -n "$(SDLWIN_CFLAGS)" || { echo "sdl-win: static mingw SDL3 not found (PKG_CONFIG_LIBDIR must reach libSDL3.a); use deploy/winbuild/build.sh"; exit 1; }
	@mkdir -p $(dir $@)
	$(WIN_CC) $(CPPFLAGS) $(CFLAGS) -DU2_HAVE_SDL $(STAMP_SDLWIN) $(SDLWIN_CFLAGS) -MMD -MP -c $< -o $@

-include $(WIN_DEPS)

# ---- Linux static-SDL build (make sdl-static) ---------------------------------
#
# The player-facing Linux binary: SDL3 linked static, glibc left dynamic (a
# static glibc is a footgun, not a feature). SDL3 dlopens X11/Wayland/ALSA at
# runtime, so what survives the link is a handful of libraries every desktop
# already has — which is the point: a real player downloads one file and runs
# it, and the container story stays what it is, a dev trick.
# SDLSTAT_PREFIX is where libSDL3.a lives (the linuxbuild container builds it
# from the pinned tarball, same source and flags as the Windows build).
SDLSTAT_PREFIX ?= /opt/sdl3-linux
SDLSTAT_CC     ?= $(CC)
SDLSTAT_PKG    ?= env PKG_CONFIG_LIBDIR=$(SDLSTAT_PREFIX)/lib/pkgconfig:$(SDLSTAT_PREFIX)/lib64/pkgconfig pkg-config
SDLSTAT_CFLAGS ?= $(shell $(SDLSTAT_PKG) --cflags sdl3 2>/dev/null)
SDLSTAT_LIBS   ?= $(shell $(SDLSTAT_PKG) --static --libs sdl3 2>/dev/null)
STATOBJ        := $(BUILD)-sdlstatic
STAT_BIN       := u2-sdl-static
STATOBJS       := $(patsubst %.c,$(STATOBJ)/%.o,$(SRCS))
STAT_DEPS      := $(STATOBJS:.o=.d)

sdl-static: $(STAT_BIN)

$(STAT_BIN): $(STATOBJS)
	$(SDLSTAT_CC) -static-libgcc -o $@ $^ $(SDLSTAT_LIBS)
	$(WRITE_STAMP)

$(STATOBJ)/%.o: %.c $(GEN) Makefile
	@test -n "$(SDLSTAT_CFLAGS)" || { echo "sdl-static: static SDL3 not found at $(SDLSTAT_PREFIX);" \
	     "use deploy/linuxbuild/build.sh (container) or point SDLSTAT_PREFIX at a libSDL3.a prefix"; exit 1; }
	@mkdir -p $(dir $@)
	$(SDLSTAT_CC) $(CPPFLAGS) $(CFLAGS) -DU2_HAVE_SDL $(STAMP_SDLSTATIC) $(SDLSTAT_CFLAGS) -MMD -MP -c $< -o $@

-include $(STAT_DEPS)

# play: put the game on the operator's screen.
#
# Unlike sdl-run this synthesises the display environment from the socket files,
# because the agent shell is not a graphical login session and a bare ./u2-sdl
# there silently falls back to the terminal renderer. It also builds the SDL
# binary when missing, and refuses rather than launching into nothing.
#
#   make play                 player's own extracted art
#   make play CLEANROOM=1     compiled-in substitute art
play: $(SDL_BIN)
	@if [ -n "$(CLEANROOM)" ]; then \
	    tools/play.sh --cleanroom; \
	else \
	    tools/play.sh; \
	fi

clean:
	rm -rf $(BUILD) $(SDLOBJ) $(WINOBJ) $(STATOBJ) $(BIN) $(SDL_BIN) $(WIN_BIN) $(STAT_BIN)

# Fail loudly if a built binary predates the sources it is made from.
#
# This exists because the SDL object tree was, until 2026-10-29, compiled with -MMD -MP but
# its .d files were never included. A header edit therefore did not rebuild the SDL objects,
# and the link mixed objects compiled against DIFFERENT versions of a struct. Adding a field
# to ctx.h produced a u2-sdl whose members were read at the wrong offsets, which surfaced as
# a hang with no error anywhere and cost four turns to find. Two stale-binary incidents in one
# day, both invisible, both from trusting that make knew what it depended on.
#
# The -include below is the cure. This target is the tripwire: cheap enough to run before any
# deploy, and it names the offenders instead of silently relinking.
#
# It grades each binary against the stamp its own link rule wrote, never against mtimes. The
# first draft ran `find src ../asm $(GEN) -newer $$bin`, which fired on two whole classes of
# non-event: every edit to the 1212-file asm/ corpus, which reaches the build only through
# $(GEN), and any git operation that rewrites mtimes without changing a byte. It went red often
# enough that people cleaned the tree to get rid of it, which is how a tripwire dies.
# Per-binary stamps are what keep the discrimination this gate was written for: one global
# stamp would let a `make` that rebuilt only `u2` declare a stale `u2-sdl` fresh.
#
# A binary with no stamp is STALE, not skipped. That is what a binary built before this change,
# or by a rule that does not stamp, is supposed to look like.
FRESH_TARGETS = $(BIN):terminal $(SDL_BIN):sdl $(STAT_BIN):sdlstatic

fresh:
	@bad=0; \
	for spec in $(FRESH_TARGETS); do \
	  bin=$${spec%%:*}; kind=$${spec##*:}; \
	  [ -e "$$bin" ] || continue; \
	  if [ "$$kind" = sdl ] && [ -z "$(SDL_CFLAGS)" ]; then \
	    echo "SKIP: $$bin cannot be built on this host (no SDL3 found). Delete it — do not keep a stale one to quiet this gate."; continue; \
	  fi; \
	  if [ "$$kind" = sdlstatic ] && [ -z "$(SDLSTAT_CFLAGS)" ]; then \
	    echo "SKIP: $$bin cannot be built on this host (no static SDL3 at $(SDLSTAT_PREFIX)). Delete it — do not keep a stale one to quiet this gate."; continue; \
	  fi; \
	  want=$$( $(FRESH_HASH) ); have=$$(cat $(STAMPDIR)/stamp.$$bin 2>/dev/null); \
	  if [ "$$want" != "$$have" ]; then \
	    echo "STALE: $$bin predates the current sources"; \
	    echo "  stamp: $${have:-<none>}"; echo "  now:   $$want"; bad=1; \
	  fi; \
	done; \
	if [ "$$bad" -ne 0 ]; then \
		echo "fresh: run 'make clean' before rebuilding."; exit 1; \
	fi; \
	echo "fresh: every binary matches its input stamp"

ccflags:
	@printf '%s\n' "$(U2_CCFLAGS)"

# gcc's static analyzer over every engine TU. The gate lives in tools/ so it can carry
# its own control and its two documented suppressions; this target is the discoverable
# entry point, not a second implementation.
analyze:
	@python3 ../tools/analyzer_gate.py

help:
	@echo "targets: all run headless test test-cp0 test-cp1 test-cp2 test-cp3 test-cp4 sdl sdl-run play clean content content-check compile-commands compile-commands-check fresh ccflags test-asan help"

-include $(DEPS) $(SDL_DEPS)
