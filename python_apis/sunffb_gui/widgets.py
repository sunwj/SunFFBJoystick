from __future__ import annotations

import math
from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QColor, QPainter, QPen, QBrush
from PyQt6.QtWidgets import QWidget, QGridLayout, QPushButton

PAD_LABEL_ANGLE = {'pull': 0.0, 'pull-left': 45.0, 'left': 90.0, 'left-push': 135.0,
                   'push': 180.0, 'push-right': 225.0, 'right': 270.0, 'right-pull': 315.0}


def direction_from_pad(label: str) -> float:
    return PAD_LABEL_ANGLE.get(label, 0.0)


def pad_label_from_deg(deg: float) -> str:
    idx = int(round((deg % 360.0) / 45.0)) % 8
    return list(PAD_LABEL_ANGLE.keys())[idx]


class DirectionPad(QWidget):
    angle_changed = pyqtSignal(float)

    _POSITIONS = [
        ('pull-left', 0, 0), ('pull', 0, 1), ('pull-right', 0, 2),
        ('left', 1, 0), ('right', 1, 2),
        ('left-push', 2, 0), ('push', 2, 1), ('push-right', 2, 2),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QGridLayout(self)
        layout.setSpacing(2)
        self._buttons = {}
        for label, row, col in self._POSITIONS:
            btn = QPushButton(label, self)
            btn.clicked.connect(lambda _=False, lbl=label: self._activate(lbl))
            layout.addWidget(btn, row, col)
            self._buttons[label] = btn
        center = QPushButton('', self)
        center.setEnabled(False)
        layout.addWidget(center, 1, 1)
        self._active_button = None

    def _activate(self, label: str) -> None:
        if self._active_button is not None:
            self._active_button.setStyleSheet("")
        self._buttons[label].setStyleSheet(
            "QPushButton { background-color: #81c784; font-weight: bold; }")
        self._active_button = self._buttons[label]
        self.angle_changed.emit(PAD_LABEL_ANGLE[label])


class ForceCanvas(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(220, 220)
        self._fx = 0.0
        self._fy = 0.0

    def set_force(self, fx: float, fy: float) -> None:
        self._fx = float(fx)
        self._fy = float(fy)
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        cx, cy = self.width() / 2.0, self.height() / 2.0
        radius = min(self.width(), self.height()) / 2.0 - 12.0
        if radius <= 0:
            return

        painter.setPen(QPen(QColor(210, 210, 210), 1))
        painter.drawEllipse(int(cx - radius), int(cy - radius),
                            int(2 * radius), int(2 * radius))

        painter.setPen(QPen(QColor(190, 190, 190), 1, Qt.PenStyle.DashLine))
        painter.drawLine(int(cx - radius), int(cy), int(cx + radius), int(cy))
        painter.drawLine(int(cx), int(cy - radius), int(cx), int(cy + radius))

        mag = math.hypot(self._fx, self._fy)
        if mag > 0.0:
            length = min(mag / 10000.0, 1.0) * radius
            ux, uy = self._fx / mag, self._fy / mag
            ex, ey = cx + ux * length, cy - uy * length
            painter.setPen(QPen(QColor(255, 40, 40), 3))
            painter.drawLine(int(cx), int(cy), int(ex), int(ey))
            painter.setBrush(QBrush(QColor(255, 40, 40)))
            self._draw_arrow_head(painter, cx, cy, ux, uy, length)

        painter.setPen(QColor(120, 120, 120))
        painter.drawText(8, self.height() - 8,
                         f"force: ({self._fx:.0f}, {self._fy:.0f})")

    @staticmethod
    def _draw_arrow_head(painter, cx, cy, ux, uy, length):
        ah = 8.0
        tip_x, tip_y = cx + ux * length, cy - uy * length
        bx, by = -uy, ux
        p1 = (tip_x - ux * ah + bx * ah * 0.5, tip_y + uy * ah + by * ah * 0.5)
        p2 = (tip_x - ux * ah - bx * ah * 0.5, tip_y + uy * ah - by * ah * 0.5)
        painter.drawPolygon(int(p1[0]), int(p1[1]), int(tip_x), int(tip_y), int(p2[0]), int(p2[1]))
