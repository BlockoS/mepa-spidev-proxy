// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause
//
// libFuzzer harness for the lan80xx-spid request parser.
//
// It #includes lan80xx_spid.c built with -DSPIPROXY_FUZZ, which drops
// main() and swaps the SPI backend for hardware-free deterministic stubs,
// giving this TU direct access to the (static) daemon internals. A fuzz
// buffer is framed as one `spiproxy_hdr + body` and fed straight into
// exec_item(), so the read/write/batch/claim/mailbox parsers are exercised
// with no sockets, epoll, or hardware.
//
// Scope: a single request per input. The socket transport framing in
// client_msg() is bypassed (exec_item is called directly), and stateful
// sequences (claim -> op -> release, mailbox in-flight guard) are not yet
// covered -- see fuzz/README.md for the planned extensions.

#include "lan80xx_spid.c"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
    client_t c;
    qitem_t it;
    uint32_t body;

    if (size < sizeof(struct spiproxy_hdr)) {
        return 0;
    }

    /* Reset the mutable dispatch state so runs stay independent. */
    g.claim_owner = NULL;
    g.claim_ncleanup = 0;
    g.mb_inflight = 0;

    memset(&c, 0, sizeof(c));
    c.used = 1;
    c.id = 1;
    c.fd = -1;   /* send_resp()'s send() fails harmlessly on a bad fd */

    memset(&it, 0, sizeof(it));
    it.c = &c;
    /* Header fields (type/flags/seq) come from the input; keep the framing
     * self-consistent so the per-type length checks behave as in production. */
    memcpy(&it.hdr, data, sizeof(it.hdr));
    body = (uint32_t)(size - sizeof(struct spiproxy_hdr));
    if (body > SPIPROXY_MSG_MAX) {
        body = SPIPROXY_MSG_MAX;
    }
    it.hdr.len = body;
    memcpy(it.body, data + sizeof(struct spiproxy_hdr), body);

    exec_item(&it);
    return 0;
}
