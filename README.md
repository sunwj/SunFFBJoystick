# SunFFB Joystick Firmware

## Overview

SunFFB Joystick Firmware is a custom USB HID PID (Physical Interface Device) compliant force feedback firmware built with the Arduino framework. It enables a microcontroller-based joystick to present itself as a full-featured force feedback device to a host system.

The firmware implements:
- USB HID joystick input reports
- USB HID PID force feedback protocol
- Real-time force calculation (constant, ramp, periodic, and condition effects)
- Multi-axis support
- External position input (e.g., encoders via serial)

---

## Features

### Input
- Multi-axis joystick (configurable NUM_AXIS)
- Button input with trigger support
- External position input via serial interface

### Force Feedback (HID PID)
Supports standard PID effects:
- Constant force
- Ramp force
- Periodic effects (sine, square, triangle, sawtooth)
- Condition effects:
  - Spring
  - Damper
  - Inertia
  - Friction

### Processing
- Real-time force computation loop (1 ms task)
- Low-pass filtering for position, speed, and acceleration (inline in `ffb_device_input`)
- Deadband handling
- Directional force vectors (polar/spherical HID coordinates; DirectInput converts API coordinates)
- Effect sample-period support (HID PID coarse texture)

### USB
- Fully compliant HID joystick + PID device
- Works with standard drivers (no custom driver required)
- Uses TinyUSB for USB handling

---

## Architecture

Sensors/Encoders
    ↓
FFBDeviceInput → filtering, speed, acceleration
    ↓
FFBForceCalculator → computes forces from effects
    ↓
Motor Driver / Actuator Output
    ↓
USB HID Reports (Joystick + PID)

---

## Project Structure

SunFFBJoystick/
├── main.cpp                  — Entry point; FreeRTOS task creation
├── constants.h               — Forwarding header (includes config_ffb.h + config_board.h)
├── config_ffb.h              — Compile-time config (NUM_AXIS, MAX_EFFECTS, report IDs)
├── config_board.h            — Board config (USB VID/PID, task periods, speed/accel scaling)
├── ffb_device_input.*        — Axis read, low-pass filtering, speed/acceleration derivation
├── ffb_force_calculator.*    — Real-time force computation (effects, conditions, envelopes)
├── ffb_report_handler.*      — Effect block management, PID state machine
├── ffb_report_types.h        — Packed HID report structs
├── ffb_report_descriptor.h   — HID report descriptor (NUM_AXIS-dependent)
├── hid_pid.h                 — HID PID usage constants
├── math_utils.h              — clamp, fast math stubs

---

## Hardware Requirements

- Microcontroller with USB device support (ESP32-S2/S3 recommended)
- Motor driver (H-bridge, servo driver, etc.)
- Position sensors (encoders, potentiometers, etc.)
- Optional LCD and buttons

---

## Software Requirements

- Arduino IDE / PlatformIO
- TinyUSB
- Compatible board package

---

## How It Works

1. Input Processing
   - Position data is read and filtered into position, speed, and acceleration

2. Effect Handling
   - Host sends HID PID reports to configure and control effects

3. Force Calculation
   - Active effects are evaluated and summed

4. Output
   - Motor transmission is enabled explicitly with `ENABLE_MOTOR_OUTPUT=1` (default: off)
   - Joystick state sent via USB

---

## Usage

1. Flash firmware to board
2. Connect via USB
3. Open a force feedback compatible game
4. Device appears as joystick + FFB device

---

## License

This project is licensed for **non-commercial use only**.

You are free to:
- Use the code for personal projects
- Modify and experiment with the firmware
- Share the code with others for non-commercial purposes

You are **not allowed to**:
- Use this project or any derivative in commercial products
- Sell hardware or software based on this firmware
- Use it in any revenue-generating activity without explicit permission

For commercial licensing inquiries, please contact the author.

---

## Acknowledgements

- TinyUSB project
- [ForceFeedback-core-library](https://github.com/JakaSimonic/ForceFeedback-core-library) by Jaka Simonic
- [picowinder](https://github.com/NolanNicholson/picowinder) by Nolan Nicholson
- Open-source force feedback and DIY controller community

## Build and validation

PlatformIO is the firmware build and test entrypoint:

```sh
pio run -e esp32-s3
pio run -e esp32-s3-axis1 -e esp32-s3-axis3
pio test -e native -e native-axis1 -e native-axis3
pio test -e native-sanitized -e native-sanitized-axis1 -e native-sanitized-axis3
python3 -m unittest discover -s python_apis -p 'test_*.py'
python3 python_apis/generate_constants.py --check
```

The default firmware uses two axes and ADC input. `NUM_AXIS` can also be
selected through build flags; regenerate the Python constants for the axis
count used by the connected firmware. Axis build environments validate
compilation; when LCD is enabled, its pin setup must match the actual board.

`ENABLE_MOTOR_OUTPUT` and `USE_SERIAL_POSITION` are independent opt-in flags
in `src/config_board.h`. The `esp32-s3-serial` environment enables both for
build validation. It is not the default flash target.

The UART protocol is `[0xAA, message ID, payload length, payload, CRC8]`.
CRC-8/MAXIM-DOW covers ID, length and payload. Force (ID 1) carries little-endian
`int32_t[NUM_AXIS]`; position (ID 2) carries little-endian `uint16_t[NUM_AXIS]`;
heartbeat (ID 3) has an empty payload. Serial position input uses unsigned
full range 0..65535 with center 32768, mapped to HID -32767..32767. No position
sample is published until a valid position frame arrives. ADC calibration is
skipped when serial position input is selected.

USB resources and the command worker are initialized before HID starts.
Validated output/feature commands are copied to the worker and completed in
order before the USB callback returns; queue backpressure waits rather than
dropping a command. Shared effect/input state uses mutexes with priority
inheritance. Physical USB enumeration and motor timing still require board
validation; native tests do not simulate FreeRTOS scheduling.

Pause and actuator enable are independent. Continue only resumes an actual
pause and never enables disabled actuators. Reset frees effects, restores full
gain and enables actuators, preserving the existing firmware behavior. A cold
boot remains INIT until Enable or the first effect start. Attack takes
precedence when attack and fade overlap; a fade longer than the effect starts
at time zero and retains its requested slope. Infinite duration ignores fade.
Positive/negative saturation retain the existing mapping to restoring negative/
positive force respectively; negative coefficients are bounded on both sides.
The GUI triangle phase follows firmware: zero, positive peak, zero, negative
peak at quarter-cycle boundaries.

### ESP32-S2 single-core target

`pio run -e esp32-s2 -e esp32-s2-axis1 -e esp32-s2-axis3 -e esp32-s2-serial`
validates the ESP32-S2 Saola reference board with the same Arduino/FreeRTOS
and TinyUSB stack. Select the actual board before flashing; provide TFT_eSPI
pin setup only when enabling the debug LCD. S2 tasks all use core 0; force calculation has priority 3,
periodic I/O and input priority 2, and LCD priority 1. USB and the HID command
worker retain high priority and block when idle. A 1 kHz RTOS tick is required.
UART receive work is bounded and always yields between batches. Missed force
deadlines yield one tick instead of repeatedly catching up and starving I/O.

ADC samples are explicitly 12-bit before calibration on S2 and S3. Three-axis
ADC input now defaults to GPIO15 instead of GPIO19, which is USB D−. Move the
third-axis wire accordingly, or override `AXIS2_ADC_PIN` with an available ADC
pin. Reserve GPIO19/20 for USB and avoid overlaps with the board's TFT pins.
Firmware tasks start without waiting for USB enumeration. Allocation/task
creation failures assert instead of silently leaving functions inactive.

Compilation and native regression tests do not verify on-board USB enumeration,
LCD wiring, ADC accuracy, watchdog behavior, or worst-case force-loop timing.
Measure these on the actual S2 with concurrent HID and UART traffic. Include
LCD traffic only when validating the optional debug display.

### Real-time update contract and measurement

The scheduling contract is independent of the number of CPU cores:

| Stream | Target | Firmware behavior |
| --- | --- | --- |
| Force calculation | 1000 Hz by default, selectable 500 Hz | `FORCE_TASK_PERIOD_MS=1` or `2` |
| Motor position to MCU | 500 fresh frames/s | Motor must transmit every 2 ms; UART parser runs every 1 ms |
| Position processing to USB | 500 fresh reports/s for a 500 Hz source | Serial input wakes processing; each new sample is forwarded without a second 2 ms sampler |
| MCU force to motor | 500 frames/s | Latest calculated force is sent every 2 ms |

The shared HID IN endpoint polls every 1 ms. New position reports have priority;
PID state uses spare slots. Failed submissions are retried, and only successful
submissions advance ADC report timing. Serial input does not resubmit stale
positions as fresh updates. ADC input and reporting retain 2 ms periods.
Queues retain the latest sample; they intentionally do not replay stale backlog.

UART 8N1 uses 10 bits per byte. At three axes, 500 Hz force frames require
`(4 + 3*4)*10*500 = 80000 bit/s` TX; position frames require
`(4 + 3*2)*10*500 = 50000 bit/s` RX. Both fit the separate full-duplex directions
at 115200 baud. The encoder/controller must independently maintain its 500 Hz
transmission and consumption rates. The wire format has no sample sequence or
source timestamp, so firmware cannot prove motor-side generation rate or loss.

Use `esp32-s2-timing` or `esp32-s3-timing` to build serial-input/motor-output
firmware with a low-priority diagnostic log on UART0 (115200 baud). These are
opt-in motor-output builds. `esp32-s2-force500` validates 500 Hz calculation.

```
pio run -e esp32-s2-timing -e esp32-s3-timing -e esp32-s2-force500
pio device monitor -e esp32-s3-timing --baud 115200
python3 python_apis/check_update_rate.py --seconds 30 --json rate.json
```

Run the HID rate probe as the sole input reader and select the board environment
matching the hardware. It is read-only and does not create effects or enable
actuators. Exercise maximum supported active effects, concurrent host commands,
continuous 500 Hz position input during acceptance testing, with LCD disabled
for production. Check the debug display separately when enabled.

`TIMING` logs show actual measured rates over each elapsed window: `force`
(calculation publication), `rx` (valid position frames), `input` (processed
samples), `usb_submit` (accepted HID writes), `usb_done` (completed USB IN
transfers), `fresh` (distinct samples submitted), and `motor_tx` (complete frames
accepted by the UART driver, **not** motor-side reception). Rates of `rx`,
`input`, `usb_done`, `fresh`, and `motor_tx` should average 500 Hz, with `force`
at 1000 Hz (or 500 Hz in that configuration). `max_gap_us` and
`gap_over_budget` expose jitter beyond 1/2 ms; they are lifetime values, not
just the last window. `max_work_us` is elapsed force/UART-write work, except
`usb_submit`, where it is received-sample age at submission. Initial warmup,
disconnection, missing source samples, and host stalls are included in lifetime
statistics. A stopped stream shows zero Hz even without another gap record.

The host probe reports read cadence, whose gaps also include OS scheduling and
HID buffering. It cannot prove wire latency or motor receipt. Validate UART TX/RX
cadence with a logic analyzer and motor-side counters; verify USB completion
and host receipt separately. S2/S3 compilation and native tests establish code
behavior, not a hardware timing guarantee. CPU capacity alone is insufficient
if interrupts, mutex contention, the motor producer, or the USB host stall.

### LCD debug switch

LCD is disabled by default (`ENABLE_LCD=0` in `src/config_board.h`). All normal,
serial and timing environments omit TFT_eSPI. With LCD disabled, no display
objects, sprite buffer, display initialization or refresh task are compiled in;
LCD pin setup is unnecessary. Timing diagnostics remain independently selectable.

For debug display, use `pio run -e esp32-s3-lcd` or
`pio run -e esp32-s2-lcd`. These opt-in environments define `ENABLE_LCD=1` and
include TFT_eSPI. Provide the actual board's TFT pin/driver setup before using
LCD. To enable LCD in another custom environment, add both `-DENABLE_LCD=1`
and `bodmer/TFT_eSPI@^2.5.43` to that environment. The LCD switch does not enable
motor output or change axis/input settings.

Validate the production 500–1000 Hz calculation and 500 Hz communication rates
with LCD disabled. LCD-enabled firmware is only for debugging.

### HID PID compatibility changes

Condition center and deadband now use the same nominal 10000 scale as coefficients and saturation in both the descriptor and firmware. Direction fields always use the angular coordinates advertised by the descriptor; Direction Enable selects a single directional condition block. Reconnect the USB device after flashing so the host reads the updated descriptor. Raw HID clients must send nominal condition parameters and angular directions.

Actuator disable mutes output while playback timers continue; Pause freezes playback. A trigger release does not stop a running effect. Sample Period holds condition output as well as waveform output. LCD remains disabled by default; enable it only for debugging.

Serial transport now supports backward-compatible variable frames, compact fixed frames, and mixed reception. See [serial transport APIs and performance validation](src/motor_protocol/README.md).

Classic CAN communication is available through the ESP32-S2/S3 TWAI controller, with default 500kbit/s and atomic force/position vectors for up to three axes. See [CAN protocol, configuration and validation](src/motor_protocol/CAN.md). UART remains the default.

Reusable communication core and UART/CAN host tools: [EmbeddedComm](lib/EmbeddedComm/README.md) and [host tools](lib/EmbeddedComm/host/README.md).
