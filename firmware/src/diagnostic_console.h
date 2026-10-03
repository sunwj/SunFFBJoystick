/**
 * Best-effort hardware UART0 logging for ESP32 firmware.
 * Producers format a bounded record and enqueue without waiting for the UART.
 * A single low-priority task drains records; overflow drops diagnostics.
 */
#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>

namespace SunFFB
{
    // Call once during setup, before starting any diagnostic producers.
    void start_diagnostic_console(HardwareSerial& port, uint32_t baud, uint32_t stackBytes,
                                  BaseType_t core);

    void diagnostic_printf(const char* format, ...);
} // namespace SunFFB
#endif
