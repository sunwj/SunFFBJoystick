"""Decode independent UART diagnostics without opening USB or serial devices."""

import test_paths

import struct
import unittest

from validate_hardware import Rig


class HardwareDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.rig = Rig.__new__(Rig)
        self.rig.health = []
        self.rig.usb_progress = []
        self.rig.deadlines = []
        self.rig.worst_events = []

    def test_deadlines_are_little_endian_and_keep_skipped_releases(self):
        self.rig.decode_health((0x7C, struct.pack("<9I", 5000, 3998, 80, 20, 704, 0, 2, 1000, 1100)))
        row = self.rig.deadlines[0]
        self.assertEqual(row["uptime_ms"], 5000)
        self.assertEqual(row["max_elapsed_us"], 704)
        self.assertEqual(row["skipped"], 2)
        self.assertEqual(row["budget_us"], 1000)

    def test_short_diagnostics_are_not_accepted(self):
        for identifier in (0x7A, 0x7C, 0x7D, 0x7E):
            self.rig.decode_health((identifier, bytes(3)))
        self.assertEqual(self.rig.deadlines, [])
        self.assertEqual(self.rig.health, [])
        self.assertEqual(self.rig.usb_progress, [])


if __name__ == "__main__":
    unittest.main()
