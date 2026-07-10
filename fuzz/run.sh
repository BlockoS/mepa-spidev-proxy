#!/usr/bin/env bash
# Run afl-fuzz for a built harness. Normally invoked by `make -C fuzz run`
# (which builds the binaries first); can also be run directly once built.
#
#   run.sh [TARGET] [SECONDS] [INSTANCES]
#
# INSTANCES=1 runs a single instance with the live AFL UI. INSTANCES>1 launches
# a fleet sharing one output dir (1 main + N-1 secondaries, each pinned to a
# free core by AFL; main + up to 2 secondaries run CMPLOG), each logging to a
# file, and prints an afl-whatsup summary at the end.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
out="$root/build/afl"

t="${1:-fuzz_msg}"
s="${2:-60}"
j="${3:-$(nproc 2>/dev/null || echo 4)}"
[ -n "$j" ] || j=$(nproc 2>/dev/null || echo 4)

case "$t" in
    fuzz_msg)   corp=corpus ;;
    fuzz_frame) corp=corpus_frame ;;
    fuzz_resp)  corp=corpus_resp ;;
    *) echo "unknown target '$t' (fuzz_msg|fuzz_frame|fuzz_resp)" >&2; exit 1 ;;
esac

cor="$root/fuzz/$corp"
dict="$root/fuzz/spiproxy.dict"
cm="$out/${t}_cmplog"
tg="$out/${t}_afl"
o="$out/out-$t"
[ -x "$tg" ] && [ -x "$cm" ] || { echo "build first: make -C fuzz T=$t" >&2; exit 1; }
# Seed corpus is generated, not committed -- create it if absent (never clobber
# a grown one).
[ -d "$cor" ] || python3 "$root/fuzz/gen_seeds.py" "$root/fuzz/corpus"

export AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export ASAN_OPTIONS=abort_on_error=1:symbolize=0:detect_leaks=0

if [ "$j" -le 1 ]; then
    exec afl-fuzz -i "$cor" -o "$o" -x "$dict" -c "$cm" -V "$s" -- "$tg"
fi

rm -rf "$o"
echo "launching $j instances (T=$t, S=${s}s); logs: $o-*.log"
afl-fuzz -i "$cor" -o "$o" -x "$dict" -c "$cm" -M main -V "$s" -- "$tg" >"$o-main.log" 2>&1 &
sleep 2
n=1
while [ "$n" -lt "$j" ]; do
    c=()
    [ "$n" -le 2 ] && c=(-c "$cm")   # a few secondaries also run CMPLOG
    afl-fuzz -i "$cor" -o "$o" "${c[@]}" -S "sec$n" -V "$s" -- "$tg" >"$o-sec$n.log" 2>&1 &
    n=$((n + 1))
done
wait
echo "== summary =="
afl-whatsup -s "$o" 2>/dev/null | sed -n '/Summary/,/Cycles/p' || true
