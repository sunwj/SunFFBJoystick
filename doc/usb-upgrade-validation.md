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

The upgrade configuration has not yet passed compilation or hardware testing.
The first installation failed because the existing Python environment lacked
`charset-normalizer` metadata; reinstalling encountered a loaded `.pyd` file.
An isolated environment successfully installed the pinned platform and Arduino
core, but SDK extraction failed on Windows' path length limit before compilation.
A temporary drive mapping did not help because the installer resolved it back to
the physical path. Use a short physical core-cache path as above when retrying.

Inspection of the downloaded Arduino 3.3.12 USBHID implementation also found
that control SET_REPORT with a nonzero report ID still enters the Feature handler
regardless of Output type. Check this callback routing and the report-completion
callback length type during migration. A newer TinyUSB control buffer does not
by itself resolve all Arduino integration issues.
