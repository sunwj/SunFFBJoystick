# SunFFB Joystick GUI Tester

A host-side graphical tester for the SunFFB Joystick firmware (ESP32-S3 HID PID force-feedback device). It provides a live predictive force display plus real device control through the HID PID interface.

## Requirements

- Python 3.14
- `PyQt6`
- `hid` package (hidapi bindings) with the bundled `hidapi/hidapi.dll` on Windows

Install dependencies with pip as needed, e.g.:

```
pip install PyQt6 hid
```

The `hidapi.dll` bundled under `python_apis/hidapi/` is used by the `hid` package for device access.

## Run

From the `python_apis/` directory:

```
cd python_apis
python -m sunffb_gui
```

The GUI is a Python module (`sunffb_gui`), so it must be launched as `python -m sunffb_gui` from `python_apis/`.

## Usage

1. **Connect**: Plug in the joystick (USB VID `0xFFFF`, PID `0x2010`), then use the connect controls in the GUI to open the device.
2. **Direction pad**: A direction pad selects the direction the force comes **from**:
   - `0` = pull — force from the front (nose-up/backward feel)
   - `90` = left
   - `180` = push
   - `270` = right
3. **Effect types**: 11 effect types are supported:
   - Constant, Ramp, Sine, Square, Triangle, Sawtooth-up, Sawtooth-down
   - Spring, Damper, Inertia, Friction
4. **Conditions**: Spring/Damper/Inertia/Friction conditions are per-axis. Each axis has its own coefficient, saturation, deadband, and center parameters. Conditions use the axis metrics directly and ignore the direction pad.
5. **Background spring**: The background spring effect is added on top of the main effect, so the total force is the main effect plus the background spring.
6. **Log panel**: The log panel shows the messages sent to/received from the device. Pair it with the firmware's serial debug output to cross-check behavior end to end.

## Notes

- If no device is connected, the GUI still opens and is usable, but sending is disabled (`is_connected` is `False`). Connect a device to enable sending.
- The polling period is 20 ms.
- The on-screen force vector is a predictive model — it mirrors FFBTestTool's `ForceModel` and computes what the device should output. The device is an HID PID force-feedback device and does not report force back, so the display is a model of the firmware behavior, not a measurement.
