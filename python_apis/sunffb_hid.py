from __future__ import annotations

import os
import ctypes as ct
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence, Type, TypeVar, Union

try:
    from .sunffb_constants_generated import *
except ImportError:
    from sunffb_constants_generated import *


_HERE = Path(__file__).resolve().parent
_HIDAPI_CANDIDATES = (
    _HERE / "hidapi" / "hidapi.dll",
    _HERE / "hidapi.dll",
)
hid = None
_HID_IMPORT_ERROR: Optional[BaseException] = None

try:
    import hid as _hid
    hid = _hid
except ImportError as first_error:
    _HID_IMPORT_ERROR = first_error
    for _dll_path in _HIDAPI_CANDIDATES:
        if not _dll_path.is_file():
            continue
        try:
            if hasattr(os, "add_dll_directory"):
                with os.add_dll_directory(str(_dll_path.parent)):
                    import hid as _hid
            else:
                ct.CDLL(str(_dll_path))
                import hid as _hid
            hid = _hid
            _HID_IMPORT_ERROR = None
            break
        except (ImportError, OSError) as error:
            _HID_IMPORT_ERROR = error


def _require_hid():
    if hid is None:
        detail = f": {_HID_IMPORT_ERROR}" if _HID_IMPORT_ERROR else ""
        raise RuntimeError(
            "The Python 'hid' package and a loadable hidapi library are required" + detail
        )
    return hid

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
    if not isinstance(report_id, int) or not 0 <= report_id <= 0xFF:
        raise ValueError(f"report_id must be an integer in 0..255, got {report_id!r}")
    if not isinstance(payload, PackedStruct):
        raise TypeError("payload must be a PackedStruct instance")
    return bytes([report_id]) + payload.to_bytes()


def _normalize_directions(directions: Sequence[int]) -> tuple[int, ...]:
    values = tuple(int(value) for value in directions)
    if len(values) != NUM_AXIS:
        raise ValueError(f"directions must contain exactly {NUM_AXIS} values")
    if any(not 0 <= value <= 0xFFFF for value in values):
        raise ValueError("directions values must be in 0..65535")
    return values


def _as_bytes(raw: Union[bytes, bytearray, memoryview, Sequence[int]]) -> bytes:
    try:
        return raw if isinstance(raw, bytes) else bytes(raw)
    except (TypeError, ValueError) as error:
        raise TypeError("HID data must be a bytes-like sequence") from error


def parse_feature_response(
    report_id: int,
    struct_type: Type[T],
    raw: Union[bytes, bytearray, memoryview, Sequence[int]],
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
        ("managedPool", ct.c_uint8),
    ]

    @property
    def managed_pool(self) -> bool:
        """Whether the device uses a managed effect pool."""
        return bool(self.managedPool & 0x01)

    @property
    def memory_management(self) -> int:
        """Python-style alias for the HID managedPool byte."""
        return self.managedPool


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
    path: Optional[object] = None


class SunFFBDevice:
    def __init__(self, vid: int, pid: int, path=None):
        self.vid = vid
        self.pid = pid
        self.path = path
        self.dev: Optional[hid.device] = None

    @staticmethod
    def enumerate(vid: int = 0, pid: int = 0) -> list[SunFFBInfo]:
        devices: list[SunFFBInfo] = []
        for d in _require_hid().enumerate(vid, pid):
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
                    path=d.get("path"),
                )
            )
        return devices

    def open(self) -> None:
        if self.path is None:
            self.dev = _require_hid().Device(self.vid, self.pid)
        else:
            self.dev = _require_hid().Device(path=self.path)

    def close(self) -> None:
        if self.dev is not None:
            self.dev.close()
            self.dev = None

    def __enter__(self) -> "SunFFBDevice":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def _require(self) -> hid.Device:
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
        directions: Sequence[int] = (0,) * NUM_AXIS,
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
                directions=(ct.c_uint16 * NUM_AXIS)(*_normalize_directions(directions)),
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
