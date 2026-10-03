# GUI package marker separating device connection, prediction, main window and drawing widgets.
# __main__.py owns startup; importing the package should not start a Qt event loop.

from .force_model import ForceModel, EffectParams, Kinematics, direction_degrees, u_from_angle
