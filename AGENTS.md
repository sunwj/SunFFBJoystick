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

src/motor_protocol/           — Project motor protocol adapters
  serial_link.h               — 0xAA + ID + length + payload + CRC8 framing
  can_protocol.h / can_link.h — Classic CAN vector codec and nonblocking link
  motor_payload.h             — Shared motor/position payload types

lib/EmbeddedComm/             — Reusable communication library
  src/EmbeddedComm/           — Serial/CAN cores and hardware adapters
  host/                      — Python UART/CAN codecs, serial GUI and CAN CLI

python_apis/                  — Host-side Python test client (hidapi-based)
  sunffb_hid.py               — Mirrors firmware report structs for host testing

tools/                        — Windows .exe test utilities (JoyTester, simFFB, etc.)

doc/                          — HID/PID spec PDFs, reference documentation
```

- **`include/tft_setup.h`** supplies the repository's ESP32-S3 LCD pin/driver setup.
- Firmware and motor-protocol adapters use `namespace SunFFB`; the independent communication library uses `namespace EmbeddedComm`.

## FreeRTOS Task Architecture

Tasks are pinned to specific cores:

| Task | Core | Priority | Purpose |
|------|------|----------|---------|
| `lcd_task` | App (core 1) | 1 | TFT display refresh (60ms) |
| `joystick_task` | App (core 1) | 2 | ADC read + axis filter (2ms) |
| `force_calculation_task` | App (core 1) | 3 | Effect computation (1ms) |
| `send_report_task` | Proto (core 0) | 2 | USB HID report transmission |
| `send_force_task` | Proto (core 0) | 2 | Serial output to motor driver |
| `receive_position_task` | Proto (core 0) | 2 | Serial input from encoder board |

On ESP32-S2 (`esp32-s2*` environments), all tasks use core 0. Periodic USB/UART I/O uses priority 2, below force calculation (3). UART receive has a bounded batch and unconditional delay; missed force deadlines also yield. ADC reads explicitly use 12-bit resolution; third-axis ADC defaults to GPIO15 to reserve GPIO19/20 for native USB. The S2 reference board is `esp32-s2-saola-1`; LCD-enabled builds require external board-specific TFT pin setup.

Tasks communicate via FreeRTOS queues and mutexes. USB callbacks dispatch bounded commands directly under the effect-handler mutex, preserving SET/GET ordering without a same-core worker round trip. This latency reduction is under hardware validation, not a confirmed USB stability fix. `semaphoreFFBDeviceInput` and `semaphoreFFBReportHandler` protect shared state between force calculation and input/report tasks.

## Serial Protocol (Motor Controller)

- **Port**: `HardwareSerial(1)`, 115200 baud, GPIO4 (TX), GPIO5 (RX)
- **Format**: default variable `0xAA + ID + LEN + payload + CRC8`; fixed `0xAB + ID + payload + CRC8` (length from ID). CRC-8/MAXIM-DOW covers ID and payload, plus LEN in variable mode. `SERIAL_FRAMING_MODE=0/1/2` selects variable/fixed/mixed; mixed receives both and transmits variable by default. Both peers must agree on fixed lengths and axis count.
- **Outbound**: `int32_t forces[NUM_AXIS]` — computed force values
- **Inbound**: `uint16_t pos[NUM_AXIS]` — encoder position feedback
- Transport uses bounded nonblocking batch reads, incremental CRC, fixed-capacity buffers and per-call byte budgets. `receiveFrame` borrows the payload until the next receive; one RX and one TX owner are supported, multiple same-direction owners need external locking. See `src/motor_protocol/README.md` for APIs and benchmark limitations.
- See `src/motor_protocol/serial_link.h`; motor output and serial position input are opt-in (`ENABLE_MOTOR_OUTPUT` / `USE_SERIAL_POSITION`, both default 0). Position 0..65535 maps around center 32768 into the signed HID range.

## TFT_eSPI Dependency

LCD is disabled by default (`ENABLE_LCD=0`). Normal builds omit TFT_eSPI and all LCD code/resources. Use `esp32-s3-lcd` or `esp32-s2-lcd` for debug display. Timing diagnostics are independent.

`esp32-s3-lcd` force-includes `include/tft_setup.h` for both application and TFT_eSPI compilation. Its `USER_SETUP_LOADED` guard prevents a competing library-cache setup. The S2 LCD environment still requires external board-specific setup.

## Hardware-in-the-loop Validation

- `esp32-s3-hil` extends the S3 LCD environment, enables UART motor output and serial position input, and overrides ESP RX to GPIO48 and TX to GPIO45. CDC is disabled in production, LCD and HIL environments after failed repair attempts; `ENABLE_USB_CDC` defaults to 0. Default production UART pins remain RX5/TX4.
- Connect adapter TX to GPIO48, adapter RX to GPIO45, and common ground using 3.3 V TTL. Disconnect motors before flashing this environment. GPIO45 is a strapping pin; do not externally drive it during boot.
- Run `python python_apis/validate_hardware.py --port COM8 --motors-disconnected`. Do not pass `--console` with CDC disabled. Port numbers must be re-enumerated on each machine. Future uploads require manual BOOT/RESET because no CDC upload port is exposed; enumerate the ROM port before flashing.
- The runner resets the effect pool, streams simulated positions, reads real UART force frames and USB joystick reports, and stops/disables/frees effects on exit when USB remains reachable. It does not verify physical motors, ADC, buttons, CAN, visual LCD correctness or wire-level timing. Keep motors disconnected after testing; firmware reset re-enables actuators.
- On Windows, Feature SET buffers must be padded to the collection's maximum Feature report size (five bytes including ID). A two-byte Create New Effect buffer failed on hardware, while the padded report succeeded. Keep firmware payload validation exact; host padding is a Windows API requirement, not a new wire layout.
- Initial hardware runs exposed intermittent USB input stalls, control-report failures and a task-watchdog reset under simultaneous traffic. Do not treat passing effect checks or approximately 500 Hz host UART reception as overall stability or worst-case timing compliance; see `doc/hardware-validation.md`.

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
- Force direction vectors are stored as `u = −D` (force *from* direction D): the HID descriptor declares angular coordinates (Direction Enable is not a coordinate-format selector; condition mode also depends on received condition blocks, as documented below); polar angle in hundredths of a degree (0..36000) uses only `directions[0]`; spherical (3-axis) uses θ=`directions[0]`, φ=`directions[1]`.
- Effects are implicitly enabled when the host starts playback (`start_effect()` promotes `DEVICE_STATE_INIT` → `DEVICE_STATE_ACTIVE`); device reset (DeviceControl=4) frees effects, restores gain and enables actuators. Pause and actuator enable are independent; Continue never enables disabled actuators. `DEVICE_STATE_INIT`/`PAUSED`/`DISABLED` produce zero force.
- Effect `samplePeriod` (ms, 0=default) holds per-effect force samples, including condition forces, between sample boundaries; it is independent of `force_calculation_task` period.
- `c_cpp_properties.json` is auto-generated by PlatformIO — do not commit manual edits.

## Condition-force Compatibility Exception

- Preserve the condition-force behavior of known-working commit `08252625a9e07d9faf9650f9111a9cf3d54bba19`, reported by the user as working on hardware. Commit `30f2eef` removed its per-axis branch when `DIRECTION_ENABLE` was set; this can discard the Y condition block and produce X-only spring, damper, inertia and friction when the direction is along X.
- With `DIRECTION_ENABLE` set and `conditionBlockFlags > 1` (a condition slot beyond slot zero has been received), calculate each received axis condition independently using its own `typeSpecificData[axis].conditionData` and normalized axis metric. Do not ignore Y/Z blocks or project these independent conditions onto a single direction. Skip unreceived slots; do not duplicate X parameters into them. This directed per-axis compatibility path does not require the individual axis-enable bits.
- With `DIRECTION_ENABLE` set and only slot zero received, retain the single-condition direction-projection model. Without `DIRECTION_ENABLE`, retain independent conditions gated by the axis-enable bits and received-slot flags.
- Envelope storage remains independent from type-specific condition slots. Envelope reports must not overwrite condition parameters or alter condition forces. Restoring compatibility does not require reverting this storage separation.
- This is an intentional, user-authorized compatibility exception and may differ from HID PID/DirectInput specification wording or its interpretation. Preserve the documented behavior even when a spec-only review suggests otherwise; do not remove it without explicit user approval and host/hardware regression evidence. Do not describe the exception as proof of strict specification compliance.
- Regression tests must cover all four condition types with `DIRECTION_ENABLE`, X/Y condition blocks and a direction along X, asserting independent nonzero X/Y output; also retain single-block projection and envelope-isolation coverage. A test that requires every directed condition to ignore the second block encodes the regression, not the compatibility requirement. Native test success is not a substitute for hardware verification of the repaired firmware.

## Real-time validation

- Force calculation: `FORCE_TASK_PERIOD_MS` is 1 (1000 Hz) or 2 (500 Hz).
- Motor UART TX is 500 Hz; serial RX polling is 1 ms and processing is arrival-driven.
- HID IN polling is 1 ms; positions have priority over PID state on the shared endpoint.
- `esp32-s2-timing`/`esp32-s3-timing` enable UART0 rate logs and motor output/serial input.
- `python_apis/check_update_rate.py` measures read-only host report cadence; it does not enable motors.
- Never claim timing compliance from compilation alone. Use `usb_done` and `fresh`, motor-side reception, and wire measurements under worst-case load.
- Periodic I/O uses priority 2 on S3 as well as S2. Force releases use esp_timer task notifications; obsolete releases are coalesced, not replayed. Count both release-to-completion deadline misses and skipped releases, including across timing windows.
- Never hold the effect-handler mutex while submitting USB IN reports. HID IN submission is deferred to the TinyUSB service task; completion counters come from the actual HID completion callback, not successful queue submission.
- Arduino 2.0.17 bundles TinyUSB 0.16 with shared interrupt/control HID buffers. `usb_hid_control_compat.cpp` and linker wrapping isolate control storage for this version; newer versions delegate to their driver. Preserve report ordering and the actual Output/Feature type.
- CDC diagnostics have a bounded queue and one capacity-limited writer. A full console must drop or defer diagnostics without blocking force computation or spinning on CDC's full-FIFO write loop.
- CDC FIFO write/flush operations are deferred to TinyUSB's service context with persistent storage owned by one console task; do not reintroduce concurrent application-task endpoint submissions. Full LCD/CDC hardware tests still fail at requested 500 Hz. The USB service continues executing deferred callbacks after host disappearance; do not claim a service deadlock or a successful CDC repair from these tests.
- Both `esp32-s3-hil-minimal` (no LCD/CDC) and `esp32-s3-hil-lcd-only` (LCD, no CDC) passed 34 checks and a 60-second requested-500-Hz soak. These environments require manual BOOT/RESET to regain a flashing port. CDC-free success is not full LCD/CDC stability or deadline compliance.
- Final `esp32-s3-hil` with CDC disabled passed 34 checks and a 60-second requested-500-Hz soak (UART receipt 500.005 Hz, 29428 USB reports). Stop/Disable/Free succeeded; 248 subsequently observed UART force frames were all zero. Do not extrapolate this short single-effect soak to worst-case deadlines or long-term endurance.
- Experimental FIFO-register relocation and endpoint/FIFO alignment both failed hardware validation. Their source, linker wrapper and tests were removed; do not reinstate them as verified repairs. CDC is disabled per the user's fallback authorization. Preserve the failure evidence in `doc/hardware-validation.md`; the common final cause of HID/CDC failure is still unresolved.
- HIL-only UART health uses variable frame ID 0x7E and five little-endian uint32 values (uptime ms, reset reason, USB mounted, USB suspended, free heap). Continuous UART uptime after a USB error is evidence against a reboot in that interval; a mounted flag alone does not prove a working host USB connection. High-rate USB stability remains unresolved; see `doc/hardware-validation.md`.
- The direct-dispatch candidate passed 36 checks and a 30-minute requested-500-Hz full-pool/LCD stress run with CDC off. Maximum observed force latency was 764 us; no completed-job misses or additional skipped releases occurred (seven lifetime skips were already present). This is bounded validation, not proof of a sole root cause, multi-day stability or wire-level compliance. An unsuccessful optional endpoint-status diagnostic was removed afterward; the cleaned source is build-verified but not the exact flashed/stress-tested binary. Preserve the capture and qualifications in `doc/hardware-validation.md`.
- The cleaned `25cdfd1` firmware was subsequently flashed and exercised through the actual `E:/github_projects/py_directinput_ffb` library. Final DirectInput capture passed 31/32 checks, including all eleven effects and three-minute rotating full-pool stress with live updates; no new deadline misses/skips occurred. The boot-wide budget check failed because the first UART sample already contained one 7586 us miss and seven skips; their pre-capture origin remains unknown. Do not claim whole-boot timing compliance or erase this history. DirectInput state polls are not fresh USB report counts. Keep motors disconnected, preserve every capture, and read `doc/hardware-validation.md` for runner corrections and limitations.
- The follow-up LCD row-group refresh and force-task startup gate build passed S3 HIL/LCD/production and S2 configurations, then passed all 32 actual-DirectInput hardware checks over 601 seconds of full-pool stress (`doc/hil-deadline-fixed-10min-results.json`). Boot-wide maximum release-to-completion was 488 us / 1000 us; completed-job misses stayed zero and the skipped-release count stayed at seven from the first UART sample through the end. The previously recorded 7586 us event did not recur, so the change correlates with improvement but does not prove its original root cause. The new worst-event record decomposes one 488 us active-effect cycle as release→start 34 us, lock wait 11 us, force calculation 336 us, and post-calculation→completion 107 us; LCD stage is sampled at record time and does not establish causality. Preserve the old failure capture and qualifications in `doc/hardware-validation.md`.

## CAN Transport

- `MOTOR_TRANSPORT=0` is UART (default); `=1` selects ESP32 TWAI. `USE_CAN_POSITION=1` supplies encoder feedback; otherwise ADC remains the input source. UART/CAN position sources are mutually exclusive.
- Defaults: 500kbit/s, TX GPIO4, RX GPIO5, standard force/position/heartbeat IDs 0x201/0x181/0x701. Requires an external CAN transceiver. Config values are overridable in `config_board.h` / build flags.
- Custom protocol uses version+axis-count, 8-bit sequence and int16 forces / uint16 positions, all little-endian. Three-axis vectors fit one classic 8-byte CAN frame. Do not treat this as a CANopen or vendor protocol.
- TX queue is disabled and API calls use zero wait. Hardware retransmission is enabled by default (`CAN_SINGLE_SHOT=0`); opt-in single-shot may lose frames to arbitration. RX is bounded to 8 frames per 1ms and handles nonblocking bus-off recovery.
- `motor_tx` measures acceptance, not wire completion; driver alerts can coalesce. Validate actual 500Hz on the CAN/motor side and USB host.
- Build environments: `esp32-s2-can`, `esp32-s3-can`, `esp32-s2-can-axis1`, `esp32-s3-can-axis3`, `esp32-s2-can-timing`, `esp32-s2-can-adc`. Protocol and wiring details: `src/motor_protocol/CAN.md`.

## C/C++ Code Style

- Follow `.clang-format`: Allman braces, with code-block opening `{` on its own line.
- Expand short functions, if/else, loops and case bodies; do not compress multiple statements onto one line. Use 4-space indentation and wrap long expressions for readability.
- Separate function definitions with a blank line. In classes, group related function declarations and data members by purpose, and separate the groups with a blank line.
- Inside functions, separate logical stages (validation, preparation, processing, state updates, and output) with a blank line where it helps reading. Keep related statements together; do not add a blank line after every statement. Keep explanatory comments attached to their block.
- Preserve each file's LF/CRLF convention. Formatting must not change logic, wire layouts or report descriptors. Do not format the forwarding `src/constants.h`.

## Reusable Communication Library

- `lib/EmbeddedComm` is a standalone header-only C++17 library, with PlatformIO and Arduino metadata. Core headers must not depend on firmware configuration, FreeRTOS, USB, axis count, or motor message types.
- `SerialLink` handles generic fixed/variable/mixed framing; fixed lengths come from an application policy. `CanLink` handles raw classic CAN frames. Arduino UART and ESP-IDF 4.x TWAI are separate hardware adapters.
- Motor payloads, message IDs, sequence/deduplication rules and force scaling stay in `src/motor_protocol`, using `MotorSerialLink` and `MotorCANLink`. Old FFB names are compatibility aliases.
- `pio test -e native-comm` compiles independent library tests with C++17 and no firmware sources. Normal native environments exclude this standalone suite.
- Porting instructions and HAL contracts: `lib/EmbeddedComm/README.md`.
