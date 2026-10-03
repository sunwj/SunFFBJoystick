import importlib.util
import os
from pathlib import Path
import time
import unittest
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
from PyQt6.QtWidgets import QApplication
from sunffb_gui.force_model import EffectParams, ForceModel, Kinematics, CONSTANT, SPRING
from sunffb_gui.main import MainWindow
from sunffb_hid import DIRECTION_ENABLE, NUM_AXIS
from test_main import FakeController
import example_cli

packet_path = Path(__file__).resolve().parents[1] / 'lib/EmbeddedComm/host/packet.py'
spec = importlib.util.spec_from_file_location('sunffb_packet_tests', packet_path)
packet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packet)
_APP = QApplication.instance() or QApplication([])

class FakeSerial:
    def __init__(self, data=b''): self.data = data
    @property
    def in_waiting(self): return len(self.data)
    def read(self, n):
        data, self.data = self.data[:n], self.data[n:]
        return data

class RegressionTests(unittest.TestCase):
    def test_serial_preserves_coalesced_frames(self):
        first = packet.pack_position([123, 456])
        last = packet.pack_position([789, 100])
        port = FakeSerial(packet.build_frame(packet.MSG_POSITION, first)
                          + packet.build_frame(packet.MSG_HEARTBEAT, b'')
                          + packet.build_frame(packet.MSG_POSITION, last))
        link = packet.SerialLink(port)
        self.assertEqual(link.receive(), (packet.MSG_POSITION, first))
        self.assertEqual(link.receive(), (packet.MSG_HEARTBEAT, b''))
        self.assertEqual(link.receive(), (packet.MSG_POSITION, last))
        self.assertIsNone(link.receive())
        self.assertEqual(link.stats['frames_rx'], 3)

    def test_serial_handles_partial_and_corrupt_frames(self):
        frame = packet.build_frame(packet.MSG_POSITION, packet.pack_position([100, 200]))
        port = FakeSerial(frame[:3]); link = packet.SerialLink(port)
        self.assertIsNone(link.receive())
        port.data = frame[3:]
        self.assertEqual(link.receive()[0], packet.MSG_POSITION)
        port.data = frame[:-1] + bytes([frame[-1] ^ 1]) + frame
        self.assertEqual(link.receive()[0], packet.MSG_POSITION)
        self.assertEqual(link.stats['crc_errors'], 1)

    def test_serial_wire_encoding_is_little_endian(self):
        self.assertEqual(packet.pack_position([0x1234]), b'\x34\x12')
        self.assertEqual(packet.pack_force([-1]), b'\xff' * 4)

    def test_negative_coefficients_are_saturated(self):
        p = EffectParams(effect_type=SPRING, pos_coeff_x=-10000,
                         neg_coeff_x=-10000, pos_sat_x=2000, neg_sat_x=3000)
        m = ForceModel(p)
        self.assertEqual(m.evaluate_effect(p, Kinematics(roll=1))[0], 2000)
        self.assertEqual(m.evaluate_effect(p, Kinematics(roll=-1))[0], -3000)

    def test_long_fade_and_infinite_duration(self):
        p = EffectParams(duration_ms=100, fade_time_ms=200, fade_level=0)
        self.assertEqual(ForceModel.envelope_factor(p, 50, 6000), .75)
        p.duration_ms = 0xFFFF
        self.assertEqual(ForceModel.envelope_factor(p, 100000, 6000), 1)

    def window(self):
        w = MainWindow(show_hid=False)
        w.controller = FakeController()
        self.addCleanup(w.close)
        return w

    def test_infinite_loop_has_no_gui_expiry(self):
        w = self.window(); w.spn_loop.setValue(255); w.start_effect()
        self.assertEqual(w._run_total_ms, 0)
        w._run_started_at = time.monotonic() - 100000
        w._on_tick()
        self.assertNotEqual(w._effect_idx, 0)

    def test_ramp_preview_restarts_at_loop_boundary(self):
        w = self.window(); w.cmb_type.setCurrentIndex(w.cmb_type.findData('ramp'))
        w.spn_dir.setValue(0); w.spn_loop.setValue(3); w.start_effect()
        duration = w._main_params.duration_ms
        with patch.object(w, '_elapsed_ms', return_value=duration):
            w._update_canvas()
        self.assertEqual(w.canvas._fy, w._main_params.ramp_start)

    def test_cli_sine_sends_a_nonzero_direction(self):
        class Device:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def create_new_effect(self, _):
                from types import SimpleNamespace
                return SimpleNamespace(blockLoadStatus=1, effectBlockIndex=1)
            def set_effect(self, report): self.report = report
            def set_periodic(self, _): pass
            def effect_operation(self, _): pass
        from types import SimpleNamespace
        device = Device()
        args = SimpleNamespace(vid=0xFFFF,pid=0x2010,duration=1000,gain=255,
                               magnitude=4000,offset=0,phase=0,period=100,
                               loop_count=1,wait=0)
        with patch.object(example_cli, 'auto_select_device'), patch.object(example_cli, 'SunFFBDevice', return_value=device), patch('builtins.print'):
            example_cli.cmd_sine(args)
        self.assertEqual(device.report.axisEnable, DIRECTION_ENABLE)
        self.assertEqual(device.report.directions[0], 9000)
        self.assertEqual(len(device.report.directions), NUM_AXIS)
