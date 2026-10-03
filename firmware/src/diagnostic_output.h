/** Bounded, best-effort logging: write only within the available UART TX capacity. */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace SunFFB
{
    template <typename Sink>
    bool try_diagnostic_write(Sink& sink, const uint8_t* data, size_t length)
    {
        // The caller serializes producers. Space can then only grow as UART TX drains.
        if (!data || !length || sink.availableForWrite() < int(length))
        {
            return false;
        }

        return sink.write(data, length) == length;
    }
}
