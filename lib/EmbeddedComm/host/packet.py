# Python UART codecs and streaming parser shared by serial tools and tests.
# Variable/fixed CRC coverage matches C++ SerialLink; mixed framing selects format by sync byte.
# send returns the serial write result, not peer acknowledgment; receive retains partial-frame state.

from __future__ import annotations

import struct
import threading
from collections import deque
from typing import Optional, Tuple

# CRC-8/MAXIM-DOW (poly 0x31, reflected poly 0x8C, init/xorout 0x00).
_CRC8_TABLE = bytes([
    0x00,0x5E,0xBC,0xE2,0x61,0x3F,0xDD,0x83,0xC2,0x9C,0x7E,0x20,0xA3,0xFD,0x1F,0x41,
    0x9D,0xC3,0x21,0x7F,0xFC,0xA2,0x40,0x1E,0x5F,0x01,0xE3,0xBD,0x3E,0x60,0x82,0xDC,
    0x23,0x7D,0x9F,0xC1,0x42,0x1C,0xFE,0xA0,0xE1,0xBF,0x5D,0x03,0x80,0xDE,0x3C,0x62,
    0xBE,0xE0,0x02,0x5C,0xDF,0x81,0x63,0x3D,0x7C,0x22,0xC0,0x9E,0x1D,0x43,0xA1,0xFF,
    0x46,0x18,0xFA,0xA4,0x27,0x79,0x9B,0xC5,0x84,0xDA,0x38,0x66,0xE5,0xBB,0x59,0x07,
    0xDB,0x85,0x67,0x39,0xBA,0xE4,0x06,0x58,0x19,0x47,0xA5,0xFB,0x78,0x26,0xC4,0x9A,
    0x65,0x3B,0xD9,0x87,0x04,0x5A,0xB8,0xE6,0xA7,0xF9,0x1B,0x45,0xC6,0x98,0x7A,0x24,
    0xF8,0xA6,0x44,0x1A,0x99,0xC7,0x25,0x7B,0x3A,0x64,0x86,0xD8,0x5B,0x05,0xE7,0xB9,
    0x8C,0xD2,0x30,0x6E,0xED,0xB3,0x51,0x0F,0x4E,0x10,0xF2,0xAC,0x2F,0x71,0x93,0xCD,
    0x11,0x4F,0xAD,0xF3,0x70,0x2E,0xCC,0x92,0xD3,0x8D,0x6F,0x31,0xB2,0xEC,0x0E,0x50,
    0xAF,0xF1,0x13,0x4D,0xCE,0x90,0x72,0x2C,0x6D,0x33,0xD1,0x8F,0x0C,0x52,0xB0,0xEE,
    0x32,0x6C,0x8E,0xD0,0x53,0x0D,0xEF,0xB1,0xF0,0xAE,0x4C,0x12,0x91,0xCF,0x2D,0x73,
    0xCA,0x94,0x76,0x28,0xAB,0xF5,0x17,0x49,0x08,0x56,0xB4,0xEA,0x69,0x37,0xD5,0x8B,
    0x57,0x09,0xEB,0xB5,0x36,0x68,0x8A,0xD4,0x95,0xCB,0x29,0x77,0xF4,0xAA,0x48,0x16,
    0xE9,0xB7,0x55,0x0B,0x88,0xD6,0x34,0x6A,0x2B,0x75,0x97,0xC9,0x4A,0x14,0xF6,0xA8,
    0x74,0x2A,0xC8,0x96,0x15,0x4B,0xA9,0xF7,0xB6,0xE8,0x0A,0x54,0xD7,0x89,0x6B,0x35,
])

SYNC = 0xAA
MAX_PAYLOAD = 64
FIXED_SYNC = 0xAB

# Message IDs (match firmware)
MSG_FORCE = 0x01
MSG_POSITION = 0x02
MSG_HEARTBEAT = 0x03


# CRC-8/MAXIM-DOW over the supplied bytes; sync-byte exclusion is handled by the frame builder.
def calc_crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = _CRC8_TABLE[crc ^ b]
    return crc


# Motor fixed lengths use int32 forces and uint16 positions; heartbeat has no payload.
def fixed_layout(num_axes: int = 2) -> dict[int, int]:
    if num_axes not in (1, 2, 3):
        raise ValueError("num_axes must be 1, 2 or 3")
    return {MSG_FORCE: 4 * num_axes, MSG_POSITION: 2 * num_axes, MSG_HEARTBEAT: 0}


# Validate capacity/ID/fixed layout before adding sync and CRC; the sync byte is not checksummed.
def build_frame(msg_id: int, payload: bytes, *, fixed: bool = False,
                fixed_lengths=None, max_payload=MAX_PAYLOAD) -> bytes:
    """Variable: AA ID LEN PAYLOAD CRC; fixed: AB ID PAYLOAD CRC."""
    if not 1 <= max_payload <= 255 or not 0 <= msg_id <= 255 or len(payload) > max_payload:
        raise ValueError("Invalid message ID or payload length")
    if fixed and (fixed_lengths if fixed_lengths is not None else fixed_layout()).get(msg_id) != len(payload):
        raise ValueError("Fixed payload length does not match message ID")
    header = bytes([msg_id]) if fixed else bytes([msg_id, len(payload)])
    body = header + payload
    return bytes([FIXED_SYNC if fixed else SYNC]) + body + bytes([calc_crc8(body)])


# Accept exactly one frame, not a stream; invalid sync, size, policy or checksum yields None.
def decode_frame(raw: bytes, *, fixed_lengths=None, max_payload=MAX_PAYLOAD) -> Optional[Tuple[int, bytes]]:
    """Decode exactly one complete frame of either format, validating sync and size."""
    if len(raw) < 3 or raw[0] not in (SYNC, FIXED_SYNC):
        return None
    fixed = raw[0] == FIXED_SYNC
    offset = 2 if fixed else 3
    if not fixed and len(raw) < 4:
        return None
    length = (fixed_lengths if fixed_lengths is not None else fixed_layout()).get(raw[1]) if fixed else raw[2]
    if not 1 <= max_payload <= 255 or length is None or not 0 <= length <= max_payload or len(raw) != offset + length + 1:
        return None
    if calc_crc8(raw[1:-1]) != raw[-1]:
        return None
    return raw[1], raw[offset:-1]


# UART force fields are signed 32-bit little-endian, unlike CAN's compact signed 16-bit fields.
def pack_force(forces: list) -> bytes:
    """Pack int32 force values."""
    return struct.pack(f'<{len(forces)}i', *forces)


def unpack_force(data: bytes) -> list:
    """Unpack int32 force values."""
    n = len(data) // 4
    return list(struct.unpack(f'<{n}i', data[:n * 4]))


# Encoder fields are unsigned 16-bit little-endian; center conversion belongs to the input task.
def pack_position(positions: list) -> bytes:
    """Pack uint16 position values."""
    return struct.pack(f'<{len(positions)}H', *positions)


def unpack_position(data: bytes) -> list:
    """Unpack uint16 position values."""
    n = len(data) // 2
    return list(struct.unpack(f'<{n}H', data[:n * 2]))


class SerialLink:
    """Thread-safe serial link with state machine receiver."""

    def __init__(self, serial_obj, *, framing="variable", num_axes=2, fixed_lengths=None, max_payload=MAX_PAYLOAD):
        if framing not in ("variable", "fixed", "mixed"):
            raise ValueError("Unknown framing mode")
        if not 1 <= max_payload <= 255:
            raise ValueError("max_payload must be 1..255")
        self._max_payload = max_payload
        self._framing = framing
        self._fixed_lengths = fixed_layout(num_axes) if fixed_lengths is None else fixed_lengths
        self._fixed = False
        self._crc = 0
        self._serial = serial_obj
        self._lock = threading.Lock()
        self._state = "IDLE"
        self._buf = bytearray()
        self._msg_id = 0
        self._payload_len = 0
        self._idx = 0
        self._crc_errors = 0
        self._len_errors = 0
        self._frames_rx = 0
        self._pending = deque()

    # Serialize the enabled format and lock a whole write so concurrent GUI commands cannot interleave bytes.
    def send(self, msg_id: int, payload: bytes, *, fixed=None) -> int:
        fixed = self._framing == "fixed" if fixed is None else fixed
        if (fixed and self._framing == "variable") or (not fixed and self._framing == "fixed"):
            raise ValueError("Frame format is not enabled")
        frame = build_frame(msg_id, payload, fixed=fixed, fixed_lengths=self._fixed_lengths, max_payload=self._max_payload)
        with self._lock:
            # No per-frame flush: enqueue directly and report the actual accepted byte count.
            written = self._serial.write(frame)
        return written

    def send_force(self, forces: list) -> int:
        return self.send(MSG_FORCE, pack_force(forces))

    def send_position(self, positions: list) -> int:
        return self.send(MSG_POSITION, pack_position(positions))

    def send_heartbeat(self) -> int:
        return self.send(MSG_HEARTBEAT, b'')

    # Consume queued decoded frames before another serial read; parsing can produce several frames per batch.
    def receive(self) -> Optional[Tuple[int, bytes]]:
        """Try to receive one frame. Returns (msg_id, payload) or None."""
        with self._lock:
            if self._pending:
                return self._pending.popleft()
            available = self._serial.in_waiting
            if available == 0:
                return None
            raw = self._serial.read(min(available, 4096))
            return self._process(raw)

    # Only accept sync markers belonging to the configured framing mode.
    def _seek_sync(self, byte):
        if byte == SYNC and self._framing != "fixed":
            self._fixed = False
            self._state = "HAVE_SYNC"
        elif byte == FIXED_SYNC and self._framing != "variable":
            self._fixed = True
            self._state = "HAVE_SYNC"

    # Reject unknown fixed IDs or oversized lengths, then reuse a possible sync byte for recovery.
    def _accept_length(self, length, byte):
        if length is None or not 0 <= length <= self._max_payload:
            self._len_errors += 1
            self._state = "IDLE"
            self._seek_sync(byte)
        else:
            self._payload_len = length
            self._buf.clear()
            self._state = "CHECK_CRC" if length == 0 else "RECEIVING"

    # Keep parser state across calls and queue every valid frame in a coalesced read.
    # CRC failure rejects one frame and attempts resynchronization without clearing later bytes.
    def _process(self, data: bytes) -> Optional[Tuple[int, bytes]]:
        for byte in data:
            if self._state == "IDLE":
                self._seek_sync(byte)
            elif self._state == "HAVE_SYNC":
                self._msg_id = byte
                self._crc = _CRC8_TABLE[byte]
                if self._fixed:
                    self._accept_length(self._fixed_lengths.get(byte), byte)
                else:
                    self._state = "HAVE_ID"
            elif self._state == "HAVE_ID":
                self._crc = _CRC8_TABLE[self._crc ^ byte]
                self._accept_length(byte, byte)
            elif self._state == "RECEIVING":
                self._buf.append(byte)
                self._crc = _CRC8_TABLE[self._crc ^ byte]
                if len(self._buf) == self._payload_len:
                    self._state = "CHECK_CRC"
            elif self._state == "CHECK_CRC":
                self._state = "IDLE"
                if byte == self._crc:
                    self._pending.append((self._msg_id, bytes(self._buf)))
                    self._frames_rx += 1
                else:
                    self._crc_errors += 1
                    self._seek_sync(byte)
        return self._pending.popleft() if self._pending else None

    @property
    # Expose parser error/frame counters; they are not acknowledgments of transmitted data.
    def stats(self):
        return {
            "crc_errors": self._crc_errors,
            "len_errors": self._len_errors,
            "frames_rx": self._frames_rx,
        }
