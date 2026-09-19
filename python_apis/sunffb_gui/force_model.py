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
        env = self.envelope_factor(params, elapsed_ms)
        if params.effect_type == CONSTANT:
            ux, uy = u_from_angle(params.direction_deg)
            scale = params.magnitude * params.gain / 255.0 * env
            return scale * ux, scale * uy
        ux, uy = u_from_angle(params.direction_deg)
        if params.effect_type == RAMP:
            val = self.ramp_value(params, 0.5) * env
            return val * ux, val * uy
        if params.effect_type in (SINE, SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN):
            frac = (0.25 + params.phase / 35999.0) % 1.0
            val = (params.offset + params.magnitude * self.periodic_wave(params.effect_type, frac)) * env
            return val * ux, val * uy
        return 0.0, 0.0

    @staticmethod
    def envelope_factor(params: EffectParams, elapsed_ms: float) -> float:
        t = max(0.0, elapsed_ms)
        if params.attack_time_ms > 0 and t < params.attack_time_ms:
            a0 = params.attack_level / MAX_FORCE
            return a0 + (1.0 - a0) * (t / params.attack_time_ms)
        fade_window = params.attack_time_ms if params.attack_time_ms > 0 else 0.0
        if params.fade_time_ms > 0 and fade_window < t <= fade_window + params.fade_time_ms:
            tgt = params.fade_level / MAX_FORCE
            return 1.0 + (tgt - 1.0) * ((t - fade_window) / params.fade_time_ms)
        return 1.0

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
            return 1.0 - 4.0 * u if u < 0.5 else 4.0 * u - 3.0
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
