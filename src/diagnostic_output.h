/** Bounded, best-effort logging: never wait for a disconnected or slow USB host. */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace SunFFB
{
    template <typename Sink>
    bool try_diagnostic_write(Sink& sink, const uint8_t* data, size_t length)
    {
        // The caller serializes producers. Space can then only grow as USB drains.
        // Arduino 2.0.17 CDC write spins when asked to write beyond available space.
        if (!data || !length || sink.availableForWrite() < int(length))
        {
            return false;
        }

        return sink.write(data, length) == length;
    }
}
