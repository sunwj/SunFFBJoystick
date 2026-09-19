import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import unittest
import ctypes as ct
from PyQt6.QtWidgets import QApplication

from sunffb_hid import ET_CONSTANT, ET_SPRING
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


class FakeDev:
    def __init__(self):
        self.created = []
        self.freed = []
        self.sets = []
        self.conditions = []
        self.ops = []
        self.envelopes = []
        self._next = [1]

    def create_effect(self, effect_type):
        idx = self._next[0]
        self._next[0] += 1
        self.created.append((effect_type, idx))
        return idx

    def free_effect(self, idx):
        self.freed.append(idx)

    def set_effect(self, r):
        self.sets.append(r)

    def set_condition(self, r):
        self.conditions.append(r)

    def effect_operation(self, r):
        self.ops.append((r.effectBlockIndex, r.effectOperation))

    def set_envelope(self, r):
        self.envelopes.append(r)

    def set_periodic(self, r):
        pass

    def set_constant_force(self, r):
        pass

    def set_ramp_force(self, r):
        pass


class FakeController:
    def __init__(self):
        self.dev = FakeDev()
        self._connected = True

    @property
    def is_connected(self):
        return self._connected

    @property
    def _device(self):
        return self.dev

    def create_effect(self, effect_type):
        return self.dev.create_effect(effect_type)

    def free_effect(self, idx):
        self.dev.free_effect(idx)

    def disconnect(self):
        self._connected = False


class SpringBlockTests(unittest.TestCase):
    def _window(self):
        w = MainWindow(show_hid=False)
        w.controller = FakeController()
        return w

    def test_apply_spring_uses_separate_block_and_keeps_main(self):
        w = self._window()
        self.assertTrue(w.apply_main_effect())  # main block idx 1
        self.assertTrue(w.apply_spring())       # must NOT reuse/free idx 1
        self.assertEqual(w._effect_idx, 1)
        self.assertEqual(w._spring_idx, 2)
        self.assertEqual(w.controller.dev.freed, [])
        self.assertEqual([t for t, _ in w.controller.dev.created],
                         [ET_CONSTANT, ET_SPRING])
        spring_sets = [r for r in w.controller.dev.sets if r.effectBlockIndex == 2]
        self.assertEqual(len(spring_sets), 1)
        cond_idx = {r.effectBlockIndex for r in w.controller.dev.conditions}
        self.assertEqual(cond_idx, {2})
        # spring must be started so it actually plays alongside the main effect
        self.assertIn((2, 1), w.controller.dev.ops)
        # predictive model still sums spring over main
        self.assertIsNotNone(w._model.spring)

    def test_stop_frees_both_blocks_and_clears_preview(self):
        w = self._window()
        w.apply_main_effect()
        w.apply_spring()
        w._update_canvas()
        self.assertNotEqual(w.canvas._fx, 0.0)  # preview showing full force
        w.stop_effect()
        self.assertEqual(sorted(w.controller.dev.freed), [1, 2])
        self.assertIn((1, 3), w.controller.dev.ops)
        self.assertIn((2, 3), w.controller.dev.ops)
        self.assertEqual(w._effect_idx, 0)
        self.assertEqual(w._spring_idx, 0)
        self.assertIsNone(w._model)
        w._update_canvas()
        self.assertEqual(w.canvas._fx, 0.0)
        self.assertEqual(w.canvas._fy, 0.0)


class WindowSmoke(unittest.TestCase):
    def test_window_creates_without_device(self):
        w = MainWindow(show_hid=False)
        self.assertFalse(w.controller.is_connected)
        w.close()


if __name__ == "__main__":
    unittest.main()
