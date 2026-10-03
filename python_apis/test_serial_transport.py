import importlib.util
from pathlib import Path
import unittest
spec = importlib.util.spec_from_file_location('serial_transport_packet', Path(__file__).resolve().parents[1] / 'src/communication/host/packet.py')
packet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packet)

class Serial:
    def __init__(self):
        self.data = bytearray()
        self.flushes = 0
        self.full = False
    @property
    def in_waiting(self): return len(self.data)
    def read(self, size):
        out = bytes(self.data[:size]); del self.data[:size]; return out
    def write(self, data):
        if self.full: return 0
        self.data.extend(data); return len(data)
    def flush(self): self.flushes += 1

class TransportTests(unittest.TestCase):
    def test_fixed_and_variable_roundtrip_all_axes(self):
        for axes in (1, 2, 3):
            layout = packet.fixed_layout(axes)
            for fixed in (False, True):
                for msg, payload in ((packet.MSG_POSITION, packet.pack_position([0xABAA]*axes)),
                                     (packet.MSG_FORCE, packet.pack_force([-10000]*axes)),
                                     (packet.MSG_HEARTBEAT, b'')):
                    frame = packet.build_frame(msg, payload, fixed=fixed, fixed_lengths=layout)
                    self.assertEqual(packet.decode_frame(frame, fixed_lengths=layout), (msg,payload))
                    self.assertEqual(len(frame),len(payload)+(3 if fixed else 4))

    def test_mixed_batch_preserves_every_frame(self):
        port = Serial();link = packet.SerialLink(port,framing='mixed')
        expected=[]
        for n in range(100):
            payload=packet.pack_position([n,65535-n]); expected.append((2,payload))
            link.send(2,payload,fixed=bool(n%2))
        self.assertEqual([link.receive() for _ in expected],expected)
        self.assertIsNone(link.receive());self.assertEqual(port.flushes,0)

    def test_fixed_byte_splits_and_crc_recovery(self):
        port=Serial();link=packet.SerialLink(port,framing='fixed',num_axes=1)
        frame=packet.build_frame(2,b'\xaa\xab',fixed=True,fixed_lengths=packet.fixed_layout(1))
        for byte in frame[:-1]:
            port.data.append(byte);self.assertIsNone(link.receive())
        port.data.append(frame[-1]);self.assertEqual(link.receive(),(2,b'\xaa\xab'))
        port.data.extend(frame[:-1]+bytes([frame[-1]^1])+frame)
        self.assertEqual(link.receive(),(2,b'\xaa\xab'));self.assertEqual(link.stats['crc_errors'],1)

    def test_invalid_sync_length_and_fixed_id(self):
        frame=packet.build_frame(3,b'')
        self.assertIsNone(packet.decode_frame(b'\x00'+frame[1:]))
        self.assertIsNone(packet.decode_frame(frame+b'\x00'))
        with self.assertRaises(ValueError): packet.build_frame(2,b'\x00',fixed=True)
        port=Serial();link=packet.SerialLink(port,framing='mixed')
        port.data.extend(b'\xab\x70\xaa\x71\xff'+frame)
        self.assertEqual(link.receive(),(3,b''));self.assertEqual(link.stats['len_errors'],2)

    def test_custom_fixed_layout_and_max_variable_payload(self):
        payload=bytes(range(64));layout={0x42:64}
        port=Serial();link=packet.SerialLink(port,framing='mixed',fixed_lengths=layout)
        self.assertEqual(link.send(0x42,payload,fixed=True),67)
        self.assertEqual(link.receive(),(0x42,payload))
        link.send(0x43,payload);self.assertEqual(link.receive(),(0x43,payload))

    def test_mode_guards_and_actual_write_count(self):
        port=Serial();link=packet.SerialLink(port,framing='fixed')
        with self.assertRaises(ValueError): link.send(3,b'',fixed=False)
        port.full=True;self.assertEqual(link.send_heartbeat(),0)
        self.assertEqual(port.flushes,0)

    def test_255_byte_payload_when_explicitly_configured(self):
        port=Serial();link=packet.SerialLink(port,max_payload=255)
        payload=bytes(range(255));self.assertEqual(link.send(0x40,payload),259)
        frame=bytes(port.data)
        self.assertEqual(packet.decode_frame(frame,max_payload=255),(0x40,payload))
        self.assertIsNone(packet.decode_frame(frame))
        self.assertEqual(link.receive(),(0x40,payload))
        with self.assertRaises(ValueError): packet.SerialLink(port,max_payload=256)

    def test_receive_read_budget(self):
        port=Serial();link=packet.SerialLink(port)
        port.data.extend(b'\0'*10000)
        self.assertIsNone(link.receive());self.assertEqual(port.in_waiting,10000-4096)

if __name__ == '__main__': unittest.main()
