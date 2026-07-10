# Fuzzing lan80xx-spid

`fuzz_msg` is a libFuzzer harness for the request parser — `exec_item()` and
the read / write / batch / claim / mailbox handlers, the surface that
consumes untrusted client messages.

It `#include`s `lan80xx_spid.c` built with `-DSPIPROXY_FUZZ`, which:
- drops `main()` (libFuzzer provides its own), and
- swaps the SPI backend (`spi_read_reg`/`spi_write_reg`) for hardware-free
  deterministic stubs (the mailbox `MB_FLAG` read reports the response
  ready, so `exec_mailbox()`'s poll loop returns immediately instead of
  spinning to its timeout).

Sanitizers are the bug oracle; the harness is built with
`-fsanitize=fuzzer,address,undefined`.

## Build (needs clang)

    cmake -S . -B build/fuzz -DSPIPROXY_FUZZ=ON -DCMAKE_C_COMPILER=clang
    cmake --build build/fuzz

## Run

    ASAN_OPTIONS=symbolize=0 UBSAN_OPTIONS=symbolize=0 \
        ./build/fuzz/fuzz_msg -print_funcs=0 -max_len=4096 corpus/

Seed `corpus/` from real traffic: capture the daemon's `-L` event log while
driving `spiproxy-cli`, or drop raw request datagrams (`spiproxy_hdr` + body)
as files. A good corpus gets the fuzzer past the trivial type/length checks
fast. A 15 s smoke run does ~9M execs clean at ~570k exec/s.

### If it appears to hang right after `#2 INITED`

That's the sanitizer's `llvm-symbolizer` subprocess blocking when libFuzzer
tries to print a newly covered function name -- an environment issue, not the
target. Disable coverage symbolization as above (`-print_funcs=0` plus
`ASAN_OPTIONS=symbolize=0`), or point `ASAN_SYMBOLIZER_PATH` at a working
`llvm-symbolizer`. (Crash reports are still symbolized on demand from the
saved reproducer.)

## Coverage report

Configure with the coverage option (clang) and build the `fuzz_coverage`
target: it replays a corpus through the harness and prints an llvm-cov
report for the parser.

    cmake -S . -B build/cov -DSPIPROXY_FUZZ=ON -DSPIPROXY_FUZZ_COVERAGE=ON \
        -DCMAKE_C_COMPILER=clang -DSPIPROXY_FUZZ_CORPUS=/path/to/corpus
    cmake --build build/cov --target fuzz_coverage

`SPIPROXY_FUZZ_CORPUS` defaults to `fuzz/corpus`; point it at a corpus grown
by a real run for a meaningful number. Only the parser subtree the harness
reaches is instrumented-and-hit -- `main`/epoll/gpio/real-SPI are compiled
out under `-DSPIPROXY_FUZZ`, so roughly half of lan80xx_spid.c reads as
uncovered by design.

For an annotated line-by-line view instead of the summary:

    llvm-cov show build/cov/fuzz_msg \
        -instr-profile=build/cov/fuzz_msg.profdata lan80xx_spid.c

## Scope and next steps

- **Single message per input.** Feed a length-prefixed sequence to exercise
  stateful paths (claim → op → release; the mailbox in-flight guard).
- **Transport framing bypassed.** `exec_item()` is called directly, so
  `client_msg()`'s `ver`/`len` checks aren't covered — add a second harness
  over that layer if wanted.
- **Depth.** Pair with AFL++ (`afl-clang-lto`, persistent mode + cmplog) to
  crack the header/CRC magic values faster than coverage alone.
- **CI.** ClusterFuzzLite in GitHub Actions gives short per-PR campaigns.
