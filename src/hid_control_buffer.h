/** Separate control storage from interrupt IN/OUT storage, as in TinyUSB >= 0.17. */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace SunFFB
{
    template <size_t Capacity>
    struct HIDControlBuffer
    {
        alignas(4) uint8_t bytes[Capacity]{};

        template <typename Reader>
        uint16_t prepare_get(uint8_t id, uint16_t requested, Reader reader)
        {
            const uint16_t limit = requested < Capacity ? requested : Capacity;
            const uint16_t prefix = id ? 1 : 0;
            if (limit <= prefix)
            {
                return 0;
            }

            bytes[0] = id;
            const uint16_t payload = reader(bytes + prefix, limit - prefix);
            return payload && payload <= limit - prefix ? payload + prefix : 0;
        }

        bool set_length_valid(uint16_t length) const
        {
            return length > 0 && length <= Capacity;
        }

        // TinyUSB's callback contract excludes an optional matching report-ID byte.
        const uint8_t* set_payload(uint8_t id, uint16_t& length) const
        {
            const bool prefixed = id && length > 1 && bytes[0] == id;
            if (prefixed)
            {
                --length;
            }
            return bytes + (prefixed ? 1 : 0);
        }
    };
}
