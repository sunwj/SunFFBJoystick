"""Classic CAN monitor and one-shot transmitter using optional python-can."""
import argparse
import math
import sys
import time

from can_packet import decode


def identifier(text):
    value = int(text, 0)
    if not 0 <= value <= 0x1FFFFFFF:
        raise argparse.ArgumentTypeError('CAN ID must be in 0..0x1FFFFFFF')
    return value


def positive_seconds(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('Time must be finite and positive')
    return value


def payload(text):
    try:
        data = bytes.fromhex(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError('Data must be hexadecimal bytes') from error
    if len(data) > 8:
        raise argparse.ArgumentTypeError('Classic CAN payload cannot exceed 8 bytes')
    return data


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument('--interface', required=True, help='python-can backend, e.g. socketcan, slcan, pcan')
    result.add_argument('--channel', required=True, help='Backend channel, e.g. can0 or COM5')
    result.add_argument('--bitrate', type=int, default=500000)
    commands = result.add_subparsers(dest='command', required=True)

    monitor = commands.add_parser('monitor', help='Receive only; never transmits application frames')
    monitor.add_argument('--duration', type=positive_seconds, default=10.0)
    monitor.add_argument('--decode-motor', action='store_true', help='Also decode the SunFFB motor protocol')
    monitor.add_argument('--axes', type=int, choices=(1, 2, 3), default=2)
    monitor.add_argument('--force-id', type=identifier, default=0x201)
    monitor.add_argument('--position-id', type=identifier, default=0x181)
    monitor.add_argument('--heartbeat-id', type=identifier, default=0x701)

    send = commands.add_parser('send', help='Transmit one raw classic CAN frame')
    send.add_argument('--id', type=identifier, required=True)
    send.add_argument('--data', type=payload, default=b'', help='Up to 8 hex bytes, e.g. "12 02 00 80 00 80"')
    send.add_argument('--extended', action='store_true')
    send.add_argument('--remote', action='store_true')
    send.add_argument('--dlc', type=int, choices=range(9), help='Remote-frame requested DLC; defaults to 0')
    send.add_argument('--timeout', type=positive_seconds, default=0.1)
    return result


def validate(args):
    if args.bitrate <= 0:
        raise ValueError('Bitrate must be positive')
    if args.command == 'send':
        if args.id > (0x1FFFFFFF if args.extended else 0x7FF):
            raise ValueError('Standard CAN ID exceeds 0x7FF; select --extended')
        if args.remote and args.data:
            raise ValueError('Remote frames do not carry payload data')
        if not args.remote and args.dlc is not None:
            raise ValueError('--dlc applies only to remote frames; data frames use payload length')
    elif args.decode_motor:
        ids = (args.force_id, args.position_id, args.heartbeat_id)
        if len(set(ids)) != 3 or any(value > 0x7FF for value in ids):
            raise ValueError('Motor protocol requires three distinct standard IDs')


def run(args, can_module, *, clock=time.monotonic, output=print):
    validate(args)
    with can_module.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate) as bus:
        if args.command == 'send':
            message = can_module.Message(
                arbitration_id=args.id, data=args.data, is_extended_id=args.extended,
                is_remote_frame=args.remote,
                dlc=(args.dlc or 0) if args.remote else len(args.data), check=True,
            )
            bus.send(message, timeout=args.timeout)
            output('Frame submitted to host CAN driver')
            return

        deadline = clock() + args.duration
        while True:
            remaining = deadline - clock()
            if remaining <= 0:
                break
            message = bus.recv(timeout=min(0.1, remaining))
            if message is None:
                continue

            output(str(message))
            if args.decode_motor and not message.is_fd and not message.is_error_frame:
                decoded = decode(
                    message.arbitration_id, bytes(message.data), axes=args.axes,
                    force_id=args.force_id, position_id=args.position_id,
                    heartbeat_id=args.heartbeat_id, extended=message.is_extended_id,
                    remote=message.is_remote_frame,
                )
                if decoded is not None:
                    output(f'  motor={decoded}')


def main(argv=None):
    argument_parser = parser()
    args = argument_parser.parse_args(argv)
    try:
        validate(args)
    except ValueError as error:
        argument_parser.error(str(error))

    try:
        import can
    except ImportError:
        argument_parser.exit(1, 'CAN tools require python-can: pip install python-can\n')

    try:
        run(args, can)
    except KeyboardInterrupt:
        return 0
    except (can.CanError, OSError, ValueError) as error:
        print(f'CAN error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
