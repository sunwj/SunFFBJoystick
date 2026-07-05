from __future__ import annotations

import struct
import threading
from typing import Optional, Tuple

# CRC-8/MAXIM (same table as firmware serial_comm.h)
_CRC8_TABLE = bytes([
    0x00,0x31,0x62,0x53,0xC4,0xF5,0xA6,0x97,0xB8,0x89,0xDA,0xEB,0x7C,0x4D,0x1E,0x2F,
    0x64,0x55,0x06,0x37,0xA0,0x91,0xC2,0xF3,0xDC,0xED,0xBE,0x8F,0x18,0x29,0x7A,0x4B,
    0xC8,0xF9,0xAA,0x9B,0x0C,0x3D,0x6E,0x5F,0x70,0x41,0x12,0x23,0xB4,0x85,0xD6,0xE7,
    0xAC,0x9D,0xCE,0xFF,0x68,0x59,0x0A,0x3B,0x14,0x25,0x76,0x47,0xD0,0xE1,0xB2,0x83,
    0x80,0xB1,0xE2,0xD3,0x44,0x75,0x26,0x17,0x38,0x09,0x5A,0x6B,0xFC,0xCD,0x9E,0xAF,
    0xE4,0xD5,0x86,0xB7,0x20,0x11,0x42,0x73,0x5C,0x6D,0x3E,0x0F,0x98,0xA9,0xFA,0xCB,
    0x48,0x79,0x2A,0x1B,0x8C,0xBD,0xEE,0xDF,0xF0,0xC1,0x92,0xA3,0x34,0x05,0x56,0x67,
    0x2C,0x1D,0x4E,0x7F,0xE8,0xD9,0x8A,0xBB,0x94,0xA5,0xF6,0xC7,0x50,0x61,0x32,0x03,
    0x00,0x31,0x62,0x53,0xC4,0xF5,0xA6,0x97,0xB8,0x89,0xDA,0xEB,0x7C,0x4D,0x1E,0x2F,
    0x64,0x55,0x06,0x37,0xA0,0x91,0xC2,0xF3,0xDC,0xED,0xBE,0x8F,0x18,0x29,0x7A,0x4B,
    0xC8,0xF9,0xAA,0x9B,0x0C,0x3D,0x6E,0x5F,0x70,0x41,0x12,0x23,0xB4,0x85,0xD6,0xE7,
    0xAC,0x9D,0xCE,0xFF,0x68,0x59,0x0A,0x3B,0x14,0x25,0x76,0x47,0xD0,0xE1,0xB2,0x83,
    0x80,0xB1,0xE2,0xD3,0x44,0x75,0x26,0x17,0x38,0x09,0x5A,0x6B,0xFC,0xCD,0x9E,0xAF,
    0xE4,0xD5,0x86,0xB7,0x20,0x11,0x42,0x73,0x5C,0x6D,0x3E,0x0F,0x98,0xA9,0xFA,0xCB,
    0x48,0x79,0x2A,0x1B,0x8C,0xBD,0xEE,0xDF,0xF0,0xC1,0x92,0xA3,0x34,0x05,0x56,0x67,
    0x2C,0x1D,0x4E,0x7F,0xE8,0xD9,0x8A,0xBB,0x94,0xA5,0xF6,0xC7,0x50,0x61,0x32,0x03,
])

SYNC = 0xAA
MAX_PAYLOAD = 64

# Message IDs (match firmware)
MSG_FORCE = 0x01
MSG_POSITION = 0x02
MSG_HEARTBEAT = 0x03


def calc_crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = _CRC8_TABLE[crc ^ b]
    return crc


def build_frame(msg_id: int, payload: bytes) -> bytes:
    """Build: [0xAA] [MSG_ID] [LEN] [PAYLOAD] [CRC8]"""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"Payload {len(payload)} > MAX_PAYLOAD")
    header = bytes([msg_id, len(payload)])
    crc = calc_crc8(header + payload)
    return bytes([SYNC]) + header + payload + bytes([crc])


def decode_frame(raw: bytes) -> Optional[Tuple[int, bytes]]:
    """Decode a single frame. Returns (msg_id, payload) or None."""
    if len(raw) < 4:
        return None
    crc = calc_crc8(raw[1:-1])
    if crc != raw[-1]:
        return None
    msg_id = raw[1]
    payload_len = raw[2]
    if payload_len != len(raw) - 4:
        return None
    return msg_id, raw[3:3 + payload_len]


def pack_force(forces: list) -> bytes:
    """Pack int32 force values."""
    return struct.pack(f'{len(forces)}i', *forces)


def unpack_force(data: bytes) -> list:
    """Unpack int32 force values."""
    n = len(data) // 4
    return list(struct.unpack(f'{n}i', data[:n * 4]))


def pack_position(positions: list) -> bytes:
    """Pack uint16 position values."""
    return struct.pack(f'{len(positions)}H', *positions)


def unpack_position(data: bytes) -> list:
    """Unpack uint16 position values."""
    n = len(data) // 2
    return list(struct.unpack(f'{n}H', data[:n * 2]))


class SerialLink:
    """Thread-safe serial link with state machine receiver."""

    def __init__(self, serial_obj):
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

    def send(self, msg_id: int, payload: bytes) -> int:
        frame = build_frame(msg_id, payload)
        with self._lock:
            self._serial.write(frame)
            self._serial.flush()
        return len(frame)

    def send_force(self, forces: list) -> int:
        return self.send(MSG_FORCE, pack_force(forces))

    def send_position(self, positions: list) -> int:
        return self.send(MSG_POSITION, pack_position(positions))

    def send_heartbeat(self) -> int:
        return self.send(MSG_HEARTBEAT, b'')

    def receive(self) -> Optional[Tuple[int, bytes]]:
        """Try to receive one frame. Returns (msg_id, payload) or None."""
        with self._lock:
            available = self._serial.in_waiting
            if available == 0:
                return None
            raw = self._serial.read(available)
            return self._process(raw)

    def _process(self, data: bytes) -> Optional[Tuple[int, bytes]]:
        last_result = None
        for b in data:
            if self._state == "IDLE":
                if b == SYNC:
                    self._state = "HAVE_SYNC"
            elif self._state == "HAVE_SYNC":
                self._msg_id = b
                self._state = "HAVE_ID"
            elif self._state == "HAVE_ID":
                self._payload_len = b
                if self._payload_len == 0 or self._payload_len > MAX_PAYLOAD:
                    self._len_errors += 1
                    self._state = "IDLE"
                else:
                    self._buf = bytearray()
                    self._idx = 0
                    self._state = "RECEIVING"
            elif self._state == "RECEIVING":
                self._buf.append(b)
                self._idx += 1
                if self._idx == self._payload_len:
                    self._state = "CHECK_CRC"
            elif self._state == "CHECK_CRC":
                expected = calc_crc8(bytes([self._msg_id, self._payload_len]) + self._buf)
                if b == expected:
                    last_result = (self._msg_id, bytes(self._buf))
                    self._frames_rx += 1
                else:
                    self._crc_errors += 1
                self._state = "IDLE"
        return last_result

    @property
    def stats(self):
        return {
            "crc_errors": self._crc_errors,
            "len_errors": self._len_errors,
            "frames_rx": self._frames_rx,
        }
