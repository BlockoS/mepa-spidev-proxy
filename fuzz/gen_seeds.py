#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
# SPDX-License-Identifier: BSD-4-Clause
"""Generate a seed corpus for fuzz_msg.

Each file is one request datagram (spiproxy_hdr + body) covering a command
type, so the fuzzer starts from valid framing instead of rediscovering it
(and CI replays them as a regression set). Layouts mirror spiproxy.h. The
harness overrides ver/len, but they are set correctly here too.

Usage:
    gen_seeds.py <corpus-dir>
"""

from __future__ import annotations

import pathlib
import struct
import sys

VER = 1
# enum spiproxy_type
READ, WRITE, BATCH, CLAIM, RELEASE, MAILBOX, STATS, TRACE = 1, 2, 3, 4, 5, 6, 7, 8
RESET = 12


def hdr(msg_type: int, body: bytes) -> bytes:
    # struct spiproxy_hdr { u8 ver; u8 type; u16 flags; u32 seq; u32 len; }
    return struct.pack("<BBHII", VER, msg_type, 0, 1, len(body)) + body


def op(slice_: int, write: int, mmd: int, reg: int, val: int) -> bytes:
    # struct spiproxy_op { u8 slice; u8 write; u8 mmd; u8 rsvd;
    #                      u16 reg; u16 rsvd2; u32 val; }
    return struct.pack("<BBBBHHI", slice_, write, mmd, 0, reg, 0, val)


def seeds() -> dict[str, bytes]:
    claim = struct.pack("<IHH", 100, 1, 0) + op(0, 0, 0x1E, 0x0000, 0)
    payload = bytes([0xAA, 0xBB, 0xCC, 0xDD])
    mb = struct.pack("<BBHI", 0, 0x19, len(payload), 0) + payload
    return {
        "read":    hdr(READ,  op(0, 0, 0x1E, 0x0000, 0)),
        "write":   hdr(WRITE, op(0, 1, 0x0B, 0x0021, 0x1234)),
        "batch":   hdr(BATCH, op(0, 0, 0x09, 0x8002, 0)
                              + op(0, 0, 0x01, 0x0001, 0)
                              + op(1, 1, 0x0B, 0xE019, 0xFF)),
        "claim":   hdr(CLAIM, claim),
        "release": hdr(RELEASE, b""),
        "mailbox": hdr(MAILBOX, mb),
        "stats":   hdr(STATS, b""),
        "trace":   hdr(TRACE, b""),
        "reset":   hdr(RESET, struct.pack("<II", 10000, 100000)),
    }


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write(__doc__ or "")
        return 2
    d = pathlib.Path(argv[1])
    d.mkdir(parents=True, exist_ok=True)
    s = seeds()
    for name, data in s.items():
        (d / f"{name}.bin").write_bytes(data)
    sys.stderr.write(f"wrote {len(s)} seeds to {d}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
