# SunFFB Joystick Firmware

## Overview

SunFFB Joystick Firmware is a custom USB HID PID (Physical Interface Device) compliant force feedback firmware built with the Arduino framework. It enables a microcontroller-based joystick to present itself as a full-featured force feedback device to a host system.

The firmware implements:
- USB HID joystick input reports
- USB HID PID force feedback protocol
- Real-time force calculation (constant, ramp, periodic, and condition effects)
- Multi-axis support
- External position input (e.g., encoders via serial)

---

## Features

### Input
- Multi-axis joystick (configurable NUM_AXIS)
- Button input with trigger support
- External position input via serial interface

### Force Feedback (HID PID)
Supports standard PID effects:
- Constant force
- Ramp force
- Periodic effects (sine, square, triangle, sawtooth)
- Condition effects:
  - Spring
  - Damper
  - Inertia
  - Friction

### Processing
- Real-time force computation loop
- Low-pass filtering for position, speed, and acceleration
- Deadband handling
- Directional force vectors (Cartesian & polar)

### USB
- Fully compliant HID joystick + PID device
- Works with standard drivers (no custom driver required)
- Uses TinyUSB for USB handling

---

## Architecture

Sensors/Encoders
    ↓
FFBDeviceInput → filtering, speed, acceleration
    ↓
FFBForceCalculator → computes forces from effects
    ↓
Motor Driver / Actuator Output
    ↓
USB HID Reports (Joystick + PID)

---

## Project Structure

SunFFBJoystick/
├── main.cpp
├── ffb_device_input.*
├── ffb_force_calculator.*
├── ffb_report_handler.*
├── ffb_report_types.h
├── ffb_report_descriptor.h
├── hid_pid.h
├── low_pass_filter.h
├── math_utils.h
├── constants.h

---

## Hardware Requirements

- Microcontroller with USB device support (ESP32-S2/S3 recommended)
- Motor driver (H-bridge, servo driver, etc.)
- Position sensors (encoders, potentiometers, etc.)
- Optional LCD and buttons

---

## Software Requirements

- Arduino IDE / PlatformIO
- TinyUSB
- Compatible board package

---

## How It Works

1. Input Processing
   - Position data is read and filtered into position, speed, and acceleration

2. Effect Handling
   - Host sends HID PID reports to configure and control effects

3. Force Calculation
   - Active effects are evaluated and summed

4. Output
   - Forces are sent to motor driver
   - Joystick state sent via USB

---

## Usage

1. Flash firmware to board
2. Connect via USB
3. Open a force feedback compatible game
4. Device appears as joystick + FFB device

---

## License

This project is licensed for **non-commercial use only**.

You are free to:
- Use the code for personal projects
- Modify and experiment with the firmware
- Share the code with others for non-commercial purposes

You are **not allowed to**:
- Use this project or any derivative in commercial products
- Sell hardware or software based on this firmware
- Use it in any revenue-generating activity without explicit permission

For commercial licensing inquiries, please contact the author.

---

## Acknowledgements

- TinyUSB project
- [ForceFeedback-core-library](https://github.com/JakaSimonic/ForceFeedback-core-library) by Jaka Simonic
- [picowinder](https://github.com/NolanNicholson/picowinder) by Nolan Nicholson
- Open-source force feedback and DIY controller community