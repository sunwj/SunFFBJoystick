#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "tusb_option.h"

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

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

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_MAX_SPEED         OPT_MODE_DEFAULT_SPEED

#define CFG_TUD_ENDPOINT0_SIZE    64

// CDC interface
#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256

// HID interface
#define CFG_TUD_HID               1
#define CFG_TUD_HID_EP_BUFSIZE    64

#ifdef __cplusplus
 }
#endif

#endif