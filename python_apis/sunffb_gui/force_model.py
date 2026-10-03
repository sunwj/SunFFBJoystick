# Two-axis software prediction for the GUI, not force telemetry read back from firmware.
# Kinematics uses normalized position/derivatives; nominal forces use the +/-10000 range.
# Preview formulas cover effects and envelopes; device state, filtering and scheduling can differ.

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


# Convert HID force-source angle to the applied XY unit vector: u=(-sin(theta), cos(theta)).
def u_from_angle(deg: float) -> Tuple[float, float]:
    rad = math.radians(deg)
    return (-math.sin(rad), math.cos(rad))


# Invert the force-vector mapping for display; wrap the resulting angle into [0, 360).
def direction_degrees(fx: float, fy: float) -> float:
    deg = math.degrees(math.atan2(-fx, fy))
    return deg % 360.0


class ForceModel:
    def __init__(self, main: EffectParams, spring: Optional[EffectParams] = None):
        self.main = main
        self.spring = spring

    # Return predicted Fx/Fy after effect gain, without applying device master gain or final firmware clipping.
    def evaluate_effect(self, params: EffectParams, kin: Kinematics, elapsed_ms: float = 0.0) -> Tuple[float, float]:
        gain = params.gain / 255.0
        if params.effect_type == CONSTANT:
            ux, uy = u_from_angle(params.direction_deg)
            amplitude = self.envelope_amplitude(params, elapsed_ms, abs(params.magnitude))
            scale = (-amplitude if params.magnitude < 0 else amplitude) * gain
            return scale * ux, scale * uy
        ux, uy = u_from_angle(params.direction_deg)
        if params.effect_type == RAMP:
            frac = min(1.0, max(0.0, elapsed_ms / max(1.0, params.duration_ms)))
            base = max(abs(params.ramp_start), abs(params.ramp_end))
            val = (self.ramp_value(params, frac) * self.envelope_factor(params, elapsed_ms, base) if base else self.envelope_amplitude(params, elapsed_ms, 0)) * gain
            return val * ux, val * uy
        if params.effect_type in (SINE, SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN):
            frac = self.periodic_u(params, elapsed_ms)
            val = (params.offset + self.envelope_amplitude(params, elapsed_ms, abs(params.magnitude)) * self.periodic_wave(params.effect_type, frac)) * gain
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
    # Spring uses position, damper velocity, inertia acceleration; friction is speed sign above a 2% threshold.
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
    # Use side-specific coefficient/saturation around center +/- deadband; positive coefficients oppose displacement.
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
            return max(-ps, min(ps, f))
        if d < -dead:
            f = -nc * (d + dead)
            return max(-ns, min(ns, f))
        return 0.0

    @staticmethod
    # Interpolate absolute attack/fade amplitude around sustain; indefinite duration has no terminal fade.
    def envelope_amplitude(params: EffectParams, elapsed_ms: float,
                           base_magnitude: float = MAX_FORCE) -> float:
        t = max(0.0, elapsed_ms)
        base = float(base_magnitude)
        if params.attack_time_ms > 0 and t < params.attack_time_ms:
            return params.attack_level + (base - params.attack_level) * t / params.attack_time_ms
        fade_start = max(0.0, params.duration_ms - params.fade_time_ms)
        if params.duration_ms != 0xFFFF and params.fade_time_ms > 0 and t > fade_start:
            progress = min(1.0, (t - fade_start) / params.fade_time_ms)
            return base + (params.fade_level - base) * progress
        return base

    @staticmethod
    # Convert absolute amplitude to a ramp scaling factor with a nonzero denominator.
    def envelope_factor(params: EffectParams, elapsed_ms: float,
                        base_magnitude: float = MAX_FORCE) -> float:
        base = max(1.0, float(base_magnitude))
        return ForceModel.envelope_amplitude(params, elapsed_ms, base) / base

    @staticmethod
    def ramp_value(params: EffectParams, frac: float) -> float:
        return params.ramp_start + (params.ramp_end - params.ramp_start) * frac

    @staticmethod
    # Convert millisecond time and HID phase to a cycle fraction, then wrap to [0, 1).
    def periodic_u(params: EffectParams, elapsed_ms: float) -> float:
        # Mirrors FFBTestTool ForceModel.Periodic: u = t/period + PhaseDeg/360.
        # Phase arrives in HID hundredths of a degree, so /100 converts to degrees,
        # then /360 gives the cycle fraction: phase/36000.
        period_ms = max(1.0, float(params.period_ms))
        return (elapsed_ms / period_ms + params.phase / 36000.0) % 1.0

    @staticmethod
    # Unit-amplitude waveforms are scaled later; triangle phase is chosen to match the firmware convention.
    def periodic_wave(wave: str, u01: float) -> float:
        u = u01 % 1.0
        if wave == SINE:
            return math.sin(2 * math.pi * u)
        if wave == SQUARE:
            return 1.0 if u < 0.5 else -1.0
        if wave == TRIANGLE:
            # Match firmware: zero at phase 0, positive peak at quarter cycle.
            u = (u + 0.25) % 1.0
            return 4.0 * u - 1.0 if u < 0.5 else 3.0 - 4.0 * u
        if wave == SAWTOOTH_UP:
            return 2.0 * u01 - 1.0
        if wave == SAWTOOTH_DOWN:
            return 1.0 - 2.0 * u01
        return 0.0

    # Add background spring and main prediction without final saturation; this is not physical force telemetry.
    def evaluate_combined(self, kin: Kinematics, elapsed_ms: float = 0.0) -> Tuple[float, float]:
        fx, fy = 0.0, 0.0
        if self.spring is not None:
            sx, sy = self.evaluate_effect(self.spring, kin, elapsed_ms)
            fx += sx
            fy += sy
        mx, my = self.evaluate_effect(self.main, kin, elapsed_ms)
        return fx + mx, fy + my
