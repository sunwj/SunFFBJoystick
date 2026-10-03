"""Test SunFFB through the user's DirectInput library, with motors disconnected.

Run from the py_directinput_ffb repository root. Only that library creates or
updates effects; the independent UART observes force output and firmware timing.
DirectInput polls are state samples, not counts of fresh USB packets.
"""

import argparse
import ctypes as ct
import json
import math
import os
from pathlib import Path
import statistics
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_hardware import Rig
import sunffb_hid as h
sys.path.insert(0, str(Path.cwd()))
import directinput_ffb as di
from directinput_ffb.dinput_definitions import DIJOYSTATE, DIJOFS_X, DIJOFS_Y, DIEFF_CARTESIAN, DIENVELOPE


class StateReader:
    def __init__(self, device):
        self.device = device

    def _require(self):
        return self

    def read(self, length, timeout):
        self.device.Poll()
        state = DIJOYSTATE()
        self.device.GetDeviceState(ct.sizeof(state), ct.byref(state))
        report = h.JoystickInputReportData()
        report.axis[0], report.axis[1] = state.lX, state.lY
        return bytes([h.REPORT_ID_JOYSTICK]) + bytes(report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--motors-disconnected", action="store_true", required=True)
    parser.add_argument("--stress", type=float, default=180)
    parser.add_argument("--report-json", type=Path, required=True)
    args = parser.parse_args()
    if args.stress < 180:
        parser.error("--stress must be at least 180 seconds")
    root = di.create_direct_input()
    matches = [item for item in di.enum_devices(root, only_attached=True, only_force_feedback=True)
               if str(item.guid_product).lower().startswith("{2010ffff-")]
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one SunFFB VID/PID device, found {len(matches)}")
    device = di.create_device(root, matches[0].guid_instance)
    handles = []
    rig = None
    cleanup_errors = []
    error = None
    if os.name == "nt":
        ct.windll.winmm.timeBeginPeriod(1)
    try:
        hwnd = di.set_cooperative_level(device)
        axes = tuple(di.enum_ffb_axes_actuator_offsets(device))
        if axes != (DIJOFS_X, DIJOFS_Y):
            raise RuntimeError(f"Expected XY force actuators, got {axes}")
        data_format = di.set_data_format(device, di.dinput_api.build_joystick_data_format(axes))
        for offset in axes:
            di.set_axis_range(device, offset, -32768, 32767)
        di.set_autocenter(device, False)
        di.acquire(device)
        di.set_device_gain(device, 10000)
        di.stop_all_effects(device)
        supported = di.enum_effects(device)
        print(json.dumps(dict(device=matches[0].product_name, axes=axes,
                              effects=[item.name for item in supported])), flush=True)
        rig = Rig(args.port, StateReader(device), position_hz=500)
        rig.sample(1.2)
        baseline = rig.deadlines[-1].copy() if rig.deadlines else None

        def play(effect):
            handles.append(effect)
            effect.download()
            effect.start()
            return effect

        def finish(effect):
            effect.stop()
            effect.unload()
            # Do not Stop/Unload the same native handle twice during final cleanup.
            handles[:] = [entry for entry in handles if entry is not effect]

        def stable(expected, name):
            data, _ = rig.sample(.4)
            tail = data[len(data)//2:]
            observed = [statistics.median(row[i+1] for row in tail) for i in range(2)] if tail else []
            rig.check(name, len(observed) == 2 and all(abs(a-b) <= 35 for a, b in zip(observed, expected)),
                      dict(expected=expected, observed=observed, uart_frames=len(data)))

        _, states = rig.sample(.4, lambda t: (12000, -9000))
        rig.check("DirectInput position roundtrip", bool(states) and abs(states[-1][1]-12000) < 100
                  and abs(states[-1][2]+9000) < 100, states[-1] if states else None)
        for angle, expected in ((0, (0, 4000)), (9000, (-4000, 0)),
                                (18000, (0, -4000)), (27000, (4000, 0))):
            effect = play(di.create_constant_force_effect(device, magnitude=4000,
                direction_hundredths_deg=angle, direction_basis=DIEFF_CARTESIAN, duration_us=0xFFFFFFFF))
            stable(expected, f"DirectInput constant direction {angle}")
            finish(effect)

        effect = play(di.create_constant_force_effect(device, magnitude=4000,
            direction_hundredths_deg=9000, duration_us=0xFFFFFFFF))
        effect.set_magnitude(2500)
        stable((-2500, 0), "DirectInput live magnitude")
        effect.apply(direction_hundredths_deg=0)
        stable((0, 2500), "DirectInput live direction")
        di.set_device_gain(device, 5000)
        stable((0, 1250), "DirectInput device gain")
        di.set_device_gain(device, 10000)
        finish(effect)

        for name in ("square", "sine", "triangle", "sawtooth_up", "sawtooth_down"):
            effect = play(getattr(di, f"create_{name}_effect")(device, magnitude=2000,
                period_us=200000, direction_hundredths_deg=9000, duration_us=0xFFFFFFFF))
            # Queued UART frames may still describe the previous effect. Retain
            # the transition evidence, then evaluate the defined steady window.
            transition, _ = rig.sample(.05)
            data, _ = rig.sample(.8)
            xs = [row[1] for row in data]
            rig.check(f"DirectInput periodic {name}", bool(xs) and min(xs) < -1850 and max(xs) > 1850
                      and max(abs(row[2]) for row in data) < 35,
                      dict(min=min(xs) if xs else None, max=max(xs) if xs else None,
                           steady_max_y=max((abs(row[2]) for row in data), default=None),
                           transition_max_y=max((abs(row[2]) for row in transition), default=None),
                           settling_seconds=.05))
            effect.set_periodic(magnitude=1000, period_us=100000)
            data, _ = rig.sample(.4)
            rig.check(f"DirectInput live periodic {name}", bool(data) and
                      max(abs(row[1]) for row in data[20:]) <= 1035, dict(uart_frames=len(data)))
            finish(effect)

        effect = play(di.create_ramp_force_effect(device, start_magnitude=-2000, end_magnitude=2000,
            direction_hundredths_deg=9000, duration_us=600000))
        data, _ = rig.sample(.85)
        early = [row[1] for row in data if row[0] < .15]
        late = [row[1] for row in data if .45 < row[0] < .55]
        rig.check("DirectInput ramp and expiration", bool(early and late) and
                  statistics.mean(early) > 900 and statistics.mean(late) < -900 and
                  all(abs(row[1]) < 35 for row in data if row[0] > .7), dict(uart_frames=len(data)))
        finish(effect)

        for name in ("spring", "damper", "inertia", "friction"):
            effect = play(getattr(di, f"create_{name}_effect")(device,
                positive_coefficient=10000, negative_coefficient=10000,
                per_axis=[dict(positive_saturation=1000, negative_saturation=1000),
                          dict(positive_saturation=2000, negative_saturation=2000)]))
            envelope = DIENVELOPE(dwSize=ct.sizeof(DIENVELOPE), dwAttackLevel=100,
                                  dwAttackTime=100000, dwFadeLevel=200, dwFadeTime=100000)
            effect.apply(envelope=envelope)
            motion = (lambda t: (22000, 22000)) if name == "spring" else (
                lambda t: (24000*math.sin(2*math.pi*t), 24000*math.sin(2*math.pi*t)))
            data, _ = rig.sample(1.5, motion)
            peaks = [max((abs(row[i+1]) for row in data), default=0) for i in range(2)]
            paired = sum(abs(row[1]) >= 950 and abs(row[2]) >= 1900 for row in data)
            rig.check(f"DirectInput XY {name} after envelope", 970 <= peaks[0] <= 1035
                      and 1970 <= peaks[1] <= 2035 and paired > 10, dict(peaks=peaks, paired=paired))
            effect.set_condition(1, positive_saturation=500, negative_saturation=500)
            data, _ = rig.sample(.8, motion)
            peaks = [max((abs(row[i+1]) for row in data[20:]), default=0) for i in range(2)]
            rig.check(f"DirectInput live Y condition {name}", 970 <= peaks[0] <= 1035
                      and 470 <= peaks[1] <= 535, dict(peaks=peaks))
            finish(effect)

        start = time.perf_counter()
        phase = -1
        pool = []
        frames = polls = updates = 0
        stress_baseline = rig.deadlines[-1].copy() if rig.deadlines else None
        while time.perf_counter() - start < args.stress:
            elapsed = time.perf_counter() - start
            current = int(elapsed // 60) % 3
            if current != phase:
                for effect in pool:
                    finish(effect)
                pool = []
                for slot in range(15):
                    if current == 0 or (current == 2 and slot % 3 == 0):
                        effect = di.create_sine_effect(device, magnitude=300, period_us=23000+slot*7000,
                            direction_hundredths_deg=slot*2300, duration_us=0xFFFFFFFF)
                    else:
                        factory = di.create_inertia_effect if current == 1 else (
                            di.create_damper_effect if slot % 3 == 1 else di.create_spring_effect)
                        effect = factory(device, positive_coefficient=10000,
                            positive_saturation=300, negative_saturation=300)
                    pool.append(play(effect))
                phase = current
                print(json.dumps(dict(stress_phase=phase, effects=len(pool), elapsed=elapsed)), flush=True)
            for slot, effect in enumerate(pool):
                effect.apply(gain=8000 if updates % 2 else 10000)
                updates += 1
            # Observe a complete batch even at the duration boundary. A final
            # sub-millisecond slice cannot be required to contain a 2 ms frame.
            data, states = rig.sample(1,
                                     lambda t: (24000*math.sin(19*t), 22000*math.cos(23*t)))
            frames += len(data)
            polls += len(states)
            if not data or not states:
                raise RuntimeError(f"DirectInput stress lost data in a full one-second batch: "
                                   f"forces={len(data)}, states={len(states)}, elapsed={time.perf_counter()-start}")
        for effect in pool:
            finish(effect)
        di.stop_all_effects(device)
        rig.sample(1.2)
        final = rig.deadlines[-1] if rig.deadlines else None
        rig.check("DirectInput full-pool stress", True,
                  dict(seconds=time.perf_counter()-start, uart_frames=frames, state_polls=polls, live_updates=updates))
        rig.check("DirectInput stress deadlines", stress_baseline is not None and final is not None
                  and final["missed"] == stress_baseline["missed"] and final["skipped"] == stress_baseline["skipped"]
                  and final["uptime_ms"]-stress_baseline["uptime_ms"] >= (args.stress-2)*1000,
                  dict(baseline=stress_baseline, final=final))
        rig.check("DirectInput boot-wide deadline budget", final is not None and
                  final["max_elapsed_us"] <= final["budget_us"] and final["missed"] == 0,
                  dict(final=final, note="Boot-wide evidence includes events before this runner opened UART"))
        rig.check("DirectInput no reset or USB unmount", len(rig.health) >= args.stress*.9 and
                  all(b["uptime_ms"] > a["uptime_ms"] for a, b in zip(rig.health, rig.health[1:])) and
                  all(row["usb_mounted"] and not row["usb_suspended"] for row in rig.health),
                  dict(records=len(rig.health), initial_deadline=baseline))
        stable((0, 0), "DirectInput stop/unload yields zero force")
    except Exception as exc:
        error = str(exc)
        if rig:
            rig.check("DirectInput execution completed", False, dict(error=error))
            rig.collect_failure_health()
        raise
    finally:
        for effect in handles:
            try:
                effect.stop()
                effect.unload()
            except Exception as exc:
                cleanup_errors.append(str(exc))
        try:
            di.stop_all_effects(device)
            device.SendForceFeedbackCommand(0x20)  # DISFFC_SETACTUATORSOFF; do not reset and re-enable.
            di.unacquire(device)
        except Exception as exc:
            cleanup_errors.append(str(exc))
        if rig:
            rig.serial.close()
            args.report_json.write_text(json.dumps(dict(library=str(Path(di.__file__).resolve()),
                device=matches[0].product_name, requested_position_hz=500,
                requested_stress_seconds=args.stress, checks=rig.results,
                uart_health=rig.health, uart_deadlines=rig.deadlines,
                force_worst_events=rig.worst_events,
                usb_progress=rig.usb_progress, execution_error=error,
                cleanup_errors=cleanup_errors), indent=2), encoding="utf-8")
        if os.name == "nt":
            ct.windll.winmm.timeEndPeriod(1)
    return 0 if rig and all(row["passed"] for row in rig.results) and not cleanup_errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
