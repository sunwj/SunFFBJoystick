import unittest

try:
    from .sunffb_hid import (
        NUM_AXIS,
        DIRECTION_ENABLE,
        REPORT_ID_JOYSTICK,
        PoolReportData,
        JoystickInputReportData,
        SetEffectReportData,
        pack_report,
        parse_feature_response,
        _normalize_directions,
    )
except ImportError:
    from sunffb_hid import (
        NUM_AXIS,
        DIRECTION_ENABLE,
        REPORT_ID_JOYSTICK,
        PoolReportData,
        JoystickInputReportData,
        SetEffectReportData,
        pack_report,
        parse_feature_response,
        _normalize_directions,
    )


class ProtocolTests(unittest.TestCase):
    def test_packed_sizes_match_num_axis_two_layout(self):
        self.assertEqual(JoystickInputReportData.sizeof(), 1 + 2 * NUM_AXIS)
        self.assertEqual(SetEffectReportData.sizeof(), 17 if NUM_AXIS == 2 else 1 + 1 + 2 + 2 + 2 + 1 + 1 + 1 + 2 * NUM_AXIS + 2)
        self.assertEqual(PoolReportData.sizeof(), 4)

    def test_round_trip_signed_axis_and_memoryview(self):
        report = JoystickInputReportData(buttons=0x05, axis=(-1234, 2345))
        packet = pack_report(REPORT_ID_JOYSTICK, report)
        decoded = parse_feature_response(REPORT_ID_JOYSTICK, JoystickInputReportData, memoryview(packet))
        self.assertEqual(decoded.buttons, 0x05)
        self.assertEqual(tuple(decoded.axis), (-1234, 2345))

    def test_report_validation_has_actionable_errors(self):
        report = JoystickInputReportData()
        with self.assertRaises(ValueError):
            pack_report(256, report)
        with self.assertRaises(TypeError):
            pack_report(REPORT_ID_JOYSTICK, object())
        with self.assertRaises(IOError):
            parse_feature_response(REPORT_ID_JOYSTICK, JoystickInputReportData, [0])
        with self.assertRaises(ValueError):
            parse_feature_response(REPORT_ID_JOYSTICK, JoystickInputReportData, [REPORT_ID_JOYSTICK, 0])

    def test_direction_validation(self):
        self.assertEqual(_normalize_directions((0,) * NUM_AXIS), (0,) * NUM_AXIS)
        with self.assertRaises(ValueError):
            _normalize_directions((0,))
        with self.assertRaises(ValueError):
            _normalize_directions((0,) * (NUM_AXIS + 1))
        with self.assertRaises(ValueError):
            _normalize_directions((-1,) * NUM_AXIS)

    def test_pool_report_managed_pool_alias(self):
        report = PoolReportData(ramPoolSize=64, maxSimultaneousEffects=15, managedPool=1)
        self.assertTrue(report.managed_pool)
        report.managedPool = 0
        self.assertFalse(report.managed_pool)


if __name__ == "__main__":
    unittest.main()