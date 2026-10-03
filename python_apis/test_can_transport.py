import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('sunffb_can_packet',Path(__file__).resolve().parents[1]/'src/communication/host/can_packet.py')
packet=importlib.util.module_from_spec(spec);spec.loader.exec_module(packet)

class CANTests(unittest.TestCase):
    def test_all_axes_force_and_position_roundtrip(self):
        for axes in (1,2,3):
            forces=[-10000,10000,0][:axes]
            identifier,payload=packet.encode_force(forces,255)
            self.assertLessEqual(len(payload),8)
            self.assertEqual(packet.decode(identifier,payload,axes=axes),('force',255,forces))
            positions=[0,65535,32768][:axes]
            identifier,payload=packet.encode_position(positions,0)
            self.assertEqual(packet.decode(identifier,payload,axes=axes),('position',0,positions))

    def test_golden_little_endian_wire(self):
        identifier,payload=packet.encode_force([-10000,10000],7)
        self.assertEqual(identifier,0x201);self.assertEqual(payload,b'\x12\x07\xf0\xd8\x10\x27')
        identifier,payload=packet.encode_position([32768,65535],8)
        self.assertEqual(identifier,0x181);self.assertEqual(payload,b'\x12\x08\x00\x80\xff\xff')

    def test_format_version_axes_and_dlc_rejection(self):
        identifier,payload=packet.encode_position([1,2])
        self.assertIsNone(packet.decode(identifier,payload,extended=True))
        self.assertIsNone(packet.decode(identifier,payload,remote=True))
        self.assertIsNone(packet.decode(identifier,payload,axes=1))
        self.assertIsNone(packet.decode(identifier,payload+b'\0'))
        self.assertIsNone(packet.decode(identifier,b'\x22'+payload[1:]))
        self.assertIsNone(packet.decode(0x182,payload))

    def test_force_range_sequence_and_id_validation(self):
        for forces in ([10001],[-10001],[],[0]*4):
            with self.assertRaises(ValueError):packet.encode_force(forces)
        for values in ([-1],[65536]):
            with self.assertRaises(ValueError):packet.encode_position(values)
        with self.assertRaises(ValueError):packet.encode_force([0],256)
        with self.assertRaises(ValueError):packet.encode_force([0],identifier=0x800)
        self.assertIsNone(packet.decode(0x201,b'\x11\x00\xff\x7f',axes=1))

    def test_custom_ids_and_heartbeat(self):
        identifier,payload=packet.encode_heartbeat(3,9,5,0x300)
        self.assertEqual(packet.decode(identifier,payload,axes=3,heartbeat_id=0x300),('heartbeat',9,5))
        self.assertIsNone(packet.decode(identifier,payload,axes=3,force_id=0x300,heartbeat_id=0x300))
        with self.assertRaises(ValueError):packet.encode_heartbeat(status=256)

if __name__=='__main__':unittest.main()
