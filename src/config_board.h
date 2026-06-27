#ifndef _CONFIG_BOARD_H_
#define _CONFIG_BOARD_H_

#include "config_ffb.h"

// ===== Display =====
#define TFT_X 0
#define TFT_Y 0
#define TFT_W 160
#define TFT_H 80

// ===== GPIO Pins =====
#define SW_PIN 16
#define TDX_PIN 4
#define RDX_PIN 5

#define AXIS0_ADC_PIN 18
#define AXIS1_ADC_PIN 17
#define AXIS2_ADC_PIN 19

// ===== USB Device Descriptor =====
#define USB_VID 0xFFFF
#define USB_PID 0x2010
#define USB_MANUFACTURER "SunFFB"
#define USB_PRODUCT "Force feedback joystick"

// ===== Timing / Rates =====
#define POLLING_RATE 2
#define SERIAL_BAUD 115200
#define UART1_BAUD 115200

// ===== Task Periods (ms) =====
#define LCD_TASK_PERIOD_MS 60
#define JOYSTICK_TASK_PERIOD_MS 2
#define FORCE_TASK_PERIOD_MS 1
#define SEND_FORCE_TASK_PERIOD_MS 2
#define RECV_POSITION_TASK_PERIOD_MS 2

// ===== Task Stack Sizes =====
#define LCD_TASK_STACK_SIZE 4096
#define TASK_STACK_SIZE 2048

// ===== ADC Calibration =====
#define ADC_CALIBRATION_SAMPLES 1000
#define ADC_CLAMP 2048
#define ADC_SCALE 2048.f

// ===== Default Runtime Values =====
#define DEFAULT_SPEED_TC 0.1f
#define DEFAULT_MAX_SPEED_SCALE 0.0025f

#endif
