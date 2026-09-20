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
    duration_ms: int = 1000
    apply_x: bool = True
    apply_y: bool = True


def u_from_angle(deg: float) -> Tuple[float, float]:
    rad = math.radians(deg)
    return (-math.sin(rad), math.cos(rad))


def direction_degrees(fx: float, fy: float) -> float:
    deg = math.degrees(math.atan2(-fx, fy))
    return deg % 360.0


class ForceModel:
    def __init__(self, main: EffectParams, spring: Optional[EffectParams] = None):
        self.main = main
        self.spring = spring

    def evaluate_effect(self, params: EffectParams, kin: Kinematics, elapsed_ms: float = 0.0) -> Tuple[float, float]:
        gain = params.gain / 255.0
        if params.effect_type == CONSTANT:
            ux, uy = u_from_angle(params.direction_deg)
            env = self.envelope_factor(params, elapsed_ms, abs(params.magnitude))
            scale = params.magnitude * gain * env
            return scale * ux, scale * uy
        ux, uy = u_from_angle(params.direction_deg)
        if params.effect_type == RAMP:
            frac = min(1.0, max(0.0, elapsed_ms / max(1.0, params.duration_ms)))
            base = max(abs(params.ramp_start), abs(params.ramp_end))
            val = self.ramp_value(params, frac) * self.envelope_factor(params, elapsed_ms, base) * gain
            return val * ux, val * uy
        if params.effect_type in (SINE, SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN):
            frac = self.periodic_u(params, elapsed_ms)
            env = self.envelope_factor(params, elapsed_ms, abs(params.magnitude))
            val = (params.offset + params.magnitude * self.periodic_wave(params.effect_type, frac) * env) * gain
            return val * ux, val * uy
        if params.effect_type in (SPRING, DAMPER, INERTIA, FRICTION):
            mx = self._condition_metric(params, 'x', kin)
            my = self._condition_metric(params, 'y', kin)
            return (self._condition_axis(params, 'x', mx) * gain if params.apply_x else 0.0,
                    self._condition_axis(params, 'y', my) * gain if params.apply_y else 0.0)
        return 0.0, 0.0

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

    @staticmethod
    def envelope_factor(params: EffectParams, elapsed_ms: float,
                        base_magnitude: float = MAX_FORCE) -> float:
        t = max(0.0, elapsed_ms)
        base = max(1.0, float(base_magnitude))
        if params.attack_time_ms > 0 and t < params.attack_time_ms:
            a0 = params.attack_level / base
            return a0 + (1.0 - a0) * (t / params.attack_time_ms)
        fade_start = max(0.0, params.duration_ms - params.fade_time_ms)
        if params.fade_time_ms > 0 and t > fade_start:
            tgt = params.fade_level / base
            progress = min(1.0, (t - fade_start) / params.fade_time_ms)
            return 1.0 + (tgt - 1.0) * progress
        return 1.0

    @staticmethod
    def ramp_value(params: EffectParams, frac: float) -> float:
        return params.ramp_start + (params.ramp_end - params.ramp_start) * frac

    @staticmethod
    def periodic_u(params: EffectParams, elapsed_ms: float) -> float:
        # Mirrors FFBTestTool ForceModel.Periodic: u = t/period + PhaseDeg/360.
        # Phase arrives in HID hundredths of a degree, so /100 converts to degrees,
        # then /360 gives the cycle fraction: phase/36000.
        period_ms = max(1.0, float(params.period_ms))
        return (elapsed_ms / period_ms + params.phase / 36000.0) % 1.0

    @staticmethod
    def periodic_wave(wave: str, u01: float) -> float:
        u = u01 % 1.0
        if wave == SINE:
            return math.sin(2 * math.pi * u)
        if wave == SQUARE:
            return 1.0 if u < 0.5 else -1.0
        if wave == TRIANGLE:
            # DI triangle: starts at min (-1), peak at u=0.5, back to min.
            return 4.0 * u - 1.0 if u < 0.5 else 3.0 - 4.0 * u
        if wave == SAWTOOTH_UP:
            return 2.0 * u01 - 1.0
        if wave == SAWTOOTH_DOWN:
            return 1.0 - 2.0 * u01
        return 0.0

    def evaluate_combined(self, kin: Kinematics, elapsed_ms: float = 0.0) -> Tuple[float, float]:
        fx, fy = 0.0, 0.0
        if self.spring is not None:
            sx, sy = self.evaluate_effect(self.spring, kin, elapsed_ms)
            fx += sx
            fy += sy
        mx, my = self.evaluate_effect(self.main, kin, elapsed_ms)
        return fx + mx, fy + my
