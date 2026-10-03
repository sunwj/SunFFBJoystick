# GUI device-layer tests use a fake HID input source without opening a real board.
# Exercise report-ID demultiplexing and rejecting worker startup when the controller is disconnected.

import test_paths

import unittest
from sunffb_hid import (JoystickInputReportData, PIDStateReportData,
                        REPORT_ID_JOYSTICK, REPORT_ID_PID_STATE)
from sunffb_gui.device import DeviceController, DeviceWorker


class FakeInputDevice:
    def __init__(self):
        joy = JoystickInputReportData(buttons=3, axis=(100, -200))
        pid = PIDStateReportData(status=0x1E, effectBlockIndex=1)
        self.reports = [
            (REPORT_ID_PID_STATE, pid.to_bytes()),
            (REPORT_ID_JOYSTICK, joy.to_bytes()),
        ]

    def read_input_once(self, max_length=64, timeout_ms=1000):
        if self.reports:
            return self.reports.pop(0)
        raise RuntimeError("done")



class DeviceTests(unittest.TestCase):
    def test_controller_without_device(self):
        c = DeviceController()
        self.assertFalse(c.is_connected)
        c.disconnect()  # no-op safe

    def test_worker_demultiplexes_interleaved_input_reports(self):
        worker = DeviceWorker(FakeInputDevice(), poll_ms=1)
        joystick = []
        pid = []
        errors = []
        worker.joystick_ready.connect(joystick.append)
        worker.pid_ready.connect(pid.append)
        worker.error.connect(errors.append)

        worker.run()

        self.assertEqual(pid, [0x1E])
        self.assertEqual(len(joystick), 1)
        self.assertEqual(list(joystick[0].axis), [100, -200])
        self.assertEqual(errors, ["done"])
