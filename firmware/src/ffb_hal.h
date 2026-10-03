/**
 * Portable clock and debug-output contract, isolating model code from direct Arduino dependencies.
 * Native tests inject virtual clocks through these symbols to exercise delays, cycles and wraparound.
 */

#ifndef FFB_HAL_H
#define FFB_HAL_H

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t _millis(void);
uint32_t _micros(void);
void _debug_printf(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
