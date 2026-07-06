// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause
//
// lan80xx-spid API/protocol: shared between the SPI proxy daemon and
// its clients.
// Transport: SOCK_SEQPACKET unix socket, one datagram = one message =
// header + body.
// Responses echo `seq` with type | SPIPROXY_RESP and the status code in
// hdr.flags.

#ifndef SPIPROXY_H
#define SPIPROXY_H

#include <stdint.h>

#define SPIPROXY_VER       1
#define SPIPROXY_SOCK      "/run/lan80xx-spid.sock"
#define SPIPROXY_BATCH_MAX 64
#define SPIPROXY_MB_MAX    1018   /* mailbox payload, chip limit */
#define SPIPROXY_MSG_MAX   4096

enum spiproxy_type {
    SPIPROXY_READ = 1,   /* body: 1 x spiproxy_op (val ignored)          */
    SPIPROXY_WRITE,      /* body: 1 x spiproxy_op                        */
    SPIPROXY_BATCH,      /* body: N x spiproxy_op, atomic, N <= 64       */
    SPIPROXY_CLAIM,      /* body: spiproxy_claim [+ cleanup ops]         */
    SPIPROXY_RELEASE,    /* body: empty                                  */
    SPIPROXY_MAILBOX,    /* body: spiproxy_mb [+ payload]                */
    SPIPROXY_STATS,      /* body: empty; response body: text             */
    SPIPROXY_TRACE,      /* body: empty; response body: text (op ring)   */
    SPIPROXY_TSFIFO_SUB, /* reserved, ENOSYS in this version             */
    SPIPROXY_BER_WINDOW, /* reserved, ENOSYS in this version             */
    SPIPROXY_DFU,        /* reserved,  ENOSYS in this version            */
    SPIPROXY_RESET,      /* body: spiproxy_reset; pulse the HW reset GPIO */
};
#define SPIPROXY_RESP 0x80

enum spiproxy_status {
    SPIPROXY_OK = 0,
    SPIPROXY_EINVAL,     /* malformed request                            */
    SPIPROXY_EIO,        /* spidev transfer failed                       */
    SPIPROXY_EBUSY,      /* device claimed by another client / MB region */
    SPIPROXY_ETIMEDOUT,  /* mailbox response timeout                     */
    SPIPROXY_ENOSYS,     /* reserved request type                       */
    SPIPROXY_ECRC,       /* mailbox response CRC mismatch                */
};

/* hdr.flags, requests: bits[1:0] = priority class */
#define SPIPROXY_PRIO_NORMAL 0   /* config / MEPA                        */
#define SPIPROXY_PRIO_HIGH   1   /* PTP-class                            */
#define SPIPROXY_PRIO_LOW    2   /* debug tools                          */

struct spiproxy_hdr {
    uint8_t  ver;
    uint8_t  type;
    uint16_t flags;      /* request: priority; response: status          */
    uint32_t seq;        /* echoed in the response                       */
    uint32_t len;        /* body bytes following this header             */
};

/* One register access. Reads return val in the response; the daemon
 * performs the pipelined dummy DEVICE_ID collect internally. */
struct spiproxy_op {
    uint8_t  slice;      /* 0..3 (channel bits of the SPI instruction)   */
    uint8_t  write;      /* 0 = read, 1 = write                          */
    uint8_t  mmd;
    uint8_t  rsvd;
    uint16_t reg;
    uint16_t rsvd2;
    uint32_t val;
};

/* Whole-device claim. cleanup ops (immediately following this struct,
 * ncleanup <= SPIPROXY_BATCH_MAX) are executed by the daemon if the
 * claim ends abnormally (client death or max_ms expiry): e.g. the
 * DFE-restore writes of an interrupted eye scan. */
struct spiproxy_claim {
    uint32_t max_ms;
    uint16_t ncleanup;
    uint16_t rsvd;
};

/* MCU mailbox transaction (one full request/response protocol run,
 * P3 in the design doc). Response body: same struct (cmd = response
 * packet id, payload_len = response payload size) + payload. */
struct spiproxy_mb {
    uint8_t  slice;      /* base slice of the package                    */
    uint8_t  cmd;        /* enDEVICE_COMMANDS_T id                       */
    uint16_t payload_len;
    uint32_t timeout_ms; /* 0 = default 500 ms                           */
};

/* Hardware reset: the daemon pulses the LAN80xx reset line (the GPIO
 * named by `lan80xx-spid -r <line-name>`, e.g. "lan8023-rst", ACTIVE_HIGH).
 * The clean full reset that a soft GLOBAL_FAST_RESET
 * over SPI cannot do from a running OS.
 * 0 fields use the daemon defaults (10 ms assert / 100 ms deassert).
 * Status SPIPROXY_ENOSYS if no reset line was configured/found. */
struct spiproxy_reset {
    uint32_t assert_us;
    uint32_t deassert_us;
};

#endif /* SPIPROXY_H */
