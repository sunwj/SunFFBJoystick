#if defined(ARDUINO_ARCH_ESP32)
#include "diagnostic_console.h"
#include "diagnostic_output.h"
#include <algorithm>
#include <stdio.h>
#include <stdarg.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace SunFFB
{
    namespace
    {
        struct ConsoleLine
        {
            uint16_t length;
            uint8_t bytes[256];
        };

        QueueHandle_t consoleLines = nullptr;
        HardwareSerial* consolePort = nullptr;

        void console_task(void*)
        {
            ConsoleLine line;
            while (true)
            {
                xQueueReceive(consoleLines, &line, portMAX_DELAY);
                size_t offset = 0;
                while (offset < line.length)
                {
                    const int space = consolePort->availableForWrite();
                    const size_t chunk =
                        space > 0 ? std::min(size_t(space), size_t(line.length) - offset) : 0;
                    if (chunk && try_diagnostic_write(*consolePort, line.bytes + offset, chunk))
                    {
                        offset += chunk;
                    }
                    // UART backpressure must not monopolize the protocol core.
                    vTaskDelay(1);
                }
            }
        }
    } // namespace

    void start_diagnostic_console(HardwareSerial& port, uint32_t baud, uint32_t stackBytes,
                                  BaseType_t core)
    {
        configASSERT(consoleLines == nullptr);
        consolePort = &port;
        port.begin(baud);
        consoleLines = xQueueCreate(16, sizeof(ConsoleLine));
        configASSERT(consoleLines != nullptr);
        const BaseType_t started =
            xTaskCreatePinnedToCore(console_task, "Console", stackBytes, nullptr, 1, nullptr, core);
        configASSERT(started == pdPASS);
    }

    static void diagnostic_vprintf(const char* format, va_list args)
    {
        // Ignore startup messages before the queue exists. Never truncate a
        // record or block a USB/model caller when the consumer falls behind.
        if (!consoleLines || !format)
        {
            return;
        }

        ConsoleLine line{};
        const int length =
            vsnprintf(reinterpret_cast<char*>(line.bytes), sizeof(line.bytes), format, args);
        if (length <= 0 || size_t(length) >= sizeof(line.bytes))
        {
            return;
        }

        line.length = uint16_t(length);
        xQueueSend(consoleLines, &line, 0);
    }

    void diagnostic_printf(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        diagnostic_vprintf(format, args);
        va_end(args);
    }
} // namespace SunFFB
#endif
