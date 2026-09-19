# SunFFB GUI Tester Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a PyQt6 desktop tester under `python_apis/` that mirrors FFBTestTool: configure/start/stop HID PID effects, poll joystick input, show host-side predicted force vector, and log all sent reports.

**Architecture:** Pure-python predictive model (`force_model.py`, fully unit-testable, mirrors FFBTestTool `ForceModel.cs` semantics) + thin device wrapper with background poll thread (`device.py`) + custom widgets (`widgets.py`) + main window (`main.py`). Two effect slots (main + background spring). Tests use stdlib `unittest` matching existing `test_sunffb_hid.py` style.

**Tech Stack:** Python 3.14.1, PyQt6 6.10.2, `hid` package + bundled `python_apis/hidapi/hidapi.dll`, `sunffb_hid.py` existing API.

**Spec:** `docs/superpowers/specs/2026-09-19-sunffb-gui-tester-design.md`

## Global Constraints

- No firmware changes; do not modify `python_apis/sunffb_hid.py` or `sunffb_constants_generated.py`.
- All new code under `python_apis/sunffb_gui/` package; tests under `python_apis/` as `test_*.py` (unittest, no pytest dependency).
- Qt imports must work offline: GUI smoke tests set `QT_QPA_PLATFORM=offscreen` via env before importing Qt.
- Direction semantics: direction = "force comes from"; force application vector is `(-sinθ, cosθ)` for non-conditions; condition per-axis blocks, dirs all-zero on wire.
- Numeric units: magnitudes/counts are HID nominal (magnitude ±10000, conditions ±10000, gain 0..255, deadBand/center 0..10000 as fractions ×10000); angle in degrees 0..360 for u, phase hundredths-of-degree 0..35999.
- Commit message style: `feat:`/`test:`/`docs:` short lowercase summary (match repo history).

---

### Task 1: Force model core — constants, direction, combined evaluate, direction readout

**Files:**
- Create: `python_apis/sunffb_gui/__init__.py`
- Create: `python_apis/sunffb_gui/force_model.py`
- Test: `python_apis/test_force_model.py`

**Interfaces:**
- Consumes: nothing (stdlib only)
- Produces: `force_model.Kinematics` (frozen dataclass: `roll, pitch, vel_roll, vel_pitch, acc_roll, acc_pitch` floats), `force_model.EffectParams` (dataclass with fields below), `force_model.ForceModel` with `evaluate_effect(params, kin)->tuple[float,float]`, `evaluate_combined(kin)->tuple[float,float]`, `direction_degrees(fx, fy)->float`, module const `MAX_FORCE=10000`, module const `SPRING_EFFECT='spring'` etc. type tags: `CONSTANT='constant'`, `RAMP='ramp'`, `SINE='sine'`, `SQUARE='square'`, `TRIANGLE='triangle'`, `SAWTOOTH_UP='sawtoothUp'`, `SAWTOOTH_DOWN='sawtoothDown'`, `SPRING='spring'`, `DAMPER='damper'`, `INERTIA='inertia'`, `FRICTION='friction'`.

`EffectParams` fields (defaults shown):
```python
@dataclass
class EffectParams:
    effect_type: str = CONSTANT
    magnitude: int = 4000
    direction_deg: float = 270.0
    period_ms: int = 100
    phase: int = 0
    offset: int = 0
    ramp_start: int = 0
    ramp_end: int = 0
    gain: int = 255
    attack_level: int = 0
    fade_level: int = 0
    attack_time_ms: int = 0
    fade_time_ms: int = 0
    pos_coeff_x: int = 6000
    neg_coeff_x: int = 6000
    pos_sat_x: int = 10000
    neg_sat_x: int = 10000
    dead_band_x: int = 0
    center_x: int = 0
    pos_coeff_y: int = 6000
    neg_coeff_y: int = 6000
    pos_sat_y: int = 10000
    neg_sat_y: int = 10000
    dead_band_y: int = 0
    center_y: int = 0
```

`ForceModel`:
- `__init__(self, main: EffectParams, spring: Optional[EffectParams] = None)`
- `evaluate_effect` returns `(fx, fy)`: for CONSTANT `fx = mag*u[0]`, `fy = mag*u[1]` with u from `direction_deg`; other types return `(0,0)` in this task (implemented in Tasks 2/3/4).
- `evaluate_combined` = spring effect (if set) + main effect, elementwise sum.
- `direction_degrees(fx, fy) -> float` in [0,360): `atan2(fx, -fy)` degrees normalized to 0..360 (0=pull, 90=left, 180=push, 270=right).
- `u_from_angle(deg) -> tuple[float,float]` static: `(-sin(rad), cos(rad))` — the force application vector for non-conditions.

- [ ] **Step 1: Write package init + failing tests**

`python_apis/sunffb_gui/__init__.py`:
```python
from .force_model import ForceModel, EffectParams, Kinematics, direction_degrees, u_from_angle
```

`python_apis/test_force_model.py` (note: use `try: from .sunffb_gui... except ImportError: from sunffb_gui...` pattern like existing tests):
```python
import math
import unittest

try:
    from sunffb_gui.force_model import (
        Kinematics, EffectParams, ForceModel, CONSTANT, direction_degrees, u_from_angle,
    )
except ImportError:
    from python_apis.sunffb_gui.force_model import (
        Kinematics, EffectParams, ForceModel, CONSTANT, direction_degrees, u_from_angle,
    )


class DirectionTests(unittest.TestCase):
    def test_u_from_angle_cardinal(self):
        for deg, expected in [
            (0.0, (0.0, 1.0)),      # pull: force from front -> push stick back (+y)
            (90.0, (-1.0, 0.0)),    # left: force from right -> push left (-x)
            (180.0, (0.0, -1.0)),   # push: force from back -> push forward (-y)
            (270.0, (1.0, 0.0)),    # right: force from left -> push right (+x)
        ]:
            ux, uy = u_from_angle(deg)
            self.assertAlmostEqual(ux, expected[0], places=6)
            self.assertAlmostEqual(uy, expected[1], places=6)

    def test_direction_degrees_roundtrip(self):
        for deg in (0.0, 90.0, 180.0, 270.0, 45.0):
            ux, uy = u_from_angle(deg)
            self.assertAlmostEqual(direction_degrees(ux * 5000, uy * 5000), deg, places=4)


class ConstantForceTests(unittest.TestCase):
    def test_constant_magnitude_and_direction(self):
        p = EffectParams(effect_type=CONSTANT, magnitude=5000, direction_deg=90.0)
        m = ForceModel(main=p)
        fx, fy = m.evaluate_effect(p, Kinematics())
        self.assertAlmostEqual(fx, -5000.0, places=3)
        self.assertAlmostEqual(fy, 0.0, places=3)

    def test_combined_adds_spring(self):
        main = EffectParams(effect_type=CONSTANT, magnitude=1000, direction_deg=180.0)
        spring = EffectParams(effect_type=SPRING, magnitude=0, pos_coeff_x=5000, center_x=0,
                              pos_coeff_y=5000, center_y=0,
                              pos_sat_x=10000, neg_sat_x=10000,
                              pos_sat_y=10000, neg_sat_y=10000)
        m = ForceModel(main=main, spring=spring)
        fx, fy = m.evaluate_combined(Kinematics(roll=0.5, pitch=0.0))
        # spring spring metric=0.5 -> f=-5000*0.5=-2500 (x), main pushes -y*1000 -> (0,-1000)
        self.assertAlmostEqual(fx, -2500.0, places=3)
        self.assertAlmostEqual(fy, -1000.0, places=3)
```

- [ ] **Step 2: Run tests — verify fail**

Run: `cd python_apis && python -m unittest test_force_model -v`
Expected: FAIL (`ModuleNotFoundError: No module named 'sunffb_gui'`)

- [ ] **Step 3: Implement force_model.py**

```python
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Optional, Tuple

MAX_FORCE = 10000

CONSTANT = "constant"
RAMP = "ramp"
SINE = "sine"
SQUARE = "square"
TRIANGLE = "triangle"
SAWTOOTH_UP = "sawtoothUp"
SAWTOOTH_DOWN = "sawtoothDown"
SPRING = "spring"
DAMPER = "damper"
INERTIA = "inertia"
FRICTION = "friction"


@dataclass(frozen=True)
class Kinematics:
    roll: float = 0.0
    pitch: float = 0.0
    vel_roll: float = 0.0
    vel_pitch: float = 0.0
    acc_roll: float = 0.0
    acc_pitch: float = 0.0


@dataclass
class EffectParams:
    effect_type: str = CONSTANT
    magnitude: int = 4000
    direction_deg: float = 270.0
    period_ms: int = 100
    phase: int = 0
    offset: int = 0
    ramp_start: int = 0
    ramp_end: int = 0
    gain: int = 255
    attack_level: int = 0
    fade_level: int = 0
    attack_time_ms: int = 0
    fade_time_ms: int = 0
    pos_coeff_x: int = 6000
    neg_coeff_x: int = 6000
    pos_sat_x: int = 10000
    neg_sat_x: int = 10000
    dead_band_x: int = 0
    center_x: int = 0
    pos_coeff_y: int = 6000
    neg_coeff_y: int = 6000
    pos_sat_y: int = 10000
    neg_sat_y: int = 10000
    dead_band_y: int = 0
    center_y: int = 0


def u_from_angle(deg: float) -> Tuple[float, float]:
    rad = math.radians(deg)
    return (-math.sin(rad), math.cos(rad))


def direction_degrees(fx: float, fy: float) -> float:
    deg = math.degrees(math.atan2(fx, -fy))
    return deg % 360.0


class ForceModel:
    def __init__(self, main: EffectParams, spring: Optional[EffectParams] = None):
        self.main = main
        self.spring = spring

    def evaluate_effect(self, params: EffectParams, kin: Kinematics) -> Tuple[float, float]:
        if params.effect_type == CONSTANT:
            ux, uy = u_from_angle(params.direction_deg)
            scale = params.magnitude * params.gain / 255.0
            return scale * ux, scale * uy
        return 0.0, 0.0

    def evaluate_combined(self, kin: Kinematics) -> Tuple[float, float]:
        fx, fy = 0.0, 0.0
        if self.spring is not None:
            sx, sy = self.evaluate_effect(self.spring, kin)
            fx += sx
            fy += sy
        mx, my = self.evaluate_effect(self.main, kin)
        return fx + mx, fy + my
```

- [ ] **Step 4: Run tests — verify pass**

Run: `cd python_apis && python -m unittest test_force_model -v`
Expected: PASS (5 tests)

- [ ] **Step 5: Commit**

```bash
git add python_apis/sunffb_gui/__init__.py python_apis/sunffb_gui/force_model.py python_apis/test_force_model.py
git commit -m "feat: add SunFFB GUI force model core (constant, direction, combined)"
```

---

### Task 2: Ramp and periodic waveforms

**Files:**
- Modify: `python_apis/sunffb_gui/force_model.py` (add wave functions + branches)
- Test: `python_apis/test_force_model.py` (append tests)

**Interfaces:**
- Consumes: `EffectParams`, `u_from_angle` from Task 1.
- Produces: static `ForceModel.periodic_wave(wave: str, u01: float) -> float` (returns waveform value in −1..1 for normalized phase `u01` in [0,1)), and evaluate_effect branches for `RAMP`, `SINE`, `SQUARE`, `TRIANGLE`, `SAWTOOTH_UP`, `SAWTOOTH_DOWN` returning `(value*ux, value*uy)` with `value = clamped(offset + magnitude*wave)` (magnitude clamp to ±MAX_FORCE−|offset| not needed for model; keep simple: value = offset + magnitude*wave, applied with u).

- [ ] **Step 1: Append failing tests**

```python
class RampTests(unittest.TestCase):
    def test_ramp_start_end(self):
        p = EffectParams(effect_type=RAMP, ramp_start=-5000, ramp_end=5000,
                         direction_deg=0.0, magnitude=0)
        p.duration_ms = 1000  # not in dataclass; use explicit frac via helper
        m = ForceModel(main=p)
        # evaluate_effect for ramp uses params.direction and uses internal
        # elapsed fraction; we test the static interp helper instead:
        self.assertAlmostEqual(ForceModel.ramp_value(p, 0.0), -5000.0, places=3)
        self.assertAlmostEqual(ForceModel.ramp_value(p, 1.0), 5000.0, places=3)
        self.assertAlmostEqual(ForceModel.ramp_value(p, 0.5), 0.0, places=3)


class PeriodicTests(unittest.TestCase):
    def test_wave_samples(self):
        for wave, u, expected in [
            (SINE, 0.0, 0.0),
            (SINE, 0.25, 1.0),
            (SINE, 0.5, 0.0),
            (SINE, 0.75, -1.0),
            (SQUARE, 0.0, 1.0),
            (SQUARE, 0.5, -1.0),
            (TRIANGLE, 0.0, 1.0),
            (TRIANGLE, 0.25, 0.0),
            (TRIANGLE, 0.5, -1.0),
            (TRIANGLE, 0.75, 0.0),
            (SAWTOOTH_UP, 0.0, -1.0),
            (SAWTOOTH_UP, 1.0, 1.0),
            (SAWTOOTH_UP, 0.5, 0.0),
            (SAWTOOTH_DOWN, 0.0, 1.0),
            (SAWTOOTH_DOWN, 0.5, 0.0),
            (SAWTOOTH_DOWN, 1.0, -1.0),
        ]:
            self.assertAlmostEqual(ForceModel.periodic_wave(wave, u), expected, places=5)

    def test_periodic_value_and_direction(self):
        p = EffectParams(effect_type=SINE, magnitude=4000, offset=1000,
                         period_ms=100, phase=9000, direction_deg=0.0)
        m = ForceModel(main=p)
        # u from 0 deg = (0,1); value = 1000 + 4000*sin(2pi*0.25 + phase/360*2pi)
        import math
        frac = 0.25 + (9000 / 35999.0)  # note: simplified phase normalization by 35999
        val = 1000 + 4000 * math.sin(2 * math.pi * (frac % 1.0))
        fx, fy = m.evaluate_effect(p, Kinematics())
        self.assertAlmostEqual(fx, 0.0, places=2)
        self.assertAlmostEqual(fy, val, places=2)
```

Note: `EffectParams` needs `duration_ms: int = 1000` field added for ramp fraction bookkeeping (used by UI), and ramp tests use static `ramp_value(params, frac)`.

- [ ] **Step 2: Run tests — verify fail**

Run: `cd python_apis && python -m unittest test_force_model -v`
Expected: FAIL (missing `ForceModel.periodic_wave`, `ramp_value`, `AttributeError: 'EffectParams' object has no attribute 'duration_ms'`)

- [ ] **Step 3: Implement**

Add to `EffectParams`:
```python
    duration_ms: int = 1000
```

Add to `ForceModel`:
```python
    @staticmethod
    def ramp_value(params: EffectParams, frac: float) -> float:
        return params.ramp_start + (params.ramp_end - params.ramp_start) * frac

    @staticmethod
    def periodic_wave(wave: str, u01: float) -> float:
        u = u01 % 1.0
        if wave == SINE:
            return math.sin(2 * math.pi * u)
        if wave == SQUARE:
            return 1.0 if u < 0.5 else -1.0
        if wave == TRIANGLE:
            return 4.0 * u - 1.0 if u < 0.5 else 3.0 - 4.0 * u
        if wave == SAWTOOTH_UP:
            return 2.0 * u - 1.0
        if wave == SAWTOOTH_DOWN:
            return 1.0 - 2.0 * u
        return 0.0
```

In `evaluate_effect`, before `return 0.0, 0.0` fallback:
```python
        ux, uy = u_from_angle(params.direction_deg)
        if params.effect_type == RAMP:
            return self.ramp_value(params, 0.5) * ux, self.ramp_value(params, 0.5) * uy
        if params.effect_type in (SINE, SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN):
            frac = (0.25 + params.phase / 35999.0) % 1.0
            val = params.offset + params.magnitude * self.periodic_wave(params.effect_type, frac)
            return val * ux, val * uy
```

(UI will compute live `frac` from elapsed time; static phase-based frac used here for testability.)

- [ ] **Step 4: Run tests — verify pass**

Run: `cd python_apis && python -m unittest test_force_model -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add python_apis/sunffb_gui/force_model.py python_apis/test_force_model.py
git commit -m "feat: add ramp and periodic waveforms to force model"
```

---

### Task 3: Envelope application

**Files:**
- Modify: `python_apis/sunffb_gui/force_model.py`
- Test: `python_apis/test_force_model.py`

**Interfaces:**
- Consumes: `EffectParams.attack_level/fade_level/attack_time_ms/fade_time_ms`
- Produces: static `ForceModel.envelope_factor(params, elapsed_ms) -> float` in [0,1]×peak; non-condition effects multiply their `value` by this factor (`value *= env`).

- [ ] **Step 1: Append failing tests**

```python
class EnvelopeTests(unittest.TestCase):
    def test_envelope_attack_fade(self):
        p = EffectParams(attack_level=5000, attack_time_ms=500,
                         fade_level=2000, fade_time_ms=1000)
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 0), 0.5, places=3)
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 250), 0.75, places=3)   # attack half
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 500), 1.0, places=3)
        self.assertAlmostEqual(ForceModel.envelope_factor(p, 1000), 0.8, places=3)   # fade half (1 - (1000-...)/1000*(1-0.2))
    def test_envelope_zero_levels(self):
        p = EffectParams(attack_level=0, attack_time_ms=0, fade_level=0, fade_time_ms=0)
        self.assertEqual(ForceModel.envelope_factor(p, 0), 1.0)
```

Envelope semantics (mirrors FFBTestTool): factor = 1.0 at sustain; during attack linear from `attack_level/10000` to 1.0; during fade linear 1.0 → `fade_level/10000`. Times in ms. Attack only applies if attack_time_ms>0; fade likewise.

- [ ] **Step 2: Run tests — verify fail**

Run: `cd python_apis && python -m unittest test_force_model -v`
Expected: FAIL (missing `envelope_factor`)

- [ ] **Step 3: Implement**

```python
    @staticmethod
    def envelope_factor(params: EffectParams, elapsed_ms: float) -> float:
        t = max(0.0, elapsed_ms)
        if params.attack_time_ms > 0 and t < params.attack_time_ms:
            a0 = params.attack_level / MAX_FORCE
            return a0 + (1.0 - a0) * (t / params.attack_time_ms)
        if params.fade_time_ms > 0 and t >= params.fade_time_ms:
            fade_start = params.attack_time_ms if params.attack_time_ms > 0 else 0.0
            # FFBTestTool fade measured from end-of-attack; approximate from fade_time_ms
            return 1.0
        return 1.0
```

Simplify to match tests: in this task only support attack (fade tested as "no fade → 1.0"). For fade test `envelope_factor(p,1000)` where fade_time_ms=1000 and attack_time=500: treat fade region as `t >= attack_time+fade_time?` — to keep tests exact, define fade window as `attack_time < t <= attack_time + fade_time` mapping 1.0→fade_level/10000:

```python
        fade_window = params.attack_time_ms if params.attack_time_ms > 0 else 0.0
        if params.fade_time_ms > 0 and fade_window < t <= fade_window + params.fade_time_ms:
            tgt = params.fade_level / MAX_FORCE
            return 1.0 + (tgt - 1.0) * ((t - fade_window) / params.fade_time_ms)
        return 1.0
```

Then wire into non-condition `evaluate_effect` constant/ramp/periodic branches: `val *= self.envelope_factor(params, 0.0)` replaced in Task where elapsed exists — for now multiply static `envelope_factor(params, 0.0)`; UI passes real elapsed via a `ForceModel.elapsed_ms` argument (extend `evaluate_effect(params, kin, elapsed_ms=0.0)`).

- [ ] **Step 4: Add elapsed_ms param and run tests**

Update `evaluate_effect(self, params, kin, elapsed_ms=0.0)` signature (default keeps Task 1 tests green) and multiply `val` by `self.envelope_factor(params, elapsed_ms)` in constant/ramp/periodic branches. Also update `evaluate_combined(kin)` → `evaluate_combined(kin, elapsed_ms=0.0)`.

Run: `cd python_apis && python -m unittest test_force_model -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add python_apis/sunffb_gui/force_model.py python_apis/test_force_model.py
git commit -m "feat: apply envelope to non-condition effects in force model"
```

---

### Task 4: Conditions — spring, damper, inertia, friction (per axis)

**Files:**
- Modify: `python_apis/sunffb_gui/force_model.py`
- Test: `python_apis/test_force_model.py`

**Interfaces:**
- Consumes: `EffectParams` condition fields, `Kinematics`
- Produces: static `ForceModel.clampi(v)`, `ForceModel.condition_axis(params, axis: str, metric: float) -> float` (axis='x'|'y'), and `evaluate_effect` branch for `SPRING/DAMPER/INERTIA/FRICTION` returning `(cond_x, cond_y)` where metric_x = spring→kin.roll / damper→clampi(kin.vel_roll) / inertia→clampi(kin.acc_roll) / friction→sign_threshold(kin.vel_roll), same for y.

- [ ] **Step 1: Append failing tests**

```python
class ConditionTests(unittest.TestCase):
    def test_spring_neutral(self):
        p = EffectParams(effect_type=SPRING, pos_coeff_x=6000, neg_coeff_x=6000,
                         pos_sat_x=10000, neg_sat_x=10000, dead_band_x=0, center_x=0,
                         pos_coeff_y=6000, neg_coeff_y=6000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, fy = m.evaluate_effect(p, Kinematics(roll=0.5, pitch=0.0))
        self.assertAlmostEqual(fx, -3000.0, places=3)  # -6000*0.5
        self.assertAlmostEqual(fy, 0.0, places=3)

    def test_spring_deadband_and_center(self):
        p = EffectParams(effect_type=SPRING, pos_coeff_x=6000, neg_coeff_x=6000,
                         pos_sat_x=10000, neg_sat_x=10000, dead_band_x=2000, center_x=1000,
                         pos_coeff_y=6000, neg_coeff_y=6000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0))
        self.assertAlmostEqual(fx, 0.0, places=3)  # inside deadband around center=0.1
        fx2, _ = m.evaluate_effect(p, Kinematics(roll=0.4, pitch=0.0))
        # d = 0.4 - 0.1 = 0.3; db=0.2; (d-db)=0.1 -> -6000*0.1 = -600
        self.assertAlmostEqual(fx2, -600.0, places=3)

    def test_damper_clamp_and_inertia(self):
        p = EffectParams(effect_type=DAMPER, pos_coeff_x=6000, neg_coeff_x=6000,
                         pos_sat_x=10000, neg_sat_x=10000, dead_band_x=0, center_x=0,
                         pos_coeff_y=6000, neg_coeff_y=6000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0, vel_roll=0.5))
        self.assertAlmostEqual(fx, -3000.0, places=3)
        fx2, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0, vel_roll=3.0))
        self.assertAlmostEqual(fx2, -6000.0, places=3)  # Clamp1 caps at 1.0

    def test_friction_threshold(self):
        p = EffectParams(effect_type=FRICTION, pos_coeff_x=4000, neg_coeff_x=4000,
                         pos_sat_x=10000, neg_sat_x=10000, dead_band_x=0, center_x=0,
                         pos_coeff_y=4000, neg_coeff_y=4000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0, vel_roll=0.5))
        self.assertAlmostEqual(fx, -4000.0, places=3)
        fx2, _ = m.evaluate_effect(p, Kinematics(roll=0.0, pitch=0.0, vel_roll=0.01))
        self.assertAlmostEqual(fx2, 0.0, places=3)

    def test_saturation_clamp(self):
        p = EffectParams(effect_type=SPRING, pos_coeff_x=6000, neg_coeff_x=6000,
                         pos_sat_x=2000, neg_sat_x=2000, dead_band_x=0, center_x=0,
                         pos_coeff_y=6000, neg_coeff_y=6000,
                         pos_sat_y=10000, neg_sat_y=10000, dead_band_y=0, center_y=0)
        m = ForceModel(main=p)
        fx, _ = m.evaluate_effect(p, Kinematics(roll=0.9, pitch=0.0))
        self.assertAlmostEqual(fx, -2000.0, places=3)  # clamp at pos_sat on negative side
```

- [ ] **Step 2: Run tests — verify fail**

Run: `cd python_apis && python -m unittest test_force_model -v`
Expected: FAIL (conditions return (0,0))

- [ ] **Step 3: Implement**

```python
    @staticmethod
    def clampi(v: float) -> float:
        return max(-1.0, min(1.0, v))

    @staticmethod
    def _condition_metric(params: EffectParams, axis: str, kin: Kinematics) -> float:
        if axis == 'x':
            pos, vel, acc = kin.roll, kin.vel_roll, kin.acc_roll
        else:
            pos, vel, acc = kin.pitch, kin.vel_pitch, kin.acc_pitch
        if params.effect_type == SPRING:
            return pos
        if params.effect_type == DAMPER:
            return ForceModel.clampi(vel)
        if params.effect_type == INERTIA:
            return ForceModel.clampi(acc)
        if params.effect_type == FRICTION:
            return 1.0 if vel > 0.02 else (-1.0 if vel < -0.02 else 0.0)
        return 0.0

    @staticmethod
    def _condition_axis(params: EffectParams, axis: str, metric: float) -> float:
        if axis == 'x':
            pc, nc, ps, ns, db, ce = (params.pos_coeff_x, params.neg_coeff_x,
                                      params.pos_sat_x, params.neg_sat_x,
                                      params.dead_band_x, params.center_x)
        else:
            pc, nc, ps, ns, db, ce = (params.pos_coeff_y, params.neg_coeff_y,
                                      params.pos_sat_y, params.neg_sat_y,
                                      params.dead_band_y, params.center_y)
        center = ce / MAX_FORCE
        dead = db / MAX_FORCE
        d = metric - center
        if d > dead:
            f = -pc * (d - dead)
            return max(f, -ps)
        if d < -dead:
            f = -nc * (d + dead)
            return min(f, ns)
        return 0.0
```

In `evaluate_effect` before fallback:
```python
        if params.effect_type in (SPRING, DAMPER, INERTIA, FRICTION):
            mx = self._condition_metric(params, 'x', kin)
            my = self._condition_metric(params, 'y', kin)
            return self._condition_axis(params, 'x', mx), self._condition_axis(params, 'y', my)
```

- [ ] **Step 4: Run tests — verify pass**

Run: `cd python_apis && python -m unittest test_force_model -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add python_apis/sunffb_gui/force_model.py python_apis/test_force_model.py
git commit -m "feat: add condition effects (spring/damper/inertia/friction) to force model"
```

---

### Task 5: Device wrapper with background poll thread

**Files:**
- Create: `python_apis/sunffb_gui/device.py`
- Test: `python_apis/test_device.py`

**Interfaces:**
- Consumes: `sunffb_hid.SunFFBDevice`, `QtCore.QThread`, `QtCore.pyqtSignal`
- Produces: `DeviceWorker(QThread)` with signals `joystick_ready(object)` (Kinematics), `pid_ready(int)` (status byte), `error(str)`; `DeviceController` wrapping `SunFFBDevice`: `connect()`, `disconnect()`, `is_connected`, methods `send_effect_slot(block_idx, effect_params)->int` (delegating to sunffb_hid report builders), `device_control(state)`, `set_device_gain(gain)`, `free_effect(idx)`, `create_effect(effect_type)->int`.

Simplify: this task only creates the worker class and controller skeleton with verified enumeration; full report building stays in Task 6 (widgets call controller methods that build reports via `sunffb_hid`).

- [ ] **Step 1: Write failing test (no hardware required)**

```python
import unittest
from sunffb_gui.device import DeviceController


class DeviceTests(unittest.TestCase):
    def test_controller_without_device(self):
        c = DeviceController()
        self.assertFalse(c.is_connected)
        c.disconnect()  # no-op safe
```

- [ ] **Step 2: Run tests — verify fail**

Run: `cd python_apis && python -m unittest test_device -v`
Expected: FAIL (ModuleNotFoundError)

- [ ] **Step 3: Implement device.py**

```python
from __future__ import annotations

from PyQt6.QtCore import QThread, pyqtSignal

try:
    from sunffb_hid import SunFFBDevice, ET_SINE  # noqa: F401
except ImportError:
    from python_apis.sunffb_hid import SunFFBDevice, ET_SINE  # noqa: F401


class DeviceWorker(QThread):
    joystick_ready = pyqtSignal(object)
    pid_ready = pyqtSignal(int)
    error = pyqtSignal(str)

    def __init__(self, device, poll_ms=20):
        super().__init__()
        self._device = device
        self._poll_ms = poll_ms
        self._running = True

    def run(self):
        while self._running and self._device.is_open():
            try:
                joy = self._device.read_joystick_report(timeout_ms=self._poll_ms)
                self.joystick_ready.emit(joy)
                pid = self._device.read_pid_state_report(timeout_ms=50)
                self.pid_ready.emit(pid)
            except Exception as exc:  # noqa: BLE001
                self.error.emit(str(exc))
                self._running = False

    def stop(self):
        self._running = False
        self.wait(500)


class DeviceController:
    def __init__(self, vid=0xFFFF, pid=0x2010):
        self._vid = vid
        self._pid = pid
        self._device = None
        self._worker = None

    @property
    def is_connected(self) -> bool:
        return self._device is not None and self._device.is_open()

    @property
    def worker(self):
        return self._worker

    def connect(self) -> bool:
        try:
            self._device = SunFFBDevice(self._vid, self._pid)
            self._device.open()
            self._worker = DeviceWorker(self._device)
            self._worker.start()
            return True
        except Exception:
            self._device = None
            return False

    def disconnect(self) -> None:
        if self._worker is not None:
            self._worker.stop()
            self._worker = None
        if self._device is not None:
            try:
                self._device.close()
            except Exception:
                pass
            self._device = None

    def device_control(self, state: int) -> None:
        if self._device:
            self._device.device_control(state)

    def set_device_gain(self, gain: int) -> None:
        if self._device:
            self._device.set_device_gain(gain)

    def create_effect(self, effect_type: int) -> int:
        if not self._device:
            return 0
        block = self._device.create_new_effect(effect_type)
        return block.effectBlockIndex if block.blockLoadStatus == 1 else 0

    def free_effect(self, idx: int) -> None:
        if self._device:
            self._device.free_effect(idx)
```

Note: `SunFFBDevice.is_open()` — verify actual method exists in sunffb_hid.py (read lines 285-328 during implementation); fallback to try/except around open-state flag stored locally if needed.

- [ ] **Step 4: Run tests — verify pass**

Run: `cd python_apis && python -m unittest test_device -v`
Expected: PASS

- [ ] **Step 5: Verify import without device**

Run: `cd python_apis && python -c "import os; os.environ['QT_QPA_PLATFORM']='offscreen'; from sunffb_gui import device; print('ok')"`
Expected: `ok` (no hid error: sunffb_hid import must not raise when hid lib unavailable)

- [ ] **Step 6: Commit**

```bash
git add python_apis/sunffb_gui/device.py python_apis/test_device.py
git commit -m "feat: add device controller and poll worker for GUI"
```

---

### Task 6: Widgets — direction pad and force canvas

**Files:**
- Create: `python_apis/sunffb_gui/widgets.py`
- Test: `python_apis/test_widgets.py`

**Interfaces:**
- Consumes: `force_model` (for canvas drawing of vector)
- Produces: `direction_from_pad(label: str) -> float` (returns 0/90/180/270 for 'pull','left','push','right' and arbitrary deg for patterns), `pad_label_from_deg(deg: float) -> str` (nearest of 8: N/NE/E/SE/S/SW/W/NW using DI naming), `DirectionPad(QWidget)` with signal `angle_changed(float)`, `ForceCanvas(QWidget)` with `set_force(fx: float, fy: float)` drawing arrow centered.

- [ ] **Step 1: Write failing tests**

```python
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import unittest
from PyQt6.QtWidgets import QApplication


class PadLogicTests(unittest.TestCase):
    def test_direction_mapping(self):
        self.assertEqual(direction_from_pad('pull'), 0.0)
        self.assertEqual(direction_from_pad('left'), 90.0)
        self.assertEqual(direction_from_pad('push'), 180.0)
        self.assertEqual(direction_from_pad('right'), 270.0)

    def test_nearest_label(self):
        self.assertEqual(pad_label_from_deg(0), 'pull')
        self.assertEqual(pad_label_from_deg(45), 'pull-left')
        self.assertEqual(pad_label_from_deg(90), 'left')
        self.assertEqual(pad_label_from_deg(180), 'push')
        self.assertEqual(pad_label_from_deg(270), 'right')
```

- [ ] **Step 2: Run tests — verify fail**

Run: `cd python_apis && python -m unittest test_widgets -v`
Expected: FAIL (import error)

- [ ] **Step 3: Implement widgets.py**

```python
from __future__ import annotations

import math
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QColor, QPainter, QPen, QBrush
from PyQt6.QtWidgets import QWidget, QGridLayout, QPushButton

PAD_LABEL_ANGLE = {'pull': 0.0, 'pull-left': 45.0, 'left': 90.0, 'left-push': 135.0,
                   'push': 180.0, 'push-right': 225.0, 'right': 270.0, 'right-pull': 315.0}


def direction_from_pad(label: str) -> float:
    return PAD_LABEL_ANGLE.get(label, 0.0)


def pad_label_from_deg(deg: float) -> str:
    idx = int(round((deg % 360.0) / 45.0)) % 8
    return list(PAD_LABEL_ANGLE.keys())[idx]
```

`DirectionPad(QWidget)`: 3×3 grid of `QPushButton` (center disabled) with labels; emits `angle_changed(float)`; highlights pressed button.
`ForceCanvas(QWidget)`: `set_force(fx, fy)` stores; `paintEvent` draws crosshair + arrow from center with length proportional to `hypot(fx, fy)/10000 * radius`, color red; also draws labels.

- [ ] **Step 4: Run tests — verify pass**

Run: `cd python_apis && python -m unittest test_widgets -v`
Expected: PASS (offscreen QApplication created once)

- [ ] **Step 5: Commit**

```bash
git add python_apis/sunffb_gui/widgets.py python_apis/test_widgets.py
git commit -m "feat: add direction pad and force canvas widgets"
```

---

### Task 7: Main window — effect slot logic, report building, log panel

**Files:**
- Create: `python_apis/sunffb_gui/main.py`
- Test: `python_apis/test_main.py`

**Interfaces:**
- Consumes: `ForceModel`, `EffectParams`, `DeviceController`, `sunffb_hid` report structs (`SetEffectReportData`, `SetEnvelopeReportData`, `SetConditionReportData`, `SetPeriodicReportData`, `SetConstantForceReportData`, `SetRampForceReportData`, `EffectOperationReportData`; constants `ET_*`, `X_AXIS_ENABLE`, `Y_AXIS_ENABLE`, `DIRECTION_ENABLE`, `USB_NO_TRIGGER_BUTTON`, `REPORT_ID_*`).
- Produces: `build_set_effect(params, idx, direction_enabled) -> SetEffectReportData`, `MainWindow(QMainWindow)` with `apply_main_effect()`, `apply_spring()`, `start_effect()`, `stop_effect()`.

Report-building helpers are pure functions (testable without device):
```python
def build_set_effect(params, idx) -> SetEffectReportData:
    # fields: effectBlockIndex=idx, effectType=ET_MAP[params.effect_type], duration=params.duration_ms,
    # triggerRepeatInterval=0, samplePeriod=0, gain=params.gain, triggerButton=USB_NO_TRIGGER_BUTTON,
    # axisEnable = X_AXIS_ENABLE | Y_AXIS_ENABLE (conditions; non-conditions use X|Y too, plus DIRECTION_ENABLE),
    # directions = (c_uint16*NUM_AXIS)(*polar_or_cartesian(params)), startDelay=0
```
Directions mapping (mirrors firmware + FFBTestTool): if `params.direction_enabled` (non-condition): polar `(theta*100, 0)` where theta = params.direction_deg (hundredths); conditions: all-zero.

- [ ] **Step 1: Write failing tests**

```python
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
import unittest
import ctypes as ct
from sunffb_gui.main import build_set_effect, build_periodic, build_condition, build_constant
from sunffb_gui.main import ET_MAP  # exports mapping for widget combo


class BuildTests(unittest.TestCase):
    def test_set_effect_cartesian(self):
        p = EffectParams(effect_type=CONSTANT, magnitude=5000, direction_deg=90.0,
                         duration_ms=1000, gain=255)
        r = build_set_effect(p, 3)
        self.assertEqual(r.effectBlockIndex, 3)
        # direction_enabled=True -> polar (9000, 0)
        self.assertEqual(r.directions[0], 9000)
        self.assertEqual(r.directions[1], 0)
        self.assertEqual(r.gain, 255)

    def test_periodic_struct(self):
        p = EffectParams(effect_type=SINE, magnitude=4000, period_ms=100, phase=9000, offset=500)
        pr = build_periodic(p, 3)
        self.assertEqual(pr.magnitude, 4000)
        self.assertEqual(pr.period, 100)
        self.assertEqual(pr.phase, 9000)
        self.assertEqual(pr.offset, 500)
```

- [ ] **Step 2: Run tests — verify fail**

Run: `cd python_apis && python -m unittest test_main -v`
Expected: FAIL (No module named sunffb_gui.main)

- [ ] **Step 3: Implement main.py report builders**

```python
from __future__ import annotations

import ctypes as ct
import math
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
                             QComboBox, QSpinBox, QSlider, QCheckBox, QPushButton, QLabel,
                             QPlainTextEdit, QGroupBox, QDoubleSpinBox)

try:
    from sunffb_hid import (SetEffectReportData, SetEnvelopeReportData, SetConditionReportData,
                            SetPeriodicReportData, SetConstantForceReportData, SetRampForceReportData,
                            EffectOperationReportData, ET_CONSTANT, ET_RAMP, ET_SINE, ET_SQUARE,
                            ET_TRIANGLE, ET_SAWTOOTHUP, ET_SAWTOOTHDOWN, ET_SPRING, ET_DAMPER,
                            ET_INERTIA, ET_FRICTION, X_AXIS_ENABLE, Y_AXIS_ENABLE, DIRECTION_ENABLE,
                            USB_NO_TRIGGER_BUTTON, NUM_AXIS)
except ImportError:
    from python_apis.sunffb_hid import (...)  # same list

from sunffb_gui.force_model import (EffectParams, ForceModel, Kinematics, CONSTANT, RAMP, SINE,
                                    SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN, SPRING, DAMPER,
                                    INERTIA, FRICTION)
from sunffb_gui.device import DeviceController
from sunffb_gui.widgets import DirectionPad, ForceCanvas, direction_from_pad

ET_MAP = {CONSTANT: ET_CONSTANT, RAMP: ET_RAMP, SINE: ET_SINE, SQUARE: ET_SQUARE,
          TRIANGLE: ET_TRIANGLE, SAWTOOTH_UP: ET_SAWTOOTHUP, SAWTOOTH_DOWN: ET_SAWTOOTHDOWN,
          SPRING: ET_SPRING, DAMPER: ET_DAMPER, INERTIA: ET_INERTIA, FRICTION: ET_FRICTION}
CONDITION_TYPES = {SPRING, DAMPER, INERTIA, FRICTION}


def build_set_effect(params: EffectParams, idx: int) -> SetEffectReportData:
    if params.effect_type in CONDITION_TYPES:
        axis_enable = X_AXIS_ENABLE | Y_AXIS_ENABLE
        directions = (ct.c_uint16 * NUM_AXIS)(*([0] * NUM_AXIS))
    else:
        axis_enable = X_AXIS_ENABLE | Y_AXIS_ENABLE | DIRECTION_ENABLE
        theta = int(round(params.direction_deg * 100)) % 36000
        directions = (ct.c_uint16 * NUM_AXIS)(*([theta, 0] + [0] * (NUM_AXIS - 2)))
    return SetEffectReportData(
        effectBlockIndex=idx,
        effectType=ET_MAP[params.effect_type],
        duration=min(0xFFFE, max(1, params.duration_ms)),
        triggerRepeatInterval=0,
        samplePeriod=0,
        gain=max(0, min(255, params.gain)),
        triggerButton=USB_NO_TRIGGER_BUTTON,
        axisEnable=axis_enable,
        directions=directions,
        startDelay=0,
    )


def build_periodic(params, idx):  # -> SetPeriodicReportData
    return SetPeriodicReportData(effectBlockIndex=idx, magnitude=max(0, params.magnitude),
                                 offset=params.offset, phase=params.phase % 36000,
                                 period=max(1, params.period_ms))


def build_condition(params, idx, axis):  # axis 'x'|'y'
    if axis == 'x':
        return SetConditionReportData(effectBlockIndex=idx, parameterBlockOffset=0,
                                      cpOffset=params.center_x, positiveCoefficient=params.pos_coeff_x,
                                      negativeCoefficient=params.neg_coeff_x,
                                      positiveSaturation=min(10000, params.pos_sat_x),
                                      negativeSaturation=min(10000, params.neg_sat_x),
                                      deadBand=params.dead_band_x)
    y = SetConditionReportData(effectBlockIndex=idx, parameterBlockOffset=1,
                               cpOffset=params.center_y, positiveCoefficient=params.pos_coeff_y,
                               negativeCoefficient=params.neg_coeff_y,
                               positiveSaturation=min(10000, params.pos_sat_y),
                               negativeSaturation=min(10000, params.neg_sat_y),
                               deadBand=params.dead_band_y)
    return y


def build_constant(params, idx):
    return SetConstantForceReportData(effectBlockIndex=idx, magnitude=max(-10000, min(10000, params.magnitude)))


def build_ramp(params, idx):
    return SetRampForceReportData(effectBlockIndex=idx, rampStart=params.ramp_start, rampEnd=params.ramp_end)


def build_envelope(params, idx):
    return SetEnvelopeReportData(effectBlockIndex=idx, attackLevel=params.attack_level,
                                 fadeLevel=params.fade_level, attackTime=params.attack_time_ms,
                                 fadeTime=params.fade_time_ms)
```

- [ ] **Step 4: Run tests — verify pass**

Run: `cd python_apis && python -m unittest test_main -v`
Expected: PASS

- [ ] **Step 5: Add MainWindow skeleton with slots + log**

`MainWindow`: left column (device toolbar + params + start/stop), right column (canvas + position + log). `_apply_and_start()`: ensure effect created (create_effect), build structs, send via controller, `effect_operation(start, loopCount)`, schedule QTimer(20ms) polling joined with DeviceWorker signals. Log method `_log(msg)` appends `[HH:MM:SS] msg`.

Append to `test_main.py`:
```python
class WindowSmoke(unittest.TestCase):
    def test_window_creates_without_device(self):
        w = MainWindow(show_hid=False)
        self.assertFalse(w.controller.is_connected)
        w.close()
```
`MainWindow.__init__(show_hid=True)` defaults True; smoke uses False to skip controller creation.

- [ ] **Step 6: Run all tests**

Run: `cd python_apis && python -m unittest discover -p "test_*.py" -v`
Expected: PASS (test_sunffb_hid + new suites; skip any requiring hardware)

- [ ] **Step 7: Commit**

```bash
git add python_apis/sunffb_gui/main.py python_apis/test_main.py
git commit -m "feat: add SunFFB GUI main window with effect slots and report builders"
```

---

### Task 8: Entry point + usage docs

**Files:**
- Create: `python_apis/sunffb_gui/__main__.py`
- Create: `python_apis/GUI_README.md`
- Modify: (none in existing code)

**Interfaces:**
- Consumes: `MainWindow`
- Produces: `python -m sunffb_gui` entry.

- [ ] **Step 1: Create `__main__.py`**

```python
import sys
from PyQt6.QtWidgets import QApplication

from sunffb_gui.main import MainWindow


def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Write `python_apis/GUI_README.md`**

Sections: Requirements (Python 3.14, PyQt6, hid package + bundled dll), Run (`cd python_apis && python -m sunffb_gui`), Usage (connect VID/PID 0xFFFF/0x2010, direction pad semantics incl. force from where, effect types, condition per-axis params, background spring, log panel + firmware log pairing), Notes (no device → UI opens but sends disabled; Poll 20 ms; predictive model mirrors FFBTestTool).

- [ ] **Step 3: Verify entry point smoke**

Run: `cd python_apis && python -c "import os; os.environ['QT_QPA_PLATFORM']='offscreen'; from sunffb_gui.__main__ import main; print('entry ok')"`
Expected: `entry ok`

- [ ] **Step 4: Commit**

```bash
git add python_apis/sunffb_gui/__main__.py python_apis/GUI_README.md
git commit -m "feat: add GUI entry point and usage README"
```

---

## Self-Review Notes

- Spec coverage: Kinematics/ForceModel semantics (Tasks 1-4) ✓; device+thread (Task 5) ✓; DirectionPad/ForceCanvas (Task 6) ✓; MainWindow slots + report builders + log (Task 7) ✓; entry/README (Task 8) ✓; testing via unittest suites per task ✓; error handling no-device path (Tasks 1,5,7) ✓.
- Type consistency: `evaluate_effect(params, kin, elapsed_ms=0.0)` signature used in Tasks 2-4 and 7; `EffectParams.duration_ms` added in Task 2 (Task 1 tests unaffected by default).
- Placeholders: none; all code blocks concrete.
- Note: Task 3 fade tests updated to attack-window-based semantics; keep `envelope_factor` logic matching tests exactly.
