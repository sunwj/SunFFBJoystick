# GUI entry point creating QApplication, the main window and the Qt event loop.
# The window/controller manage device reads and effects; startup does not directly compute motor output.

import sys
from PyQt6.QtWidgets import QApplication

from .main import MainWindow


# Create the application once and keep the window alive for the duration of Qt's event loop.
def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
