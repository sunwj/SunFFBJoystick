from __future__ import annotations

import ctypes as ct
import math
import time
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
                             QComboBox, QSpinBox, QSlider, QCheckBox, QPushButton, QLabel,
                             QPlainTextEdit, QGroupBox, QDoubleSpinBox)

try:
    from sunffb_hid import (SetEffectReportData, SetEnvelopeReportData, SetConditionReportData,
                            SetPeriodicReportData, SetConstantForceReportData, SetRampForceReportData,
                            EffectOperationReportData, ET_CONSTANT, ET_RAMP, ET_SINE, ET_SQUARE,
                            ET_TRIANGLE, ET_SAWTOOTH_UP, ET_SAWTOOTH_DOWN, ET_SPRING, ET_DAMPER,
                            ET_INERTIA, ET_FRICTION, X_AXIS_ENABLE, Y_AXIS_ENABLE, DIRECTION_ENABLE,
                            USB_NO_TRIGGER_BUTTON, NUM_AXIS)
except ImportError:
    from python_apis.sunffb_hid import (SetEffectReportData, SetEnvelopeReportData, SetConditionReportData,
                                        SetPeriodicReportData, SetConstantForceReportData, SetRampForceReportData,
                                        EffectOperationReportData, ET_CONSTANT, ET_RAMP, ET_SINE, ET_SQUARE,
                                        ET_TRIANGLE, ET_SAWTOOTH_UP, ET_SAWTOOTH_DOWN, ET_SPRING, ET_DAMPER,
                                        ET_INERTIA, ET_FRICTION, X_AXIS_ENABLE, Y_AXIS_ENABLE, DIRECTION_ENABLE,
                                        USB_NO_TRIGGER_BUTTON, NUM_AXIS)

from sunffb_gui.force_model import (EffectParams, ForceModel, Kinematics, CONSTANT, RAMP, SINE,
                                    SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN, SPRING, DAMPER,
                                    INERTIA, FRICTION)
from sunffb_gui.device import DeviceController
from sunffb_gui.widgets import DirectionPad, ForceCanvas, direction_from_pad

ET_MAP = {CONSTANT: ET_CONSTANT, RAMP: ET_RAMP, SINE: ET_SINE, SQUARE: ET_SQUARE,
          TRIANGLE: ET_TRIANGLE, SAWTOOTH_UP: ET_SAWTOOTH_UP, SAWTOOTH_DOWN: ET_SAWTOOTH_DOWN,
          SPRING: ET_SPRING, DAMPER: ET_DAMPER, INERTIA: ET_INERTIA, FRICTION: ET_FRICTION}
CONDITION_TYPES = {SPRING, DAMPER, INERTIA, FRICTION}
PERIODIC_TYPES = {SINE, SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN}


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


class MainWindow(QMainWindow):
    def __init__(self, show_hid: bool = True, parent=None):
        super().__init__(parent)
        self.setWindowTitle("SunFFB Joystick GUI Tester")
        self.resize(900, 620)
        self.controller = DeviceController()
        self._effect_idx = 0
        self._effect_type = None
        self._main_params = EffectParams()
        self._spring_params = None
        self._run_started_at = 0.0
        self._run_total_ms = 0
        self._last_kin = Kinematics()
        self._model = ForceModel(self._main_params)

        central = QWidget(self)
        root = QHBoxLayout(central)
        left = QVBoxLayout()
        right = QVBoxLayout()
        root.addLayout(left, 1)
        root.addLayout(right, 1)
        self.setCentralWidget(central)

        dev_group = QGroupBox("Device", self)
        dev_layout = QHBoxLayout(dev_group)
        self.btn_connect = QPushButton("Connect", dev_group)
        self.btn_connect.clicked.connect(self.toggle_connect)
        self.lbl_status = QLabel("not connected", dev_group)
        dev_layout.addWidget(self.btn_connect)
        dev_layout.addWidget(self.lbl_status, 1)
        left.addWidget(dev_group)

        params_group = QGroupBox("Effect", self)
        grid = QGridLayout(params_group)
        self.cmb_type = QComboBox(params_group)
        for label in ET_MAP:
            self.cmb_type.addItem(label, label)
        self.spn_dir = QDoubleSpinBox(params_group)
        self.spn_dir.setRange(0.0, 359.9)
        self.spn_dir.setSuffix("\u00b0")
        self.spn_dir.setValue(270.0)
        self.spn_mag = QSpinBox(params_group)
        self.spn_mag.setRange(-10000, 10000)
        self.spn_mag.setValue(4000)
        self.spn_duration = QSpinBox(params_group)
        self.spn_duration.setRange(0, 65535)
        self.spn_duration.setValue(1000)
        self.spn_duration.setSuffix(" ms")
        self.spn_gain = QSpinBox(params_group)
        self.spn_gain.setRange(0, 255)
        self.spn_gain.setValue(255)
        self.spn_period = QSpinBox(params_group)
        self.spn_period.setRange(1, 65535)
        self.spn_period.setValue(100)
        self.spn_period.setSuffix(" ms")
        self.spn_phase = QSpinBox(params_group)
        self.spn_phase.setRange(0, 35999)
        self.spn_offset = QSpinBox(params_group)
        self.spn_offset.setRange(-10000, 10000)
        self.spn_ramp_start = QSpinBox(params_group)
        self.spn_ramp_start.setRange(-10000, 10000)
        self.spn_ramp_end = QSpinBox(params_group)
        self.spn_ramp_end.setRange(-10000, 10000)
        self.spn_loop = QSpinBox(params_group)
        self.spn_loop.setRange(0, 255)
        self.spn_loop.setValue(1)
        self.spn_cond_coeff = QSpinBox(params_group)
        self.spn_cond_coeff.setRange(-10000, 10000)
        self.spn_cond_coeff.setValue(6000)
        self.spn_cond_sat = QSpinBox(params_group)
        self.spn_cond_sat.setRange(0, 10000)
        self.spn_cond_sat.setValue(10000)
        self.spn_cond_db = QSpinBox(params_group)
        self.spn_cond_db.setRange(0, 10000)
        self.spn_cond_center = QSpinBox(params_group)
        self.spn_cond_center.setRange(-10000, 10000)
        self.spn_spring = QSpinBox(params_group)
        self.spn_spring.setRange(0, 10000)
        self.spn_spring.setValue(6000)
        rows = [
            ("type", self.cmb_type, 2), ("dir", self.spn_dir, 1), ("magnitude", self.spn_mag, 2),
            ("duration", self.spn_duration, 2), ("gain", self.spn_gain, 2),
            ("period", self.spn_period, 2), ("phase", self.spn_phase, 3),
            ("offset", self.spn_offset, 3), ("ramp start", self.spn_ramp_start, 3),
            ("ramp end", self.spn_ramp_end, 3), ("loop count", self.spn_loop, 3),
            ("cond coeff", self.spn_cond_coeff, 3), ("cond sat", self.spn_cond_sat, 3),
            ("cond deadband", self.spn_cond_db, 3), ("cond center", self.spn_cond_center, 3),
            ("spring stiffness", self.spn_spring, 3),
        ]
        for row, (name, widget, span) in enumerate(rows):
            grid.addWidget(QLabel(name, params_group), row, 0)
            grid.addWidget(widget, row, 1, 1, span)
        self.pad = DirectionPad(params_group)
        grid.addWidget(self.pad, len(rows), 0, 1, 4)
        self.pad.angle_changed.connect(self.spn_dir.setValue)
        left.addWidget(params_group)

        btns = QHBoxLayout()
        self.btn_start = QPushButton("Start", self)
        self.btn_start.clicked.connect(self.start_effect)
        self.btn_stop = QPushButton("Stop", self)
        self.btn_stop.clicked.connect(self.stop_effect)
        self.btn_spring = QPushButton("Apply spring", self)
        self.btn_spring.clicked.connect(self.apply_spring)
        btns.addWidget(self.btn_start)
        btns.addWidget(self.btn_stop)
        btns.addWidget(self.btn_spring)
        left.addLayout(btns)
        left.addStretch(1)

        right.addWidget(QLabel("force", self))
        self.canvas = ForceCanvas(self)
        right.addWidget(self.canvas, 3)
        self.lbl_pos = QLabel("pos: (0, 0)", self)
        right.addWidget(self.lbl_pos)
        self.log = QPlainTextEdit(self)
        self.log.setReadOnly(True)
        right.addWidget(self.log, 2)

        self._timer = QTimer(self)
        self._timer.setInterval(20)
        self._timer.timeout.connect(self._on_tick)

        if show_hid:
            self.toggle_connect()
        self._log("ready")

    def toggle_connect(self):
        if self.controller.is_connected:
            self.controller.disconnect()
            self._timer.stop()
            self.btn_connect.setText("Connect")
            self.lbl_status.setText("not connected")
            self._log("disconnected")
            return
        if self.controller.connect():
            self.btn_connect.setText("Disconnect")
            self.lbl_status.setText("connected")
            worker = self.controller.worker
            if worker is not None:
                worker.joystick_ready.connect(self._on_joystick)
                worker.pid_ready.connect(self._on_pid)
                worker.error.connect(self._on_error)
            self._log("connected")
        else:
            self.lbl_status.setText("not connected")
            self._log("connect failed")

    def _on_joystick(self, joy):
        ax = joy.axis
        self._last_kin = Kinematics(roll=ax[0] / 32767.0,
                                    pitch=ax[1] / 32767.0 if NUM_AXIS >= 2 else 0.0)
        self.lbl_pos.setText(f"pos: ({ax[0]}, {ax[1]})")
        self._update_canvas()

    def _on_pid(self, status: int):
        self.lbl_status.setText(f"connected \u2014 pid {status}")

    def _on_error(self, msg: str):
        self._log(f"worker error: {msg}")

    def _on_tick(self):
        if self._run_total_ms > 0:
            elapsed_ms = (time.monotonic() - self._run_started_at) * 1000.0
            if elapsed_ms >= self._run_total_ms:
                self.stop_effect()
        self._update_canvas()

    def _update_canvas(self):
        fx, fy = self._model.evaluate_combined(self._last_kin, 0.0)
        self.canvas.set_force(fx, fy)

    def _gather_params(self) -> EffectParams:
        cond = self.spn_cond_coeff.value()
        return EffectParams(
            effect_type=str(self.cmb_type.currentData()),
            magnitude=self.spn_mag.value(),
            direction_deg=float(self.spn_dir.value()),
            period_ms=self.spn_period.value(),
            phase=self.spn_phase.value(),
            offset=self.spn_offset.value(),
            ramp_start=self.spn_ramp_start.value(),
            ramp_end=self.spn_ramp_end.value(),
            gain=self.spn_gain.value(),
            pos_coeff_x=cond, neg_coeff_x=cond,
            pos_coeff_y=cond, neg_coeff_y=cond,
            pos_sat_x=self.spn_cond_sat.value(), neg_sat_x=self.spn_cond_sat.value(),
            pos_sat_y=self.spn_cond_sat.value(), neg_sat_y=self.spn_cond_sat.value(),
            dead_band_x=self.spn_cond_db.value(), dead_band_y=self.spn_cond_db.value(),
            center_x=self.spn_cond_center.value(), center_y=self.spn_cond_center.value(),
            duration_ms=self.spn_duration.value(),
        )

    def _ensure_effect(self, effect_type: str) -> bool:
        if not self.controller.is_connected:
            self._log("device not connected")
            return False
        if self._effect_idx == 0 or self._effect_type != effect_type:
            if self._effect_idx:
                self.controller.free_effect(self._effect_idx)
                self._effect_idx = 0
            idx = self.controller.create_effect(ET_MAP[effect_type])
            if idx == 0:
                self._effect_type = None
                self._log("create effect failed")
                return False
            self._effect_idx = idx
            self._effect_type = effect_type
        return True

    @staticmethod
    def _send_payload(dev, params, idx):
        if params.effect_type in CONDITION_TYPES:
            dev.set_condition(build_condition(params, idx, 'x'))
            dev.set_condition(build_condition(params, idx, 'y'))
        elif params.effect_type in PERIODIC_TYPES:
            dev.set_periodic(build_periodic(params, idx))
        elif params.effect_type == CONSTANT:
            dev.set_constant_force(build_constant(params, idx))
        elif params.effect_type == RAMP:
            dev.set_ramp_force(build_ramp(params, idx))

    def _apply_and_start(self):
        params = self._gather_params()
        if not self._ensure_effect(params.effect_type):
            return
        dev = self.controller._device
        if dev is None:
            return
        try:
            dev.set_effect(build_set_effect(params, self._effect_idx))
            self._send_payload(dev, params, self._effect_idx)
            dev.set_envelope(build_envelope(params, self._effect_idx))
            dev.effect_operation(EffectOperationReportData(
                effectBlockIndex=self._effect_idx,
                effectOperation=1,
                loopCount=max(0, min(255, self.spn_loop.value()))))
        except Exception as exc:  # noqa: BLE001
            self._log(f"start failed: {exc}")
            return
        self._main_params = params
        self._model = ForceModel(params, self._spring_params)
        self._run_started_at = time.monotonic()
        dur = params.duration_ms
        loop = self.spn_loop.value()
        self._run_total_ms = dur * loop if (0 < dur < 0xFFFF and loop > 0) else 0
        if not self._timer.isActive():
            self._timer.start()
        self._log(f"started {params.effect_type} block {self._effect_idx} "
                  f"loop {self.spn_loop.value()}")

    def apply_main_effect(self) -> bool:
        params = self._gather_params()
        if not self._ensure_effect(params.effect_type):
            return False
        dev = self.controller._device
        if dev is None:
            return False
        try:
            dev.set_effect(build_set_effect(params, self._effect_idx))
            self._send_payload(dev, params, self._effect_idx)
            dev.set_envelope(build_envelope(params, self._effect_idx))
        except Exception as exc:  # noqa: BLE001
            self._log(f"apply failed: {exc}")
            return False
        self._main_params = params
        self._model = ForceModel(params, self._spring_params)
        self._log(f"applied {params.effect_type} block {self._effect_idx}")
        return True

    def apply_spring(self) -> bool:
        if not self.controller.is_connected:
            self._log("device not connected")
            return False
        main = self._gather_params()
        spring = EffectParams(
            effect_type=SPRING,
            pos_coeff_x=self.spn_spring.value(), neg_coeff_x=self.spn_spring.value(),
            pos_coeff_y=self.spn_spring.value(), neg_coeff_y=self.spn_spring.value(),
            pos_sat_x=self.spn_cond_sat.value(), neg_sat_x=self.spn_cond_sat.value(),
            pos_sat_y=self.spn_cond_sat.value(), neg_sat_y=self.spn_cond_sat.value(),
            dead_band_x=0, dead_band_y=0, center_x=0, center_y=0,
            duration_ms=main.duration_ms, gain=main.gain,
        )
        if not self._ensure_effect(SPRING):
            return False
        dev = self.controller._device
        if dev is None:
            return False
        try:
            dev.set_effect(build_set_effect(spring, self._effect_idx))
            dev.set_condition(build_condition(spring, self._effect_idx, 'x'))
            dev.set_condition(build_condition(spring, self._effect_idx, 'y'))
        except Exception as exc:  # noqa: BLE001
            self._log(f"spring apply failed: {exc}")
            return False
        self._spring_params = spring
        self._model = ForceModel(main, spring)
        self._log(f"applied spring block {self._effect_idx}")
        return True

    def start_effect(self):
        self._apply_and_start()

    def stop_effect(self):
        if self._effect_idx == 0:
            return
        dev = self.controller._device
        if dev is not None:
            try:
                dev.effect_operation(EffectOperationReportData(
                    effectBlockIndex=self._effect_idx, effectOperation=3, loopCount=0))
            except Exception as exc:  # noqa: BLE001
                self._log(f"stop failed: {exc}")
        self._timer.stop()
        self._run_total_ms = 0
        self._log(f"stopped block {self._effect_idx}")

    def _log(self, msg: str) -> None:
        stamp = time.strftime("%H:%M:%S")
        self.log.appendPlainText(f"[{stamp}] {msg}")

    def closeEvent(self, event):
        self._timer.stop()
        self.controller.disconnect()
        super().closeEvent(event)
