// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause
//
// libFuzzer harness for the spiproxy-cli response parser: parse_resp(), which
// interprets a datagram the CLI receives from the daemon. The daemon is
// trusted, but a buggy one -- or a local process squatting the socket while
// the daemon is down -- could return a malformed response, so the length
// handling must stay bounded.
//
// It #includes spiproxy_cli.c built with -DSPIPROXY_FUZZ (which drops main()).
// The input is one raw response datagram, copied into a heap buffer sized to
// exactly the input so any read past the received bytes is a genuine
// out-of-bounds ASan catches (xfer's fixed stack buffer would only read
// uninitialized bytes). want_seq/want_type are taken from the datagram so the
// seq/type gate passes and the length clamp + copy -- the path that had the
// over-read -- is always exercised.

#include "spiproxy_cli.c"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const size_t cap = sizeof(struct spiproxy_hdr) + SPIPROXY_MSG_MAX;
    uint8_t resp[SPIPROXY_MSG_MAX];
    uint32_t rlen = sizeof(resp);
    uint32_t want_seq = 0;
    uint8_t want_type = 0;
    uint8_t *msg;

    if (size > cap) {
        size = cap;                    /* recv() caps at the buffer */
    }
    msg = malloc(size ? size : 1);     /* exact size: over-read -> heap OOB */
    memcpy(msg, data, size);

    if (size >= sizeof(struct spiproxy_hdr)) {
        const struct spiproxy_hdr *h = (const struct spiproxy_hdr *)msg;
        want_seq = h->seq;                                /* pass the seq/type */
        want_type = (uint8_t)(h->type & ~SPIPROXY_RESP);  /* gate -> reach copy */
    }
    parse_resp(msg, (ssize_t)size, want_seq, want_type, resp, &rlen);
    free(msg);
    return 0;
}
