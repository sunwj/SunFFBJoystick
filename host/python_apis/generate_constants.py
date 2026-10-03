#!/usr/bin/env python3
# Extract protocol constants from config_ffb.h to prevent host/firmware report and axis-count drift.
# A limited parser for project constants, not a complete C preprocessor.
# --check compares only; --generate overwrites the generated module. Regenerate after axis changes.

"""Parse firmware/src/config_ffb.h and generate matching Python constants.

Usage:
    python host/python_apis/generate_constants.py              # print generated
    python host/python_apis/generate_constants.py --check       # diff only, exit 1 on mismatch
    python host/python_apis/generate_constants.py --generate    # overwrite file
"""

import argparse
import re
import sys
from pathlib import Path

SRC_HEADER = Path(__file__).resolve().parents[2] / "firmware" / "src" / "config_ffb.h"
OUTPUT_FILE = Path(__file__).parent / "sunffb_constants_generated.py"

SYNC_KEYS = [
    "NUM_AXIS", "MAX_EFFECTS", "NUM_SUPPORTED_EFFECTS",
    "USB_NO_TRIGGER_BUTTON", "USB_DURATION_INFINITE",
    "USB_AXIS_MAX_ABSOLUTE", "USB_MAX_MAGNITUDE",
    "USB_MAX_EFFECT_GAIN", "USB_MAX_DEVICE_GAIN",
    "ET_CONSTANT", "ET_RAMP", "ET_SQUARE", "ET_SINE",
    "ET_TRIANGLE", "ET_SAWTOOTH_UP", "ET_SAWTOOTH_DOWN",
    "ET_SPRING", "ET_DAMPER", "ET_INERTIA", "ET_FRICTION",
    "X_AXIS_ENABLE", "Y_AXIS_ENABLE", "Z_AXIS_ENABLE",
    "DIRECTION_ENABLE",
    "REPORT_ID_JOYSTICK", "REPORT_ID_PID_STATE",
    "REPORT_ID_SET_EFFECT_REPORT", "REPORT_ID_SET_ENVELOPE_REPORT",
    "REPORT_ID_SET_CONDITION_REPORT", "REPORT_ID_SET_PERIODIC_REPORT",
    "REPORT_ID_SET_CONSTANT_FORCE_REPORT", "REPORT_ID_SET_RAMP_FORCE_REPORT",
    "REPORT_ID_CUSTOM_FORCE_DATA_REPORT", "REPORT_ID_DOWNLOAD_FORCE_SAMPLE",
    "REPORT_ID_EFFECT_OPERATION_REPORT", "REPORT_ID_BLOCK_FREE_REPORT",
    "REPORT_ID_DEVICE_CONTROL_REPORT", "REPORT_ID_DEVICE_GAIN_REPORT",
    "REPORT_ID_SET_CUSTOM_FORCE_REPORT", "REPORT_ID_CREATE_NEW_EFFECT_REPORT",
    "REPORT_ID_BLOCK_LOAD_REPORT", "REPORT_ID_POOL_REPORT",
]


# Evaluate only the simple numeric comparisons supported by this project's conditional definitions.
def _eval_condition(line: str, known: dict[str, int]) -> bool:
    """Evaluate a simple #if/#elif condition against known defines."""
    expr = line.split(None, 1)[1].strip() if len(line.split(None, 1)) > 1 else ""
    m = re.match(r"(\w+)\s*(==|!=|>=|<=|>|<)\s*(\w+)", expr)
    if m:
        name, op, val = m.group(1), m.group(2), m.group(3)
        if name in known:
            left = known[name]
            right = int(val, 16) if val.startswith("0x") else int(val)
            if op == "==": return left == right
            if op == "!=": return left != right
            if op == ">=": return left >= right
            if op == "<=": return left <= right
            if op == ">": return left > right
            if op == "<": return left < right
    return False


def _is_active(skip_stack: list[bool]) -> bool:
    """Line is active only if all stack levels are True."""
    return all(skip_stack) if skip_stack else True


# Track active conditional branches and collect supported numeric SYNC_KEYS only.
# Do not extend header syntax without updating this limited parser and checking generated results.
def parse_c_header(path: Path) -> dict[str, int]:
    content = path.read_text()
    lines = content.splitlines()
    all_defines: dict[str, str] = {}  # all defines for #ifdef evaluation
    consts: dict[str, int] = {}  # only SYNC_KEYS for output
    skip_stack: list[bool] = []

    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("//"):
            continue

        # Track #if / #elif / #else / #endif
        if stripped.startswith("#ifdef "):
            name = stripped.split(None, 1)[1].strip()
            skip_stack.append(name in all_defines) if _is_active(skip_stack) else skip_stack.append(False)
            continue
        elif stripped.startswith("#ifndef "):
            name = stripped.split(None, 1)[1].strip()
            skip_stack.append(name not in all_defines) if _is_active(skip_stack) else skip_stack.append(False)
            continue
        elif stripped.startswith("#if "):
            if not _is_active(skip_stack):
                skip_stack.append(False)
            else:
                skip_stack.append(_eval_condition(stripped, consts))
            continue

        elif stripped.startswith("#elif "):
            if len(skip_stack) == 0:
                continue
            if not _is_active(skip_stack[:-1]):
                # Parent branch is inactive, stay inactive
                continue
            if skip_stack[-1]:
                skip_stack[-1] = False
            else:
                skip_stack[-1] = _eval_condition(stripped, consts)
            continue

        elif stripped.startswith("#else"):
            if len(skip_stack) == 0:
                continue
            if not _is_active(skip_stack[:-1]):
                continue
            skip_stack[-1] = not skip_stack[-1]
            continue

        elif stripped.startswith("#endif"):
            if len(skip_stack) > 0:
                skip_stack.pop()
            continue

        # Skip lines in inactive branches
        if not _is_active(skip_stack):
            continue

        # Parse #define
        m = re.match(r"#define\s+(\w+)\s+([\da-fA-FxX_.]+)", stripped)
        if m:
            name, raw = m.group(1), m.group(2)
            all_defines[name] = raw  # track for #ifdef evaluation
            if name not in SYNC_KEYS or "." in raw:
                continue
            if raw.startswith("0x") or raw.startswith("0X"):
                consts[name] = int(raw, 16)
            else:
                consts[name] = int(raw)

    return consts


# Render deterministic assignments for the selected constants; the result is maintained by this generator.
def generate_python(consts: dict[str, int]) -> str:
    lines = [
        "# Auto-generated by generate_constants.py - DO NOT EDIT",
        "# Regenerate: python host/python_apis/generate_constants.py --generate",
        "",
    ]
    for name, value in consts.items():
        lines.append(f"{name} = {value}  # 0x{value:X}")
    lines.append("")
    return "\n".join(lines)


# Check mode exits on drift; generate mode writes the module. Merely reading constants does not touch firmware.
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Diff only, exit 1 on mismatch")
    parser.add_argument("--generate", action="store_true", help="Overwrite generated file")
    args = parser.parse_args()

    if not SRC_HEADER.exists():
        print(f"ERROR: {SRC_HEADER} not found")
        sys.exit(1)

    consts = parse_c_header(SRC_HEADER)
    generated = generate_python(consts)

    if args.generate:
        OUTPUT_FILE.write_text(generated)
        print(f"Written {OUTPUT_FILE}")
    elif args.check:
        if OUTPUT_FILE.exists():
            existing = OUTPUT_FILE.read_text()
            if existing != generated:
                print("MISMATCH - run --generate to update")
                print("---")
                print(generated)
                sys.exit(1)
            else:
                print("OK - constants are in sync")
        else:
            print(f"{OUTPUT_FILE} does not exist - run --generate first")
            sys.exit(1)
    else:
        print(generated)


if __name__ == "__main__":
    main()
