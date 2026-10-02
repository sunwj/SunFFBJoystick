import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import unittest
import ctypes as ct
from PyQt6.QtWidgets import QApplication

from sunffb_hid import ET_CONSTANT, ET_SPRING
from sunffb_gui.force_model import EffectParams, CONSTANT, SINE, DAMPER, SPRING
from sunffb_gui.main import (build_set_effect, build_periodic, build_condition, build_constant,
                             ET_MAP, CONDITION_TYPES, MainWindow)

_APP = QApplication.instance() or QApplication([])


class BuildTests(unittest.TestCase):
    def test_set_effect_polar(self):
        p = EffectParams(effect_type=CONSTANT, magnitude=5000, direction_deg=90.0,
                         duration_ms=1000, gain=255)
        r = build_set_effect(p, 3)
        self.assertEqual(r.effectBlockIndex, 3)
        # The descriptor always declares angular directions: polar (9000, 0).
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

    def test_condition_effects_are_infinite(self):
        p = EffectParams(effect_type=SPRING, duration_ms=100)
        report = build_set_effect(p, 1)
        self.assertEqual(report.duration, 0xFFFF)


class FakeDev:
    def __init__(self):
        self.created = []
        self.freed = []
        self.sets = []
        self.conditions = []
        self.ops = []
        self.envelopes = []
        self.ramps = []
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
        self.ramps.append(r)


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
    def test_condition_effects_keep_both_axis_blocks(self):
        for effect_type in CONDITION_TYPES:
            with self.subTest(effect_type=effect_type):
                w = self._window()
                try:
                    w.cmb_type.setCurrentIndex(w.cmb_type.findData(effect_type))
                    w.chk_effect_on.setChecked(True)
                    blocks = [r.parameterBlockOffset for r in w.controller.dev.conditions]
                    self.assertEqual(blocks, [0, 1])
                    self.assertEqual(w.controller.dev.envelopes, [])
                finally:
                    w.close()

    def test_default_ramp_sends_nonzero_ramp_and_previews_it(self):
        w = self._window()
        try:
            w.cmb_type.setCurrentIndex(w.cmb_type.findData("ramp"))
            w.chk_effect_on.setChecked(True)
            report = w.controller.dev.ramps[-1]
            self.assertEqual((report.rampStart, report.rampEnd), (0, 4000))
            self.assertEqual(w.controller.dev.sets[-1].duration, 1500)
            self.assertIn((w._effect_idx, 1), w.controller.dev.ops)
            fx, fy = w._model.evaluate_combined(w._last_kin, 750)
            self.assertAlmostEqual(fx, 2000)
            self.assertAlmostEqual(fy, 0)
            self.assertFalse(w.duration_row.isEnabled())
            self.assertFalse(w.spn_mag.isEnabled())
            self.assertEqual(w.spn_ramp_duration.minimum(), 1)
        finally:
            w.close()

    def test_switching_playing_effect_starts_each_new_block(self):
        for effect_type in ET_MAP:
            if effect_type == CONSTANT:
                continue
            with self.subTest(effect_type=effect_type):
                w = self._window()
                try:
                    w.chk_effect_on.setChecked(True)
                    previous = w._effect_idx
                    w.cmb_type.setCurrentIndex(w.cmb_type.findData(effect_type))
                    self.assertNotEqual(w._effect_idx, previous)
                    self.assertIn(previous, w.controller.dev.freed)
                    self.assertIn((w._effect_idx, 1), w.controller.dev.ops)
                    self.assertEqual(w._main_params.effect_type, effect_type)
                finally:
                    w.close()

    def test_live_duration_change_updates_expiry(self):
        w = self._window()
        try:
            w.chk_infinite.setChecked(False)
            w.chk_effect_on.setChecked(True)
            w.spn_duration.setValue(2000)
            self.assertEqual(w._run_total_ms, 2000)
            w.spn_loop.setValue(0)
            self.assertEqual(w._run_total_ms, 2000)
            w.chk_infinite.setChecked(True)
            self.assertEqual(w._run_total_ms, 0)
        finally:
            w.close()

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

    def test_start_effect_keeps_loop_control_alive(self):
        """The rebuilt reference layout must own controls used by start_effect."""
        w = self._window()
        try:
            self.assertEqual(w.spn_loop.value(), 1)
            w.start_effect()
            self.assertIn((1, 1), w.controller.dev.ops)
        finally:
            w.close()

    def test_expiring_main_effect_keeps_background_spring(self):
        w = self._window()
        try:
            w.apply_main_effect()
            w.apply_spring()
            w._stop_main_effect()
            self.assertEqual(w._effect_idx, 0)
            self.assertEqual(w._spring_idx, 2)
            self.assertNotIn(2, w.controller.dev.freed)
            self.assertFalse(w.chk_effect_on.isChecked())
        finally:
            w.close()


class WindowSmoke(unittest.TestCase):
    def test_window_creates_without_device(self):
        w = MainWindow(show_hid=False)
        self.assertFalse(w.controller.is_connected)
        w.close()

    def test_reference_layout_and_dynamic_effect_panels(self):
        w = MainWindow(show_hid=False)
        self.assertTrue(hasattr(w, "spn_device_gain"))
        self.assertTrue(hasattr(w, "chk_spring_on"))
        self.assertTrue(w.chk_spring_on.isChecked())
        self.assertFalse(w.grp_periodic.isEnabled())
        self.assertTrue(w.grp_envelope.isEnabled())

        w.cmb_type.setCurrentIndex(w.cmb_type.findData(SINE))
        self.assertTrue(w.grp_periodic.isEnabled())
        self.assertFalse(w.grp_condition.isEnabled())
        w.cmb_type.setCurrentIndex(w.cmb_type.findData(DAMPER))
        self.assertTrue(w.grp_condition.isEnabled())
        self.assertFalse(w.grp_envelope.isEnabled())
        w.close()


if __name__ == "__main__":
    unittest.main()
