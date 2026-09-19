# SPDX-License-Identifier: BSD-3-Clause
"""Offline NS51 wire codec. No device discovery, transport or hardware imports."""

from dataclasses import dataclass
import hashlib
import struct
from typing import Optional

BODY = bytes.fromhex("618851feca34127856") + b"NS51-PUBLIC"
DESCRIPTOR_BYTES = b"NS51\x01\x1a\xec" + BODY + struct.pack("<IIIII", 5000, 100, 20, 20, 4)
DESCRIPTOR = hashlib.sha256(DESCRIPTOR_BYTES).digest()
MAX_PAYLOAD = 145
MAX_LINE = 301
KINDS = {
    0x80: "HELLO", 0x81: "ARMED", 0x82: "TX_REQUESTED", 0x83: "SCHEDULED",
    0x84: "PHY_TX_DONE", 0x85: "RX_FRAME", 0x86: "RX_ERROR",
    0x87: "STOP_REQUESTED", 0x88: "STOP_ACCEPTED", 0x89: "TERMINAL",
    0x8A: "FAULT", 0x8B: "TX_ERROR",
}


class ProtocolError(ValueError):
    pass


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def encode(kind: int, payload: bytes = b"") -> bytes:
    if type(kind) is not int or not 0 <= kind <= 255:
        raise ProtocolError("type must be an unsigned byte")
    if not isinstance(payload, bytes) or len(payload) > MAX_PAYLOAD:
        raise ProtocolError("invalid payload")
    raw = bytes((1, kind, len(payload))) + payload
    raw += struct.pack("<H", crc16(raw))
    return raw.hex().upper().encode("ascii") + b"\n"


def decode(line: bytes) -> tuple[int, bytes]:
    if not isinstance(line, bytes) or not 11 <= len(line) <= MAX_LINE:
        raise ProtocolError("invalid line length")
    if not line.endswith(b"\n") or any(c not in b"0123456789ABCDEF" for c in line[:-1]):
        raise ProtocolError("expected uppercase hex and one LF")
    if (len(line) - 1) % 2:
        raise ProtocolError("odd hex length")
    raw = bytes.fromhex(line[:-1].decode("ascii"))
    if raw[0] != 1 or raw[2] != len(raw) - 5:
        raise ProtocolError("version or length mismatch")
    if crc16(raw[:-2]) != int.from_bytes(raw[-2:], "little"):
        raise ProtocolError("serial CRC mismatch (not an RF FCS)")
    return raw[1], raw[3:-2]


def command(kind: int, nonce: Optional[bytes] = None, descriptor: bytes = DESCRIPTOR) -> bytes:
    if type(kind) is not int:
        raise ProtocolError("command must be an integer")
    if kind == 0 and nonce is None:
        return encode(0)
    if kind not in (1, 2) or not isinstance(nonce, bytes) or len(nonce) != 8 or not any(nonce):
        raise ProtocolError("ARM/RUN require a nonzero eight-byte host nonce")
    if not isinstance(descriptor, bytes) or len(descriptor) != 32:
        raise ProtocolError("expected the exact 32-byte descriptor digest")
    return encode(kind, descriptor + nonce)


@dataclass(frozen=True)
class Record:
    kind: str
    sequence: int
    state: int
    fault: int
    service_ms: int
    nonce: bytes
    data: bytes


def record(line: bytes) -> Record:
    kind, payload = decode(line)
    if kind not in KINDS or len(payload) < 16:
        raise ProtocolError("not an NS51 record")
    sequence, state, fault, service_ms, nonce = struct.unpack("<HBBI8s", payload[:16])
    data = payload[16:]
    lengths = {0x80: 32, 0x81: 0, 0x82: 0, 0x83: 1, 0x84: 0,
               0x86: 1, 0x87: 0, 0x88: 1, 0x89: 6, 0x8A: 0, 0x8B: 1}
    if not 1 <= sequence <= 34 or state > 6 or fault > 18:
        raise ProtocolError("record header outside bounded ABI")
    if (state == 6) != (fault != 0):
        raise ProtocolError("fault/state mismatch")
    if kind == 0x85:
        if len(data) < 7 or data[2] != 1 or not 3 <= data[3] <= 125 or len(data) != data[3] + 4:
            raise ProtocolError("RX must contain a CRC-validated body without FCS")
    elif len(data) != lengths[kind]:
        raise ProtocolError("record-specific length mismatch")
    if kind in (0x83, 0x88) and data[0] > 1:
        raise ProtocolError("invalid scheduling boolean")
    if kind == 0x80 and data != DESCRIPTOR:
        raise ProtocolError("different fixture descriptor")
    if kind == 0x89 and (any(data[i] > 1 for i in (0, 1, 3, 4, 5)) or data[2] > 4):
        raise ProtocolError("invalid terminal summary")
    if kind == 0x8A and fault == 0:
        raise ProtocolError("fault record without fault")
    return Record(KINDS[kind], sequence, state, fault, service_ms, nonce, data)
