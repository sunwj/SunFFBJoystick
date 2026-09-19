import unittest
from sunffb_gui.device import DeviceController


class DeviceTests(unittest.TestCase):
    def test_controller_without_device(self):
        c = DeviceController()
        self.assertFalse(c.is_connected)
        c.disconnect()  # no-op safe
