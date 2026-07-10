#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
# SPDX-License-Identifier: BSD-4-Clause
"""Generate a seed corpus for fuzz_msg.

Each file is one fuzz input in the harness format:

    [u16 spi_feed_len][spi_feed][ framed message ... ]

where the spi_feed drives the stub SPI backend (device replies) and each
framed message is a u16 (top bit = client 0/1, low 15 bits = length) followed
by a spiproxy_hdr + body. Seeds cover one valid request per command type plus
stateful sequences (claim/release, cross-client, and a mailbox with a foreign
op queued behind it that trips the in-flight guard) and a mailbox with a
fabricated response, so the fuzzer starts from valid framing and CI has a
regression set. Layouts mirror spiproxy.h; the harness overrides ver/len.

A sibling corpus_frame/ is also written for the fuzz_frame harness: raw
datagrams (a u16 length prefix + a valid spiproxy_hdr + body) that the fuzzer
mutates into the transport reject paths (bad ver, length mismatch, oversize).

Usage:
    gen_seeds.py <corpus-dir>       # e.g. fuzz/corpus (+ fuzz/corpus_frame)
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


def frame(msg: bytes, client: int = 0) -> bytes:
    return struct.pack("<H", ((client & 1) << 15) | (len(msg) & 0x7FFF)) + msg


def dgram(datagram: bytes) -> bytes:
    """u16 length prefix + a raw datagram, for the fuzz_frame harness."""
    return struct.pack("<H", len(datagram)) + datagram


def make(messages: list[tuple[int, bytes]], feed: bytes = b"") -> bytes:
    """messages: list of (client, message-bytes); prepend the spi feed."""
    out = struct.pack("<H", len(feed)) + feed
    for client, msg in messages:
        out += frame(msg, client)
    return out


def one(msg_type: int, body: bytes, feed: bytes = b"") -> bytes:
    return make([(0, hdr(msg_type, body))], feed)


def seeds() -> dict[str, bytes]:
    claim = struct.pack("<IHH", 100, 1, 0) + op(0, 0, 0x1E, 0x0000, 0)
    payload = bytes([0xAA, 0xBB, 0xCC, 0xDD])
    mb = struct.pack("<BBHI", 0, 0x19, len(payload), 0) + payload
    rd = op(0, 0, 0x1E, 0x0000, 0)
    rel = hdr(RELEASE, b"")
    # A fabricated mailbox response: the header word read (2nd read consumed)
    # encodes len=8, enough to walk the response-assembly + CRC path.
    mb_feed = bytes([0, 0, 0, 0,  0, 0, 8, 0]) + bytes([0xAA] * 8)
    return {
        "read":    one(READ,  rd),
        "write":   one(WRITE, op(0, 1, 0x0B, 0x0021, 0x1234)),
        "batch":   one(BATCH, op(0, 0, 0x09, 0x8002, 0)
                              + op(0, 0, 0x01, 0x0001, 0)
                              + op(1, 1, 0x0B, 0xE019, 0xFF)),
        "claim":   one(CLAIM, claim),
        "release": one(RELEASE, b""),
        "mailbox": one(MAILBOX, mb),
        "mailbox_resp": one(MAILBOX, mb, feed=mb_feed),
        "stats":   one(STATS, b""),
        "trace":   one(TRACE, b""),
        "reset":   one(RESET, struct.pack("<II", 10000, 100000)),
        # stateful sequences (exercise the queue + claim gating)
        "claim_seq":   make([(0, hdr(CLAIM, claim)), (0, hdr(READ, rd)), (0, rel)]),
        "cross_claim": make([(0, hdr(CLAIM, claim)), (1, hdr(READ, rd)), (0, rel)]),
        # a foreign op to the guarded MB region queued behind a mailbox tx: it
        # is dispatched during the mailbox poll and hits the in-flight guard.
        "mb_guard":    make([(0, hdr(MAILBOX, mb)),
                             (0, hdr(READ, op(0, 0, 0x1E, 0xD800, 0)))],
                            feed=mb_feed),
    }


def frame_seeds() -> dict[str, bytes]:
    """Seeds for fuzz_frame: valid datagrams (hdr() already sets ver/len), which
    the fuzzer mutates into the ver / length-mismatch / oversize reject paths."""
    rd = op(0, 0, 0x1E, 0x0000, 0)
    claim = struct.pack("<IHH", 100, 1, 0) + op(0, 0, 0x1E, 0x0000, 0)
    return {
        "read":  dgram(hdr(READ, rd)),
        "write": dgram(hdr(WRITE, op(0, 1, 0x0B, 0x0021, 0x1234))),
        "stats": dgram(hdr(STATS, b"")),
        "claim": dgram(hdr(CLAIM, claim)),
        # two datagrams back-to-back (the harness alternates clients)
        "seq":   dgram(hdr(CLAIM, claim)) + dgram(hdr(READ, rd)),
    }


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write(__doc__ or "")
        return 2
    # fuzz_msg corpus in <dir>, fuzz_frame corpus in the sibling corpus_frame/.
    d = pathlib.Path(argv[1])
    for path, table in ((d, seeds()), (d.parent / "corpus_frame", frame_seeds())):
        path.mkdir(parents=True, exist_ok=True)
        for name, data in table.items():
            (path / f"{name}.bin").write_bytes(data)
        sys.stderr.write(f"wrote {len(table)} seeds to {path}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
