/**
 * Board configuration: display, GPIO, USB identity, task periods and transport switches.
 * Options guarded by #ifndef accept build-time overrides; other values are maintained here.
 * Axis count comes from config_ffb.h. Motor output is opt-in; verify wiring and protocol first.
 */

#ifndef _CONFIG_BOARD_H_
#define _CONFIG_BOARD_H_

#include "config_ffb.h"

// ===== Display (debug only; disabled for normal use) =====
#ifndef ENABLE_LCD
#define ENABLE_LCD 0
#endif
#if ENABLE_LCD != 0 && ENABLE_LCD != 1
#error "ENABLE_LCD must be 0 or 1"
#endif
// The ST7735 setup is 80x160 portrait; the application rotates it to landscape.
#define TFT_X 0
#define TFT_Y 0
#define TFT_W 160
#define TFT_H 80

// ===== GPIO Pins =====
// #define USE_BUTTON
#define SW_PIN 16
#define TDX_PIN 4
#define RDX_PIN 5

#define AXIS0_ADC_PIN 18
#define AXIS1_ADC_PIN 17
// GPIO19/20 are the native USB data lines on both S2 and S3.
#ifndef AXIS2_ADC_PIN
#define AXIS2_ADC_PIN 15
#endif

// ===== USB Device Descriptor =====
#define DEVICE_VID 0xFFFF
#define DEVICE_PID 0x2010
#define DEVICE_MANUFACTURER "SunFFB"
#define DEVICE_PRODUCT "Force feedback joystick"
#define USB_REPORT_TIMEOUT_MS 2

// Motor transmission and encoder input are opt-in.
#ifndef ENABLE_MOTOR_OUTPUT
#define ENABLE_MOTOR_OUTPUT 0
#endif
#ifndef USE_SERIAL_POSITION
#define USE_SERIAL_POSITION 0
#endif

// ===== Timing / Rates =====
#define POLLING_RATE 1 // USB endpoint slots: position + PID state share this endpoint.
#define POSITION_REPORT_PERIOD_MS 2
#define SERIAL_BAUD 115200
#define UART1_BAUD 115200

// ===== Task Periods (ms) =====
#define LCD_TASK_PERIOD_MS 60
#define JOYSTICK_TASK_PERIOD_MS 2
#ifndef FORCE_TASK_PERIOD_MS
#define FORCE_TASK_PERIOD_MS 1 // Set to 2 for 500 Hz calculation.
#endif
#define SEND_FORCE_TASK_PERIOD_MS 2
#define RECV_POSITION_TASK_PERIOD_MS 1

// ===== Task Stack Sizes =====
// ESP-IDF task creation takes stack size in bytes, not the word count used by some FreeRTOS ports.
#define LCD_TASK_STACK_SIZE 4096
#define TIMING_TASK_STACK_SIZE 4096
#define TASK_STACK_SIZE 2048

// ===== ADC Calibration =====
// Average startup samples establish each axis center; subsequent 12-bit ADC displacement maps to HID range.
#define ADC_CALIBRATION_SAMPLES 1000
#define ADC_CLAMP 2048
#define ADC_SCALE 2048.f

// Optional low-priority UART0 timing log (does not use the motor UART).
#ifndef ENABLE_TIMING_DIAGNOSTICS
#define ENABLE_TIMING_DIAGNOSTICS 0
#endif

// ===== Performance =====
#define FORCE_TASK_MAX_TIME_US 800

// ===== Default Runtime Values =====
#define DEFAULT_SPEED_TC 0.01f
#define DEFAULT_MAX_SPEED_SCALE 1.0f
#define DEFAULT_MAX_ACCEL_SCALE 1.0f

// UART framing: 0=legacy variable (0xAA), 1=fixed (0xAB), 2=mixed RX/variable TX.
#ifndef SERIAL_FRAMING_MODE
#define SERIAL_FRAMING_MODE 0
#endif
static_assert(SERIAL_FRAMING_MODE >= 0 && SERIAL_FRAMING_MODE <= 2, "Invalid UART framing mode");

// Motor transport: 0=UART (default), 1=classic CAN via ESP32 TWAI.
#ifndef MOTOR_TRANSPORT
#define MOTOR_TRANSPORT 0
#endif
#ifndef USE_CAN_POSITION
#define USE_CAN_POSITION 0
#endif
#define USE_EXTERNAL_POSITION (USE_SERIAL_POSITION || USE_CAN_POSITION)
#ifndef CAN_BITRATE
#define CAN_BITRATE 500000
#endif
#ifndef CAN_SINGLE_SHOT
#define CAN_SINGLE_SHOT 0
#endif
static_assert(CAN_SINGLE_SHOT == 0 || CAN_SINGLE_SHOT == 1, "Invalid CAN retransmission option");
#ifndef CAN_TX_PIN
#define CAN_TX_PIN 4
#endif
#ifndef CAN_RX_PIN
#define CAN_RX_PIN 5
#endif
#ifndef CAN_FORCE_ID
#define CAN_FORCE_ID 0x201
#endif
#ifndef CAN_POSITION_ID
#define CAN_POSITION_ID 0x181
#endif
#ifndef CAN_HEARTBEAT_ID
#define CAN_HEARTBEAT_ID 0x701
#endif
static_assert(MOTOR_TRANSPORT == 0 || MOTOR_TRANSPORT == 1, "Invalid motor transport");
static_assert(!(USE_SERIAL_POSITION && USE_CAN_POSITION), "Select one external position source");
static_assert(!USE_CAN_POSITION || MOTOR_TRANSPORT == 1, "CAN position requires CAN transport");
static_assert(!USE_SERIAL_POSITION || MOTOR_TRANSPORT == 0, "Serial position requires UART transport");
static_assert(CAN_BITRATE == 125000 || CAN_BITRATE == 250000 || CAN_BITRATE == 500000 || CAN_BITRATE == 1000000,
              "Unsupported CAN bitrate");
static_assert(CAN_FORCE_ID <= 0x7FF && CAN_POSITION_ID <= 0x7FF && CAN_HEARTBEAT_ID <= 0x7FF &&
              CAN_FORCE_ID >= 0 && CAN_POSITION_ID >= 0 && CAN_HEARTBEAT_ID >= 0,
              "CAN protocol uses 11-bit standard IDs");
static_assert(CAN_FORCE_ID != CAN_POSITION_ID && CAN_FORCE_ID != CAN_HEARTBEAT_ID && CAN_POSITION_ID != CAN_HEARTBEAT_ID,
              "CAN IDs must be distinct");
#if MOTOR_TRANSPORT == 1
static_assert(CAN_TX_PIN != CAN_RX_PIN && CAN_TX_PIN != 19 && CAN_TX_PIN != 20 && CAN_RX_PIN != 19 && CAN_RX_PIN != 20,
              "CAN pins overlap each other or native USB");
#endif

#endif
