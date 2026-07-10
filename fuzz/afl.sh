#!/usr/bin/env bash
# Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
# SPDX-License-Identifier: BSD-4-Clause
#
# Build (and optionally run) an AFL++ fuzzer for the lan80xx-spid request path.
#
# The libFuzzer harness (fuzz/fuzz_msg.c) is reused unchanged: AFL++'s clang
# wrapper recognizes -fsanitize=fuzzer and links its own persistent-mode driver
# around LLVMFuzzerTestOneInput/LLVMFuzzerInitialize.
#
# Two binaries are built into build/afl/:
#   fuzz_msg_afl     - instrumented target, ASan+UBSan as the bug oracle
#   fuzz_msg_cmplog  - CMPLOG (redqueen) companion passed to afl-fuzz -c; it
#                      cracks the mailbox-response CRC and the discrete
#                      type/mmd/register comparisons that block coverage-only
#                      mutation
#
# Usage:
#   fuzz/afl.sh                build both binaries, print the run command
#   fuzz/afl.sh run [SECONDS]  build if needed, then afl-fuzz (default 60s)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
out="$root/build/afl"

# Prefer LTO instrumentation (best coverage), fall back to PCGUARD (afl-clang-
# fast). LTO needs a matching ld.lld; probe by actually linking so a broken LTO
# toolchain (missing lld) falls through instead of erroring mid-build.
probe() { printf 'int main(void){return 0;}\n' | "$1" -x c - -o /dev/null 2>/dev/null; }
aflcc=""
for c in afl-clang-lto afl-clang-fast afl-cc; do
    if command -v "$c" >/dev/null 2>&1 && probe "$c"; then aflcc="$c"; break; fi
done
if [ -z "$aflcc" ]; then
    echo "No working AFL++ compiler found." >&2
    echo "Install AFL++ (sudo apt-get install -y afl++); for afl-clang-lto also" >&2
    echo "install a matching lld (e.g. lld-21), else afl-clang-fast is used." >&2
    exit 1
fi

mkdir -p "$out"
cflags=(-DSPIPROXY_FUZZ -I"$root" -g -O2 -fsanitize=fuzzer -fno-sanitize-recover=all)
srcs=("$root/fuzz/fuzz_msg.c" "$root/log.c" "$root/parse.c")

echo "[afl] compiler: $aflcc"
echo "[afl] building fuzz_msg_afl (ASan+UBSan oracle)"
AFL_USE_ASAN=1 AFL_USE_UBSAN=1 "$aflcc" "${cflags[@]}" "${srcs[@]}" -o "$out/fuzz_msg_afl"

echo "[afl] building fuzz_msg_cmplog (CMPLOG, no sanitizers)"
AFL_LLVM_CMPLOG=1 "$aflcc" "${cflags[@]}" "${srcs[@]}" -o "$out/fuzz_msg_cmplog"

# afl-fuzz options come before "--"; the target binary and its args after.
aflargs=(-i "$root/fuzz/corpus" -o "$out/out"
         -x "$root/fuzz/spiproxy.dict" -c "$out/fuzz_msg_cmplog")
target="$out/fuzz_msg_afl"

if [ "${1:-}" = "run" ]; then
    secs="${2:-60}"
    echo "[afl] running afl-fuzz for ${secs}s"
    AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
        ASAN_OPTIONS=abort_on_error=1:symbolize=0:detect_leaks=0 \
        afl-fuzz "${aflargs[@]}" -V "$secs" -- "$target"
else
    echo
    echo "[afl] built. Run with:"
    echo "  afl-fuzz ${aflargs[*]} -- $target"
    echo
    echo "afl-fuzz may need a one-time host tweak:"
    echo "  echo core | sudo tee /proc/sys/kernel/core_pattern"
    echo "  (or export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1)"
fi
