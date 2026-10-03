"""Exercise real USB HID -> firmware -> UART force output, with no motors attached.

Requires esp32-s3-hil, a 3.3 V UART (ESP RX48/TX45), and common ground.
This is destructive to the current effect pool. It attempts to stop/disable effects
on exit; USB failure can prevent cleanup, so keep motors disconnected.
CDC is disabled in shipped environments; omit --console. Host timestamps describe
received batches, not precise wire timing.
"""

import argparse
import ctypes as ct
import json
import math
import os
import re
import statistics
import struct
import sys
import time
from pathlib import Path

import serial
import sunffb_hid as h

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lib/EmbeddedComm/host"))
from packet import SerialLink, MSG_FORCE, build_frame, pack_position, unpack_force


class Rig:
    def __init__(self, port, device, console=None, position_hz=250):
        self.device = device
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0, write_timeout=1)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.link = SerialLink(self.serial)
        self.results = []
        self.console = serial.Serial(console, 115200, timeout=0) if console else None
        self.console_data = bytearray()
        self.position_period = 1 / position_hz
        self.health = []
        self.usb_progress = []
        self.deadlines = []

    def decode_health(self, frame):
        if frame[0] == 0x7C and len(frame[1]) == 36:
            self.deadlines.append(dict(zip(
                ("uptime_ms", "count", "max_wake_us", "max_lock_us", "max_elapsed_us",
                 "missed", "skipped", "budget_us", "force_stack_free"),
                struct.unpack("<9I", frame[1]))))
        if frame[0] == 0x7D and len(frame[1]) == 32:
            values = struct.unpack("<8I", frame[1])
            self.usb_progress.append(dict(zip(
                ("uptime_ms", "console_stage", "console_calls", "hid_calls",
                 "command_starts", "command_ends", "usb_task_state", "usb_stack_free"), values)))
        if frame[0] == 0x7E and len(frame[1]) == 20:
            values = struct.unpack("<5I", frame[1])
            self.health.append(dict(uptime_ms=values[0], reset_reason=values[1],
                                    usb_mounted=values[2], usb_suspended=values[3], heap=values[4]))

    def sample(self, seconds=0.3, position=lambda t: (0, 0), raw=None):
        # One consumer per transport; timestamp frames at host receipt. Continue
        # reading USB while streaming positions so its endpoint never fills up.
        forces, axes = [], []
        start = time.perf_counter()
        next_send = start
        while time.perf_counter() - start < seconds:
            now = time.perf_counter()
            if self.console and self.console.in_waiting:
                self.console_data.extend(self.console.read(self.console.in_waiting))
            if now >= next_send:
                values = [max(0, min(65535, round(v + 32768))) for v in position(now - start)]
                self.serial.write(raw if raw is not None else build_frame(2, pack_position(values)))
                next_send += self.position_period
                if next_send <= now:
                    # Skip missed releases rather than sending a catch-up burst.
                    next_send = now + self.position_period
            while (frame := self.link.receive()) is not None:
                self.decode_health(frame)
                if frame[0] == MSG_FORCE and len(frame[1]) == 8:
                    forces.append((time.perf_counter() - start, *unpack_force(frame[1])))
            report = self.device._require().read(64, 0)
            if report and report[0] == h.REPORT_ID_JOYSTICK:
                axes.append((time.perf_counter() - start, *h.JoystickInputReportData.from_bytes(bytes(report[1:])).axis))
            time.sleep(0.001)
        return forces, axes

    def collect_failure_health(self):
        # Do not reopen/retry HID and hide a failure. Read only the independent
        # UART for six seconds so an actual reset is distinguishable from USB loss.
        end = time.perf_counter() + 6
        while time.perf_counter() < end:
            while (frame := self.link.receive()) is not None:
                self.decode_health(frame)
            time.sleep(.01)
        print(json.dumps(dict(failure_uart_health=self.health[-10:])), flush=True)
        print(json.dumps(dict(failure_usb_progress=self.usb_progress[-10:])), flush=True)

    def check(self, name, condition, evidence):
        result = dict(name=name, passed=bool(condition), evidence=evidence)
        self.results.append(result)
        print(json.dumps(result), flush=True)

    def reset(self):
        self.device.device_control(4)
        self.sample(0.12)

    def effect(self, kind, duration=65535, angle=9000, gain=255, sample=0, delay=0):
        block = self.device.create_new_effect(kind)
        if block.blockLoadStatus != 1:
            raise RuntimeError(f"Allocation failed: {block.blockLoadStatus}")
        idx = block.effectBlockIndex
        self.device.set_effect(h.SetEffectReportData(
            effectBlockIndex=idx, effectType=kind, duration=duration,
            samplePeriod=sample, gain=gain, triggerButton=h.USB_NO_TRIGGER_BUTTON,
            axisEnable=h.DIRECTION_ENABLE, directions=(ct.c_uint16 * 2)(angle, 0), startDelay=delay))
        return idx

    def start(self, idx):
        self.device.effect_operation(h.EffectOperationReportData(
            effectBlockIndex=idx, effectOperation=1, loopCount=1))

    def constant(self, value=4000, **kwargs):
        idx = self.effect(h.ET_CONSTANT, **kwargs)
        self.device.set_constant_force(h.SetConstantForceReportData(effectBlockIndex=idx, magnitude=value))
        self.start(idx)
        return idx

    def stable(self, expected, name):
        data, _ = self.sample()
        if not data:
            data, _ = self.sample(6)
        if not data:
            # CDC startup waits up to five seconds for a host. Allow a freshly
            # flashed board to complete initialization before diagnosing wiring.
            data, _ = self.sample(6)
        tail = data[len(data)//2:]
        observed = [statistics.median(row[i+1] for row in tail) for i in range(2)] if tail else []
        self.check(name, bool(tail) and all(abs(a-b) <= 25 for a, b in zip(observed, expected)),
                   dict(expected=expected, observed=observed, frames=len(data)))

    def run(self, soak):
        self.reset()
        data, _ = self.sample()
        if not data:
            raise RuntimeError("No UART force frames: check firmware boot, adapter voltage and RX/TX wiring")
        for target in [(12000, -9000), (-16000, 18000), (32767, -32767)]:
            _, axes = self.sample(0.5, lambda t: target)
            observed = list(axes[-1][1:]) if axes else []
            self.check("position roundtrip " + str(target), bool(axes) and
                       all(abs(a-b) < 100 for a, b in zip(observed, target)), observed)

        self.reset()
        capacity = self.device.get_pool_report().maxSimultaneousEffects
        blocks = [self.device.create_new_effect(h.ET_CONSTANT) for _ in range(capacity)]
        full = self.device.create_new_effect(h.ET_CONSTANT)
        self.check("pool allocation and full status", all(b.blockLoadStatus == 1 for b in blocks)
                   and len({b.effectBlockIndex for b in blocks}) == capacity and full.blockLoadStatus == 2,
                   dict(capacity=capacity, full_status=full.blockLoadStatus))
        self.device.free_effect(blocks[0].effectBlockIndex)
        self.check("pool reuse", self.device.create_new_effect(h.ET_CONSTANT).blockLoadStatus == 1, {})

        for angle, expected in [(9000, (-4000, 0)), (0, (0, 4000)), (18000, (0, -4000)), (27000, (4000, 0))]:
            self.reset()
            self.constant(angle=angle)
            self.stable(expected, f"constant direction {angle}")
        self.device.set_device_gain(128)
        self.stable((4000*128/255, 0), "device gain")
        self.device.set_device_gain(255)
        for command, expected, name in [(5, (0, 0), "pause"), (6, (4000, 0), "continue"),
                                         (2, (0, 0), "disable"), (6, (0, 0), "continue remains disabled"),
                                         (1, (4000, 0), "enable"), (3, (0, 0), "stop all")]:
            self.device.device_control(command)
            self.stable(expected, name)
        self.reset()
        self.constant(gain=128)
        self.stable((-4000*128/255, 0), "effect gain")
        self.constant(value=9000)
        self.stable((-10000, 0), "effect sum saturation")

        for kind in range(h.ET_SQUARE, h.ET_SAWTOOTH_DOWN + 1):
            self.reset()
            idx = self.effect(kind)
            self.device.set_periodic(h.SetPeriodicReportData(effectBlockIndex=idx,
                                                            magnitude=3000, offset=500, period=400))
            self.start(idx)
            data, _ = self.sample(1.1)
            xs = [r[1] for r in data]
            self.check(f"periodic type {kind} range and cross-axis isolation",
                       bool(xs) and min(xs) < -3300 and max(xs) > 2300 and
                       max(abs(r[2]) for r in data) < 5 and max(abs(x) for x in xs) <= 3501,
                       dict(min=min(xs) if xs else None, max=max(xs) if xs else None, frames=len(data)))

        self.reset()
        idx = self.effect(h.ET_RAMP, duration=600)
        self.device.set_ramp_force(h.SetRampForceReportData(effectBlockIndex=idx, rampStart=-3000, rampEnd=3000))
        self.start(idx)
        data, _ = self.sample(0.85)
        early = [r[1] for r in data if r[0] < .15]
        late = [r[1] for r in data if .45 < r[0] < .57]
        self.check("ramp and finite duration", bool(early and late) and statistics.mean(early) > 1000
                   and statistics.mean(late) < -1000 and all(abs(r[1]) < 5 for r in data if r[0] > .7),
                   dict(frames=len(data)))

        self.reset()
        idx = self.effect(h.ET_CONSTANT, duration=1000)
        self.device.set_constant_force(h.SetConstantForceReportData(effectBlockIndex=idx, magnitude=4000))
        self.device.set_envelope(h.SetEnvelopeReportData(effectBlockIndex=idx, attackLevel=0, fadeLevel=0,
                                                        attackTime=300, fadeTime=300))
        self.start(idx)
        data, _ = self.sample(1.15)
        def amplitude(lo, hi):
            values = [abs(r[1]) for r in data if lo < r[0] < hi]
            return statistics.mean(values) if values else -1
        values = [amplitude(.01, .08), amplitude(.4, .6), amplitude(.92, .98)]
        self.check("constant attack/fade envelope", 0 <= values[0] < 1600 and values[1] > 3900
                   and 0 <= values[2] < 1600, values)

        self.reset()
        self.constant(duration=400, delay=250)
        data, _ = self.sample(.85)
        before = [r[1] for r in data if .04 < r[0] < .18]
        during = [r[1] for r in data if .33 < r[0] < .55]
        after = [r[1] for r in data if r[0] > .75]
        self.check("start delay and expiration", bool(before and during and after) and
                   all(abs(v) < 5 for v in before + after) and
                   all(abs(v+4000) < 5 for v in during), dict(frames=len(data)))

        self.reset()
        idx = self.effect(h.ET_RAMP, duration=1000, sample=100)
        self.device.set_ramp_force(h.SetRampForceReportData(effectBlockIndex=idx,
                                                          rampStart=0, rampEnd=4000))
        self.start(idx)
        data, _ = self.sample(.85)
        steps = sorted({r[1] for r in data if .1 < r[0] < .75})
        self.check("samplePeriod holds ramp samples", 4 <= len(steps) <= 9 and
                   all(abs(v/400-round(v/400)) < .01 for v in steps), dict(levels=steps))

        for kind in (h.ET_SPRING, h.ET_DAMPER, h.ET_INERTIA, h.ET_FRICTION):
            self.reset()
            idx = self.effect(kind)
            for axis, saturation in enumerate((1000, 2000)):
                self.device.set_condition(h.SetConditionReportData(effectBlockIndex=idx,
                    parameterBlockOffset=axis, positiveCoefficient=10000, negativeCoefficient=10000,
                    positiveSaturation=saturation, negativeSaturation=saturation))
            # Deliberately send an envelope after both slots: it must not corrupt them.
            self.device.set_envelope(h.SetEnvelopeReportData(effectBlockIndex=idx,
                attackLevel=123, fadeLevel=456, attackTime=100, fadeTime=100))
            self.start(idx)
            motion = (lambda t: (20000, 20000)) if kind == h.ET_SPRING else (
                lambda t: (22000*math.sin(2*math.pi*t), 22000*math.sin(2*math.pi*t)))
            data, _ = self.sample(1.5, motion)
            peaks = [max((abs(r[i+1]) for r in data), default=0) for i in range(2)]
            paired = sum(abs(r[1]) >= 900 and abs(r[2]) >= 1800 for r in data)
            self.check(f"condition {kind}: directed XY blocks and envelope isolation",
                       990 <= peaks[0] <= 1000 and 1990 <= peaks[1] <= 2000 and paired > 10,
                       dict(peaks=peaks, paired_frames=paired))

        self.reset()
        self.sample(.3, lambda t: (7000, -8000))
        bad = bytearray(build_frame(2, pack_position([60000, 60000])))
        bad[-1] ^= 1
        _, axes = self.sample(.25, raw=bytes(bad))
        # A valid position already queued before corruption can arrive late.
        # It is not evidence of a corrupted packet being accepted.
        self.check("CRC rejects corrupted positions", all(abs(r[1]-7000) < 100 and
                   abs(r[2]+8000) < 100 for r in axes), dict(trailing_reports=axes))
        _, axes = self.sample(.4, lambda t: (-10000, 11000))
        self.check("parser recovers after bad CRC", bool(axes) and abs(axes[-1][1] + 10000) < 100
                   and abs(axes[-1][2] - 11000) < 100, list(axes[-1]) if axes else [])

        self.reset()
        self.constant()
        self.sample(.1)
        start = time.perf_counter()
        data, axes = self.sample(soak, lambda t: (15000*math.sin(t), 12000*math.cos(t)))
        elapsed = time.perf_counter() - start
        uart_hz = len(data) / elapsed
        self.check("loaded soak: UART output and USB input survive", len(axes) > soak * min(40, .5/self.position_period) and
                   450 < uart_hz < 550 and all(abs(r[1]+4000) < 5 and abs(r[2]) < 5 for r in data),
                   dict(seconds=elapsed, uart_host_received_hz=uart_hz, usb_reports=len(axes),
                        uart_parser=self.link.stats))

    def endurance(self, seconds):
        # Rotate full-pool workloads; one-second batches bound host memory usage.
        capacity = self.device.get_pool_report().maxSimultaneousEffects
        start = time.perf_counter()
        phase = -1
        totals = dict(seconds=0, uart_frames=0, usb_reports=0)
        baseline = None
        health_start = len(self.health)
        while time.perf_counter() - start < seconds:
            elapsed = time.perf_counter() - start
            current = int(elapsed // 60) % 3
            if current != phase:
                self.reset()
                for slot in range(capacity):
                    kind = 4 if current == 0 else (h.ET_INERTIA if current == 1 else (4, h.ET_DAMPER, h.ET_SPRING)[slot % 3])
                    idx = self.effect(kind, angle=(slot * 2300) % 36000)
                    if kind == 4:
                        self.device.set_periodic(h.SetPeriodicReportData(
                            effectBlockIndex=idx, magnitude=500, period=23 + slot * 7, phase=slot * 1700))
                    else:
                        for axis in range(2):
                            self.device.set_condition(h.SetConditionReportData(
                                effectBlockIndex=idx, parameterBlockOffset=axis,
                                positiveCoefficient=10000, negativeCoefficient=10000,
                                positiveSaturation=500, negativeSaturation=500))
                    self.device.set_envelope(h.SetEnvelopeReportData(
                        effectBlockIndex=idx, attackTime=100, attackLevel=100, fadeTime=100, fadeLevel=100))
                    self.start(idx)
                phase = current
                print(json.dumps(dict(workload=phase, active_effects=capacity, elapsed=elapsed)), flush=True)
            data, axes = self.sample(min(1, seconds - (time.perf_counter() - start)),
                                     lambda t: (24000*math.sin(19*t), 22000*math.cos(23*t)))
            totals["uart_frames"] += len(data)
            totals["usb_reports"] += len(axes)
            if not data or not axes:
                raise RuntimeError("Endurance transport stopped: empty one-second batch")
            if self.deadlines and baseline is None:
                baseline = self.deadlines[-1].copy()
            if int(time.perf_counter() - start) % 30 == 0:
                print(json.dumps(dict(endurance_progress=round(time.perf_counter() - start),
                                      deadline=self.deadlines[-1] if self.deadlines else None)), flush=True)
        # Drain one more diagnostic period so the final tested interval is covered.
        self.sample(1.2)
        totals["seconds"] = time.perf_counter() - start
        health = self.health[health_start:]
        self.check("endurance: continuous USB/UART and no reset", len(health) >= seconds * .9 and
                   all(b["uptime_ms"] > a["uptime_ms"] for a, b in zip(health, health[1:])) and
                   all(r["usb_mounted"] == 1 and r["usb_suspended"] == 0 for r in health), totals)
        final = self.deadlines[-1] if self.deadlines else None
        self.check("full-pool force deadlines", baseline is not None and final is not None and
                   final["uptime_ms"] - baseline["uptime_ms"] >= (seconds - 3) * 1000 and
                   final["missed"] == baseline["missed"] and final["skipped"] == baseline["skipped"] and
                   final["max_elapsed_us"] <= final["budget_us"], dict(baseline=baseline, final=final))

    def timing_summary(self):
        text = self.console_data.decode(errors="replace")
        streams = {}
        pattern = r"TIMING (\w+) hz=([\d.]+) max_gap_us=(\d+) gap_over_budget=(\d+) max_work_us=(\d+) fail=(\d+)"
        for name, hz, gap, over, work, failed in re.findall(pattern, text):
            item = streams.setdefault(name, dict(windows=0, min_hz=100000, max_hz=0,
                                                max_gap_us=0, max_work_us=0, failures=0))
            item["windows"] += 1
            item["min_hz"] = min(item["min_hz"], float(hz))
            item["max_hz"] = max(item["max_hz"], float(hz))
            item["max_gap_us"] = max(item["max_gap_us"], int(gap))
            item["max_work_us"] = max(item["max_work_us"], int(work))
            item["failures"] += int(failed)
        deadlines = [tuple(map(int, values)) for values in re.findall(
            r"DEADLINE force count=(\d+) wake_us=(\d+) lock_us=(\d+) elapsed_us=(\d+) missed=(\d+) skipped=(\d+)", text)]
        summary = dict(streams=streams, deadline_windows=len(deadlines),
                       max_wake_us=max((r[1] for r in deadlines), default=None),
                       max_lock_us=max((r[2] for r in deadlines), default=None),
                       max_elapsed_us=max((r[3] for r in deadlines), default=None),
                       missed=sum(r[4] for r in deadlines), skipped=sum(r[5] for r in deadlines))
        print(json.dumps(dict(timing_summary=summary)), flush=True)
        return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--motors-disconnected", action="store_true", required=True)
    parser.add_argument("--soak", type=float, default=30)
    parser.add_argument("--endurance", type=float, default=0, help="Full-pool rotating stress duration in seconds")
    parser.add_argument("--console", help="Optional USB CDC port for firmware timing logs")
    parser.add_argument("--position-hz", type=float, default=250)
    parser.add_argument("--report-json", type=Path, help="Save checks and complete firmware logs")
    args = parser.parse_args()
    if args.soak < 5:
        parser.error("--soak must be at least 5 seconds")
    if args.endurance and args.endurance < 180:
        parser.error("--endurance must be at least 180 seconds to cover all workloads")
    if not 1 <= args.position_hz <= 500:
        parser.error("--position-hz must be in 1..500")
    # Avoid Windows' coarse default sleep quantum throttling the simulated encoder.
    if os.name == "nt":
        ct.windll.winmm.timeBeginPeriod(1)
    with h.SunFFBDevice(0xFFFF, 0x2010) as device:
        rig = Rig(args.port, device, args.console, args.position_hz)
        failure = None
        try:
            rig.run(args.soak)
            if args.endurance:
                rig.endurance(args.endurance)
        except Exception as error:
            failure = str(error)
            rig.check("hardware execution completed", False, dict(error=failure))
            rig.collect_failure_health()
            raise
        finally:
            try:
                device.device_control(3)
                device.device_control(2)
                device.free_effect(255)
            except Exception as error:
                print(f"Cleanup could not stop the device (USB disconnected): {error}", file=sys.stderr)
            finally:
                rig.serial.close()
                if rig.console:
                    rig.console.close()
                if os.name == "nt":
                    ct.windll.winmm.timeEndPeriod(1)
                timing = rig.timing_summary() if rig.console_data else {}
                if args.report_json:
                    args.report_json.write_text(json.dumps(dict(
                        position_hz=args.position_hz, soak_seconds=args.soak,
                        checks=rig.results, timing=timing, uart_health=rig.health,
                        usb_progress=rig.usb_progress,
                        endurance_seconds=args.endurance, uart_deadlines=rig.deadlines,
                        execution_error=failure,
                        firmware_log=rig.console_data.decode(errors="replace")), indent=2), encoding="utf-8")
        print(json.dumps(dict(passed=sum(r["passed"] for r in rig.results), total=len(rig.results))))
        return 0 if all(r["passed"] for r in rig.results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
