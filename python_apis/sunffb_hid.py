from __future__ import annotations

import os
import ctypes as ct
from dataclasses import dataclass
from typing import Optional, Type, TypeVar, Union

paths = [os.path.join('hidapi', 'hidapi.dll')]
for p in paths:
    try:
       ct.cdll.LoadLibrary(p)
       import hid
       print(f"hidapi loaded from {p}")
       break
    except:
        print(f"Failed to load hidapi from {p}")


# =========================
# Firmware-matched constants
# =========================

NUM_AXIS = 2
MAX_EFFECTS = 15
USB_NO_TRIGGER_BUTTON = 0xFF
USB_DURATION_INFINITE = 0xFFFF
USB_AXIS_MAX_ABSOLUTE = 32767
USB_MAX_MAGNITUDE = 10000
USB_MAX_EFFECT_GAIN = 255
USB_MAX_DEVICE_GAIN = 255

# Effect types
ET_CONSTANT = 0x01
ET_RAMP = 0x02
ET_SQUARE = 0x03
ET_SINE = 0x04
ET_TRIANGLE = 0x05
ET_SAWTOOTH_UP = 0x06
ET_SAWTOOTH_DOWN = 0x07
ET_SPRING = 0x08
ET_DAMPER = 0x09
ET_INERTIA = 0x0A
ET_FRICTION = 0x0B

# Axis enable flags for NUM_AXIS == 2
X_AXIS_ENABLE = 0x01
Y_AXIS_ENABLE = 0x02
DIRECTION_ENABLE = 0x04

# Report IDs
REPORT_ID_JOYSTICK = 1
REPORT_ID_PID_STATE = 2

REPORT_ID_SET_EFFECT_REPORT = 3
REPORT_ID_SET_ENVELOPE_REPORT = 4
REPORT_ID_SET_CONDITION_REPORT = 5
REPORT_ID_SET_PERIODIC_REPORT = 6
REPORT_ID_SET_CONSTANT_FORCE_REPORT = 7
REPORT_ID_SET_RAMP_FORCE_REPORT = 8
REPORT_ID_CUSTOM_FORCE_DATA_REPORT = 9
REPORT_ID_DOWNLOAD_FORCE_SAMPLE = 10
REPORT_ID_EFFECT_OPERATION_REPORT = 11
REPORT_ID_BLOCK_FREE_REPORT = 12
REPORT_ID_DEVICE_CONTROL_REPORT = 13
REPORT_ID_DEVICE_GAIN_REPORT = 14
REPORT_ID_SET_CUSTOM_FORCE_REPORT = 15
REPORT_ID_CREATE_NEW_EFFECT_REPORT = 16
REPORT_ID_BLOCK_LOAD_REPORT = 17
REPORT_ID_POOL_REPORT = 18


T = TypeVar("T", bound="PackedStruct")


class PackedStruct(ct.LittleEndianStructure):
    _pack_ = 1

    def to_bytes(self) -> bytes:
        return ct.string_at(ct.addressof(self), ct.sizeof(self))

    @classmethod
    def sizeof(cls) -> int:
        return ct.sizeof(cls)

    @classmethod
    def from_bytes(cls: Type[T], data: Union[bytes, bytearray, memoryview]) -> T:
        if len(data) < ct.sizeof(cls):
            raise ValueError(f"{cls.__name__}: need {ct.sizeof(cls)} bytes, got {len(data)}")
        return cls.from_buffer_copy(bytes(data[: ct.sizeof(cls)]))


def pack_report(report_id: int, payload: PackedStruct) -> bytes:
    return bytes([report_id]) + payload.to_bytes()


def _as_bytes(raw: Union[bytes, bytearray, list[int]]) -> bytes:
    return bytes(raw) if not isinstance(raw, bytes) else raw


def parse_feature_response(
    report_id: int,
    struct_type: Type[T],
    raw: Union[bytes, bytearray, list[int]],
) -> T:
    raw_bytes = _as_bytes(raw)
    if not raw_bytes:
        raise IOError("Empty HID response")
    if raw_bytes[0] != report_id:
        raise IOError(f"Unexpected report ID: expected {report_id}, got {raw_bytes[0]}")
    return struct_type.from_bytes(raw_bytes[1:])


class JoystickInputReportData(PackedStruct):
    _fields_ = [
        ("buttons", ct.c_uint8),
        ("axis", ct.c_int16 * NUM_AXIS),
    ]


class PIDStateReportData(PackedStruct):
    _fields_ = [
        ("status", ct.c_uint8),
        ("effectBlockIndex", ct.c_uint8),
    ]

    @property
    def device_paused(self) -> bool:
        return bool(self.status & (1 << 0))

    @property
    def actuators_enabled(self) -> bool:
        return bool(self.status & (1 << 1))

    @property
    def safety_switch(self) -> bool:
        return bool(self.status & (1 << 2))

    @property
    def actuator_override_switch(self) -> bool:
        return bool(self.status & (1 << 3))

    @property
    def actuator_power(self) -> bool:
        return bool(self.status & (1 << 4))

    @property
    def effect_playing_flag(self) -> bool:
        return bool(self.effectBlockIndex & 0x01)

    @property
    def playing_effect_id(self) -> int:
        return self.effectBlockIndex >> 1


class SetEffectReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
        ("effectType", ct.c_uint8),
        ("duration", ct.c_uint16),
        ("triggerRepeatInterval", ct.c_uint16),
        ("samplePeriod", ct.c_uint16),
        ("gain", ct.c_uint8),
        ("triggerButton", ct.c_uint8),
        ("axisEnable", ct.c_uint8),
        ("directions", ct.c_uint16 * NUM_AXIS),
        ("startDelay", ct.c_uint16),
    ]


class SetEnvelopeReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
        ("attackLevel", ct.c_uint16),
        ("fadeLevel", ct.c_uint16),
        ("attackTime", ct.c_uint16),
        ("fadeTime", ct.c_uint16),
    ]


class SetConditionReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
        ("parameterBlockOffset", ct.c_uint8),
        ("cpOffset", ct.c_int16),
        ("positiveCoefficient", ct.c_int16),
        ("negativeCoefficient", ct.c_int16),
        ("positiveSaturation", ct.c_uint16),
        ("negativeSaturation", ct.c_uint16),
        ("deadBand", ct.c_uint16),
    ]


class SetPeriodicReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
        ("magnitude", ct.c_uint16),
        ("offset", ct.c_int16),
        ("phase", ct.c_uint16),
        ("period", ct.c_uint16),
    ]


class SetConstantForceReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
        ("magnitude", ct.c_int16),
    ]


class SetRampForceReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
        ("rampStart", ct.c_int16),
        ("rampEnd", ct.c_int16),
    ]


class EffectOperationReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
        ("effectOperation", ct.c_uint8),  # 1=start, 2=start solo, 3=stop
        ("loopCount", ct.c_uint8),
    ]


class BlockFreeReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
    ]


class DeviceControlReportData(PackedStruct):
    _fields_ = [
        ("state", ct.c_uint8),  # 1=enable, 2=disable, 3=stop all, 4=reset, 5=pause, 6=continue
    ]


class DeviceGainReportData(PackedStruct):
    _fields_ = [
        ("gain", ct.c_uint8),
    ]


class CreateNewEffectReportData(PackedStruct):
    _fields_ = [
        ("effectType", ct.c_uint8),
    ]


class BlockLoadReportData(PackedStruct):
    _fields_ = [
        ("effectBlockIndex", ct.c_uint8),
        ("blockLoadStatus", ct.c_uint8),  # 1=success, 2=full, 3=error
        ("ramPoolAvailable", ct.c_uint16),
    ]


class PoolReportData(PackedStruct):
    _fields_ = [
        ("ramPoolSize", ct.c_uint16),
        ("maxSimultaneousEffects", ct.c_uint8),
        ("memoryManagement", ct.c_uint8),
    ]


@dataclass
class SunFFBInfo:
    vendor_id: int
    product_id: int
    manufacturer_string: Optional[str]
    product_string: Optional[str]
    serial_number: Optional[str]
    interface_number: Optional[int]
    usage_page: Optional[int]
    usage: Optional[int]


class SunFFBDevice:
    def __init__(self, vid: int, pid: int):
        self.vid = vid
        self.pid = pid
        self.dev: Optional[hid.device] = None

    @staticmethod
    def enumerate(vid: int = 0, pid: int = 0) -> list[SunFFBInfo]:
        devices: list[SunFFBInfo] = []
        for d in hid.enumerate(vid, pid):
            devices.append(
                SunFFBInfo(
                    vendor_id=d["vendor_id"],
                    product_id=d["product_id"],
                    manufacturer_string=d.get("manufacturer_string"),
                    product_string=d.get("product_string"),
                    serial_number=d.get("serial_number"),
                    interface_number=d.get("interface_number"),
                    usage_page=d.get("usage_page"),
                    usage=d.get("usage"),
                )
            )
        return devices

    def open(self) -> None:
        self.dev = hid.Device(self.vid, self.pid)

    def close(self) -> None:
        if self.dev is not None:
            self.dev.close()
            self.dev = None

    def __enter__(self) -> "SunFFBDevice":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def _require(self) -> hid.device:
        if self.dev is None:
            raise RuntimeError("Device not open")
        return self.dev

    def send_output(self, report_id: int, payload: PackedStruct) -> int:
        data = pack_report(report_id, payload)
        return self._require().write(data)

    def send_feature(self, report_id: int, payload: PackedStruct) -> int:
        data = pack_report(report_id, payload)
        return self._require().send_feature_report(data)

    def get_feature(self, report_id: int, struct_type: Type[T]) -> T:
        total_length = 1 + struct_type.sizeof()
        raw = self._require().get_feature_report(report_id, total_length)
        return parse_feature_response(report_id, struct_type, raw)

    def read_input_once(self, max_length: int = 64, timeout_ms: int = 1000) -> tuple[int, bytes]:
        raw = self._require().read(max_length, timeout_ms)
        if not raw:
            raise TimeoutError("Timed out waiting for input report")
        raw_bytes = bytes(raw)
        return raw_bytes[0], raw_bytes[1:]

    def create_new_effect(self, effect_type: int) -> BlockLoadReportData:
        self.send_feature(
            REPORT_ID_CREATE_NEW_EFFECT_REPORT,
            CreateNewEffectReportData(effectType=effect_type),
        )
        return self.get_block_load_report()

    def get_block_load_report(self) -> BlockLoadReportData:
        return self.get_feature(REPORT_ID_BLOCK_LOAD_REPORT, BlockLoadReportData)

    def get_pool_report(self) -> PoolReportData:
        return self.get_feature(REPORT_ID_POOL_REPORT, PoolReportData)

    def set_effect(self, data: SetEffectReportData) -> int:
        return self.send_output(REPORT_ID_SET_EFFECT_REPORT, data)

    def set_envelope(self, data: SetEnvelopeReportData) -> int:
        return self.send_output(REPORT_ID_SET_ENVELOPE_REPORT, data)

    def set_condition(self, data: SetConditionReportData) -> int:
        return self.send_output(REPORT_ID_SET_CONDITION_REPORT, data)

    def set_periodic(self, data: SetPeriodicReportData) -> int:
        return self.send_output(REPORT_ID_SET_PERIODIC_REPORT, data)

    def set_constant_force(self, data: SetConstantForceReportData) -> int:
        return self.send_output(REPORT_ID_SET_CONSTANT_FORCE_REPORT, data)

    def set_ramp_force(self, data: SetRampForceReportData) -> int:
        return self.send_output(REPORT_ID_SET_RAMP_FORCE_REPORT, data)

    def effect_operation(self, data: EffectOperationReportData) -> int:
        return self.send_output(REPORT_ID_EFFECT_OPERATION_REPORT, data)

    def free_effect(self, effect_block_index: int) -> int:
        return self.send_output(
            REPORT_ID_BLOCK_FREE_REPORT,
            BlockFreeReportData(effectBlockIndex=effect_block_index),
        )

    def device_control(self, state: int) -> int:
        return self.send_output(
            REPORT_ID_DEVICE_CONTROL_REPORT,
            DeviceControlReportData(state=state),
        )

    def set_device_gain(self, gain: int) -> int:
        gain = max(0, min(USB_MAX_DEVICE_GAIN, int(gain)))
        return self.send_output(
            REPORT_ID_DEVICE_GAIN_REPORT,
            DeviceGainReportData(gain=gain),
        )

    def read_joystick_report(self, timeout_ms: int = 1000) -> JoystickInputReportData:
        report_id, payload = self.read_input_once(1 + JoystickInputReportData.sizeof(), timeout_ms)
        if report_id != REPORT_ID_JOYSTICK:
            raise IOError(f"Expected joystick report {REPORT_ID_JOYSTICK}, got {report_id}")
        return JoystickInputReportData.from_bytes(payload)

    def read_pid_state_report(self, timeout_ms: int = 1000) -> PIDStateReportData:
        report_id, payload = self.read_input_once(1 + PIDStateReportData.sizeof(), timeout_ms)
        if report_id != REPORT_ID_PID_STATE:
            raise IOError(f"Expected PID state report {REPORT_ID_PID_STATE}, got {report_id}")
        return PIDStateReportData.from_bytes(payload)

    def create_constant_effect(
        self,
        magnitude: int,
        duration_ms: int = 1000,
        gain: int = USB_MAX_EFFECT_GAIN,
        axis_enable: int = DIRECTION_ENABLE,
        directions: tuple[int, int] = (0, 0),
        start_delay_ms: int = 0,
        loop_count: int = 1,
        start: bool = True,
    ) -> int:
        block = self.create_new_effect(ET_CONSTANT)
        if block.blockLoadStatus != 1 or block.effectBlockIndex == 0:
            raise RuntimeError(
                f"Create effect failed: status={block.blockLoadStatus}, ramPoolAvailable={block.ramPoolAvailable}"
            )

        effect_id = block.effectBlockIndex

        self.set_effect(
            SetEffectReportData(
                effectBlockIndex=effect_id,
                effectType=ET_CONSTANT,
                duration=max(0, min(USB_DURATION_INFINITE, int(duration_ms))),
                triggerRepeatInterval=0,
                samplePeriod=0,
                gain=max(0, min(USB_MAX_EFFECT_GAIN, int(gain))),
                triggerButton=USB_NO_TRIGGER_BUTTON,
                axisEnable=axis_enable,
                directions=(ct.c_uint16 * NUM_AXIS)(*directions),
                startDelay=max(0, min(USB_DURATION_INFINITE, int(start_delay_ms))),
            )
        )

        self.set_constant_force(
            SetConstantForceReportData(
                effectBlockIndex=effect_id,
                magnitude=max(-USB_MAX_MAGNITUDE, min(USB_MAX_MAGNITUDE, int(magnitude))),
            )
        )

        if start:
            self.effect_operation(
                EffectOperationReportData(
                    effectBlockIndex=effect_id,
                    effectOperation=1,
                    loopCount=max(0, min(255, int(loop_count))),
                )
            )

        return effect_id
