from __future__ import annotations

import argparse
import time

from sunffb_hid import (
    ET_CONSTANT,
    ET_SINE,
    USB_NO_TRIGGER_BUTTON,
    X_AXIS_ENABLE,
    DIRECTION_ENABLE,
    NUM_AXIS,
    SunFFBDevice,
    SetEffectReportData,
    SetPeriodicReportData,
    EffectOperationReportData,
)


def auto_select_device(vid: int, pid: int):
    devices = SunFFBDevice.enumerate(vid, pid)
    if not devices:
        raise SystemExit(f"No HID devices found for VID=0x{vid:04X}, PID=0x{pid:04X}")
    if len(devices) > 1:
        print("Multiple devices found. Using the first one:")
    for i, d in enumerate(devices):
        print(
            f"[{i}] VID=0x{d.vendor_id:04X} PID=0x{d.product_id:04X} "
            f"product={d.product_string!r} manufacturer={d.manufacturer_string!r} "
            f"usage_page={d.usage_page} usage={d.usage}"
        )
    return devices[0]


def cmd_list(args):
    devices = SunFFBDevice.enumerate(args.vid, args.pid)
    if not devices:
        print("No devices found.")
        return
    for i, d in enumerate(devices):
        print(
            f"    VID=0x{d.vendor_id:04X} PID=0x{d.product_id:04X}\n"
            f"    product={d.product_string!r} manufacturer={d.manufacturer_string!r}\n"
            f"    serial={d.serial_number!r} usage_page={d.usage_page} usage={d.usage}\n"
        )


def cmd_pool(args):
    dev_info = auto_select_device(args.vid, args.pid)
    with SunFFBDevice(args.vid, args.pid) as dev:
        pool = dev.get_pool_report()
        print(f"ramPoolSize={pool.ramPoolSize}")
        print(f"maxSimultaneousEffects={pool.maxSimultaneousEffects}")
        print(f"memoryManagement={pool.memoryManagement}")


def cmd_enable(args):
    dev_info = auto_select_device(args.vid, args.pid)
    with SunFFBDevice(args.vid, args.pid) as dev:
        dev.device_control(1)
        print("Actuators enabled.")


def cmd_disable(args):
    dev_info = auto_select_device(args.vid, args.pid)
    with SunFFBDevice(args.vid, args.pid) as dev:
        dev.device_control(2)
        print("Actuators disabled.")


def cmd_stop_all(args):
    dev_info = auto_select_device(args.vid, args.pid)
    with SunFFBDevice(args.vid, args.pid) as dev:
        dev.device_control(3)
        print("Stopped all effects.")


def cmd_reset(args):
    dev_info = auto_select_device(args.vid, args.pid)
    with SunFFBDevice(args.vid, args.pid) as dev:
        dev.device_control(4)
        print("Device reset command sent.")


def cmd_gain(args):
    dev_info = auto_select_device(args.vid, args.pid)
    with SunFFBDevice(args.vid, args.pid) as dev:
        dev.set_device_gain(args.gain)
        print(f"Device gain set to {args.gain}.")


def cmd_constant(args):
    dev_info = auto_select_device(args.vid, args.pid)
    with SunFFBDevice(args.vid, args.pid) as dev:
        effect_id = dev.create_constant_effect(
            magnitude=args.magnitude,
            duration_ms=args.duration,
            gain=args.gain,
            axis_enable=DIRECTION_ENABLE,
            directions=(9000, 0),
            loop_count=args.loop_count,
            start=True,
        )
        print(f"Created constant effect block {effect_id}.")
        if args.wait > 0:
            print(f"Waiting {args.wait:.2f}s before freeing the effect...")
            time.sleep(args.wait)
            dev.free_effect(effect_id)
            print("Freed effect.")


def cmd_sine(args):
    dev_info = auto_select_device(args.vid, args.pid)
    with SunFFBDevice(args.vid, args.pid) as dev:
        block = dev.create_new_effect(ET_SINE)
        if block.blockLoadStatus != 1 or block.effectBlockIndex == 0:
            raise SystemExit(
                f"Create effect failed: status={block.blockLoadStatus}, ram={block.ramPoolAvailable}"
            )

        effect_id = block.effectBlockIndex
        dev.set_effect(
            SetEffectReportData(
                effectBlockIndex=effect_id,
                effectType=ET_SINE,
                duration=args.duration,
                triggerRepeatInterval=0,
                samplePeriod=0,
                gain=args.gain,
                triggerButton=USB_NO_TRIGGER_BUTTON,
                axisEnable=X_AXIS_ENABLE,
                directions=(__import__("ctypes").c_uint16 * NUM_AXIS)(0, 0),
                startDelay=0,
            )
        )
        dev.set_periodic(
            SetPeriodicReportData(
                effectBlockIndex=effect_id,
                magnitude=args.magnitude,
                offset=args.offset,
                phase=args.phase,
                period=args.period,
            )
        )
        dev.effect_operation(
            EffectOperationReportData(
                effectBlockIndex=effect_id,
                effectOperation=1,
                loopCount=max(1, args.loop_count),
            )
        )
        print(f"Created sine effect block {effect_id}.")
        if args.wait > 0:
            print(f"Waiting {args.wait:.2f}s before freeing the effect...")
            time.sleep(args.wait)
            dev.free_effect(effect_id)
            print("Freed effect.")


def build_parser():
    parser = argparse.ArgumentParser(description="SunFFB HID CLI")
    parser.add_argument("--vid", type=lambda x: int(x, 0), required=True, help="USB vendor ID")
    parser.add_argument("--pid", type=lambda x: int(x, 0), required=True, help="USB product ID")

    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("list", help="List matching HID devices")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("pool", help="Read pool feature report")
    p.set_defaults(func=cmd_pool)

    p = sub.add_parser("enable", help="Enable actuators")
    p.set_defaults(func=cmd_enable)

    p = sub.add_parser("disable", help="Disable actuators")
    p.set_defaults(func=cmd_disable)

    p = sub.add_parser("stop-all", help="Stop all effects")
    p.set_defaults(func=cmd_stop_all)

    p = sub.add_parser("reset", help="Reset the device")
    p.set_defaults(func=cmd_reset)

    p = sub.add_parser("gain", help="Set device gain")
    p.add_argument("gain", type=int, help="0..255")
    p.set_defaults(func=cmd_gain)

    p = sub.add_parser("constant", help="Create and start a constant force effect")
    p.add_argument("--magnitude", type=int, required=True, help="-10000..10000")
    p.add_argument("--duration", type=int, default=1000, help="milliseconds")
    p.add_argument("--gain", type=int, default=255, help="0..255")
    p.add_argument("--loop-count", type=int, default=1, help="0..255")
    p.add_argument("--wait", type=float, default=0.0, help="seconds to wait before free")
    p.set_defaults(func=cmd_constant)

    p = sub.add_parser("sine", help="Create and start a sine periodic effect")
    p.add_argument("--magnitude", type=int, required=True, help="0..10000")
    p.add_argument("--duration", type=int, default=2000, help="milliseconds")
    p.add_argument("--period", type=int, default=100, help="milliseconds")
    p.add_argument("--gain", type=int, default=255, help="0..255")
    p.add_argument("--offset", type=int, default=0, help="signed offset")
    p.add_argument("--phase", type=int, default=0, help="phase")
    p.add_argument("--loop-count", type=int, default=1, help="0..255")
    p.add_argument("--wait", type=float, default=0.0, help="seconds to wait before free")
    p.set_defaults(func=cmd_sine)

    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
