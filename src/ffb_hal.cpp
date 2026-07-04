#include "ffb_hal.h"

// ============================================================================
// Platform detection + implementation for _millis(), _micros(), _debug_printf
// ============================================================================

#if defined(ARDUINO)
// ===== Arduino (ESP32-S3, Teensy, etc.) =====
#include <Arduino.h>

uint32_t _millis(void)
{
    return millis();
}

uint32_t _micros(void)
{
    return micros();
}

void _debug_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    Serial.print(buf);
    va_end(args);
}

#elif defined(PICO_RP2040) || defined(RASPBERRYPI_PICO)
// ===== Raspberry Pi Pico (bare-metal SDK) =====
#include <pico/time.h>
#include <stdio.h>

uint32_t _millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

uint32_t _micros(void)
{
    return time_us_32();
}

void _debug_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

#elif defined(STM32)            \
    || defined(STM32F0)         \
    || defined(STM32F1)         \
    || defined(STM32F2)         \
    || defined(STM32F3)         \
    || defined(STM32F4)         \
    || defined(STM32F7)         \
    || defined(STM32G0)         \
    || defined(STM32G4)         \
    || defined(STM32H7)         \
    || defined(STM32L0)         \
    || defined(STM32L1)         \
    || defined(STM32L4)         \
    || defined(STM32WB)         \
    || defined(STM32MP1)        \
    || defined(STM32U5)         \
    || defined(STM32C0)
// ===== STM32 (HAL-based projects) =====
// _millis:  HAL_GetTick()
// _micros:  DWT cycle counter  (Cortex-M3/M4/M7/M33)
// _debug_printf:  vprintf (user must retarget stdout to desired UART)

// DWT registers are at fixed addresses in the Cortex-M system block.
#define DWT_CTRL    (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t*)0xE0001004)
#define DEMCR       (*(volatile uint32_t*)0xE000EDFC)

extern uint32_t SystemCoreClock;

uint32_t _millis(void)
{
    return HAL_GetTick();
}

uint32_t _micros(void)
{
    static int dwtEnabled = 0;
    if (!dwtEnabled)
    {
        DEMCR  |= (1UL << 24);   // TRCENA  — enable trace and debug block
        DWT_CTRL |= 1;            // CYCCNTENA — enable cycle counter
        dwtEnabled = 1;
    }
    return DWT_CYCCNT / (SystemCoreClock / 1000000UL);
}

void _debug_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

#else
// ===== Unknown platform — weak stubs =====
// Override these by defining strong symbols in your own translation unit.
__attribute__((weak))
uint32_t _millis(void)
{
    return 0;
}

__attribute__((weak))
uint32_t _micros(void)
{
    return 0;
}

__attribute__((weak))
void _debug_printf(const char* fmt, ...)
{
    (void)fmt;
}

#endif // platform detection
