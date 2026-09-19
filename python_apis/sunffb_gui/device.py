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
        while self._running:
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
        self._open = False

    @property
    def is_connected(self) -> bool:
        return self._device is not None and self._open

    @property
    def worker(self):
        return self._worker

    def connect(self) -> bool:
        try:
            self._device = SunFFBDevice(self._vid, self._pid)
            self._device.open()
            self._open = True
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
        self._open = False

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
