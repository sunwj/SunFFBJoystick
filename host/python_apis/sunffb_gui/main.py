# GUI coordination: controls -> EffectParams -> HID reports, alongside local force prediction.
# Main effects and background spring use separate allocated blocks; do not stop one when replacing the other.
# Predicted force is not measured motor force; positions come from the device polling worker.
# Condition effects send separate axis 0/1 blocks; direction fields cannot replace the second condition.

from __future__ import annotations

import ctypes as ct
import math
import time
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
                             QComboBox, QSpinBox, QSlider, QCheckBox, QPushButton, QLabel,
                             QPlainTextEdit, QGroupBox, QDoubleSpinBox, QSplitter,
                             QScrollArea, QFormLayout)

try:
    from sunffb_hid import (SetEffectReportData, SetEnvelopeReportData, SetConditionReportData,
                            SetPeriodicReportData, SetConstantForceReportData, SetRampForceReportData,
                            EffectOperationReportData, ET_CONSTANT, ET_RAMP, ET_SINE, ET_SQUARE,
                            ET_TRIANGLE, ET_SAWTOOTH_UP, ET_SAWTOOTH_DOWN, ET_SPRING, ET_DAMPER,
                            ET_INERTIA, ET_FRICTION, X_AXIS_ENABLE, DIRECTION_ENABLE,
                            USB_NO_TRIGGER_BUTTON, NUM_AXIS)
except ImportError:
    from ..sunffb_hid import (SetEffectReportData, SetEnvelopeReportData, SetConditionReportData,
                                        SetPeriodicReportData, SetConstantForceReportData, SetRampForceReportData,
                                        EffectOperationReportData, ET_CONSTANT, ET_RAMP, ET_SINE, ET_SQUARE,
                                        ET_TRIANGLE, ET_SAWTOOTH_UP, ET_SAWTOOTH_DOWN, ET_SPRING, ET_DAMPER,
                                        ET_INERTIA, ET_FRICTION, X_AXIS_ENABLE, DIRECTION_ENABLE,
                                        USB_NO_TRIGGER_BUTTON, NUM_AXIS)

try:
    from .force_model import (EffectParams, ForceModel, Kinematics, CONSTANT, RAMP, SINE,
                              SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN, SPRING, DAMPER,
                              INERTIA, FRICTION)
    from .device import DeviceController
    from .widgets import DirectionPad, ForceCanvas, direction_from_pad
except ImportError:
    from sunffb_gui.force_model import (EffectParams, ForceModel, Kinematics, CONSTANT, RAMP, SINE,
                                        SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN, SPRING, DAMPER,
                                        INERTIA, FRICTION)
    from sunffb_gui.device import DeviceController
    from sunffb_gui.widgets import DirectionPad, ForceCanvas, direction_from_pad

Y_AXIS_ENABLE = 0x02 if NUM_AXIS >= 2 else 0

ET_MAP = {CONSTANT: ET_CONSTANT, RAMP: ET_RAMP, SINE: ET_SINE, SQUARE: ET_SQUARE,
          TRIANGLE: ET_TRIANGLE, SAWTOOTH_UP: ET_SAWTOOTH_UP, SAWTOOTH_DOWN: ET_SAWTOOTH_DOWN,
          SPRING: ET_SPRING, DAMPER: ET_DAMPER, INERTIA: ET_INERTIA, FRICTION: ET_FRICTION}
CONDITION_TYPES = {SPRING, DAMPER, INERTIA, FRICTION}
PERIODIC_TYPES = {SINE, SQUARE, TRIANGLE, SAWTOOTH_UP, SAWTOOTH_DOWN}


# Translate GUI units to the packed common report. Conditions select axis bits without direction projection;
# other effects use a hundredths-of-degree angle and a directed force vector.
def build_set_effect(params: EffectParams, idx: int) -> SetEffectReportData:
    if params.effect_type in CONDITION_TYPES:
        axis_enable = (X_AXIS_ENABLE if params.apply_x else 0)
        if NUM_AXIS >= 2 and params.apply_y:
            axis_enable |= Y_AXIS_ENABLE
        directions = (ct.c_uint16 * NUM_AXIS)(*([0] * NUM_AXIS))
    else:
        axis_enable = X_AXIS_ENABLE | (Y_AXIS_ENABLE if NUM_AXIS >= 2 else 0) | DIRECTION_ENABLE
        theta = int(round(params.direction_deg * 100)) % 36000
        directions = (ct.c_uint16 * NUM_AXIS)(*([theta] + [0] * (NUM_AXIS - 1)))
    return SetEffectReportData(
        effectBlockIndex=idx,
        effectType=ET_MAP[params.effect_type],
        duration=(0xFFFF if params.effect_type in CONDITION_TYPES or params.duration_ms >= 0xFFFF
                  else max(1, params.duration_ms)),
        triggerRepeatInterval=0,
        samplePeriod=0,
        gain=max(0, min(255, params.gain)),
        triggerButton=USB_NO_TRIGGER_BUTTON,
        axisEnable=axis_enable,
        directions=directions,
        startDelay=0,
    )


# Clamp magnitude nonnegative and wrap phase to one cycle; offset remains signed.
def build_periodic(params, idx):  # -> SetPeriodicReportData
    return SetPeriodicReportData(effectBlockIndex=idx, magnitude=max(0, params.magnitude),
                                 offset=params.offset, phase=params.phase % 36000,
                                 period=max(1, params.period_ms))


# Each axis gets its own offset, coefficients, saturations and deadband in nominal 10000-unit scale.
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


# Build type-specific magnitude only; common duration, gain and direction are in Set Effect.
def build_constant(params, idx):
    return SetConstantForceReportData(effectBlockIndex=idx, magnitude=max(-10000, min(10000, params.magnitude)))


# Use ramp endpoints, not the generic magnitude control, for the type-specific payload.
def build_ramp(params, idx):
    return SetRampForceReportData(effectBlockIndex=idx, rampStart=params.ramp_start, rampEnd=params.ramp_end)


# Attack/fade levels are absolute amplitudes; time fields use milliseconds.
def build_envelope(params, idx):
    return SetEnvelopeReportData(effectBlockIndex=idx, attackLevel=params.attack_level,
                                 fadeLevel=params.fade_level, attackTime=params.attack_time_ms,
                                 fadeTime=params.fade_time_ms)


class MainWindow(QMainWindow):
    def __init__(self, show_hid: bool = True, parent=None):
        super().__init__(parent)
        self.setWindowTitle("SunFFB Joystick GUI Tester")
        self.resize(1280, 860)
        self.controller = DeviceController()
        self._effect_idx = 0
        self._effect_type = None
        self._spring_idx = 0
        self._main_params = EffectParams()
        self._spring_params = None
        self._run_started_at = 0.0
        self._run_total_ms = 0
        self._last_kin = Kinematics()
        self._last_sample_time = None
        self._last_position = None
        self._last_velocity = None
        self._model = None

        central = QWidget(self)
        root = QHBoxLayout(central)
        left = QVBoxLayout()
        right = QVBoxLayout()
        # Match FFBTestTool: live device/force view on the left, controls on the right.
        root.addLayout(right, 1)
        root.addLayout(left, 1)
        self.setCentralWidget(central)

        dev_group = QGroupBox("Device", self)
        dev_layout = QHBoxLayout(dev_group)
        self.cmb_devices = QComboBox(dev_group)
        self.btn_rescan = QPushButton("Rescan", dev_group)
        self.btn_connect = QPushButton("Connect", dev_group)
        self.btn_log = QPushButton("Open log", dev_group)
        self.btn_connect.clicked.connect(self.toggle_connect)
        self.btn_rescan.clicked.connect(self.rescan_devices)
        self.btn_log.clicked.connect(lambda: self._log("session log is shown below"))
        self.lbl_status = QLabel("not connected", dev_group)
        dev_layout.addWidget(self.cmb_devices, 2)
        dev_layout.addWidget(self.btn_rescan)
        dev_layout.addWidget(self.btn_connect)
        dev_layout.addWidget(self.btn_log)
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
        self.spn_ramp_end.setValue(4000)
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
        self.spn_dir.valueChanged.connect(self.pad.set_angle)
        self.pad.set_angle(self.spn_dir.value())
        left.addWidget(params_group)

        spring_group = QGroupBox("Background spring safeguard", self)
        spring_layout = QHBoxLayout(spring_group)
        self.chk_spring_on = QCheckBox("on (centring safeguard)", spring_group)
        self.chk_spring_on.setChecked(True)
        self.spn_bg_spring = QSpinBox(spring_group)
        self.spn_bg_spring.setRange(0, 10000)
        self.spn_bg_spring.setValue(8000)
        spring_layout.addWidget(self.chk_spring_on)
        spring_layout.addWidget(QLabel("stiffness", spring_group))
        spring_layout.addWidget(self.spn_bg_spring)
        left.addWidget(spring_group)

        btns = QHBoxLayout()
        self.btn_start = QPushButton("Start", self)
        self.btn_start.clicked.connect(self.start_effect)
        self.btn_stop = QPushButton("Stop", self)
        self.btn_stop.clicked.connect(self.stop_effect)
        self.btn_spring = QPushButton("Apply spring", self)
        self.btn_spring.clicked.connect(self.apply_spring)
        self.chk_effect_on = QCheckBox("effect on", self)
        self.chk_effect_on.stateChanged.connect(self._on_effect_toggle)
        self.chk_spring_on.stateChanged.connect(self._on_spring_toggle)
        btns.insertWidget(0, self.chk_effect_on)
        btns.addWidget(self.btn_start)
        btns.addWidget(self.btn_stop)
        btns.addWidget(self.btn_spring)
        left.addLayout(btns)
        # FFBTestTool uses checkboxes for playback; retain the buttons as a
        # compatibility surface for tests and scripts, but keep them out of
        # the normal GUI workflow.
        self.btn_start.hide()
        self.btn_stop.hide()
        self.btn_spring.hide()
        left.addStretch(1)

        right.addWidget(QLabel("force", self))
        self.canvas = ForceCanvas(self)
        right.addWidget(self.canvas, 3)
        self.lbl_pos = QLabel("pos: (0, 0)", self)
        right.addWidget(self.lbl_pos)
        self.log = QPlainTextEdit(self)
        self.log.setReadOnly(True)
        right.addWidget(self.log, 2)

        self._build_reference_layout()

        self._timer = QTimer(self)
        self._timer.setInterval(20)
        self._timer.timeout.connect(self._on_tick)
        for control in (self.spn_dir, self.spn_mag, self.spn_duration, self.spn_gain,
                        self.spn_period, self.spn_phase, self.spn_offset,
                        self.spn_ramp_start, self.spn_ramp_end, self.spn_loop,
                        self.spn_cond_coeff, self.spn_cond_sat, self.spn_cond_db,
                        self.spn_cond_center, self.spn_bg_spring,
                        self.spn_spring_x, self.spn_spring_y,
                        self.spn_ramp_duration, self.spn_cond_neg_coeff,
                        self.spn_cond_neg_sat, self.spn_attack_level,
                        self.spn_attack_time, self.spn_fade_level, self.spn_fade_time):
            control.valueChanged.connect(self._on_live_change)
        self.cmb_type.currentIndexChanged.connect(self._on_live_change)
        for control in (self.chk_infinite, self.chk_envelope, self.chk_axis_x, self.chk_axis_y):
            control.stateChanged.connect(self._on_live_change)

        if show_hid:
            self.rescan_devices()
            self.toggle_connect()
        self._log("ready")

    # Link spin box and slider values with Qt signals while retaining a reusable labeled layout.
    def _slider_row(self, label: str, spin: QSpinBox, parent=None) -> QWidget:
        row = QWidget(parent)
        layout = QHBoxLayout(row)
        layout.setContentsMargins(4, 1, 4, 1)
        caption = QLabel(label, row)
        caption.setMinimumWidth(126)
        slider = QSlider(Qt.Orientation.Horizontal, row)
        if isinstance(spin, QDoubleSpinBox):
            slider.setRange(round(spin.minimum() * 10), round(spin.maximum() * 10))
            slider.setValue(round(spin.value() * 10))
            slider.setSingleStep(max(1, round(spin.singleStep() * 10)))
            slider.valueChanged.connect(lambda value: spin.setValue(value / 10.0))
            spin.valueChanged.connect(lambda value: slider.setValue(round(value * 10)))
        else:
            slider.setRange(spin.minimum(), spin.maximum())
            slider.setValue(spin.value())
            slider.setSingleStep(max(1, spin.singleStep()))
            slider.valueChanged.connect(spin.setValue)
            spin.valueChanged.connect(slider.setValue)
        spin.setFixedWidth(82)
        layout.addWidget(caption)
        layout.addWidget(slider, 1)
        layout.addWidget(spin)
        return row

    # Group shared controls and effect-specific panels; signal wiring drives later report updates.
    def _build_reference_layout(self):
        """Build the visible UI following FFBTestTool's MainForm layout."""
        central = QWidget(self)
        root = QHBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        split = QSplitter(Qt.Orientation.Horizontal, central)
        root.addWidget(split)
        self.setCentralWidget(central)

        # Left: device/yoke visualisation and a fixed-width diagnostic readout.
        view = QWidget(split)
        view_layout = QVBoxLayout(view)
        view_layout.setContentsMargins(8, 6, 8, 6)
        rotation_row = QHBoxLayout()
        rotation_row.addWidget(QLabel("Device rotation range", view))
        self.spn_rotation = QSpinBox(view)
        self.spn_rotation.setRange(10, 3600)
        self.spn_rotation.setValue(180)
        self.spn_rotation.setSuffix(" deg")
        rotation_row.addWidget(self.spn_rotation)
        rotation_row.addStretch(1)
        view_layout.addLayout(rotation_row)
        self.canvas.setStyleSheet("background:#11151b; color:#c4d0c4;")
        view_layout.addWidget(self.canvas, 1)
        view_layout.addWidget(self.lbl_pos)
        self.lbl_debug = QLabel(view)
        self.lbl_debug.setMinimumHeight(112)
        self.lbl_debug.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
        self.lbl_debug.setStyleSheet("background:#11151b;color:#c4d0c4;font-family:Consolas;padding:6px;")
        view_layout.addWidget(self.lbl_debug)
        self.log.setMaximumBlockCount(1000)
        self.log.setFixedHeight(100)
        self.log.setStyleSheet("background:#11151b;color:#c4d0c4;font-family:Consolas;")
        view_layout.addWidget(self.log)

        # Right: scrollable control console.
        scroll = QScrollArea(split)
        scroll.setWidgetResizable(True)
        controls = QWidget(scroll)
        control_layout = QVBoxLayout(controls)
        control_layout.setContentsMargins(8, 8, 8, 8)
        scroll.setWidget(controls)

        device_group = QGroupBox("Device", controls)
        device_layout = QVBoxLayout(device_group)
        device_row = QHBoxLayout()
        for widget, stretch in ((self.cmb_devices, 1), (self.btn_rescan, 0),
                                (self.btn_connect, 0), (self.btn_log, 0)):
            device_row.addWidget(widget, stretch)
        device_layout.addLayout(device_row)
        device_layout.addWidget(self.lbl_status)
        self.spn_device_gain = QSpinBox(device_group)
        self.spn_device_gain.setRange(0, 10000)
        self.spn_device_gain.setValue(10000)
        self.spn_device_gain.valueChanged.connect(
            lambda value: self.controller.set_device_gain(round(value * 255 / 10000))
            if self.controller.is_connected else None)
        device_layout.addWidget(self._slider_row("Device gain", self.spn_device_gain, device_group))
        control_layout.addWidget(device_group)

        effects_row = QHBoxLayout()
        spring_group = QGroupBox("Spring Effect", controls)
        spring_layout = QVBoxLayout(spring_group)
        spring_layout.addWidget(self.chk_spring_on)
        spring_layout.addWidget(self._slider_row("Magnitude", self.spn_bg_spring, spring_group))
        self.spn_spring_x = QSpinBox(spring_group)
        self.spn_spring_y = QSpinBox(spring_group)
        for spin in (self.spn_spring_x, self.spn_spring_y):
            spin.setRange(-10000, 10000)
        spring_layout.addWidget(self._slider_row("Offset X", self.spn_spring_x, spring_group))
        spring_layout.addWidget(self._slider_row("Offset Y", self.spn_spring_y, spring_group))
        effects_row.addWidget(spring_group, 1)

        effect_group = QGroupBox("Effect", controls)
        effect_layout = QVBoxLayout(effect_group)
        type_row = QHBoxLayout()
        type_row.addWidget(self.chk_effect_on)
        type_row.addWidget(self.cmb_type, 1)
        effect_layout.addLayout(type_row)
        effect_layout.addWidget(self._slider_row("Magnitude", self.spn_mag, effect_group))
        effect_layout.addWidget(self._slider_row("Direction °", self.spn_dir, effect_group))
        effect_layout.addWidget(self.pad)
        self.duration_row = self._slider_row("Duration ms", self.spn_duration, effect_group)
        duration_row = self.duration_row
        self.chk_infinite = QCheckBox("inf", duration_row)
        self.chk_infinite.setChecked(True)
        duration_row.layout().insertWidget(2, self.chk_infinite)
        effect_layout.addWidget(duration_row)
        effect_layout.addWidget(self._slider_row("Effect gain", self.spn_gain, effect_group))
        # ``params_group`` is replaced by this reference layout below.  Keep
        # every legacy control that is still used by the start path parented
        # to a live widget; otherwise Qt destroys it with the old group and
        # ``start_effect`` sees a wrapped/deleted QSpinBox.
        effect_layout.addWidget(self._slider_row("Loop count", self.spn_loop, effect_group))
        effects_row.addWidget(effect_group, 1)
        control_layout.addLayout(effects_row)

        self.grp_periodic = QGroupBox("Periodic  (square / sine / triangle / sawtooth)", controls)
        periodic_layout = QVBoxLayout(self.grp_periodic)
        periodic_layout.addWidget(self._slider_row("Period ms", self.spn_period, self.grp_periodic))
        periodic_layout.addWidget(self._slider_row("Phase °×100", self.spn_phase, self.grp_periodic))
        periodic_layout.addWidget(self._slider_row("Offset", self.spn_offset, self.grp_periodic))
        control_layout.addWidget(self.grp_periodic)

        self.grp_ramp = QGroupBox("Ramp force", controls)
        ramp_layout = QVBoxLayout(self.grp_ramp)
        ramp_layout.addWidget(self._slider_row("Ramp start", self.spn_ramp_start, self.grp_ramp))
        ramp_layout.addWidget(self._slider_row("Ramp end", self.spn_ramp_end, self.grp_ramp))
        self.spn_ramp_duration = QSpinBox(self.grp_ramp)
        self.spn_ramp_duration.setRange(1, 20000)
        self.spn_ramp_duration.setValue(1500)
        ramp_layout.addWidget(self._slider_row("Ramp duration ms", self.spn_ramp_duration, self.grp_ramp))
        control_layout.addWidget(self.grp_ramp)

        self.grp_condition = QGroupBox("Condition  (spring / damper / inertia / friction)", controls)
        condition_layout = QVBoxLayout(self.grp_condition)
        self.spn_cond_neg_coeff = QSpinBox(self.grp_condition)
        self.spn_cond_neg_coeff.setRange(-10000, 10000)
        self.spn_cond_neg_coeff.setValue(self.spn_cond_coeff.value())
        self.spn_cond_neg_sat = QSpinBox(self.grp_condition)
        self.spn_cond_neg_sat.setRange(0, 10000)
        self.spn_cond_neg_sat.setValue(self.spn_cond_sat.value())
        condition_layout.addWidget(self._slider_row("Pos coeff", self.spn_cond_coeff, self.grp_condition))
        condition_layout.addWidget(self._slider_row("Neg coeff", self.spn_cond_neg_coeff, self.grp_condition))
        condition_layout.addWidget(self._slider_row("Pos saturation", self.spn_cond_sat, self.grp_condition))
        condition_layout.addWidget(self._slider_row("Neg saturation", self.spn_cond_neg_sat, self.grp_condition))
        condition_layout.addWidget(self._slider_row("Dead band", self.spn_cond_db, self.grp_condition))
        condition_layout.addWidget(self._slider_row("Centre offset", self.spn_cond_center, self.grp_condition))
        axes_row = QHBoxLayout()
        axes_row.addWidget(QLabel("Axes", self.grp_condition))
        self.chk_axis_x = QCheckBox("roll (X)", self.grp_condition)
        self.chk_axis_y = QCheckBox("pitch (Y)", self.grp_condition)
        self.chk_axis_x.setChecked(True)
        self.chk_axis_y.setChecked(NUM_AXIS >= 2)
        self.chk_axis_y.setEnabled(NUM_AXIS >= 2)
        axes_row.addWidget(self.chk_axis_x)
        axes_row.addWidget(self.chk_axis_y)
        axes_row.addStretch(1)
        condition_layout.addLayout(axes_row)
        control_layout.addWidget(self.grp_condition)

        self.grp_envelope = QGroupBox("Envelope  (constant / ramp / periodic)", controls)
        envelope_layout = QVBoxLayout(self.grp_envelope)
        self.chk_envelope = QCheckBox("use envelope", self.grp_envelope)
        envelope_layout.addWidget(self.chk_envelope)
        self.spn_attack_level = QSpinBox(self.grp_envelope)
        self.spn_attack_time = QSpinBox(self.grp_envelope)
        self.spn_fade_level = QSpinBox(self.grp_envelope)
        self.spn_fade_time = QSpinBox(self.grp_envelope)
        for spin in (self.spn_attack_level, self.spn_fade_level):
            spin.setRange(0, 10000)
        for spin in (self.spn_attack_time, self.spn_fade_time):
            spin.setRange(0, 5000)
            spin.setValue(200)
        envelope_layout.addWidget(self._slider_row("Attack level", self.spn_attack_level, self.grp_envelope))
        envelope_layout.addWidget(self._slider_row("Attack ms", self.spn_attack_time, self.grp_envelope))
        envelope_layout.addWidget(self._slider_row("Fade level", self.spn_fade_level, self.grp_envelope))
        envelope_layout.addWidget(self._slider_row("Fade ms", self.spn_fade_time, self.grp_envelope))
        control_layout.addWidget(self.grp_envelope)
        control_layout.addStretch(1)

        split.addWidget(view)
        split.addWidget(scroll)
        split.setSizes([530, 750])
        self.cmb_type.currentIndexChanged.connect(self._update_effect_panels)
        self._update_effect_panels()

    # Disable irrelevant controls so the UI does not imply they affect the selected model.
    def _update_effect_panels(self, *_args):
        effect_type = str(self.cmb_type.currentData())
        self.grp_periodic.setEnabled(effect_type in PERIODIC_TYPES)
        self.grp_ramp.setEnabled(effect_type == RAMP)
        self.grp_condition.setEnabled(effect_type in CONDITION_TYPES)
        self.grp_envelope.setEnabled(effect_type not in CONDITION_TYPES)
        is_condition = effect_type in CONDITION_TYPES
        is_ramp = effect_type == RAMP
        self.duration_row.setEnabled(not is_condition and not is_ramp)
        self.spn_mag.parentWidget().setEnabled(not is_condition and not is_ramp)
        self.grp_ramp.setToolTip("Ramp uses start/end force and its own duration; Magnitude does not apply.")
        if is_condition:
            self.chk_infinite.setChecked(True)
        self.chk_infinite.setEnabled(not is_condition and not is_ramp)

    # Enumeration updates the selector only; it neither opens a device nor starts effects.
    def rescan_devices(self):
        self.cmb_devices.clear()
        try:
            devices = self.controller.enumerate()
            for info in devices:
                label = (getattr(info, "product_string", None)
                         or getattr(info, "manufacturer_string", None)
                         or getattr(info, "path", "SunFFB device"))
                self.cmb_devices.addItem(str(label), info)
            self._log(f"found {len(devices)} device(s)")
        except Exception as exc:  # noqa: BLE001
            self._log(f"rescan failed: {exc}")

    # Toggle only the main block; background spring retains independent ownership and lifecycle.
    def _on_effect_toggle(self, state: int):
        if state and self.controller.is_connected:
            self.start_effect()
        elif not state:
            self._stop_main_effect()

    # Stop/free only the spring block on deselection, preserving a running main effect.
    def _on_spring_toggle(self, state: int):
        if state and self.controller.is_connected:
            self.apply_spring()
        elif not state and self._spring_idx:
            idx = self._spring_idx
            dev = self.controller._device
            if dev is not None:
                try:
                    dev.effect_operation(EffectOperationReportData(
                        effectBlockIndex=idx, effectOperation=3, loopCount=0))
                    self.controller.free_effect(idx)
                except Exception as exc:  # noqa: BLE001
                    self._log(f"spring stop failed: {exc}")
            self._spring_idx = 0
            self._spring_params = None
            self._model = ForceModel(self._main_params) if self._effect_idx else None
            self._update_canvas()

    # Parameter edits update an existing block; type or timing edits need allocation/restart to stay synchronized.
    def _on_live_change(self, *_args):
        if self.sender() in (self.spn_bg_spring, self.spn_spring_x, self.spn_spring_y):
            if self.chk_spring_on.isChecked():
                self.apply_spring()
            return
        if self.controller.is_connected and self.chk_effect_on.isChecked():
            # New blocks are not playing until an Effect Operation is sent.
            # Timing edits also need a fresh start to synchronize the device
            # duration/loop count and the GUI's expiry timer.
            timing_controls = (self.spn_duration, self.spn_ramp_duration,
                               self.spn_loop, self.chk_infinite)
            if (not self._effect_idx or self._effect_type != self.cmb_type.currentData()
                    or self.sender() in timing_controls):
                self.start_effect()
            else:
                self.apply_main_effect()

    # Connect signals before starting polling to avoid missing early reports or errors.
    # Disconnect remembers requested toggles but stops/releases current effects before closing the handle.
    def toggle_connect(self):
        if self.controller.is_connected:
            restore_effect = self.chk_effect_on.isChecked()
            restore_spring = self.chk_spring_on.isChecked()
            self.stop_effect()
            self.controller.disconnect()
            self._timer.stop()
            self.btn_connect.setText("Connect")
            self.lbl_status.setText("not connected")
            self._desired_effect_on = restore_effect
            self._desired_spring_on = restore_spring
            self._log("disconnected")
            return
        selected = self.cmb_devices.currentData()
        if self.controller.connect(getattr(selected, "path", None), start_worker=False):
            self.btn_connect.setText("Disconnect")
            self.lbl_status.setText("connected")
            worker = self.controller.worker
            if worker is not None:
                worker.joystick_ready.connect(self._on_joystick)
                worker.pid_ready.connect(self._on_pid)
                worker.error.connect(self._on_error)
            self.controller.start_worker()
            self._log("connected")
            restore_spring = getattr(self, "_desired_spring_on", self.chk_spring_on.isChecked())
            restore_effect = getattr(self, "_desired_effect_on", self.chk_effect_on.isChecked())
            self._set_toggle_silent(self.chk_spring_on, restore_spring)
            if restore_spring:
                self.apply_spring()
            self._set_toggle_silent(self.chk_effect_on, restore_effect)
            if restore_effect:
                self.start_effect()
        else:
            self.lbl_status.setText("not connected")
            detail = self.controller.last_error
            self._log(f"connect failed{': ' + detail if detail else ''}")

    # Estimate normalized motion from host arrival times for preview, not firmware's internal filter state.
    # Discard derivative history after a long gap to avoid a spurious velocity/acceleration spike.
    def _on_joystick(self, joy):
        ax = joy.axis
        now = time.monotonic()
        roll = ax[0] / 32767.0
        pitch = ax[1] / 32767.0 if NUM_AXIS >= 2 else 0.0
        if self._last_sample_time is None or self._last_position is None:
            vel_roll = vel_pitch = acc_roll = acc_pitch = 0.0
        else:
            dt = now - self._last_sample_time
            if dt <= 0.0 or dt > 0.5:
                vel_roll = vel_pitch = acc_roll = acc_pitch = 0.0
            else:
                vel_roll = (roll - self._last_position[0]) / dt
                vel_pitch = (pitch - self._last_position[1]) / dt
                acc_roll = ((vel_roll - self._last_velocity[0]) / dt
                            if self._last_velocity is not None else 0.0)
                acc_pitch = ((vel_pitch - self._last_velocity[1]) / dt
                             if self._last_velocity is not None else 0.0)
        self._last_sample_time = now
        self._last_position = (roll, pitch)
        self._last_velocity = (vel_roll, vel_pitch)
        self._last_kin = Kinematics(roll=roll, pitch=pitch,
                                    vel_roll=vel_roll, vel_pitch=vel_pitch,
                                    acc_roll=acc_roll, acc_pitch=acc_pitch)
        self.lbl_pos.setText("pos: (" + ", ".join(str(ax[i]) for i in range(NUM_AXIS)) + ")")
        self.canvas.set_position(self._last_kin.roll, self._last_kin.pitch)
        self._update_canvas()

    # Display device status bits; this signal does not carry measured output force.
    def _on_pid(self, status: int):
        self.lbl_status.setText(f"connected \u2014 pid {status}")

    # Stop the local session and clear desired playback after an input-worker failure.
    def _on_error(self, msg: str):
        self._log(f"worker error: {msg}")
        self.stop_effect()
        self.controller.disconnect()
        self._desired_effect_on = False
        self._desired_spring_on = False
        self.btn_connect.setText("Connect")
        self.lbl_status.setText("not connected")

    # Expire finite main playback while preserving background spring, then refresh local prediction.
    def _on_tick(self):
        if self._run_total_ms > 0:
            elapsed_ms = self._elapsed_ms()
            if elapsed_ms >= self._run_total_ms:
                self._stop_main_effect()
        self._update_canvas()

    # Use monotonic time for playback duration; wall-clock adjustments must not affect phase.
    def _elapsed_ms(self) -> float:
        if self._run_started_at == 0.0:
            return 0.0
        return (time.monotonic() - self._run_started_at) * 1000.0

    # Predict each iteration using wrapped main-effect time; this is commanded/model force, not motor telemetry.
    def _update_canvas(self):
        if self._model is None:
            fx, fy = 0.0, 0.0
        else:
            elapsed = self._elapsed_ms()
            duration = self._main_params.duration_ms
            if 0 < duration < 0xFFFF:
                elapsed %= duration
            fx, fy = self._model.evaluate_combined(self._last_kin, elapsed)
        self.canvas.set_force(fx, fy)
        if hasattr(self, "lbl_debug"):
            mag = math.hypot(fx, fy)
            direction = math.degrees(math.atan2(-fx, fy)) % 360.0 if mag >= 1 else 0.0
            self.lbl_debug.setText(
                f"DEVICE  {'connected' if self.controller.is_connected else 'not connected'}"
                f"    axes {'XY' if NUM_AXIS >= 2 else 'X'}\n"
                f"POS     roll {self._last_kin.roll:+.3f}  pitch {self._last_kin.pitch:+.3f}\n"
                f"SPRING  {'ON' if self._spring_idx else 'off'}"
                f"    EFFECT {'ON' if self._effect_idx else 'off'}"
                f"  {self._main_params.effect_type}\n\n"
                f"COMMANDED FORCE   Fx {fx:+.0f}   Fy {fy:+.0f}\n"
                f"                  |F| {mag:.0f}   dir {direction:.1f}°")

    # Snapshot controls into EffectParams; conditions are indefinite, and ramps use their dedicated duration.
    def _gather_params(self) -> EffectParams:
        cond = self.spn_cond_coeff.value()
        effect_type = str(self.cmb_type.currentData())
        duration = self.spn_ramp_duration.value() if effect_type == RAMP else self.spn_duration.value()
        if effect_type in CONDITION_TYPES:
            duration = 0xFFFF
        elif effect_type != RAMP and self.chk_infinite.isChecked():
            duration = 0xFFFF
        use_envelope = self.chk_envelope.isChecked() and effect_type not in CONDITION_TYPES
        return EffectParams(
            effect_type=effect_type,
            magnitude=self.spn_mag.value(),
            direction_deg=float(self.spn_dir.value()),
            period_ms=self.spn_period.value(),
            phase=self.spn_phase.value(),
            offset=self.spn_offset.value(),
            ramp_start=self.spn_ramp_start.value(),
            ramp_end=self.spn_ramp_end.value(),
            gain=self.spn_gain.value(),
            pos_coeff_x=cond, neg_coeff_x=self.spn_cond_neg_coeff.value(),
            pos_coeff_y=cond, neg_coeff_y=self.spn_cond_neg_coeff.value(),
            pos_sat_x=self.spn_cond_sat.value(), neg_sat_x=self.spn_cond_neg_sat.value(),
            pos_sat_y=self.spn_cond_sat.value(), neg_sat_y=self.spn_cond_neg_sat.value(),
            dead_band_x=self.spn_cond_db.value(), dead_band_y=self.spn_cond_db.value(),
            center_x=self.spn_cond_center.value(), center_y=self.spn_cond_center.value(),
            duration_ms=duration,
            attack_level=self.spn_attack_level.value() if use_envelope else 0,
            attack_time_ms=self.spn_attack_time.value() if use_envelope else 0,
            fade_level=self.spn_fade_level.value() if use_envelope else 0,
            fade_time_ms=self.spn_fade_time.value() if use_envelope else 0,
            apply_x=self.chk_axis_x.isChecked(),
            apply_y=self.chk_axis_y.isChecked(),
        )

    # Reuse only a matching main-effect type; stop/free a replaced block before requesting another ID.
    def _ensure_effect(self, effect_type: str) -> bool:
        if not self.controller.is_connected:
            self._log("device not connected")
            return False
        if self._effect_idx == 0 or self._effect_type != effect_type:
            old_idx = self._effect_idx
            self._effect_idx = 0
            try:
                if old_idx:
                    dev = self.controller._device
                    if dev is not None:
                        dev.effect_operation(EffectOperationReportData(
                            effectBlockIndex=old_idx, effectOperation=3, loopCount=0))
                    self.controller.free_effect(old_idx)
                idx = self.controller.create_effect(ET_MAP[effect_type])
            except Exception as exc:  # noqa: BLE001
                self._log(f"create effect failed: {exc}")
                return False
            if idx == 0:
                self._effect_type = None
                self._log("create effect failed")
                return False
            self._effect_idx = idx
            self._effect_type = effect_type
        return True

    # Allocate the background spring independently and retain its ID across main-effect changes.
    def _ensure_spring(self) -> bool:
        if not self.controller.is_connected:
            self._log("device not connected")
            return False
        if self._spring_idx == 0:
            idx = self.controller.create_effect(ET_SPRING)
            if idx == 0:
                self._log("create spring effect failed")
                return False
            self._spring_idx = idx
        return True

    @staticmethod
    # Send enabled condition axes individually, or the appropriate scalar type-specific report.
    # Do not send an envelope here: it is separate and does not apply to conditions.
    def _send_payload(dev, params, idx):
        if params.effect_type in CONDITION_TYPES:
            if params.apply_x:
                dev.set_condition(build_condition(params, idx, 'x'))
            if NUM_AXIS >= 2 and params.apply_y:
                dev.set_condition(build_condition(params, idx, 'y'))
        elif params.effect_type in PERIODIC_TYPES:
            dev.set_periodic(build_periodic(params, idx))
        elif params.effect_type == CONSTANT:
            dev.set_constant_force(build_constant(params, idx))
        elif params.effect_type == RAMP:
            dev.set_ramp_force(build_ramp(params, idx))

    # Send common and type-specific parameters before Effect Operation; update local timing only after success.
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
            if params.effect_type not in CONDITION_TYPES:
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
        self._run_total_ms = dur * max(1, loop) if (0 < dur < 0xFFFF and loop != 0xFF) else 0
        if not self._timer.isActive():
            self._timer.start()
        self._log(f"started {params.effect_type} block {self._effect_idx} "
                  f"loop {self.spn_loop.value()}")

    # Reconfigure the current main block without restarting its timeline; start paths are handled separately.
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
            if params.effect_type not in CONDITION_TYPES:
                dev.set_envelope(build_envelope(params, self._effect_idx))
        except Exception as exc:  # noqa: BLE001
            self._log(f"apply failed: {exc}")
            return False
        self._main_params = params
        self._model = ForceModel(params, self._spring_params)
        self._log(f"applied {params.effect_type} block {self._effect_idx}")
        return True

    # Configure/start an independent XY spring block; never use a main-effect envelope to store Y conditions.
    def apply_spring(self) -> bool:
        if not self.controller.is_connected:
            self._log("device not connected")
            return False
        main = self._main_params if self._effect_idx else EffectParams(magnitude=0)
        spring = EffectParams(
            effect_type=SPRING,
            pos_coeff_x=self.spn_bg_spring.value(), neg_coeff_x=self.spn_bg_spring.value(),
            pos_coeff_y=self.spn_bg_spring.value(), neg_coeff_y=self.spn_bg_spring.value(),
            pos_sat_x=self.spn_cond_sat.value(), neg_sat_x=self.spn_cond_sat.value(),
            pos_sat_y=self.spn_cond_sat.value(), neg_sat_y=self.spn_cond_sat.value(),
            dead_band_x=0, dead_band_y=0,
            center_x=self.spn_spring_x.value(), center_y=self.spn_spring_y.value(),
            duration_ms=main.duration_ms, gain=main.gain,
        )
        if not self._ensure_spring():
            return False
        dev = self.controller._device
        if dev is None:
            return False
        try:
            dev.set_effect(build_set_effect(spring, self._spring_idx))
            dev.set_condition(build_condition(spring, self._spring_idx, 'x'))
            if NUM_AXIS >= 2:
                dev.set_condition(build_condition(spring, self._spring_idx, 'y'))
            dev.effect_operation(EffectOperationReportData(
                effectBlockIndex=self._spring_idx,
                effectOperation=1,
                loopCount=0))
        except Exception as exc:  # noqa: BLE001
            self._log(f"spring apply failed: {exc}")
            return False
        self._spring_params = spring
        self._model = ForceModel(main, spring)
        self._log(f"applied spring block {self._spring_idx}")
        return True

    def start_effect(self):
        self._apply_and_start()

    # Stop all GUI-owned effects and clear local prediction/history, but do not disable device-wide actuators.
    def stop_effect(self):
        idxs = [i for i in (self._effect_idx, self._spring_idx) if i]
        dev = self.controller._device
        if dev is not None:
            for idx in idxs:
                try:
                    dev.effect_operation(EffectOperationReportData(
                        effectBlockIndex=idx, effectOperation=3, loopCount=0))
                except Exception as exc:  # noqa: BLE001
                    self._log(f"stop failed: {exc}")
                try:
                    self.controller.free_effect(idx)
                except Exception as exc:  # noqa: BLE001
                    self._log(f"free failed: {exc}")
        self._effect_idx = 0
        self._effect_type = None
        self._spring_idx = 0
        self._spring_params = None
        self._model = None
        self._timer.stop()
        self._run_total_ms = 0
        self._run_started_at = 0.0
        self._set_toggle_silent(self.chk_effect_on, False)
        self._set_toggle_silent(self.chk_spring_on, False)
        self._last_sample_time = None
        self._last_position = None
        self._last_velocity = None
        self._update_canvas()
        self._log("stopped")

    @staticmethod
    # Restore the previous signal-blocking state to avoid recursive start/stop commands during UI updates.
    def _set_toggle_silent(control, checked: bool):
        blocked = control.blockSignals(True)
        control.setChecked(checked)
        control.blockSignals(blocked)

    # Release only the main block; keep the spring preview active if its separate block remains allocated.
    def _stop_main_effect(self):
        if not self._effect_idx:
            return
        idx = self._effect_idx
        dev = self.controller._device
        if dev is not None:
            try:
                dev.effect_operation(EffectOperationReportData(
                    effectBlockIndex=idx, effectOperation=3, loopCount=0))
                self.controller.free_effect(idx)
            except Exception as exc:  # noqa: BLE001
                self._log(f"effect stop failed: {exc}")
        self._effect_idx = 0
        self._effect_type = None
        self._model = ForceModel(EffectParams(magnitude=0), self._spring_params) if self._spring_idx else None
        self._timer.stop()
        self._run_total_ms = 0
        self._run_started_at = 0.0
        self._set_toggle_silent(self.chk_effect_on, False)
        self._update_canvas()
        self._log("effect stopped")

    def _log(self, msg: str) -> None:
        stamp = time.strftime("%H:%M:%S")
        self.log.appendPlainText(f"[{stamp}] {msg}")

    # Stop timers and GUI-owned effects, then stop polling/close the handle before accepting window closure.
    def closeEvent(self, event):
        self._timer.stop()
        self.stop_effect()
        self.controller.disconnect()
        super().closeEvent(event)
