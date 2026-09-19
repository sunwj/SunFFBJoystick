import math
import unittest

try:
    from sunffb_gui.force_model import (
        Kinematics, EffectParams, ForceModel, CONSTANT, RAMP, SINE, SQUARE, TRIANGLE,
        SAWTOOTH_UP, SAWTOOTH_DOWN, direction_degrees, u_from_angle,
    )
except ImportError:
    from python_apis.sunffb_gui.force_model import (
        Kinematics, EffectParams, ForceModel, CONSTANT, RAMP, SINE, SQUARE, TRIANGLE,
        SAWTOOTH_UP, SAWTOOTH_DOWN, direction_degrees, u_from_angle,
    )


class DirectionTests(unittest.TestCase):
    def test_u_from_angle_cardinal(self):
        for deg, expected in [
            (0.0, (0.0, 1.0)),      # pull: force from front -> push stick back (+y)
            (90.0, (-1.0, 0.0)),    # left: force from right -> push left (-x)
            (180.0, (0.0, -1.0)),   # push: force from back -> push forward (-y)
            (270.0, (1.0, 0.0)),    # right: force from left -> push right (+x)
        ]:
            ux, uy = u_from_angle(deg)
            self.assertAlmostEqual(ux, expected[0], places=6)
            self.assertAlmostEqual(uy, expected[1], places=6)

    def test_direction_degrees_roundtrip(self):
        for deg in (0.0, 90.0, 180.0, 270.0, 45.0):
            ux, uy = u_from_angle(deg)
            self.assertAlmostEqual(direction_degrees(ux * 5000, uy * 5000), deg, places=4)


class ConstantForceTests(unittest.TestCase):
    def test_constant_magnitude_and_direction(self):
        p = EffectParams(effect_type=CONSTANT, magnitude=5000, direction_deg=90.0)
        m = ForceModel(main=p)
        fx, fy = m.evaluate_effect(p, Kinematics())
        self.assertAlmostEqual(fx, -5000.0, places=3)
        self.assertAlmostEqual(fy, 0.0, places=3)

    def test_combined_no_spring(self):
        main = EffectParams(effect_type=CONSTANT, magnitude=1000, direction_deg=180.0)
        m = ForceModel(main=main)
        fx, fy = m.evaluate_combined(Kinematics())
        self.assertAlmostEqual(fx, 0.0, places=3)
        self.assertAlmostEqual(fy, -1000.0, places=3)


class RampTests(unittest.TestCase):
    def test_ramp_start_end(self):
        p = EffectParams(effect_type=RAMP, ramp_start=-5000, ramp_end=5000,
                         direction_deg=0.0, magnitude=0)
        p.duration_ms = 1000  # not in dataclass; use explicit frac via helper
        m = ForceModel(main=p)
        # evaluate_effect for ramp uses params.direction and uses internal
        # elapsed fraction; we test the static interp helper instead:
        self.assertAlmostEqual(ForceModel.ramp_value(p, 0.0), -5000.0, places=3)
        self.assertAlmostEqual(ForceModel.ramp_value(p, 1.0), 5000.0, places=3)
        self.assertAlmostEqual(ForceModel.ramp_value(p, 0.5), 0.0, places=3)


class PeriodicTests(unittest.TestCase):
    def test_wave_samples(self):
        for wave, u, expected in [
            (SINE, 0.0, 0.0),
            (SINE, 0.25, 1.0),
            (SINE, 0.5, 0.0),
            (SINE, 0.75, -1.0),
            (SQUARE, 0.0, 1.0),
            (SQUARE, 0.5, -1.0),
            (TRIANGLE, 0.0, 1.0),
            (TRIANGLE, 0.25, 0.0),
            (TRIANGLE, 0.5, -1.0),
            (TRIANGLE, 0.75, 0.0),
            (SAWTOOTH_UP, 0.0, -1.0),
            (SAWTOOTH_UP, 1.0, 1.0),
            (SAWTOOTH_UP, 0.5, 0.0),
            (SAWTOOTH_DOWN, 0.0, 1.0),
            (SAWTOOTH_DOWN, 0.5, 0.0),
            (SAWTOOTH_DOWN, 1.0, -1.0),
        ]:
            self.assertAlmostEqual(ForceModel.periodic_wave(wave, u), expected, places=5)

    def test_periodic_value_and_direction(self):
        p = EffectParams(effect_type=SINE, magnitude=4000, offset=1000,
                         period_ms=100, phase=9000, direction_deg=0.0)
        m = ForceModel(main=p)
        # u from 0 deg = (0,1); value = 1000 + 4000*sin(2pi*0.25 + phase/360*2pi)
        import math
        frac = 0.25 + (9000 / 35999.0)  # note: simplified phase normalization by 35999
        val = 1000 + 4000 * math.sin(2 * math.pi * (frac % 1.0))
        fx, fy = m.evaluate_effect(p, Kinematics())
        self.assertAlmostEqual(fx, 0.0, places=2)
        self.assertAlmostEqual(fy, val, places=2)


if __name__ == "__main__":
    unittest.main()
