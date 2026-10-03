# USB framework upgrade validation

Existing production configurations pin PlatformIO Espressif32 6.12.0.
The two opt-in environments pin
[pioarduino 55.03.312-1](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.312-1),
whose release uses Arduino-ESP32 3.3.12 and ESP-IDF 5.5.5.

- `esp32-s3-upgrade`: base S3 firmware, without LCD or motor output.
- `esp32-s3-upgrade-hil`: LCD, UART RX48/TX45, simulated position input, motor
  output and independent HIL telemetry. Keep motors disconnected.

CDC remains absent. The TinyUSB 0.16 buffer backport delegates to the upstream
driver on newer versions. The report-completion wrapper remains necessary for
the project's USB completion statistics. Upgrade builds require TinyUSB >=0.17.

Build from the repository root:

```powershell
pio run -e esp32-s3-upgrade -e esp32-s3-upgrade-hil
```

For an isolated package/Python cache on Windows:

```powershell
$env:PLATFORMIO_CORE_DIR = 'E:\pio-upgrade'
pio run -e esp32-s3-upgrade -e esp32-s3-upgrade-hil
```

Flash only after selecting the corresponding environment and re-enumerating
the ROM bootloader port. After reset, validate Output/Feature report handling,
all eleven effects, independent X/Y conditions after envelope and live updates,
and stop/unload zero output. Run the DirectInput HIL runner from the external
library root as documented in `tests/README.md`, preserving each capture.
Require no added deadline misses/skips, no USB disconnect or MCU reset, and
record lifetime maxima including startup. Build success alone does not qualify
the upgrade for production or establish that historical USB failures are fixed.

## Initial build attempt

The initial upgrade configuration did not reach compilation or hardware testing.
The first installation failed because the existing Python environment lacked
`charset-normalizer` metadata; reinstalling encountered a loaded `.pyd` file.
An isolated environment successfully installed the pinned platform and Arduino
core, but SDK extraction failed on Windows' path length limit before compilation.
A temporary drive mapping did not help because the installer resolved it back to
the physical path. A separate physical cache at `E:/pio-upgrade` subsequently
installed the complete toolchain and SDK successfully.

Inspection of the downloaded Arduino 3.3.12 USBHID implementation also found
that control SET_REPORT with a nonzero report ID still enters the Feature handler
regardless of Output type. Upgrade environments now wrap `tud_hid_set_report_cb`
to retain the actual report type and to strip the report ID from interrupt OUT
payloads before dispatch. TinyUSB continues to own its control buffers; this
adapter corrects Arduino callback routing rather than replacing driver storage.
The completion wrapper derives its length type from the installed TinyUSB
declaration and saturates before narrowing to the project's uint16 hook.

## Build verification after repairs

Both `esp32-s3-upgrade` and `esp32-s3-upgrade-hil` now compile and link with
Arduino 3.3.12, ESP-IDF 5.5.5, GCC 14.2.0 and bundled TinyUSB 0.21.0. The
TinyUSB 0.16 buffer backport is inactive in these builds. Production `esp32-s3`
and the legacy `esp32-s3-hil` configurations also compiled successfully.

Two additional build issues were resolved:

- Arduino's newer USB library compiles MSC sources requiring FS headers, even
  though this firmware registers HID only. Upgrade environments use
  `lib_ldf_mode=deep` to discover these dependencies; `chain` and `deep+` failed
  with missing `FS.h` on this installation.
- Newlib/toolchain integer typedefs made `int32_t` a different template argument
  from plain integer bounds in the serial position mapping. An explicit
  `std::clamp<int32_t>` preserves the existing -32767..32767 conversion.

The base upgrade binary uses 57716 bytes of RAM and 384955 bytes of flash; the
HIL upgrade uses 58268 bytes of RAM and 440079 bytes of flash.

## Hardware validation

The `esp32-s3-upgrade-hil` image was flashed to the S3 and tested with the actual
Windows DirectInput library. The motor remained disconnected; encoder positions
were simulated over UART at a requested 500 Hz and force output was observed on
that UART. Both test captures are preserved:

- `doc/usb-upgrade-directinput-3min-results.json`: all 32 checks passed during
  three minutes of rotating 15-effect stress with live parameter updates.
- `doc/usb-upgrade-directinput-10min-results.json`: all 32 checks passed during
  602 seconds of the same full-pool stress, including 300493 UART force frames,
  361533 DirectInput state polls and 8865 live updates.

Both captures verified position round-trip, four constant-force directions,
live magnitude/direction/gain, all five periodic effects, ramp expiry, XY spring,
damper, inertia and friction after envelope configuration and live Y-axis
updates, and zero force after stop/unload. The long run observed no MCU reset or
USB unmount. Boot-wide force timing at 891 seconds uptime reported a 509 us
maximum against a 1000 us budget, zero completed-job misses and eight skipped
releases. The skipped count was already eight at the first timing sample and
did not increase during the stress run. Free force-task stack was 1020 bytes at
the final sample.

These results validate the exercised S3 configuration and host path under this
bounded test. DirectInput state polls are not fresh USB packet counts; the UART
health records show the device remained mounted but do not constitute a USB
wire-level capture. The LCD image was flashed, but its physical display was not
visually inspected. Keep the historical CDC and earlier deadline failure
captures and qualifications intact; this run does not establish the root cause
of those prior failures or multi-day endurance.
