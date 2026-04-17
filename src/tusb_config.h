#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "tusb_option.h"

// Pick the right MCU for your target.
// Examples:
//   OPT_MCU_ESP32S2
//   OPT_MCU_ESP32S3
//   OPT_MCU_RP2040
#define CFG_TUSB_MCU              OPT_MCU_ESP32S3

// Change to OPT_OS_NONE if you are not using FreeRTOS.
#define CFG_TUSB_OS               OPT_OS_FREERTOS

#define CFG_TUSB_RHPORT0_MODE     OPT_MODE_DEVICE
#define CFG_TUD_ENABLED           1

#define CFG_TUD_ENDPOINT0_SIZE    64

// One HID interface
#define CFG_TUD_HID               1
#define CFG_TUD_HID_EP_BUFSIZE    64

#ifdef __cplusplus
 }
#endif

#endif