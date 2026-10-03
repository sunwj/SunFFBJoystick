# Direction widget mapping tests keep labels, angles and selection state consistent.
# UI conventions only: no HID commands and no physical calibration of motor force direction.

import test_paths

import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import unittest
from PyQt6.QtWidgets import QApplication

from sunffb_gui.widgets import direction_from_pad, pad_label_from_deg


class PadLogicTests(unittest.TestCase):
    def test_direction_mapping(self):
        self.assertEqual(direction_from_pad('pull'), 0.0)
        self.assertEqual(direction_from_pad('left'), 90.0)
        self.assertEqual(direction_from_pad('push'), 180.0)
        self.assertEqual(direction_from_pad('right'), 270.0)

    def test_nearest_label(self):
        self.assertEqual(pad_label_from_deg(0), 'pull')
        self.assertEqual(pad_label_from_deg(45), 'pull-left')
        self.assertEqual(pad_label_from_deg(90), 'left')
        self.assertEqual(pad_label_from_deg(180), 'push')
        self.assertEqual(pad_label_from_deg(270), 'right')


if __name__ == "__main__":
    unittest.main()
