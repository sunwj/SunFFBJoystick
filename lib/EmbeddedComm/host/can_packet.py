# Python motor CAN codecs matching firmware IDs, version/axis header, sequence and little-endian fields.
# Byte conversion only: no bus access, retransmission or duplicate-rejection policy.
# Forces use int16 and positions uint16; callers must select the peer's axis count.

"""SunFFB classic CAN protocol. No python-can dependency for encoding/testing."""
import struct
FORCE_ID = 0x201
POSITION_ID = 0x181
HEARTBEAT_ID = 0x701
MAX_FORCE = 10000


# Validate 1..3 axes and byte-sized sequence before packing the version/axis prefix.
def _header(axes, sequence):
    if axes not in (1, 2, 3) or not 0 <= sequence <= 255:
        raise ValueError('Invalid axis count or sequence')
    return bytes([0x10 | axes, sequence])


# Application motor messages use standard 11-bit CAN identifiers only.
def _id(identifier):
    if not 0 <= identifier <= 0x7FF:
        raise ValueError('Protocol requires an 11-bit standard CAN ID')
    return identifier


# Encode nominal bounded forces as signed little-endian int16 values after the two-byte prefix.
def encode_force(forces, sequence=0, identifier=FORCE_ID):
    if any(not -MAX_FORCE <= value <= MAX_FORCE for value in forces):
        raise ValueError('Force must be in -10000..10000')
    payload = _header(len(forces), sequence) + struct.pack(f'<{len(forces)}h', *forces)
    return _id(identifier), payload


# Encode raw uint16 positions; do not apply the firmware's signed-center mapping here.
def encode_position(positions, sequence=0, identifier=POSITION_ID):
    if any(not 0 <= value <= 65535 for value in positions):
        raise ValueError('Position must be in 0..65535')
    payload = _header(len(positions), sequence) + struct.pack(f'<{len(positions)}H', *positions)
    return _id(identifier), payload


# Heartbeat carries version/axes, sequence and a one-byte application status.
def encode_heartbeat(axes=2, sequence=0, status=0, identifier=HEARTBEAT_ID):
    if not 0 <= status <= 255:
        raise ValueError('Status must be in 0..255')
    return _id(identifier), _header(axes, sequence) + bytes([status])


# Reject remote/extended frames and mismatched layouts; decode recognized application IDs only.
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
