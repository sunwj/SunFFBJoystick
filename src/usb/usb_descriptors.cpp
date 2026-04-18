#include <Arduino.h>
#include "tusb.h"
#include <string.h>

#include "ffb_report_descriptor.h"
#include "constants.h"

// -----------------------------------------------------------------------------
// Replace these with your real values
// -----------------------------------------------------------------------------
#define USB_VID   0x1234
#define USB_PID   0x5678
#define USB_BCD   0x0100

// -----------------------------------------------------------------------------
// Interface / endpoint layout
// -----------------------------------------------------------------------------
enum
{
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

// #if defined(TUD_ENDPOINT_ONE_DIRECTION_ONLY)
//   // MCUs that don't support a same endpoint number with different direction IN and OUT defined in tusb_mcu.h
//   //    e.g EP1 OUT & EP1 IN cannot exist together
//   #define EPNUM_HID_OUT   0x01
//   #define EPNUM_HID_IN    0x82
// #else
//   #define EPNUM_HID_OUT   0x01
//   #define EPNUM_HID_IN    0x81
// #endif

#define EPNUM_HID_IN        0x81
#define EPNUM_CDC_NOTIF     0x82
#define EPNUM_CDC_OUT       0x03
#define EPNUM_CDC_IN        0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

// -----------------------------------------------------------------------------
// Device descriptor
// -----------------------------------------------------------------------------
static tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = USB_BCD,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &desc_device;
}

static const uint8_t desc_hid_report[] =
{
    // Windows expects all of the I/O/F reports to be wrapped in an application collection;
    // otherwise, the device won't be registered as capable of force-feedback.
    // Linux is fine either way.
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_JOYSTICK),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),

    // Input Reports
    FFB_REPORT_DESC_INPUT_JOYSTICK,
    FFB_REPORT_DESC_INPUT_PID_STATE,
    // Output Reports
    FFB_REPORT_DESC_OUTPUT_SET_EFFECT,
    FFB_REPORT_DESC_OUTPUT_SET_ENVELOPE,
    FFB_REPORT_DESC_OUTPUT_SET_CONDITION,
    FFB_REPORT_DESC_OUTPUT_SET_PERIODIC,
    FFB_REPORT_DESC_OUTPUT_SET_CONSTANT,
    FFB_REPORT_DESC_OUTPUT_SET_RAMP,
    FFB_REPORT_DESC_OUTPUT_EFFECT_OPERATION,
    FFB_REPORT_DESC_OUTPUT_BLOCK_FREE,
    FFB_REPORT_DESC_OUTPUT_DEVICE_CONTROL,
    FFB_REPORT_DESC_OUTPUT_DEVICE_GAIN,
    // Feature Reports
    FFB_REPORT_DESC_FEATURE_CREATE_NEW_EFFECT,
    FFB_REPORT_DESC_FEATURE_BLOCK_LOAD,
    FFB_REPORT_DESC_FEATURE_POOL_REPORT,

    HID_COLLECTION_END
};

uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void) instance;
    return desc_hid_report;
}

// -----------------------------------------------------------------------------
// Configuration descriptor
// -----------------------------------------------------------------------------
static uint8_t const desc_configuration[] =
{
    // config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),

    // cdc descriptor
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // interface number, string index, protocol, report descriptor length,
    // endpoint IN address, endpoint size, polling interval
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 2)
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return desc_configuration;
}

// -----------------------------------------------------------------------------
// String descriptors
// -----------------------------------------------------------------------------
static const char * string_desc_arr[] =
{
    (const char[]){ 0x09, 0x04 }, // 0: English
    "Your Name",                  // 1: Manufacturer
    "SunFFB Joystick",            // 2: Product
    "000001",                     // 3: Serial
    "SunFFB CDC"                  // 4: CDC interface
};

static uint16_t _desc_str[32];

uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;

    uint8_t chr_count;

    if (index == 0)
    {
        _desc_str[1] = 0x0409;
        chr_count = 1;
    }
    else
    {
        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        const char * str = string_desc_arr[index];
        chr_count = (uint8_t) strlen(str);

        if (chr_count > 31)
            chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++)
        {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}