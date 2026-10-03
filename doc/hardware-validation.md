# ESP32-S3 hardware-in-the-loop validation

Date: 2026-10-03. Firmware base: `947c552`, plus the uncommitted HIL
environment and overridable UART pin definitions described here.

## Equipment and firmware

- ESP32-S3 with ST7735 LCD enabled; native USB HID and manually registered CDC.
- Running USB CDC: COM7; CH340 USB UART: COM8; ROM upload port: COM5.
- ESP RX GPIO48 connects to adapter TX; ESP TX GPIO45 connects to adapter RX.
- Common ground; motors physically disconnected (confirmed by the user).
- UART: 115200 baud, 8N1, variable framing, CRC-8/MAXIM-DOW.
- Adapter TTL voltage was requested to be 3.3 V but was not independently measured.
- `esp32-s3-hil` enables motor-protocol output, serial position input and timing
  diagnostics without changing production defaults. Do not connect motors while
  this firmware is installed. The firmware's device reset enables actuators.

Build/upload command (select the actually enumerated port):

```powershell
pio run -e esp32-s3-hil --target upload --upload-port COM5
```

The first upload attempt on running CDC COM7 switched the device into ROM mode
and returned a port error. Retrying COM5 succeeded, verified flash hashes, and
the board returned to COM7 automatically. No physical reset was required.

## Completed functional run

```powershell
python python_apis/validate_hardware.py --port COM8 --motors-disconnected --position-hz 50 --soak 30
```

Result: **34 of 34 checks passed**. This is a functional run with 50 Hz requested
simulated encoder input, not a 500 Hz input or worst-case stress certification.

Checks use actual USB commands and actual UART force output, not a host-only
force model or firmware test injection:

- UART positions round-trip through USB for both signs and signed-axis limits.
- Fifteen effect slots allocate; pool-full status and freed-slot reuse work.
- Constant force directions 0/90/180/270 degrees produce the expected axes/signs.
- Effect/device gains, summed-force clamping, pause/continue, disable/enable,
  Continue while disabled, and Stop All produce expected output.
- Square, sine, triangle, sawtooth-up/down cover both extrema, bounded offset
  magnitude and cross-axis isolation. This range check is not a point-by-point
  independent proof of waveform phase/shape.
- Ramp progression/expiration, constant attack/fade envelope, start delay and
  100 ms ramp sample holding pass host-time-window checks.
- Spring, damper, inertia and friction use directed X/Y condition blocks with
  distinct saturation values. All four produce simultaneous nonzero X/Y force,
  including after an envelope is sent. Observed absolute peaks: X=1000, Y=2000.
  Spring uses static displacement; other conditions use sinusoidal simulated
  movement. This confirms the compatibility regression repair, not strict PID
  conformance or a full independent derivative-model comparison.
- Corrupt position CRCs are rejected; the next valid stream recovers.
- Thirty-second constant-force soak with moving X/Y feedback: 499.975 UART
  frames/s received by the host, 1444 USB position reports; zero host parser CRC
  or length errors. Force remains (-4000, 0). No disconnection during this run.
- Exit cleanup sent Stop All, Disable Actuators and Free All.

Python regression suite: **72 tests passed**, including new Windows Feature
padding and non-Windows exact-length tests. HIL compilation/upload succeeded.

## Problems found: overall validation is NOT passed

1. **Windows short Feature SET:** a two-byte Create New Effect buffer failed
   with `HidD_SetFeature` error 0x1F. A five-byte buffer succeeded and returned
   successful allocation. The host client now pads Windows Feature SET to the
   descriptor collection's maximum FeatureReportByteLength (ID plus four-byte
   payload). This does not change the packed firmware structures.
2. **Intermittent USB input loss:** after an initial aborted run, UART forces
   continued at approximately 500 Hz but no USB positions were received despite
   serial positions being accepted. Fresh boot restored input. The exact cause
   has not been established; do not attribute this solely to Feature padding.
3. **Watchdog/reset under repeated traffic:** the CDC boot log reported
   `reset_reason=6` (ESP_RST_TASK_WDT). Runs requesting 250 Hz positions,
   measured approximately 202-211 Hz by firmware during captured windows,
   intermittently aborted with USB write cancellation (0x3E3). Another run
   returned report ID 2 where Block Load Feature ID 17 was requested. Failures
   occurred both with and without host CDC logging. Their common root cause
   remains unresolved; high-rate stability must be retested after diagnosis.
4. **Deadline compliance unproven:** captured timing maxima included force
   gaps of 8013 us and force work/lock time of 7792 us, exceeding the 1 ms/800 us
   targets. Average force rate near 1000 Hz does not erase missed deadlines.
   Historical counter maxima can include setup, command processing and earlier
   test stages; they do not isolate the root cause. USB freshness was measured
   only at the simulated input rate, not 500 Hz.

The test runner fails on failed assertions and USB errors; it does not silently
retry or downgrade a stress failure into a pass. `--position-hz 50` is a separate
diagnostic run, not a replacement result for the default 250 Hz workload.

## Scope limits and next investigation

### Final fallback decision (user-authorized)

The second FIFO experiment aligned DIEPCTL's FIFO selector with the endpoint-
indexed configuration and ISR writes confirmed in the bundled binary. It
still failed at requested 500 Hz during effect allocation (Windows Feature
SET 0x1F); only three checks completed. The independent UART showed no reboot
and the USB service continued running. Raw evidence:
`doc/hil-fifo-aligned-500hz-results.json`. Neither FIFO experiment is a verified
complete repair or proof of the final cause of HID/CDC failure.

Per the user's instruction to try repair and disable CDC if unsuccessful,
CDC is now disabled in all shipped environments, including `esp32-s3-lcd`
and `esp32-s3-hil`; `config_board.h` explicitly defaults ENABLE_USB_CDC to 0.
LCD, HID and the independent motor UART remain enabled in HIL. Both experimental
FIFO patches and their linker flag/tests were removed. Earlier sections retain
the investigation history and should not be read as the current implementation.
Future flashing requires manual BOOT/RESET because the CDC port no longer exists.
Production motor output remains opt-in; HIL firmware is not a production pin map.

The final CDC-off HIL firmware was flashed and passed all 34 checks plus a
60.001-second requested-500-Hz soak. UART receipt was 500.005 Hz, USB received
29428 position reports, and UART CRC/length errors were zero. Raw evidence:
`doc/hil-cdc-off-final-500hz-results.json`. No USB disappearance occurred and
cleanup successfully sent Stop All, Disable and Free All. A subsequent independent
UART read received 248 force frames, all zero. Final regression suites passed
203 native C++ tests and 72 Python tests; S3, S2 and S3 LCD builds succeeded.
This short workload cannot establish worst-case real-time or motor safety
compliance. CDC-free force deadline logs are not captured by the current UART
health frames.

### LCD/CDC isolation follow-up

The LCD-only build has now passed all 34 checks and a 60-second requested
500 Hz soak: UART receipt 500.008 Hz, 29190 USB position reports, zero UART
CRC/length errors. Raw results: `doc/hil-lcd-only-500hz-results.json`.
After Stop/Disable/Free cleanup, an independent 0.5-second UART read received
247 force frames, all (0, 0). This eliminates LCD removal as a requirement
for this short passing workload, but does not prove worst-case deadlines.

### Legacy TX FIFO indexing defect: repair awaiting hardware validation

Update: the FIFO-register relocation build was flashed and **failed** after
27 passing checks, during Block Load Feature GET (Windows 0x1F). It did not
complete the soak. `doc/hil-fifo-fixed-500hz-results.json` captures 17 deadline
windows: maximum elapsed 375 us, missed=0, skipped=0. UART uptime remained
continuous. Unlike earlier disappearance, Windows still listed HID/CDC; a
new read-only HID handle could fetch Pool, but no joystick input was received.
This independent probe is diagnostic, not a retry that makes the failed run pass.
Stop/Disable/Free through a new handle succeeded, followed by 248 UART force
frames, all zero. No reset is presently required for safety.

The upstream 0.16 ISR also passes the endpoint number to transmit_packet's
FIFO selector. Relocating DIEPTXF alone may therefore be incomplete: compare
the bundled ISR before choosing a consistent endpoint/FIFO repair. The current
register-relocation patch is not a verified complete fix.

In the bundled S3 binary, `dcd_edpt_open` chooses a TX FIFO independently of
the endpoint number (DIEPCTL.TXFNUM), but configures DIEPTXF using the endpoint
number. This mismatch is also visible in the upstream
[TinyUSB 0.16 driver](https://github.com/hathach/tinyusb/blob/0.16.0/src/portable/espressif/esp32sx/dcd_esp32sx.c).
Arduino's CDC reservation makes HID use IN endpoint 3, while the first allocated
TX FIFO is 1. HID alone gets endpoint/FIFO 1, masking the defect. The current
firmware's `dcd_edpt_open` disassembly confirms the final FIFO-register address
uses the endpoint register rather than the separately chosen FIFO register.

`usb_fifo_compat.cpp` wraps endpoint opening for the legacy 0.16 S2/S3 driver.
It snapshots the unrelated FIFO register before the stock driver overwrites it,
restores that register, and places the new layout in the FIFO actually selected
by DIEPCTL. Endpoint addresses, HID reports, CDC descriptors and allocation
policy remain unchanged; matching endpoint/FIFO numbers are left alone. Newer
TinyUSB versions delegate to the stock driver. Native regression tests cover
misaligned indices, preservation, matching indices and invalid bounds.
The full LCD/CDC HIL firmware compiled successfully with this repair; hardware
validation is pending. Do not yet claim the observed disconnect is resolved.

The full LCD/CDC deferred-write build was flashed and tested. It **still
failed** under requested 500 Hz (raw records:
`doc/hil-cdc-deferred-500hz-results.json` and
`doc/hil-usb-progress-500hz-results.json`). Do not label CDC deferral a verified
USB-disconnection repair. In the instrumented run, after failure the independent
UART recorded continuous uptime 7191–12191 ms, console callback count increasing
1045–6045, console stage=4 (returned from write/flush), HID submission count frozen
at 1318, commands started=completed=31, USB service minimum free stack=2944.
The service continued processing deferred console work; this is not a service
deadlock or observed reboot. It does not rule out controller/endpoint or host
failure. Captured deadline windows had maximum elapsed=81 us, missed=0, skipped=0;
the failed run is not a worst-case endurance/timing pass.

HIL-only variable UART frame 0x7D carries eight uint32 fields: uptime ms,
console callback stage, console callbacks, HID submit callbacks, HID commands
started, HID commands completed, USB service task state, minimum free service
stack. Stage 1/2/3/4 means entry/before FIFO write/before flush/finished. The
callback counters are not USB wire completions.

`esp32-s3-hil-minimal` removes LCD and CDC together, retaining two-axis UART
input/output and the HIL health frames. At requested 500 Hz it passed all
34 functional checks and a 60-second soak: UART host receipt 499.999 Hz,
26898 USB position reports, and zero UART CRC/length errors. Raw results:
`doc/hil-minimal-500hz-results.json`. This is an isolation result, not a fix
verification for the LCD/CDC build; no force deadline logs were captured.
Cleanup sent Stop All, Disable and Free All successfully.

`esp32-s3-hil-lcd-only` retains LCD without CDC for a further comparison;
it has compiled but has not been flashed or hardware tested yet. Both
CDC-free diagnostic environments require manual BOOT/RESET for subsequent
flashing because they expose no CDC upload port.

CDC FIFO writes/flushes now use a persistent single-writer slot deferred to
the TinyUSB service context. Each callback writes at most the available
capacity and never spins waiting for the host. The full LCD/CDC HIL build
compiled successfully after this change, but the subsequent hardware tests
above failed.
Minimal-build success narrows the investigation; it does not by itself
distinguish LCD load, CDC interface composition and CDC traffic.

### Follow-up: high-rate diagnosis (2026-10-03)

The overall high-rate validation remains **FAILED**. The latest raw result is
`doc/hil-500hz-results.json`; do not interpret the short pre-failure windows as
a successful 60-second soak.

Implemented changes isolate HID control report storage on TinyUSB 0.16,
release the effect-handler mutex before USB submission, submit HID IN from
the TinyUSB service context, and serialize bounded CDC diagnostic writes.
Periodic I/O now uses priority 2 and missed periodic releases yield. Force
releases use an esp_timer notification with obsolete releases coalesced;
deadline diagnostics separately count late completions and skipped releases.
The buffer issue is visible in the upstream
[0.16 driver](https://github.com/hathach/tinyusb/blob/0.16.0/src/class/hid/hid_device.c)
and its isolated control buffer in
[0.17](https://github.com/hathach/tinyusb/blob/0.17.0/src/class/hid/hid_device.c).
These repairs do not establish that every USB fault has the same cause.

The new HIL-only UART diagnostic frame (variable framing, ID 0x7E) contains
five little-endian uint32 fields: uptime milliseconds, reset reason, USB
mounted, USB suspended, free heap. It is sent once per second by the existing
single UART TX owner. Production builds do not enable this frame.

At requested 500 Hz, the latest run failed during Device Reset with Windows
HID write cancellation 0x3E3. Independent UART health remained continuous
from uptime 10094 through 17094 ms, with mounted=1, suspended=0 and stable
heap=327048. No MCU reboot occurred during this observed failure interval.
The firmware's mounted flag is not proof that Windows or the physical USB
link remained operational. Windows no longer listed the device after failure.
Captured force windows measured 1000 Hz; one captured deadline window had
maximum wake=53 us, lock wait=36 us, release-to-completion=80 us, missed=0,
skipped=0. USB completion was 432–483 Hz, not a proven 500 Hz guarantee.
Cleanup could not send Stop/Disable because USB was unavailable; keep motors
disconnected and reset before further testing. Watchdog endurance and
worst-case deadline compliance remain unverified.

Native regression suites passed 203 cases including the skipped-release
assertions; Python passed 72 tests and the production S3 build succeeded.
Continue by isolating CDC/LCD and inspecting USB service/endpoint
progress, rather than classifying this USB disappearance as a watchdog reset.

No actual motor, CAN bus, ADC conversion, physical trigger/button, visual LCD
contents, logic-analyzer timing, exact USB wire completion or long-duration
thermal endurance was validated. LCD code ran concurrently; that does not prove
the displayed pixels are correct. Serial/USB timestamps are host batch receipt
times, not wire-level timestamps. The functional soak contains one active
constant effect, not the maximum active-effect workload.

Next: capture UART0 panic/backtrace and per-task stack/idle information while
reproducing the default workload; inspect core-0 USB scheduling and callback/
handler-lock interactions. Do not disable the watchdog or claim the problem is
fixed from low-rate success. Repeat default/high-rate and worst-case tests after
a root-cause repair. Keep motors disconnected in the meantime.
# Extended stability and full-pool deadlines

## Direct-dispatch repair candidate

USB callbacks now execute bounded report dispatch directly under the handler
mutex, instead of copying to a same-priority worker and blocking the USB service
until that worker returns. Packed report validation, local copies and SET/GET
ordering are unchanged. This removes a scheduling round trip; it does not prove
that the worker was the sole cause of every historical USB failure.

`doc/hil-direct-dispatch-500hz-results.json` records 36/36 checks passing and
1801.202 seconds of rotating full-pool stress with LCD enabled, CDC disabled,
motors disconnected and requested 500 Hz simulated positions. Fifteen effects
were reallocated each minute across sine, dual-axis inertia and mixed workloads.
Host received 898169 force frames and 704688 joystick reports. There were no
observed USB errors or MCU resets; all 1836 health records reported heap 328816.
Maximum boot-wide release-to-completion latency was 764 us against a 1000 us
budget, wake latency 188 us and lock wait 164 us. No completed-job deadline misses
were recorded. Seven skipped releases were already present in the first captured
record and did not increase throughout the run; do not report zero lifetime skips.
After cleanup, 497 additional UART force frames were all zero.

An experimental read-only endpoint-status record did not fit the diagnostic UART
burst and produced no host records. Its transmitter/decoder were removed after
this run; it is not evidence of controller state. The currently flashed candidate
still contains that unsuccessful best-effort diagnostic attempt. The cleaned
source removes it without changing USB command dispatch, but that cleaned binary
has not been flashed and stress-tested. Preserve this distinction when reporting
the tested firmware. Native tests passed 200/200 across 1/2/3 axes and S2/S3/HIL
builds passed. This 30-minute result is not a worst-case execution-time proof,
wire-level timing measurement, or a multi-day endurance qualification.

Run `python python_apis/validate_hardware.py --port COM8 --motors-disconnected --position-hz 500 --soak 10 --endurance 1800 --report-json doc/hil-endurance-500hz-results.json` with `esp32-s3-hil`.
The test rotates fifteen sine, dual-axis inertia, and mixed effects every minute,
with envelopes, LCD enabled and serial position feedback. Host sample batches are
bounded to one second. Motors must remain disconnected, including after failure:
USB loss can prevent stop/disable cleanup.

HIL-only UART record 0x7C contains nine little-endian uint32 values: uptime_ms,
lifetime force count, maximum wake/lock/release-to-completion latency in microseconds,
missed deadlines, skipped releases, deadline budget in microseconds, and force-task
stack high-water mark. These counters are non-consuming and survive lost diagnostic
records; reset is detected separately through uptime continuity. Console windows
cannot erase UART deadline evidence. Maxima cover the entire boot, not only the
current workload. Host receipt cadence is not a wire-time measurement.

The first extended-validation attempt stopped during effect-pool allocation with
Windows Feature-report error 0x1F, before endurance began. Independent UART showed
no MCU reset, USB service still running, and stable heap. The captured lifetime
maximum release-to-completion latency was 615 us against a 1000 us budget, with
zero completed jobs over budget but ten skipped releases (four added during the
test). This is **not** a long-term stability or maximum-load pass; disabling CDC
alone has not eliminated the USB fault. Evidence is in
`doc/hil-endurance-500hz-results.json`.

After manual reset, the second attempt passed all 34 functional checks and
completed approximately three minutes of rotating full-pool stress. USB failed
again during the next effect-pool configuration, this time on Set Effect Output
with error 0x1F. The maximum observed latency was 704 us, completed-job deadline
misses remained zero. Skipped releases stayed at six during the three completed
stress phases, but increased to ten around the USB failure (four additional
skipped cycles). UART uptime continued and heap remained stable. The planned 30-minute test
did not complete; repeated allocation/control traffic remains an unresolved USB
stability blocker. See `doc/hil-endurance-500hz-retest-results.json`. Neither
capture establishes a worst-case execution-time bound or timing compliance.

