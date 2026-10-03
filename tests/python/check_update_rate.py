# Read-only HID cadence probe that separates report IDs without creating effects or enabling motors.
# Warm up before collecting host receive intervals; OS scheduling and buffering affect results.
# Unchanged coordinates can still be fresh samples; this probe alone cannot certify real-time deadlines.

"""Read-only host HID rate probe; does not create effects or enable motors."""
from __future__ import annotations

import test_paths

import argparse
import json
import time
from pathlib import Path

from sunffb_hid import REPORT_ID_JOYSTICK, SunFFBDevice


# Nearest-rank-style index on sorted gaps; empty measurements have no percentile rather than zero.
def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


# Drain startup backlog, then timestamp joystick reports during a bounded read-only measurement window.
def measure(device: SunFFBDevice, seconds: float) -> dict:
    # Drain startup backlog for a short warmup before timing the read window.
    warmup_end = time.perf_counter() + 1.0
    while time.perf_counter() < warmup_end:
        try:
            device.read_input_once(timeout_ms=50)
        except TimeoutError:
            pass
    started = time.perf_counter()
    deadline = started + seconds
    received: list[float] = []
    pid_reports = 0
    while time.perf_counter() < deadline:
        remaining_ms = max(1, min(50, int((deadline - time.perf_counter()) * 1000)))
        try:
            report_id, _ = device.read_input_once(timeout_ms=remaining_ms)
        except TimeoutError:
            continue
        now = time.perf_counter()
        if now > deadline:
            break
        if report_id == REPORT_ID_JOYSTICK:
            received.append(now)
        else:
            pid_reports += 1
    intervals_ms = [(b - a) * 1000 for a, b in zip(received, received[1:])]
    return {
        "requested_window_s": seconds,
        "position_reports": len(received),
        "position_hz": len(received) / seconds,
        "other_reports": pid_reports,
        "read_gap_p50_ms": percentile(intervals_ms, 0.50),
        "read_gap_p99_ms": percentile(intervals_ms, 0.99),
        "read_gap_max_ms": max(intervals_ms, default=None),
        "note": "Host read timestamps include OS scheduling and HID buffering. "
        "Use firmware usb_done/fresh counters and a wire trace for transport timing. "
        "Unchanged coordinate values are still valid new samples; HID has no sequence field.",
    }


# Validate the duration before opening HID; optional JSON output saves results, not device state.
def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=0xFFFF)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=0x2010)
    parser.add_argument("--json", type=Path, help="Save the measurement to this file")
    args = parser.parse_args()
    if not 0 < args.seconds <= 3600:
        parser.error("--seconds must be in (0, 3600]")
    with SunFFBDevice(args.vid, args.pid) as device:
        result = measure(device, args.seconds)
    encoded = json.dumps(result, indent=2)
    print(encoded)
    if args.json:
        args.json.write_text(encoded + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
