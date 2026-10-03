# Tests and hardware validation

Production firmware lives in `firmware/src` and board setup in `firmware/include`.
Reusable Python client and GUI code live in `host/python_apis`; tests import them
through `tests/python/test_paths.py`, independently of the working directory.
HIL-only instrumentation lives in `firmware/diagnostics` and is included only
when `ENABLE_HIL_DIAGNOSTICS=1`. It is not part of production builds.

Run these commands from the repository root:

```powershell
pio test -e native -e native-axis1 -e native-axis3
pio test -e native-comm
python -B -m unittest discover -s tests/python
python -B host/python_apis/generate_constants.py --check
python -B tests/python/validate_hardware.py --port COM8 --motors-disconnected
python -B tests/python/check_update_rate.py --seconds 30 --json rate.json
```

DirectInput validation imports the external project's actual library. Run from
`E:/github_projects/py_directinput_ffb` with motors disconnected:

```powershell
python -B E:/github_projects/SunFFBJoystick/tests/python/validate_directinput.py --motors-disconnected --port COM8 --stress 600 --report-json E:/github_projects/SunFFBJoystick/doc/new-directinput-results.json
```

Re-enumerate ports before flashing or testing. HIL firmware enables UART motor
output and simulates encoder input; disconnect motors before using it. Keep
historical captures intact and use a new report filename for each run.
