# Connection layer between the GUI and HID client: worker reads input, controller owns the handle.
# One read loop demultiplexes position/PID reports and emits signals to the GUI thread.
# Stop polling before closing the handle; expose connection failures rather than marking them successful.

from __future__ import annotations

from PyQt6.QtCore import QThread, pyqtSignal

try:
    from sunffb_hid import (SunFFBDevice, JoystickInputReportData, PIDStateReportData,
                            REPORT_ID_JOYSTICK, REPORT_ID_PID_STATE, ET_SINE)  # noqa: F401
except ImportError:
    from python_apis.sunffb_hid import (SunFFBDevice, JoystickInputReportData, PIDStateReportData,
                                        REPORT_ID_JOYSTICK, REPORT_ID_PID_STATE, ET_SINE)  # noqa: F401


class DeviceWorker(QThread):
    joystick_ready = pyqtSignal(object)
    pid_ready = pyqtSignal(int)
    error = pyqtSignal(str)

    def __init__(self, device, poll_ms=20):
        super().__init__()
        self._device = device
        self._poll_ms = poll_ms
        self._running = True

    # A single worker reads both input report types; timeout is normal polling, other failures terminate the loop.
    def run(self):
        while self._running:
            try:
                report_id, payload = self._device.read_input_once(timeout_ms=self._poll_ms)
                if report_id == REPORT_ID_JOYSTICK:
                    joy = JoystickInputReportData.from_bytes(payload)
                    self.joystick_ready.emit(joy)
                elif report_id == REPORT_ID_PID_STATE:
                    pid = PIDStateReportData.from_bytes(payload)
                    self.pid_ready.emit(pid.status)
            except TimeoutError:
                continue
            except Exception as exc:  # noqa: BLE001
                self.error.emit(str(exc))
                self._running = False

    # Request cooperative exit and wait up to 500 ms; the worker's bounded read timeout normally permits exit.
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
        self._last_error = None

    @property
    def is_connected(self) -> bool:
        return self._device is not None and self._open

    @property
    def worker(self):
        return self._worker

    @property
    def last_error(self):
        return self._last_error

    def enumerate(self):
        """Return matching HID devices for the device selector."""
        return SunFFBDevice.enumerate(self._vid, self._pid)

    # Replace the previous session; defer worker startup when the GUI still needs to connect signal handlers.
    def connect(self, path=None, start_worker=True) -> bool:
        self._last_error = None
        try:
            self.disconnect()
            self._device = SunFFBDevice(self._vid, self._pid, path=path)
            self._device.open()
            self._open = True
            self._worker = DeviceWorker(self._device)
            if start_worker:
                self._worker.start()
            return True
        except Exception as exc:
            self._last_error = str(exc)
            try:
                if self._device is not None:
                    self._device.close()
            except Exception:
                pass
            self._device = None
            self._open = False
            return False

    # Start polling only after signals are connected; repeated calls do not create competing readers.
    def start_worker(self) -> bool:
        """Start input polling after the GUI has connected its Qt signals."""
        if not self.is_connected or self._worker is None:
            return False
        if not self._worker.isRunning():
            self._worker.start()
        return True

    # Stop the worker before closing its HID handle, then clear the connection state.
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

    # Return zero for no connection or failed allocation; a nonzero ID is allocated, not yet playing.
    def create_effect(self, effect_type: int) -> int:
        if not self._device:
            return 0
        block = self._device.create_new_effect(effect_type)
        return block.effectBlockIndex if block.blockLoadStatus == 1 else 0

    def free_effect(self, idx: int) -> None:
        if self._device:
            self._device.free_effect(idx)
