import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import unittest
import ctypes as ct
from PyQt6.QtWidgets import QApplication

from sunffb_gui.force_model import EffectParams, CONSTANT, SINE
from sunffb_gui.main import (build_set_effect, build_periodic, build_condition, build_constant,
                             ET_MAP, MainWindow)

_APP = QApplication.instance() or QApplication([])


class BuildTests(unittest.TestCase):
    def test_set_effect_cartesian(self):
        p = EffectParams(effect_type=CONSTANT, magnitude=5000, direction_deg=90.0,
                         duration_ms=1000, gain=255)
        r = build_set_effect(p, 3)
        self.assertEqual(r.effectBlockIndex, 3)
        # direction_enabled=True -> polar (9000, 0)
        self.assertEqual(r.directions[0], 9000)
        self.assertEqual(r.directions[1], 0)
        self.assertEqual(r.gain, 255)

    def test_periodic_struct(self):
        p = EffectParams(effect_type=SINE, magnitude=4000, period_ms=100, phase=9000, offset=500)
        pr = build_periodic(p, 3)
        self.assertEqual(pr.magnitude, 4000)
        self.assertEqual(pr.period, 100)
        self.assertEqual(pr.phase, 9000)
        self.assertEqual(pr.offset, 500)


class WindowSmoke(unittest.TestCase):
    def test_window_creates_without_device(self):
        w = MainWindow(show_hid=False)
        self.assertFalse(w.controller.is_connected)
        w.close()


if __name__ == "__main__":
    unittest.main()
