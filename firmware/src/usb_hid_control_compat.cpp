/**
 * Backport TinyUSB 0.17's separate HID control buffer to Arduino's prebuilt 0.16.
 * Linker wrapping changes only GET/SET_REPORT storage, leaving the stock driver
 * responsible for descriptors, endpoints, idle/protocol and interrupt transfers.
 * Never gate this race fix on input rate or silently retry corrupted Feature data.
 * Reference: https://github.com/hathach/tinyusb/blob/0.17.0/src/class/hid/hid_device.c
 */
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <esp32-hal-tinyusb.h>
#include "device/usbd_pvt.h"
#include "hid_control_buffer.h"

#if defined(VALIDATE_USB_UPGRADE) && VALIDATE_USB_UPGRADE
static_assert(TUSB_VERSION_MAJOR > 0 || TUSB_VERSION_MINOR >= 17,
              "Upgrade validation requires TinyUSB with isolated HID control storage");
#endif

extern "C" bool __real_hidd_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                            const tusb_control_request_t* request);
extern void hid_set_report_callback(uint8_t id, hid_report_type_t type,
                                    const uint8_t* buffer, uint16_t length);
extern void hid_report_complete_hook(const uint8_t* report, uint16_t length);

// Match the installed TinyUSB ABI: newer releases use a wider completion length.
template <typename Callback>
struct HIDCompletionLength;

template <typename Result, typename Instance, typename Report, typename Length>
struct HIDCompletionLength<Result (*)(Instance, Report, Length)>
{
    using Type = Length;
};

using HIDCompletionLengthType = HIDCompletionLength<decltype(&tud_hid_report_complete_cb)>::Type;
extern "C" void __real_tud_hid_report_complete_cb(uint8_t instance, const uint8_t* report,
                                                 HIDCompletionLengthType length);

extern "C" void __wrap_tud_hid_report_complete_cb(uint8_t instance, const uint8_t* report,
                                                 HIDCompletionLengthType length)
{
    __real_tud_hid_report_complete_cb(instance, report, length);
    // The project hook reads the report ID only; saturate instead of narrowing to zero.
    hid_report_complete_hook(report, length > UINT16_MAX ? UINT16_MAX : uint16_t(length));
}

#if defined(VALIDATE_USB_UPGRADE) && VALIDATE_USB_UPGRADE
extern "C" void __real_tud_hid_set_report_cb(uint8_t instance, uint8_t id,
    hid_report_type_t type, const uint8_t* buffer, uint16_t length);

extern "C" void __wrap_tud_hid_set_report_cb(uint8_t instance, uint8_t id,
    hid_report_type_t type, const uint8_t* buffer, uint16_t length)
{
    if (instance != 0)
    {
        __real_tud_hid_set_report_cb(instance, id, type, buffer, length);
        return;
    }

    // Arduino 3.3.12 still classifies nonzero-ID control Output reports as Features.
    // TinyUSB owns the buffers; preserve its actual type before Arduino discards it.
    if (type == HID_REPORT_TYPE_OUTPUT && id == 0)
    {
        if (!buffer || length == 0)
        {
            return;
        }
        id = *buffer++;
        --length;
    }
    hid_set_report_callback(id, type, buffer, length);
}
#endif

#if TUSB_VERSION_MAJOR == 0 && TUSB_VERSION_MINOR == 16
static_assert(CFG_TUD_HID == 1, "Control buffer backport requires the single HID interface");
static SunFFB::HIDControlBuffer<CFG_TUD_HID_EP_BUFSIZE> controlReport;
#endif

extern "C" bool __wrap_hidd_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                            const tusb_control_request_t* request)
{
#if TUSB_VERSION_MAJOR == 0 && TUSB_VERSION_MINOR == 16
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_CLASS &&
        (request->bRequest == HID_REQ_CONTROL_GET_REPORT ||
         request->bRequest == HID_REQ_CONTROL_SET_REPORT))
    {
        // The USB device exposes a single HID interface. Reject
        // unexpected interface numbers instead of routing their data to HID 0.
        if (request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE || request->wIndex != 0)
        {
            return false;
        }

        const uint8_t id = tu_u16_low(request->wValue);
        const auto type = static_cast<hid_report_type_t>(tu_u16_high(request->wValue));
        if (request->bRequest == HID_REQ_CONTROL_GET_REPORT)
        {
            if (stage == CONTROL_STAGE_SETUP)
            {
                const uint16_t length = controlReport.prepare_get(id, request->wLength,
                    [=](uint8_t* buffer, uint16_t capacity)
                    {
                        return tud_hid_get_report_cb(0, id, type, buffer, capacity);
                    });
                return length && tud_control_xfer(rhport, request, controlReport.bytes, length);
            }
            return true;
        }

        if (!controlReport.set_length_valid(request->wLength))
        {
            return false;
        }
        if (stage == CONTROL_STAGE_SETUP)
        {
            return tud_control_xfer(rhport, request, controlReport.bytes, request->wLength);
        }
        if (stage == CONTROL_STAGE_ACK)
        {
            uint16_t length = request->wLength;
            const uint8_t* payload = controlReport.set_payload(id, length);
            // Arduino 2.0.17 routes all control SETs to _onSetFeature even for
            // Output type. Preserve the actual request type for strict dispatch.
            hid_set_report_callback(id, type, payload, length);
        }
        return true;
    }
#endif

    // >=0.17 already owns isolated buffers; keep this adapter a no-op on upgrade.
    return __real_hidd_control_xfer_cb(rhport, stage, request);
}
#endif
