// Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
// SPDX-License-Identifier: BSD-4-Clause
//
// spiproxy-cli: debug/test client for lan80xx-spid.
//
//   spiproxy-cli [-s sock] read  <slice> <mmd> <reg>
//   spiproxy-cli [-s sock] write <slice> <mmd> <reg> <val>
//   spiproxy-cli [-s sock] bench <n>              # proxied read RTT stats
//   spiproxy-cli [-s sock] hammer <seconds>       # low-prio read flood
//   spiproxy-cli [-s sock] claimtest <ms>         # claim, read, hold, release
//   spiproxy-cli [-s sock] [-t ms] mailbox <cmd> [hexbytes]  # -t: MB timeout
//   spiproxy-cli [-s sock] reset [assert_us [deassert_us]]  # HW reset pulse
//   spiproxy-cli [-s sock] stats | trace

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "spiproxy.h"
#include "lan80xx_regs.h"
#include "parse.h"

static int sock_fd = -1;
static const char *sock = SPIPROXY_SOCK;
static uint32_t seq;

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

static int xfer(uint8_t type, uint16_t flags, const void *body, uint32_t blen,
                void *resp, uint32_t *rlen)
{
    uint8_t msg[sizeof(struct spiproxy_hdr) + SPIPROXY_MSG_MAX];
    struct spiproxy_hdr *h = (struct spiproxy_hdr *)msg;
    ssize_t n;

    if (sock_fd < 0) {   /* connect lazily, so arg validation can fail first */
        struct sockaddr_un sa = { .sun_family = AF_UNIX };

        sock_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        strncpy(sa.sun_path, sock, sizeof(sa.sun_path) - 1);
        if (connect(sock_fd, (struct sockaddr *)&sa, sizeof(sa))) {
            perror(sock);
            close(sock_fd);
            sock_fd = -1;
            return -1;
        }
    }

    h->ver = SPIPROXY_VER;
    h->type = type;
    h->flags = flags;
    h->seq = ++seq;
    h->len = blen;
    if (blen) {
        memcpy(msg + sizeof(*h), body, blen);
    }
    if (send(sock_fd, msg, sizeof(*h) + blen, 0) < 0) {
        perror("send");
        return -1;
    }
    n = recv(sock_fd, msg, sizeof(msg), 0);
    if (n < (ssize_t)sizeof(*h)) {
        perror("recv");
        return -1;
    }
    if (h->seq != seq || h->type != (type | SPIPROXY_RESP)) {
        fprintf(stderr, "bad response (seq %u type %#x)\n", h->seq, h->type);
        return -1;
    }
    if (resp && rlen) {
        *rlen = h->len < *rlen ? h->len : *rlen;
        memcpy(resp, msg + sizeof(*h), *rlen);
    }
    return h->flags; /* status */
}

static int cmp_u32(const void *a, const void *b)
{
    return (int)(*(const uint32_t *)a - *(const uint32_t *)b);
}

int main(int argc, char **argv)
{
    struct spiproxy_op op = { 0 };
    uint32_t rlen;
    unsigned long v, timeout_ms = 0;
    int o, st;

    while ((o = getopt(argc, argv, "s:t:h")) != -1) {
        if (o == 's') {
            sock = optarg;
        } else if (o == 't') {
            if (!parse_uint(optarg, 0, 0, 0xffffffff, &timeout_ms)) {
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "see header comment for usage\n");
            return EXIT_FAILURE;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc < 1) {
        fprintf(stderr, "missing command\n");
        return EXIT_FAILURE;
    }

    if (!strcmp(argv[0], "read") && argc == 4) {
        if (!parse_uint(argv[1], 0, 0, 0xff, &v)) return EXIT_FAILURE;
        op.slice = (uint8_t)v;
        if (!parse_uint(argv[2], 0, 0, 0xff, &v)) return EXIT_FAILURE;
        op.mmd = (uint8_t)v;
        if (!parse_uint(argv[3], 0, 0, 0xffff, &v)) return EXIT_FAILURE;
        op.reg = (uint16_t)v;
        rlen = sizeof(op);
        st = xfer(SPIPROXY_READ, 0, &op, sizeof(op), &op, &rlen);
        printf("status=%d slice=%u mmd=0x%02x reg=0x%04x val=0x%08x\n",
               st, op.slice, op.mmd, op.reg, op.val);
        return st != SPIPROXY_OK;
    }
    if (!strcmp(argv[0], "write") && argc == 5) {
        if (!parse_uint(argv[1], 0, 0, 0xff, &v)) return EXIT_FAILURE;
        op.slice = (uint8_t)v;
        if (!parse_uint(argv[2], 0, 0, 0xff, &v)) return EXIT_FAILURE;
        op.mmd = (uint8_t)v;
        if (!parse_uint(argv[3], 0, 0, 0xffff, &v)) return EXIT_FAILURE;
        op.reg = (uint16_t)v;
        if (!parse_uint(argv[4], 0, 0, 0xffffffff, &v)) return EXIT_FAILURE;
        op.val = (uint32_t)v;
        rlen = sizeof(op);
        st = xfer(SPIPROXY_WRITE, 0, &op, sizeof(op), &op, &rlen);
        printf("status=%d\n", st);
        return st != SPIPROXY_OK;
    }
    if (!strcmp(argv[0], "reset")) {
        struct spiproxy_reset rq = { 0 };

        if (argc >= 2) {
            if (!parse_uint(argv[1], 0, 0, 0xffffffff, &v)) return EXIT_FAILURE;
            rq.assert_us = (uint32_t)v;
        }
        if (argc >= 3) {
            if (!parse_uint(argv[2], 0, 0, 0xffffffff, &v)) return EXIT_FAILURE;
            rq.deassert_us = (uint32_t)v;
        }
        st = xfer(SPIPROXY_RESET, 0, &rq, sizeof(rq), NULL, NULL);
        if (st == SPIPROXY_ENOSYS)
            fprintf(stderr,
                    "reset: daemon has no reset GPIO configured (start it"
                    " with -r <line-name>, e.g. -r lan8023-rst)\n");
        printf("status=%d\n", st);
        return st != SPIPROXY_OK;
    }
    if (!strcmp(argv[0], "bench") && argc == 2) {
        int n, i;
        uint32_t *lat;
        uint64_t t;

        if (!parse_uint(argv[1], 10, 1, INT_MAX, &v)) return EXIT_FAILURE;
        n = (int)v;
        lat = calloc(n, sizeof(*lat));
        if (lat == NULL) {
            perror("calloc");
            return EXIT_FAILURE;
        }
        op.mmd = LAN80XX_MMD_GLOBAL;
        op.reg = LAN80XX_MCU_IO_MNGT_MISC_DEVICE_ID_REG;
        for (i = -50; i < n; i++) {
            t = now_us();
            rlen = sizeof(op);
            if (xfer(SPIPROXY_READ, 0, &op, sizeof(op), &op, &rlen) != SPIPROXY_OK) {
                fprintf(stderr, "read failed at %d\n", i);
                return EXIT_FAILURE;
            }
            if (i >= 0) {
                lat[i] = (uint32_t)(now_us() - t);
            }
        }
        qsort(lat, n, sizeof(*lat), cmp_u32);
        printf("proxied read rtt: n=%d min=%u p50=%u p90=%u p99=%u max=%u us (val=0x%08x)\n",
               n, lat[0], lat[n / 2], lat[(int)(n * 0.9)],
               lat[(int)(n * 0.99)], lat[n - 1], op.val);
        return EXIT_SUCCESS;
    }
    if (!strcmp(argv[0], "hammer") && argc == 2) {
        uint64_t t_end, cnt = 0;

        if (!parse_uint(argv[1], 10, 0, INT_MAX, &v)) return EXIT_FAILURE;
        t_end = now_us() + (uint64_t)v * 1000000;
        op.mmd = LAN80XX_MMD_GLOBAL;
        op.reg = LAN80XX_MCU_IO_MNGT_MISC_DEVICE_ID_REG;
        while (now_us() < t_end) {
            rlen = sizeof(op);
            if (xfer(SPIPROXY_READ, SPIPROXY_PRIO_LOW, &op, sizeof(op),
                     &op, &rlen) != SPIPROXY_OK) {
                fprintf(stderr, "hammer: read failed\n");
                return EXIT_FAILURE;
            }
            if (op.val == 0 || (op.val & 0xff00) != 0x8000) {
                fprintf(stderr, "hammer: bad DEVICE_ID 0x%08x after %llu reads\n",
                        op.val, (unsigned long long)cnt);
                return EXIT_FAILURE;
            }
            cnt++;
        }
        printf("hammer: %llu reads, all DEVICE_ID ok (0x%08x)\n",
               (unsigned long long)cnt, op.val);
        return EXIT_SUCCESS;
    }
    if (!strcmp(argv[0], "claimtest") && argc == 2) {
        struct {
            struct spiproxy_claim cl;
            struct spiproxy_op cleanup[1];
        } __attribute__((packed)) req = {
            .cl = { .ncleanup = 1 },
            /* harmless marker cleanup op: read DEVICE_ID */
            .cleanup = { { .slice = 0, .write = 0,
                           .mmd = LAN80XX_MMD_GLOBAL,
                           .reg = LAN80XX_MCU_IO_MNGT_MISC_DEVICE_ID_REG } },
        };
        if (!parse_uint(argv[1], 10, 0, 0xffffffff, &v)) return EXIT_FAILURE;
        req.cl.max_ms = (uint32_t)v;
        st = xfer(SPIPROXY_CLAIM, 0, &req, sizeof(req), NULL, NULL);
        printf("claim: status=%d\n", st);
        if (st != SPIPROXY_OK) {
            return EXIT_FAILURE;
        }
        op.mmd = LAN80XX_MMD_GLOBAL;
        op.reg = LAN80XX_MCU_IO_MNGT_MISC_DEVICE_ID_REG;
        rlen = sizeof(op);
        st = xfer(SPIPROXY_READ, 0, &op, sizeof(op), &op, &rlen);
        printf("read under claim: status=%d val=0x%08x\n", st, op.val);
        usleep(200000);
        st = xfer(SPIPROXY_RELEASE, 0, NULL, 0, NULL, NULL);
        printf("release: status=%d\n", st);
        return EXIT_SUCCESS;
    }
    if (!strcmp(argv[0], "mailbox") && argc >= 2) {
        struct {
            struct spiproxy_mb mb;
            uint8_t payload[SPIPROXY_MB_MAX];
        } __attribute__((packed)) req = { 0 };
        uint8_t resp[sizeof(struct spiproxy_mb) + SPIPROXY_MB_MAX];
        struct spiproxy_mb *rmb = (struct spiproxy_mb *)resp;
        int i;

        if (!parse_uint(argv[1], 0, 0, 0xff, &v)) return EXIT_FAILURE;
        req.mb.cmd = (uint8_t)v;
        req.mb.timeout_ms = (uint32_t)timeout_ms;  /* -t; 0 = daemon default */
        for (i = 2; i < argc; i++) {   /* every remaining arg is a hex byte */
            if (req.mb.payload_len >= SPIPROXY_MB_MAX) {
                fprintf(stderr, "mailbox: too many payload bytes (max %d)\n",
                        SPIPROXY_MB_MAX);
                return EXIT_FAILURE;
            }
            if (!parse_uint(argv[i], 16, 0, 0xff, &v)) return EXIT_FAILURE;
            req.payload[req.mb.payload_len++] = (uint8_t)v;
        }
        rlen = sizeof(resp);
        st = xfer(SPIPROXY_MAILBOX, 0, &req, sizeof(req.mb) + req.mb.payload_len,
                  resp, &rlen);
        printf("mailbox: status=%d resp_id=0x%02x payload_len=%u\n",
               st, rmb->cmd, rmb->payload_len);
        for (i = 0; i < rmb->payload_len && st == SPIPROXY_OK; i++) {
            printf("%02x%s", resp[sizeof(*rmb) + i], (i & 15) == 15 ? "\n" : " ");
        }
        if (st == SPIPROXY_OK && rmb->payload_len) {
            printf("\n");
        }
        return st != SPIPROXY_OK;
    }
    if (!strcmp(argv[0], "stats") || !strcmp(argv[0], "trace")) {
        char txt[SPIPROXY_MSG_MAX + 1];
        rlen = SPIPROXY_MSG_MAX;
        st = xfer(!strcmp(argv[0], "stats") ? SPIPROXY_STATS : SPIPROXY_TRACE,
                  0, NULL, 0, txt, &rlen);
        txt[rlen] = 0;
        fputs(txt, stdout);
        return st != SPIPROXY_OK;
    }
    fprintf(stderr, "unknown command '%s'\n", argv[0]);
    return EXIT_FAILURE;
}
