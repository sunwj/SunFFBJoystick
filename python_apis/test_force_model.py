import math
import unittest

try:
    from sunffb_gui.force_model import (
        Kinematics, EffectParams, ForceModel, CONSTANT, direction_degrees, u_from_angle,
    )
except ImportError:
    from python_apis.sunffb_gui.force_model import (
        Kinematics, EffectParams, ForceModel, CONSTANT, direction_degrees, u_from_angle,
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


if __name__ == "__main__":
    unittest.main()
