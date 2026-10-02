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

From the repository root, the equivalent command is:

```
python -m python_apis.sunffb_gui
```

The GUI is a Python module (`sunffb_gui`), so it must be launched as `python -m sunffb_gui` from `python_apis/`.

## Usage

1. **Connect**: Plug in the joystick (USB VID `0xFFFF`, PID `0x2010`), press **Rescan**, select the device, then press **Connect**. The device view and status are on the left, with controls on the right.
2. **Background spring**: The centring safeguard is enabled by default. Its checkbox and stiffness control are separate from the selected effect; turning it off stops only the spring block.
3. **Effect on/off**: Select an effect and tick **effect on**. Parameter changes are sent live while it is running; there is no separate Start/Stop workflow. The legacy methods remain available to scripts.
4. **Direction pad**: A compass-style pad selects the direction the force comes **from**:
   - `0` = pull — force from the front (nose-up/backward feel)
   - `90` = left
   - `180` = push
   - `270` = right
5. **Effect types**: 11 effect types are supported:
   - Constant, Ramp, Sine, Square, Triangle, Sawtooth-up, Sawtooth-down
   - Spring, Damper, Inertia, Friction
6. **Conditions**: Spring/Damper/Inertia/Friction conditions are per-axis. Each axis has its own coefficient, saturation, deadband, and center parameters. Conditions use the axis metrics directly and ignore the direction pad.
7. **Force view**: The left canvas shows the live device position, commanded force vector, and combined force readout. It is a prediction, not a force sensor.
8. **Log panel**: The log panel shows event/status messages from the GUI session (connect/disconnect, effect start/stop, worker errors). It does not log raw HID report bytes; pair it with the firmware's serial debug output to cross-check behavior end to end.

## Notes

- If no device is connected, the GUI still opens and is usable, but sending is disabled (`is_connected` is `False`). Connect a device to enable sending.
- The polling period is 20 ms.
- The on-screen force vector is a predictive model — it mirrors FFBTestTool's `ForceModel` and computes what the device should output. The device is an HID PID force-feedback device and does not report force back, so the display is a model of the firmware behavior, not a measurement.

- Loop count 255 repeats indefinitely; finite loops restart the preview at each
  effect-duration boundary.
- Triangle phase matches firmware: phase zero starts at zero and rises to the
  positive peak at one quarter cycle.
- Infinite effect duration ignores fade. A fade longer than a finite effect
  starts at time zero, preserving the requested fade slope. Attack takes
  precedence while attack and fade overlap.
