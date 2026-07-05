import sys
import serial
import threading
from serial.tools import list_ports
from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout,
    QTextEdit, QLineEdit, QPushButton, QMessageBox, QLabel, QComboBox
)
from PyQt6.QtCore import pyqtSignal, QObject

from packet import SerialLink, MSG_FORCE, MSG_POSITION, unpack_force, unpack_position

# ========== Signal Bridge ==========
class SerialSignalBridge(QObject):
    data_received = pyqtSignal(str)

# ========== Main GUI ==========
class SerialGUI(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("SunFFB Serial Terminal")
        self.resize(700, 450)

        self.link: SerialLink | None = None
        self.serial = None
        self.read_thread = None
        self.running = False
        self.signals = SerialSignalBridge()
        self.signals.data_received.connect(self.update_display)

        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout()

        # Port settings
        self.port_label = QLabel("Port:")
        self.port_box = QComboBox()
        ports = [p.device for p in list_ports.comports()]
        ports.reverse()
        self.port_box.addItems(ports)

        self.baud_label = QLabel("Baud:")
        self.baud_box = QComboBox()
        self.baud_box.addItems(["115200", "460800", "921600"])
        self.baud_box.setCurrentText("115200")

        settings_layout = QHBoxLayout()
        settings_layout.addWidget(self.port_label)
        settings_layout.addWidget(self.port_box)
        settings_layout.addWidget(self.baud_label)
        settings_layout.addWidget(self.baud_box)

        # Display
        self.text_display = QTextEdit()
        self.text_display.setReadOnly(True)
        self.text_display.document().setMaximumBlockCount(2000)

        # Input
        self.input_line = QLineEdit()
        self.input_line.setPlaceholderText("f Fx Fy  or  p Px Py  (e.g. f 5000 -3000)")
        self.input_line.returnPressed.connect(self.send_data)

        # Buttons
        self.connect_button = QPushButton("Connect")
        self.connect_button.clicked.connect(self.toggle_connection)

        button_layout = QHBoxLayout()
        button_layout.addWidget(self.connect_button)

        layout.addLayout(settings_layout)
        layout.addWidget(self.text_display)
        layout.addWidget(self.input_line)
        layout.addLayout(button_layout)
        self.setLayout(layout)

    def toggle_connection(self):
        if self.serial and self.serial.is_open:
            self.disconnect_serial()
        else:
            self.connect_serial()

    def connect_serial(self):
        port = self.port_box.currentText()
        baud = int(self.baud_box.currentText())
        try:
            self.serial = serial.Serial(port, baud, timeout=0.05)
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
            self.link = SerialLink(self.serial)
            self.running = True
            self.read_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.read_thread.start()
            self.connect_button.setText("Disconnect")
            self.log(f"[INFO] Connected to {port} at {baud}")
        except serial.SerialException as e:
            QMessageBox.critical(self, "Error", f"Could not open port:\n{e}")

    def disconnect_serial(self):
        self.running = False
        if self.serial and self.serial.is_open:
            self.serial.close()
            self.log("[INFO] Disconnected.")
        self.connect_button.setText("Connect")
        self.link = None

    def read_serial(self):
        while self.running:
            try:
                result = self.link.receive()
                if result:
                    msg_id, payload = result
                    if msg_id == MSG_FORCE:
                        forces = unpack_force(payload)
                        self.signals.data_received.emit(
                            f"[FORCE] {' '.join(f'{v:>7d}' for v in forces)}")
                    elif msg_id == MSG_POSITION:
                        positions = unpack_position(payload)
                        self.signals.data_received.emit(
                            f"[POS  ] {' '.join(f'{v:>6d}' for v in positions)}")
                    elif msg_id == MSG_HEARTBEAT:
                        self.signals.data_received.emit("[HB    ] heartbeat")
                    else:
                        self.signals.data_received.emit(
                            f"[0x{msg_id:02X}] {' '.join(f'{b:02X}' for b in payload)}")
            except Exception as e:
                self.signals.data_received.emit(f"[ERROR] {e}")
                self.running = False

    def send_data(self):
        if not self.serial or not self.serial.is_open:
            QMessageBox.warning(self, "Not Connected", "Connect first.")
            return

        text = self.input_line.text().strip()
        if not text:
            return

        try:
            parts = text.split()
            cmd = parts[0].lower()

            if cmd == 'f' and len(parts) >= 2:
                forces = [int(x) for x in parts[1:]]
                self.link.send_force(forces)
                self.log(f"[SENT FORCE] {forces}")
            elif cmd == 'p' and len(parts) >= 2:
                positions = [int(x) for x in parts[1:]]
                self.link.send_position(positions)
                self.log(f"[SENT POS]   {positions}")
            elif cmd == 'hb':
                self.link.send_heartbeat()
                self.log("[SENT HB]")
            else:
                self.log(f"[WARN] Unknown command. Use: f Fx Fy | p Px Py | hb")
                return

            self.input_line.clear()
        except Exception as e:
            self.log(f"[ERROR] Send failed: {e}")

    def update_display(self, message):
        self.text_display.append(message)

    def log(self, message):
        self.text_display.append(message)

    def closeEvent(self, event):
        self.running = False
        if self.serial and self.serial.is_open:
            self.serial.close()
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    gui = SerialGUI()
    gui.show()
    sys.exit(app.exec())
