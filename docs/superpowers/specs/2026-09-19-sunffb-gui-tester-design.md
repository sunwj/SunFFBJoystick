# SunFFB GUI Tester — Design

Date: 2026-09-19
Status: Approved by user (2026-09-19)

## Goal

A PyQt6 desktop test application under `python_apis/` that mirrors the workflow of
[FFBTestTool](https://github.com/barsk/FFBTestTool) against the SunFFB firmware
(VID `0xFFFF`, PID `0x2010`): set effect parameters, start/stop effects, read back
joystick input, display the host-side predicted force vector, and log all sent
reports for side-by-side comparison with firmware debug output.

## Environment (verified)

- Python 3.14.1 (Windows)
- PyQt6 6.10.2 installed (`PyQt6-Qt6 6.10.2`, `PyQt6_sip 13.11.1`)
- `hid` package installed; `sunffb_hid.py` auto-loads the bundled
  `python_apis/hidapi/hidapi.dll` via `os.add_dll_directory` fallback
- GUI uses standard `unittest` for tests (matches `test_sunffb_hid.py` style)

## Architecture

```
python_apis/sunffb_gui/
  __init__.py
  force_model.py    # Pure predictive model (no Qt, no device, fully testable)
  device.py         # SunFFBDevice wrapper + background poll QThread
  widgets.py        # DirectionPad, parameter panels, force canvas widgets
  main.py           # MainWindow assembly, QTimer refresh, application entry
python_apis/test_force_model.py   # unittest numerical tests vs ForceModel semantics
```

### Data flow

- UI widgets -> parameter snapshot -> effect slot logic (two slots: main + background
  spring, mirroring FFBTestTool `_main`/`_spring`) -> build report structs ->
  `SunFFBDevice.send_*`/feature methods (reuse `sunffb_hid.py`).
- `QTimer` (20 ms) polls joystick input via background QThread; each tick feeds
  `force_model.Kinematics` -> `EvaluateCombined` -> force canvas + direction readout.
- PID state report polled at ~100 ms for status lamps (actuators/paused/playing id).
- All sent reports echoed to the log panel with timestamps (format aligned with
  firmware `SERIAL_PRINT` labels, e.g. `Set effect: idx=.. type=..`).

### MainWindow components

- Direction pad: 8-way buttons (push=180°, pull=0°, left=90°, right=270°) + angle
  slider + spin; semantics identical to FFBTestTool (direction = "force comes from").
- Effect type combo: constant / ramp / periodic (sine, square, triangle, sawtoothUp,
  sawtoothDown) / spring / damper / inertia / friction; dynamic parameter area.
- Common params: magnitude, duration(ms), gain, loopCount, startDelay, envelope
  (attack/fade level+time), trigger button.
- Condition params: posCoeff/negCoeff/posSat/negSat/deadBand/center offset (per axis
  X and Y, like FFBTestTool per-axis ConditionSet).
- Device toolbar: connect/disconnect, enable/disable/stopAll/reset, device gain.
- Real-time area: position bars, predicted force vector canvas, direction degree
  readout, PID status lamps.
- Log panel.

## ForceModel semantics (mirrors FFBTestTool ForceModel.cs)

- `Kinematics(pos, vel, acc)`: pos normalized to −1..1 (from joystick report /
  AXIS_RANGE=10000); vel/acc in full-scale/s (i.e. 1.0 = full deflection per second,
  matching `Clamp1`).
- `EvaluateCombined = BackgroundSpring + MainEffect`; result `(Fx, Fy)` in nominal
  −10000..10000. Fx = roll(+right), Fy = pitch(+pull back).
- Non-condition effects: `u = (−sinθ, cosθ)`; magnitude = constant / ramp linear
  interpolation / periodic waveform (`value = offset + magnitude*wave`, wave:
  sine=sin, square=±1, triangle, sawtoothUp=2u−1, sawtoothDown=1−2u, u normalized
  phase + phase/360); envelope applied multiplicatively to value; return
  `value × u`.
- Conditions: metric = spring→pos, damper→Clamp1(vel), inertia→Clamp1(acc),
  friction→sign(vel) if |vel|>0.02 else 0; center = CenterOffset/10000,
  db = DeadBand/10000, d = metric − center; positive side
  `f = −PosCoeff·(d − db)` clamped to −posSat; negative side
  `f = −NegCoeff·(d + db)` clamped to −negSat.
- Direction readout: `atan2(Fx, −Fy)` → degrees ("force comes from"), displayed as
  0°=pull, 90°=left, 180°=push, 270°=right.

## Error handling

- hid library unavailable or no devices: UI opens in "not connected" state, send
  buttons disabled, parameters still adjustable.
- Device disconnect detected during poll: status lamps update, re-connect button
  enabled; no crash.
- Report send errors: logged, effect state re-synced on next user action.

## Testing

`test_force_model.py` (unittest):
- Spring exact-value anchors (incl. deadband boundary, center offset)
- Condition metrics mapping (damper/inertia Clamp1, friction 2% threshold)
- Periodic waveforms sampled at phases (0/90/180/270)
- Envelope linear interpolation
- Direction 90° → u = (−1, 0); background spring + main effect sum

Non-goals: no firmware changes, no changes to existing `sunffb_hid.py` API,
no packaging (run from `python_apis/`), no serial communication.
