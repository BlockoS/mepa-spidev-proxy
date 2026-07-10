# Fuzzing lan80xx-spid

Two libFuzzer harnesses cover the surface that consumes untrusted client
input:
- `fuzz_msg` — the request parser: `exec_item()` and the read / write / batch
  / claim / mailbox handlers, driven through the real `enqueue()` /
  `drain_queues()` scheduler.
- `fuzz_frame` — the transport parse: `client_frame()`, the header
  `ver` / length-vs-bytecount / `SPIPROXY_MSG_MAX` validation a datagram hits
  before dispatch.

Both `#include` `lan80xx_spid.c` built with `-DSPIPROXY_FUZZ`, which:
- drops `main()` (libFuzzer provides its own), and
- swaps the SPI backend (`spi_read_reg`/`spi_write_reg`) for hardware-free
  stubs; the mailbox `MB_FLAG` read models the MCU handshake so
  `exec_mailbox()` runs one poll pass and returns instead of spinning to its
  timeout.

Shared harness setup lives in `fuzz/fuzz_common.h`. Sanitizers are the bug
oracle: both are built with `-fsanitize=fuzzer,address,undefined
-fno-sanitize-recover=all`, so a UB or memory error aborts and is saved as a
reproducer rather than merely printed.

## Build (needs clang)

    cmake -S . -B build/fuzz -DSPIPROXY_FUZZ=ON -DCMAKE_C_COMPILER=clang
    cmake --build build/fuzz

## Run

    ASAN_OPTIONS=symbolize=0 UBSAN_OPTIONS=symbolize=0 \
        ./build/fuzz/fuzz_msg -print_funcs=0 -max_len=4096 fuzz/corpus
    ASAN_OPTIONS=symbolize=0 UBSAN_OPTIONS=symbolize=0 \
        ./build/fuzz/fuzz_frame -print_funcs=0 fuzz/corpus_frame

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

## Seed corpus

`fuzz/corpus/` ships a small seed set -- one valid request per command type
(read/write/batch/claim/release/mailbox/stats/trace/reset), ~200 bytes total.
Regenerate with:

    python3 fuzz/gen_seeds.py fuzz/corpus

That also writes `fuzz/corpus_frame/` (raw datagrams) for `fuzz_frame`. Seeds
bootstrap the fuzzer past the framing checks and double as a CI regression
set. Grow the corpus with a real run, and minimize it back down with
`fuzz_msg -merge=1 fuzz/corpus grown/`.

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

For an annotated line-by-line HTML view instead of the summary, build the
`fuzz_coverage_html` target (writes `coverage-html/index.html` in the build
dir):

    cmake --build build/cov --target fuzz_coverage_html

## AFL++ (cracking the CRC / magic values)

libFuzzer's random mutation rarely satisfies the mailbox response CRC or the
discrete type/mmd/register constants, so those branches stay shallow. AFL++'s
CMPLOG (redqueen) instrumentation solves comparisons directly. The same
harness runs under AFL++ unchanged -- its clang wrapper links an AFL
persistent-mode driver when it sees `-fsanitize=fuzzer`.

    sudo apt-get install -y afl++      # 4.33+
    fuzz/afl.sh                        # build build/afl/fuzz_msg_{afl,cmplog}
    fuzz/afl.sh run 300                # build + fuzz 5 min (cmplog + dict)

`fuzz_msg_afl` carries ASan+UBSan (the oracle); `fuzz_msg_cmplog` is the `-c`
comparison-cracking companion. Findings land in
`build/afl/out/default/crashes/`; reproduce one against the libFuzzer binary:

    build/fuzz/fuzz_msg build/afl/out/default/crashes/id:000000*

`fuzz/spiproxy.dict` lists the protocol's discrete constants; pass it to
libFuzzer too with `-dict=fuzz/spiproxy.dict`.

## Scope and next steps

- **Real scheduler + fuzzed device replies.** Each input is a `spi_feed`
  followed by a sequence of length-prefixed messages. The messages go through
  the real `enqueue()`/`drain_queues()` path against two clients, exercising
  the priority queues, `item_eligible()` claim gating, and the cross-client
  lint hazards (page-ride / cor-poach / rmw-race); the SPI backend returns the
  `spi_feed`, so mailbox response parsing / CRC is driven by the input.
- **Mailbox in-flight guard + abnormal cleanup.** The stub `MB_FLAG` models
  the MCU handshake so `exec_mailbox()` runs one `drain_queues()` pass with
  `mb_inflight` set -- a foreign op to the mailbox region queued behind it
  then trips the in-flight guard, and a second mailbox is rejected as
  non-reentrant. After draining, both clients are dropped, covering the
  abnormal `claim_end()` cleanup-op path.
- **Transport framing.** `fuzz_frame` feeds raw datagrams to `client_frame()`
  (copied into an aligned buffer, as `recv()` fills one), covering the `ver` /
  length-mismatch / oversize reject paths. `fuzz_msg` still hands messages to
  `enqueue()` directly, skipping that layer by design.
- **Depth.** AFL++ CMPLOG cracks the mailbox CRC / magic-value branches that
  coverage-only mutation stalls on -- see the AFL++ section above.
- **CI.** ClusterFuzzLite in GitHub Actions gives short per-PR campaigns.
