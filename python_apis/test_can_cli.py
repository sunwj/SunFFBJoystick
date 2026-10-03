# Hardware-free CAN CLI tests inject a fake bus, clock and output sink.
# Monitoring must not transmit; validate one-shot ID/DLC/RTR handling separately from driver submission.

import importlib.util
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace

host = Path(__file__).resolve().parents[1] / 'lib/EmbeddedComm/host'
sys.path.insert(0, str(host))
try:
    spec = importlib.util.spec_from_file_location('embeddedcomm_can_cli', host / 'can_cli.py')
    cli = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cli)
finally:
    sys.path.remove(str(host))


class FakeBus:
    def __init__(self):
        self.sent = []
        self.messages = []
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.closed = True

    def send(self, message, timeout):
        self.sent.append((message, timeout))

    def recv(self, timeout):
        return self.messages.pop(0) if self.messages else None


class CanToolTests(unittest.TestCase):
    def args(self, *arguments):
        return cli.parser().parse_args(['--interface', 'virtual', '--channel', 'test', *arguments])

    def module(self, bus):
        return SimpleNamespace(Bus=lambda **_: bus, Message=lambda **fields: SimpleNamespace(**fields))

    def test_send_once_and_close(self):
        bus = FakeBus()
        cli.run(self.args('send', '--id', '0x181', '--data', '12 02 00 80'), self.module(bus), output=lambda _: None)
        self.assertTrue(bus.closed)
        self.assertEqual(len(bus.sent), 1)
        self.assertEqual(bus.sent[0][0].data, b'\x12\x02\x00\x80')
        self.assertEqual(bus.sent[0][0].dlc, 4)
        self.assertFalse(bus.sent[0][0].is_extended_id)

    def test_remote_frame_dlc(self):
        bus = FakeBus()
        cli.run(self.args('send', '--id', '0x12345', '--extended', '--remote', '--dlc', '8'), self.module(bus), output=lambda _: None)
        self.assertEqual(bus.sent[0][0].dlc, 8)
        self.assertEqual(bus.sent[0][0].data, b'')
        self.assertTrue(bus.sent[0][0].is_remote_frame)

    def test_invalid_frame_rejected_before_opening_bus(self):
        for arguments in [('send', '--id', '0x800'),
                          ('send', '--id', '1', '--remote', '--data', '01'),
                          ('send', '--id', '1', '--dlc', '2')]:
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                cli.run(self.args(*arguments), None)

    def test_monitor_receives_decodes_and_never_sends(self):
        bus = FakeBus()
        bus.messages = [SimpleNamespace(arbitration_id=0x181, data=b'\x12\x02\x00\x80\x00\x80',
                                        is_extended_id=False, is_remote_frame=False,
                                        is_fd=False, is_error_frame=False)]
        times = iter([0.0, 0.0, 0.5, 1.0])
        output = []
        cli.run(self.args('monitor', '--duration', '1', '--decode-motor'), self.module(bus),
                clock=lambda: next(times), output=output.append)
        self.assertEqual(bus.sent, [])
        self.assertTrue(bus.closed)
        self.assertIn("motor=('position', 2, [32768, 32768])", output[1])

    def test_motor_ids_and_finite_time(self):
        with self.assertRaises(ValueError):
            cli.validate(self.args('monitor', '--decode-motor', '--force-id', '0x181'))
        for value in ('nan', 'inf', '0', '-1'):
            with self.assertRaises(cli.argparse.ArgumentTypeError):
                cli.positive_seconds(value)
        with self.assertRaises(cli.argparse.ArgumentTypeError):
            cli.payload('00 ' * 9)


if __name__ == '__main__':
    unittest.main()
