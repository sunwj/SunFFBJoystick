# SunFFB Joystick Firmware — Agent Instructions

## Build / Flash / Monitor

| Action | Command |
|--------|---------|
| Build | `pio run -e esp32-s3` |
| Upload | `pio run -e esp32-s3 --target upload` |
| Serial monitor | `pio device monitor -e esp32-s3 --baud 115200` |
| Clean | `pio run -e esp32-s3 --target clean` |

No Make, npm, or other build systems — PlatformIO is the only entrypoint.

## Target Hardware

- **Board**: ESP32-S3 (`4d_systems_esp32s3_gen4_r8n16`) — 8MB PSRAM, 16MB flash
- **Framework**: Arduino + FreeRTOS (dual-core task scheduling)
- **USB**: TinyUSB, VID `0xFFFF`, PID `0x2010`, HID PID-compliant force feedback device

## Compile-Time Configuration

- **`src/constants.h`** is a forwarding header: it only `#include`s `config_ffb.h` and `config_board.h`. Do not edit it.
- **`src/config_ffb.h`** — `NUM_AXIS` (1/2/3) is the primary compile-time switch. It gates `#if`/`#elif` blocks across the entire codebase for axis count, HID descriptors, pin mappings, force direction formulas, and report struct sizes. Also defines `MAX_EFFECTS`, effect types (ET_*), HID PID report IDs (REPORT_ID_*, 1–18), and PID constants (`USB_MAX_MAGNITUDE` 10000, `USB_DURATION_INFINITE`, `USB_NO_TRIGGER_BUTTON`, `USB_AXIS_MAX_ABSOLUTE`, etc.). Changing `NUM_AXIS` requires a full rebuild.
- **`src/config_board.h`** — USB VID/PID (`0xFFFF`/`0x2010`), task periods (`FORCE_TASK_PERIOD_MS=1`, `SEND_FORCE_TASK_PERIOD_MS=2`), ADC scale, and speed/acceleration normalization scales (`DEFAULT_MAX_SPEED_SCALE`/`DEFAULT_MAX_ACCEL_SCALE`, both `1.0f`).
- **C++ standard**: Forced to `-std=gnu++2a` in `platformio.ini` (overrides Arduino default `gnu++11`).

## Source Layout

```
src/                          — All firmware source (SunFFB namespace)
  main.cpp                    — Entry point; FreeRTOS task creation
  constants.h                 — Forwarding header (config_ffb.h + config_board.h)
  config_ffb.h                — Compile-time config (NUM_AXIS, effect types, report IDs)
  config_board.h              — Board config (USB VID/PID, task periods, scales)
  ffb_report_types.h          — Packed HID report structs
  ffb_report_descriptor.h     — HID descriptor macros (NUM_AXIS-dependent)
  ffb_report_handler.*        — Effect block management, PID state machine
  ffb_force_calculator.*      — Real-time force computation (effects, conditions, envelopes)
  ffb_device_input.h          — Axis filtering, speed/acceleration derivation
  ffb_device_input.cpp        — Axis filtering and motion derivatives
  math_utils.h                — clamp, fast math stubs
  hid_pid.h                   — HID PID usage constants

src/communication/            — Serial protocol to external motor controller
  serial_link.h               — 0xAA + ID + length + payload + CRC8 framing
  serial_hal_arduino.h         — HardwareSerial adapter
  can_protocol.h / can_link.h — Classic CAN vector codec and nonblocking link
  can_hal_esp32.h              — ESP32-S2/S3 TWAI driver, filtering and bus-off recovery
  motor_payload.h             — Shared motor/position payload types
  host/                       — Python UART/CAN codecs and serial terminal

python_apis/                  — Host-side Python test client (hidapi-based)
  sunffb_hid.py               — Mirrors firmware report structs for host testing

tools/                        — Windows .exe test utilities (JoyTester, simFFB, etc.)

doc/                          — HID/PID spec PDFs, reference documentation
```

- **`include/`** is an empty placeholder (contains only a README).
- All code lives under `namespace SunFFB`.

## FreeRTOS Task Architecture

Tasks are pinned to specific cores:

| Task | Core | Priority | Purpose |
|------|------|----------|---------|
| `lcd_task` | App (core 1) | 1 | TFT display refresh (60ms) |
| `joystick_task` | App (core 1) | 2 | ADC read + axis filter (2ms) |
| `force_calculation_task` | App (core 1) | 3 | Effect computation (1ms) |
| `send_report_task` | Proto (core 0) | max-1 | USB HID report transmission |
| `send_force_task` | Proto (core 0) | max-1 | Serial output to motor driver |
| `receive_position_task` | Proto (core 0) | max-1 | Serial input from encoder board |

On ESP32-S2 (`esp32-s2*` environments), all tasks use core 0. Periodic USB/UART I/O uses priority 2, below force calculation (3). UART receive has a bounded batch and unconditional delay; missed force deadlines also yield. ADC reads explicitly use 12-bit resolution; third-axis ADC defaults to GPIO15 to reserve GPIO19/20 for native USB. The S2 reference board is `esp32-s2-saola-1`; LCD-enabled builds require external board-specific TFT pin setup.

Tasks communicate via FreeRTOS queues and mutexes. `hid_command_task` processes copied USB commands on core 0; USB callbacks wait for completion to preserve report ordering. `semaphoreFFBDeviceInput` and `semaphoreFFBReportHandler` protect shared state between force calculation and input/report tasks.

## Serial Protocol (Motor Controller)

- **Port**: `HardwareSerial(1)`, 115200 baud, GPIO4 (TX), GPIO5 (RX)
- **Format**: default variable `0xAA + ID + LEN + payload + CRC8`; fixed `0xAB + ID + payload + CRC8` (length from ID). CRC-8/MAXIM-DOW covers ID and payload, plus LEN in variable mode. `SERIAL_FRAMING_MODE=0/1/2` selects variable/fixed/mixed; mixed receives both and transmits variable by default. Both peers must agree on fixed lengths and axis count.
- **Outbound**: `int32_t forces[NUM_AXIS]` — computed force values
- **Inbound**: `uint16_t pos[NUM_AXIS]` — encoder position feedback
- Transport uses bounded nonblocking batch reads, incremental CRC, fixed-capacity buffers and per-call byte budgets. `receiveFrame` borrows the payload until the next receive; one RX and one TX owner are supported, multiple same-direction owners need external locking. See `src/communication/README.md` for APIs and benchmark limitations.
- See `src/communication/serial_link.h`; motor output and serial position input are opt-in (`ENABLE_MOTOR_OUTPUT` / `USE_SERIAL_POSITION`, both default 0). Position 0..65535 maps around center 32768 into the signed HID range.

## TFT_eSPI Dependency

LCD is disabled by default (`ENABLE_LCD=0`). Normal builds omit TFT_eSPI and all LCD code/resources. Use `esp32-s3-lcd` or `esp32-s2-lcd` for debug display. Timing diagnostics are independent.

When LCD is enabled, the `TFT_eSPI` library (via `lib_deps`) requires a board-specific `User_Setup_Select.h` to map LCD pins. This config is **not in the repository** — it must exist in the PlatformIO library cache or be provided externally. LCD-enabled builds require this external setup.

## Testing

Automated tests are available via `pio test -e native -e native-axis1 -e native-axis3` and `pio test -e native-sanitized`; Python tests use `python3 -m unittest discover -s python_apis`. Hardware validation is done via:
- `python_apis/sunffb_hid.py` — Python host client, mirrors firmware report structs
- `tools/*.exe` — Windows utilities (JoyTester, simFFB, etc.)
- Serial monitor for `SERIAL_PRINT` debug output (enable in source files)

## Key Gotchas

- Heavy use of `#pragma unroll` on `NUM_AXIS`-sized loops — the compiler must know `NUM_AXIS` at compile time.
- `loop()` deletes the main task (`vTaskDelete(nullptr)`); all work is in FreeRTOS tasks.
- HID report IDs are sequential (1–18) defined in `src/config_ffb.h`; changing them requires updating descriptor, handler, and Python client in sync.
- `python_apis/sunffb_hid.py` constants must match `src/config_ffb.h` exactly for correct communication.
- Condition-effect parameters: coefficient/saturation/deadband are host-sent nominal values (−10000..10000), normalized against `USB_MAX_MAGNITUDE` (10000); axis metrics (position/speed/acceleration) are normalized against each axis's `maxX` (maxSpeed = `USB_AXIS_MAX_ABSOLUTE` × `DEFAULT_MAX_SPEED_SCALE`). Speed/accel scales default to 1.0 (1 full-scale/s = max force), matching FFBTestTool's Clamp1 model.
- Friction is evaluated as a sign function of speed (with 2% threshold), not as a linear speed condition (see `ET_FRICTION` branch in `ffb_force_calculator.cpp`).
- Force direction vectors are stored as `u = −D` (force *from* direction D): the HID descriptor declares angular coordinates (Direction Enable selects condition mode, not coordinate format); polar angle in hundredths of a degree (0..36000) uses only `directions[0]`; spherical (3-axis) uses θ=`directions[0]`, φ=`directions[1]`.
- Effects are implicitly enabled when the host starts playback (`start_effect()` promotes `DEVICE_STATE_INIT` → `DEVICE_STATE_ACTIVE`); device reset (DeviceControl=4) frees effects, restores gain and enables actuators. Pause and actuator enable are independent; Continue never enables disabled actuators. `DEVICE_STATE_INIT`/`PAUSED`/`DISABLED` produce zero force.
- Effect `samplePeriod` (ms, 0=default) holds per-effect force samples, including condition forces, between sample boundaries; it is independent of `force_calculation_task` period.
- `c_cpp_properties.json` is auto-generated by PlatformIO — do not commit manual edits.

## Real-time validation

- Force calculation: `FORCE_TASK_PERIOD_MS` is 1 (1000 Hz) or 2 (500 Hz).
- Motor UART TX is 500 Hz; serial RX polling is 1 ms and processing is arrival-driven.
- HID IN polling is 1 ms; positions have priority over PID state on the shared endpoint.
- `esp32-s2-timing`/`esp32-s3-timing` enable UART0 rate logs and motor output/serial input.
- `python_apis/check_update_rate.py` measures read-only host report cadence; it does not enable motors.
- Never claim timing compliance from compilation alone. Use `usb_done` and `fresh`, motor-side reception, and wire measurements under worst-case load.

## CAN Transport

- `MOTOR_TRANSPORT=0` is UART (default); `=1` selects ESP32 TWAI. `USE_CAN_POSITION=1` supplies encoder feedback; otherwise ADC remains the input source. UART/CAN position sources are mutually exclusive.
- Defaults: 500kbit/s, TX GPIO4, RX GPIO5, standard force/position/heartbeat IDs 0x201/0x181/0x701. Requires an external CAN transceiver. Config values are overridable in `config_board.h` / build flags.
- Custom protocol uses version+axis-count, 8-bit sequence and int16 forces / uint16 positions, all little-endian. Three-axis vectors fit one classic 8-byte CAN frame. Do not treat this as a CANopen or vendor protocol.
- TX queue is disabled and API calls use zero wait. Hardware retransmission is enabled by default (`CAN_SINGLE_SHOT=0`); opt-in single-shot may lose frames to arbitration. RX is bounded to 8 frames per 1ms and handles nonblocking bus-off recovery.
- `motor_tx` measures acceptance, not wire completion; driver alerts can coalesce. Validate actual 500Hz on the CAN/motor side and USB host.
- Build environments: `esp32-s2-can`, `esp32-s3-can`, `esp32-s2-can-axis1`, `esp32-s3-can-axis3`, `esp32-s2-can-timing`, `esp32-s2-can-adc`. Protocol and wiring details: `src/communication/CAN.md`.
