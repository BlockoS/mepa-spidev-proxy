# AFL++ builds for the lan80xx-spid fuzz harnesses.
#
# The libFuzzer builds live in CMake (-DSPIPROXY_FUZZ). AFL needs its own clang
# wrapper, so it stays out of the CMake tree (see README.md). Each harness is
# built twice: an ASan+UBSan target (the oracle) and a CMPLOG companion for
# afl-fuzz -c.
#
#   make MESA_ROOT_DIR=/path/to/mesa            build every harness's _afl + _cmplog
#   make run MESA_ROOT_DIR=... T=fuzz_msg S=600 build (if needed) then afl-fuzz T
#   make clean
#
# MESA_ROOT_DIR points at a Microchip MESA checkout; lan80xx_regs.h is generated
# from it (not committed) -- same variable name as the CMake build.
#
# Targets: fuzz_msg (request path), fuzz_frame (transport parse),
#          fuzz_resp (CLI response parse).

ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))/..
OUT  := $(ROOT)/build/afl

HARNESSES := fuzz_msg fuzz_frame fuzz_resp
SRCS      := $(ROOT)/log.c $(ROOT)/parse.c
# -I$(OUT) picks up the generated header (below) without polluting the source.
CFLAGS    := -DSPIPROXY_FUZZ -I$(ROOT) -I$(OUT) -g -O2 -fsanitize=fuzzer -fno-sanitize-recover=all

# lan80xx_regs.h is generated from MESA (not committed) into the build dir; set
# MESA_ROOT_DIR to a checkout (same variable name the CMake build uses).
MESA_ROOT_DIR ?=
REGS_H        := $(OUT)/lan80xx_regs.h

# Prefer afl-clang-lto (best instrumentation); probe by actually linking so a
# missing ld.lld falls back to afl-clang-fast instead of erroring mid-build.
AFLCC := $(shell for c in afl-clang-lto afl-clang-fast afl-cc; do \
	command -v $$c >/dev/null 2>&1 && printf 'int main(void){return 0;}' | \
	$$c -x c - -o /dev/null 2>/dev/null && { echo $$c; break; }; done)

TARGETS := $(HARNESSES:%=$(OUT)/%_afl) $(HARNESSES:%=$(OUT)/%_cmplog)

T ?= fuzz_msg
S ?= 60
# J = instances to launch. Default: physical cores (one fuzzer per core; leaving
# the SMT siblings idle avoids execution-unit/cache contention that halves
# per-core throughput). J=1 = single instance with the live UI.
J ?= $(shell n=$$(lscpu -p=core 2>/dev/null | grep -v '^\#' | sort -un | wc -l); [ "$$n" -ge 1 ] && echo $$n || nproc 2>/dev/null || echo 4)
W ?= 5                                       # status refresh interval (seconds)

.PHONY: all run status clean check-cc
all: check-cc $(TARGETS)

check-cc:
	@[ -n "$(AFLCC)" ] || { echo "no working AFL++ compiler; install afl++ (and lld for afl-clang-lto)" >&2; exit 1; }

$(OUT):
	mkdir -p $@

# Generated register enums (not committed, into the build dir) -- needs MESA.
$(REGS_H): $(ROOT)/gen_lan80xx_regs_header.py | $(OUT)
	@[ -n "$(MESA_ROOT_DIR)" ] || { echo "set MESA_ROOT_DIR=/path/to/mesa (lan80xx_regs.h is generated from it)" >&2; exit 1; }
	python3 $< $(MESA_ROOT_DIR) $@

# ASan+UBSan oracle target.
$(OUT)/%_afl: $(ROOT)/fuzz/%.c $(SRCS) $(REGS_H) | $(OUT)
	AFL_USE_ASAN=1 AFL_USE_UBSAN=1 $(AFLCC) $(CFLAGS) $< $(SRCS) -o $@

# CMPLOG companion (no sanitizers).
$(OUT)/%_cmplog: $(ROOT)/fuzz/%.c $(SRCS) $(REGS_H) | $(OUT)
	AFL_LLVM_CMPLOG=1 $(AFLCC) $(CFLAGS) $< $(SRCS) -o $@

# Build the harness, then hand off to run.sh (single instance, or a J-way fleet).
run: check-cc $(OUT)/$(T)_afl $(OUT)/$(T)_cmplog
	@$(ROOT)/fuzz/run.sh $(T) $(S) $(J)

# Live afl-whatsup dashboard for T's run dir (use from a second terminal while
# a run is in progress). W sets the refresh interval.
status:
	@o=$(OUT)/out-$(T); \
	[ -d $$o ] || { echo "no run dir $$o -- start one with: make run T=$(T)" >&2; exit 1; }; \
	if command -v watch >/dev/null 2>&1; then exec watch -n $(W) afl-whatsup -s $$o; \
	else afl-whatsup -s $$o; fi

clean:
	rm -f $(TARGETS) $(REGS_H)
