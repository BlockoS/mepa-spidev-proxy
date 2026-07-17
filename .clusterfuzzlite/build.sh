#!/bin/bash -eu
# ClusterFuzzLite / OSS-Fuzz build script.
#
# $CC/$CFLAGS carry the sanitizer and fuzzing instrumentation the chosen
# $SANITIZER needs; the fuzzing engine (its main() + driver) is linked via
# $LIB_FUZZING_ENGINE. Do NOT add -fsanitize=fuzzer here -- the harness has no
# main() under SPIPROXY_FUZZ, exactly what the engine expects.

src="$SRC/mepa-spidev-proxy"

# Register enums are generated, not committed; produce lan80xx_regs.h from the
# sparse MESA headers fetched in the Dockerfile, into a build dir (not the
# source tree) that the compile adds via -I.
gen="$WORK/regs"
mkdir -p "$gen"
python3 "$src/gen_lan80xx_regs_header.py" "$SRC/mesa" "$gen/lan80xx_regs.h"

# fuzz_resp (the CLI parse_resp harness) is intentionally not built here: under
# SPIPROXY_FUZZ spiproxy_cli.c compiles to so little that OSS-Fuzz's
# bad_build_check rejects it as under-instrumented. It runs via CMake/AFL
# locally instead.
for h in fuzz_msg fuzz_frame; do
    # shellcheck disable=SC2086  # $CFLAGS/$LIB_FUZZING_ENGINE are flag lists
    $CC $CFLAGS -DSPIPROXY_FUZZ -I"$src" -I"$gen" \
        "$src/fuzz/$h.c" "$src/log.c" "$src/parse.c" \
        $LIB_FUZZING_ENGINE -o "$OUT/$h"
done

# Seed corpora (generated, not committed): <target>_seed_corpus.zip is
# auto-loaded for that target.
python3 "$src/fuzz/gen_seeds.py" "$src/fuzz/corpus"
zip -j "$OUT/fuzz_msg_seed_corpus.zip"   "$src/fuzz/corpus/"*
zip -j "$OUT/fuzz_frame_seed_corpus.zip" "$src/fuzz/corpus_frame/"*

# Shared protocol dictionary (<target>.dict is auto-loaded).
cp "$src/fuzz/spiproxy.dict" "$OUT/fuzz_msg.dict"
cp "$src/fuzz/spiproxy.dict" "$OUT/fuzz_frame.dict"
