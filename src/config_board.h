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
#define USB_VID 0xFFFF
#define USB_PID 0x2010
#define USB_MANUFACTURER "SunFFB"
#define USB_PRODUCT "Force feedback joystick"

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
#define LCD_TASK_STACK_SIZE 4096
#define TIMING_TASK_STACK_SIZE 4096
#define TASK_STACK_SIZE 2048

// ===== ADC Calibration =====
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

#endif
