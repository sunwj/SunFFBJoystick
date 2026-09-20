from __future__ import annotations

import math
from PyQt6.QtCore import Qt, pyqtSignal, QPointF, QRectF
from PyQt6.QtGui import QColor, QPainter, QPen, QBrush, QPolygonF
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
        ('push', 0, 1),
        ('left', 1, 0), ('right', 1, 2),
        ('pull', 2, 1),
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
        self._center = QPushButton('0°', self)
        self._center.setEnabled(False)
        layout.addWidget(self._center, 1, 1)
        self._active_button = None

    def _activate(self, label: str) -> None:
        if self._active_button is not None:
            self._active_button.setStyleSheet("")
        self._buttons[label].setStyleSheet(
            "QPushButton { background-color: #81c784; font-weight: bold; }")
        self._active_button = self._buttons[label]
        angle = PAD_LABEL_ANGLE[label]
        self.set_angle(angle)
        self.angle_changed.emit(angle)

    def set_angle(self, angle: float) -> None:
        self._center.setText(f"{angle % 360.0:.0f}°")


class ForceCanvas(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(220, 220)
        self._fx = 0.0
        self._fy = 0.0
        self._px = 0.0
        self._py = 0.0

    def set_force(self, fx: float, fy: float) -> None:
        self._fx = float(fx)
        self._fy = float(fy)
        self.update()

    def set_position(self, px: float, py: float) -> None:
        self._px = max(-1.0, min(1.0, float(px)))
        self._py = max(-1.0, min(1.0, float(py)))
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        cx, cy = self.width() / 2.0, self.height() / 2.0
        radius = min(self.width(), self.height()) / 2.0 - 12.0
        if radius <= 0:
            return

        painter.fillRect(self.rect(), QColor(17, 21, 27))
        painter.setPen(QPen(QColor(49, 57, 67), 1))
        painter.drawEllipse(int(cx - radius), int(cy - radius),
                            int(2 * radius), int(2 * radius))

        painter.setPen(QPen(QColor(45, 54, 65), 1, Qt.PenStyle.DashLine))
        painter.drawLine(int(cx - radius), int(cy), int(cx + radius), int(cy))
        painter.drawLine(int(cx), int(cy - radius), int(cx), int(cy + radius))

        # Front-on yoke: pitch shifts the hub and roll rotates the wheel.
        hub_y = cy - self._py * radius * 0.18
        painter.save()
        painter.translate(cx, hub_y)
        painter.rotate(self._px * 90.0)
        painter.setPen(QPen(QColor(202, 211, 223), 7, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap))
        wheel_r = radius * 0.55
        painter.drawArc(QRectF(-wheel_r, -wheel_r, wheel_r * 2, wheel_r * 2), 25 * 16, 310 * 16)
        painter.drawLine(QPointF(-wheel_r, 0), QPointF(wheel_r, 0))
        painter.setBrush(QBrush(QColor(190, 200, 214)))
        painter.drawRoundedRect(QRectF(-wheel_r - 14, -24, 28, 48), 8, 8)
        painter.drawRoundedRect(QRectF(wheel_r - 14, -24, 28, 48), 8, 8)
        painter.setBrush(QBrush(QColor(116, 126, 143)))
        painter.drawEllipse(QRectF(-18, -18, 36, 36))
        painter.restore()

        painter.setPen(QColor(190, 200, 214))
        painter.drawText(8, int(cy), "ROLL -  (left)")
        painter.drawText(self.width() - 112, int(cy), "ROLL +  (right)")
        painter.drawText(int(cx - 90), 24, "PITCH -  (push / nose-down)")
        painter.drawText(int(cx - 86), self.height() - 42, "PITCH +  (pull / nose-up)")

        mag = math.hypot(self._fx, self._fy)
        fx_len = max(-radius, min(radius, self._fx / 10000.0 * radius))
        fy_len = max(-radius, min(radius, self._fy / 10000.0 * radius))
        painter.setPen(QPen(QColor(0, 214, 229), 3))
        painter.drawLine(QPointF(cx, hub_y), QPointF(cx + fx_len, hub_y))
        painter.setPen(QPen(QColor(157, 94, 255), 3))
        painter.drawLine(QPointF(cx, hub_y), QPointF(cx, hub_y - fy_len))
        if mag > 0.0:
            length = min(mag / 10000.0, 1.0) * radius
            ux, uy = self._fx / mag, self._fy / mag
            ex, ey = cx + ux * length, hub_y - uy * length
            painter.setPen(QPen(QColor(35, 185, 255), 4))
            painter.drawLine(int(cx), int(hub_y), int(ex), int(ey))
            painter.setBrush(QBrush(QColor(35, 185, 255)))
            self._draw_arrow_head(painter, cx, hub_y, ux, uy, length)

        painter.setPen(QColor(93, 215, 238))
        painter.drawText(8, self.height() - 8,
                         f"Fx {self._fx:+.0f}   Fy {self._fy:+.0f}   |F| {mag:.0f}")

    @staticmethod
    def _draw_arrow_head(painter, cx, cy, ux, uy, length):
        dirx = ux
        diry = -uy
        tip = QPointF(cx + dirx * length, cy + diry * length)
        bx, by = tip.x() - dirx * 8, tip.y() - diry * 8
        arrowhead = QPolygonF([tip,
                               QPointF(bx - diry * 5, by + dirx * 5),
                               QPointF(bx + diry * 5, by - dirx * 5)])
        painter.drawPolygon(arrowhead)
