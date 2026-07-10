// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause
//
// libFuzzer harness for the lan80xx-spid request path.
//
// It #includes lan80xx_spid.c built with -DSPIPROXY_FUZZ, which drops main()
// and swaps the SPI backend for a fuzzer-driven stub, giving this TU direct
// access to the (static) daemon internals.
//
// Each input is:
//     [u16 spi_feed_len][spi_feed bytes][ message sequence ]
// The spi_feed drives the stub SPI backend (spi_read_reg returns successive
// 4-byte little-endian words from it), so device replies -- mailbox response
// words in particular -- are fuzzed. The message sequence is a run of
// length-prefixed requests; each is a 16-bit frame (top bit = which of two
// clients, low 15 bits = message length) followed by the spiproxy_hdr + body.
//
// Rather than call exec_item() directly, the harness enqueue()s the messages
// and runs the real drain_queues() scheduler, so the queue, priority classes,
// item_eligible() and the whole-device claim gating are exercised, along with
// the cross-client lint hazards (page-ride / cor-poach / rmw-race). No
// sockets, epoll, or hardware.
//
// After draining, both clients are dropped, exercising the queue purge and --
// for a claim still held -- the abnormal claim_end() cleanup-op path.
//
// Not covered: client_msg()'s transport framing (bypassed here; messages are
// handed to enqueue() directly).

#include "lan80xx_spid.c"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#define FUZZ_NCLIENTS 2

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    g.freq = 5000000;   /* nonzero; unused by the stub backend, kept sane */
    g.rst_fd = -1;      /* RESET returns ENOSYS -> no usleep on fuzz input */
    g.ep_fd = -1;       /* client_drop()'s epoll_ctl() fails harmlessly */
    g.log = NULL;       /* no event-log side output */
    g.lctx.tag = "";
    setlogmask(0);      /* drop syslog output (perf; keeps the system log clean) */
    return 0;
}

/* Fresh per-input daemon state: dispatch/lint flags, the trace + rmw rings,
 * and the qitem freelist/queues (rebuilt, not the ~1 MB of bodies). */
static void fuzz_reset(void)
{
    int i;

    g.claim_owner = NULL;
    g.claim_ncleanup = 0;
    g.claim_deadline = 0;
    g.mb_inflight = 0;
    g.ring_w = 0;
    g.rd_w = 0;
    g.n_queued = 0;
    memset(g.page_owner, 0, sizeof(g.page_owner));
    memset(g.cor_last, 0, sizeof(g.cor_last));
    memset(g.rd_ring, 0, sizeof(g.rd_ring));
    for (i = 0; i < MAX_QITEMS - 1; i++) {
        g.pool[i].next = &g.pool[i + 1];
    }
    g.pool[MAX_QITEMS - 1].next = NULL;
    g.freel = &g.pool[0];
    for (i = 0; i < 3; i++) {
        g.qh[i] = NULL;
        g.qt[i] = NULL;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    client_t cl[FUZZ_NCLIENTS];
    size_t off, feed_len;
    int i;

    if (size < 2) {
        return 0;
    }
    /* [u16 spi_feed_len][spi_feed][message sequence] */
    feed_len = (size_t)(data[0] | ((size_t)data[1] << 8));
    off = 2;
    if (feed_len > size - off) {
        feed_len = size - off;
    }
    g_fuzz_spi = data + off;
    g_fuzz_spi_len = feed_len;
    g_fuzz_spi_pos = 0;
    off += feed_len;

    fuzz_reset();
    for (i = 0; i < FUZZ_NCLIENTS; i++) {
        memset(&cl[i], 0, sizeof(cl[i]));
        cl[i].used = 1;
        cl[i].id = (uint32_t)(i + 1);
        cl[i].fd = -1;   /* send_resp()'s send() fails harmlessly */
    }

    /* Enqueue a sequence of length-prefixed messages. */
    while (off + 2 <= size) {
        uint16_t framing = (uint16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
        unsigned who = framing >> 15;          /* client selector (0/1) */
        size_t mlen = framing & 0x7FFFU;       /* message length (hdr + body) */
        struct spiproxy_hdr h;
        size_t avail;

        off += 2;
        avail = size - off;
        if (mlen > avail) {
            mlen = avail;
        }
        if (mlen < sizeof(h)) {
            break;   /* not a whole message left */
        }
        if (mlen > sizeof(h) + SPIPROXY_MSG_MAX) {
            mlen = sizeof(h) + SPIPROXY_MSG_MAX;
        }
        memcpy(&h, data + off, sizeof(h));
        h.len = (uint32_t)(mlen - sizeof(h));   /* self-consistent framing */
        /* enqueue() only reads the body, so casting away const is safe. */
        enqueue(&cl[who % FUZZ_NCLIENTS], &h,
                (uint8_t *)(uintptr_t)(data + off + sizeof(h)));
        off += mlen;
    }

    /* Run the real scheduler: eligibility, priority, claim gating. */
    drain_queues(2 * MAX_QITEMS);

    /* Drop both clients: exercises the queue purge and, when a claim is still
     * held, the abnormal claim_end() cleanup-op path. */
    for (i = 0; i < FUZZ_NCLIENTS; i++) {
        client_drop(&cl[i]);
    }
    return 0;
}
