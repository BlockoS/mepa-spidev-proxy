// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause
//
// libFuzzer harness for the lan80xx-spid request parser.
//
// It #includes lan80xx_spid.c built with -DSPIPROXY_FUZZ, which drops main()
// and swaps the SPI backend for hardware-free deterministic stubs, giving
// this TU direct access to the (static) daemon internals.
//
// Each input is a *sequence* of length-prefixed requests replayed through
// exec_item() against two synthetic clients, so stateful paths are exercised:
// whole-device claim/release, and the cross-client lint hazards (page-ride /
// cor-poach / rmw-race) that only fire across differing client ids. Framing
// per message: a 16-bit little-endian word whose top bit selects the client
// (0/1) and whose low 15 bits are the message length (spiproxy_hdr + body);
// the message bytes follow. No sockets, epoll, or hardware.
//
// Not covered: client_msg()'s transport framing (exec_item is called
// directly) and the abnormal-claim cleanup path (needs client death or the
// max_ms expiry -- both in the compiled-out transport layer).

#include "lan80xx_spid.c"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FUZZ_NCLIENTS 2

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    g.freq = 5000000;   /* nonzero; unused by the stub backend, kept sane */
    g.rst_fd = -1;      /* RESET returns ENOSYS -> no usleep on fuzz input */
    g.log = NULL;       /* no event-log side output */
    g.lctx.tag = "";
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    client_t cl[FUZZ_NCLIENTS];
    size_t off = 0;
    int i;

    /* Fresh dispatch + lint state so each input is independent. The qitem
     * pool is untouched (exec_item is called directly), so there is no need
     * to clear the ~1 MB of it. */
    g.claim_owner = NULL;
    g.claim_ncleanup = 0;
    g.claim_deadline = 0;
    g.mb_inflight = 0;
    g.ring_w = 0;
    g.rd_w = 0;
    memset(g.page_owner, 0, sizeof(g.page_owner));
    memset(g.cor_last, 0, sizeof(g.cor_last));
    memset(g.rd_ring, 0, sizeof(g.rd_ring));

    for (i = 0; i < FUZZ_NCLIENTS; i++) {
        memset(&cl[i], 0, sizeof(cl[i]));
        cl[i].used = 1;
        cl[i].id = (uint32_t)(i + 1);
        cl[i].fd = -1;   /* send_resp()'s send() fails harmlessly */
    }

    /* Replay a sequence of length-prefixed messages. */
    while (off + 2 <= size) {
        uint16_t framing = (uint16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
        unsigned who = framing >> 15;         /* client selector (0/1) */
        size_t mlen = framing & 0x7FFFU;      /* message length (hdr + body) */
        size_t avail;
        qitem_t it;
        uint32_t body;

        off += 2;
        avail = size - off;
        if (mlen > avail) {
            mlen = avail;
        }
        if (mlen < sizeof(struct spiproxy_hdr)) {
            break;   /* not a whole message left */
        }
        if (mlen > sizeof(struct spiproxy_hdr) + SPIPROXY_MSG_MAX) {
            mlen = sizeof(struct spiproxy_hdr) + SPIPROXY_MSG_MAX;
        }

        memset(&it, 0, sizeof(it));
        it.c = &cl[who % FUZZ_NCLIENTS];
        memcpy(&it.hdr, data + off, sizeof(it.hdr));
        body = (uint32_t)(mlen - sizeof(struct spiproxy_hdr));
        it.hdr.len = body;   /* keep framing self-consistent for the checks */
        memcpy(it.body, data + off + sizeof(struct spiproxy_hdr), body);

        exec_item(&it);
        off += mlen;
    }
    return 0;
}
