import math
import unittest

try:
    from sunffb_gui.force_model import (
        Kinematics, EffectParams, ForceModel, CONSTANT, RAMP, SINE, SQUARE, TRIANGLE,
        SAWTOOTH_UP, SAWTOOTH_DOWN, SPRING, DAMPER, INERTIA, FRICTION,
        direction_degrees, u_from_angle,
    )
except ImportError:
    from python_apis.sunffb_gui.force_model import (
        Kinematics, EffectParams, ForceModel, CONSTANT, RAMP, SINE, SQUARE, TRIANGLE,
        SAWTOOTH_UP, SAWTOOTH_DOWN, SPRING, DAMPER, INERTIA, FRICTION,
        direction_degrees, u_from_angle,
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
        p.duration_ms = 1000
        m = ForceModel(main=p)
        _, y250 = m.evaluate_effect(p, Kinematics(), elapsed_ms=250)
        _, y750 = m.evaluate_effect(p, Kinematics(), elapsed_ms=750)
        self.assertAlmostEqual(y250, -2500.0, places=3)
        self.assertAlmostEqual(y750, 2500.0, places=3)
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
            (TRIANGLE, 0.0, -1.0),
            (TRIANGLE, 0.25, 0.0),
            (TRIANGLE, 0.5, 1.0),
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
        # phase 9000 (hundredths of a degree) == 90 deg == 0.25 cycle; FFBTestTool:
        # u = t/period + PhaseDeg/360 with PhaseDeg in degrees -> 0.25 at t=0.
        val = 1000 + 4000 * math.sin(2 * math.pi * 0.25)
        fx, fy = m.evaluate_effect(p, Kinematics())
        self.assertAlmostEqual(fx, 0.0, places=2)
        self.assertAlmostEqual(fy, val, places=2)

    def test_periodic_phase_hundredths_cycle(self):
        # Phase is in HID hundredths of a degree: 9000 -> 90deg -> u=0.25.
        for phase, u in [(0, 0.0), (9000, 0.25), (18000, 0.5), (27000, 0.75)]:
            p = EffectParams(effect_type=SINE, magnitude=4000, offset=0,
                             period_ms=100, phase=phase, direction_deg=0.0)
            m = ForceModel(main=p)
            fx, fy = m.evaluate_effect(p, Kinematics())
            self.assertAlmostEqual(fy, 4000 * math.sin(2 * math.pi * u), places=2)

    def test_periodic_uses_elapsed_time(self):
        # FFBTestTool uses real elapsed time: u = t/period + phase/360.
        p = EffectParams(effect_type=TRIANGLE, magnitude=4000, offset=0,
                         period_ms=100, phase=0, direction_deg=0.0)
        m = ForceModel(main=p)
        fx, fy = m.evaluate_effect(p, Kinematics(), elapsed_ms=25.0)
        self.assertAlmostEqual(fy, 0.0, places=2)   # u=0.25 -> wave=0
        fx2, fy2 = m.evaluate_effect(p, Kinematics(), elapsed_ms=50.0)
        self.assertAlmostEqual(fy2, 4000.0, places=2)  # u=0.5 -> wave=+1 (peak)
        fx3, fy3 = m.evaluate_effect(p, Kinematics(), elapsed_ms=100.0)
        self.assertAlmostEqual(fy3, -4000.0, places=2)  # u wraps to 0 -> -1 (start min)

    def test_periodic_sawtooth_full_amplitude(self):
        # Sawtooth spans -magnitude..+magnitude per single period (C# 2u-1 / 1-2u).
        p = EffectParams(effect_type=SAWTOOTH_UP, magnitude=4000, offset=0,
                         period_ms=100, phase=0, direction_deg=0.0)
        m = ForceModel(main=p)
        _, y0 = m.evaluate_effect(p, Kinematics(), elapsed_ms=0.0)
        self.assertAlmostEqual(y0, -4000.0, places=2)
        _, y50 = m.evaluate_effect(p, Kinematics(), elapsed_ms=50.0)
        self.assertAlmostEqual(y50, 0.0, places=2)
        _, y99 = m.evaluate_effect(p, Kinematics(), elapsed_ms=99.0)
        self.assertAlmostEqual(y99, 4000.0 * (2 * 0.99 - 1.0), places=2)


class EnvelopeTests(unittest.TestCase):
    def test_envelope_attack_fade(self):
        p = EffectParams(attack_level=5000, attack_time_ms=500,
                         fade_level=2000, fade_time_ms=1000, duration_ms=2000)
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 0), 0.5, places=3)
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 250), 0.75, places=3)   # attack half
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 500), 1.0, places=3)
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 1000), 1.0, places=3)
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 1500), 0.6, places=3)
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 2000), 0.2, places=3)
    def test_envelope_zero_levels(self):
        p = EffectParams(attack_level=0, attack_time_ms=0, fade_level=0, fade_time_ms=0)
        self.assertEqual(ForceModel.envelope_factor(p, 0), 1.0)
    def test_envelope_applied_to_constant(self):
        p = EffectParams(effect_type=CONSTANT, magnitude=10000, direction_deg=270.0,
                         attack_level=5000, attack_time_ms=500)
        m = ForceModel(main=p)
        fx, fy = m.evaluate_effect(p, Kinematics(), elapsed_ms=250.0)
        self.assertAlmostEqual(fx, 7500.0, places=3)
        self.assertAlmostEqual(fy, 0.0, places=3)


class ConditionTests(unittest.TestCase):
    def test_spring_neutral(self):
        p = EffectParams(effect_type=SPRING, pos_coeff_x=6000, neg_coeff_x=6000,
                         pos_sat_x=10000, neg_sat_x=10000, dead_band_x=0, center_x=0,
                         pos_coeff_y=6000, neg_coeff_y=6000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, fy = m.evaluate_effect(p, Kinematics(roll=0.5, pitch=0.0))
        self.assertAlmostEqual(fx, -3000.0, places=3)  # -6000*0.5
        self.assertAlmostEqual(fy, 0.0, places=3)

    def test_spring_deadband_and_center(self):
        p = EffectParams(effect_type=SPRING, pos_coeff_x=6000, neg_coeff_x=6000,
                         pos_sat_x=10000, neg_sat_x=10000, dead_band_x=2000, center_x=1000,
                         pos_coeff_y=6000, neg_coeff_y=6000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0))
        self.assertAlmostEqual(fx, 0.0, places=3)  # inside deadband around center=0.1
        fx2, _ = m.evaluate_effect(p, Kinematics(roll=0.4, pitch=0.0))
        # d = 0.4 - 0.1 = 0.3; db=0.2; (d-db)=0.1 -> -6000*0.1 = -600
        self.assertAlmostEqual(fx2, -600.0, places=3)

    def test_damper_clamp_and_inertia(self):
        p = EffectParams(effect_type=DAMPER, pos_coeff_x=6000, neg_coeff_x=6000,
                         pos_sat_x=10000, neg_sat_x=10000, dead_band_x=0, center_x=0,
                         pos_coeff_y=6000, neg_coeff_y=6000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0, vel_roll=0.5))
        self.assertAlmostEqual(fx, -3000.0, places=3)
        fx2, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0, vel_roll=3.0))
        self.assertAlmostEqual(fx2, -6000.0, places=3)  # Clamp1 caps at 1.0

    def test_friction_threshold(self):
        p = EffectParams(effect_type=FRICTION, pos_coeff_x=4000, neg_coeff_x=4000,
                         pos_sat_x=10000, neg_sat_x=10000, dead_band_x=0, center_x=0,
                         pos_coeff_y=4000, neg_coeff_y=4000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0, vel_roll=0.5))
        self.assertAlmostEqual(fx, -4000.0, places=3)
        fx2, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0, vel_roll=0.01))
        self.assertAlmostEqual(fx2, 0.0, places=3)

    def test_saturation_clamp(self):
        p = EffectParams(effect_type=SPRING, pos_coeff_x=6000, neg_coeff_x=6000,
                         pos_sat_x=2000, neg_sat_x=2000, dead_band_x=0, center_x=0,
                         pos_coeff_y=6000, neg_coeff_y=6000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, _ = m.evaluate_effect(p, Kinematics(roll=0.9, pitch=0.0))
        self.assertAlmostEqual(fx, -2000.0, places=3)  # clamp at pos_sat on negative side

    def test_combined_adds_spring(self):
        main = EffectParams(effect_type=CONSTANT, magnitude=1000, direction_deg=180.0)
        spring = EffectParams(effect_type=SPRING, magnitude=0, pos_coeff_x=5000, center_x=0,
                              pos_coeff_y=5000, center_y=0,
                              pos_sat_x=10000, neg_sat_x=10000,
                              pos_sat_y=10000, neg_sat_y=10000)
        m = ForceModel(main=main, spring=spring)
        fx, fy = m.evaluate_combined(Kinematics(roll=0.5, pitch=0.0))
        # spring spring metric=0.5 -> f=-5000*0.5=-2500 (x), main pushes -y*1000 -> (0,-1000)
        self.assertAlmostEqual(fx, -2500.0, places=3)
        self.assertAlmostEqual(fy, -1000.0, places=3)


if __name__ == "__main__":
    unittest.main()
