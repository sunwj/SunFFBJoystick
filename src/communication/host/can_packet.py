"""SunFFB classic CAN protocol. No python-can dependency for encoding/testing."""
import struct
FORCE_ID = 0x201
POSITION_ID = 0x181
HEARTBEAT_ID = 0x701
MAX_FORCE = 10000


def _header(axes, sequence):
    if axes not in (1, 2, 3) or not 0 <= sequence <= 255:
        raise ValueError('Invalid axis count or sequence')
    return bytes([0x10 | axes, sequence])


def _id(identifier):
    if not 0 <= identifier <= 0x7FF:
        raise ValueError('Protocol requires an 11-bit standard CAN ID')
    return identifier


def encode_force(forces, sequence=0, identifier=FORCE_ID):
    if any(not -MAX_FORCE <= value <= MAX_FORCE for value in forces):
        raise ValueError('Force must be in -10000..10000')
    payload = _header(len(forces), sequence) + struct.pack(f'<{len(forces)}h', *forces)
    return _id(identifier), payload


def encode_position(positions, sequence=0, identifier=POSITION_ID):
    if any(not 0 <= value <= 65535 for value in positions):
        raise ValueError('Position must be in 0..65535')
    payload = _header(len(positions), sequence) + struct.pack(f'<{len(positions)}H', *positions)
    return _id(identifier), payload


def encode_heartbeat(axes=2, sequence=0, status=0, identifier=HEARTBEAT_ID):
    if not 0 <= status <= 255:
        raise ValueError('Status must be in 0..255')
    return _id(identifier), _header(axes, sequence) + bytes([status])


def decode(identifier, payload, *, axes=2, force_id=FORCE_ID,
           position_id=POSITION_ID, heartbeat_id=HEARTBEAT_ID,
           extended=False, remote=False):
    """Return (kind, sequence, values/status), or None for invalid/unrelated frames."""
    if axes not in (1, 2, 3) or extended or remote or not 0 <= identifier <= 0x7FF:
        return None
    ids = (force_id, position_id, heartbeat_id)
    if len(set(ids)) != 3 or any(not 0 <= value <= 0x7FF for value in ids):
        return None
    if len(payload) < 2 or payload[0] != 0x10 | axes:
        return None
    sequence = payload[1]
    if identifier == heartbeat_id:
        return ('heartbeat', sequence, payload[2]) if len(payload) == 3 else None
    if len(payload) != 2 + 2 * axes:
        return None
    if identifier == position_id:
        return 'position', sequence, list(struct.unpack(f'<{axes}H', payload[2:]))
    if identifier == force_id:
        values = list(struct.unpack(f'<{axes}h', payload[2:]))
        if any(not -MAX_FORCE <= value <= MAX_FORCE for value in values):
            return None
        return 'force', sequence, values
    return None
