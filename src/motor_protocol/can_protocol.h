#ifndef SUNFFB_CAN_PROTOCOL_H
#define SUNFFB_CAN_PROTOCOL_H

#include <stdint.h>
#include <EmbeddedComm/CanFrame.h>
#include "motor_payload.h"

namespace SunFFB
{
    using CANFrame = EmbeddedComm::CANFrame;

    struct CANIds
    {
        uint16_t force = 0x201;
        uint16_t position = 0x181;
        uint16_t heartbeat = 0x701;

        constexpr bool valid() const
        {
            return force <= 0x7FF && position <= 0x7FF && heartbeat <= 0x7FF && force != position &&
                   force != heartbeat && position != heartbeat;
        }
    };

    constexpr uint8_t CAN_VECTOR_LENGTH = 2 + NUM_AXIS * 2;
    constexpr uint8_t CAN_VERSION_AXES = 0x10 | NUM_AXIS;

    static_assert(CAN_VECTOR_LENGTH <= 8, "CAN vector exceeds classic CAN payload");
    static_assert(USB_MAX_MAGNITUDE <= 32767, "CAN force magnitude must fit int16");

    inline void can_store_u16(uint8_t* out, uint16_t value)
    {
        out[0] = uint8_t(value);
        out[1] = uint8_t(value >> 8);
    }

    inline uint16_t can_load_u16(const uint8_t* in)
    {
        return uint16_t(in[0]) | (uint16_t(in[1]) << 8);
    }

    inline bool can_vector_valid(const CANFrame& frame, uint16_t id)
    {
        return id <= 0x7FF && frame.id == id && !frame.extended && !frame.remote &&
               frame.length == CAN_VECTOR_LENGTH && frame.data[0] == CAN_VERSION_AXES;
    }

    inline bool encode_can_force(CANFrame& out, const int32_t* forces, uint8_t sequence,
                                 uint16_t id = 0x201)
    {
        if (!forces || id > 0x7FF)
            return false;

        for (uint8_t i = 0; i < NUM_AXIS; ++i)
            if (forces[i] < -USB_MAX_MAGNITUDE || forces[i] > USB_MAX_MAGNITUDE)
                return false;

        out = {};
        out.id = id;
        out.length = CAN_VECTOR_LENGTH;
        out.data[0] = CAN_VERSION_AXES;
        out.data[1] = sequence;

        for (uint8_t i = 0; i < NUM_AXIS; ++i)
            can_store_u16(&out.data[2 + 2 * i], uint16_t(forces[i]));
        return true;
    }

    inline bool decode_can_force(const CANFrame& frame, ForcePayload& out, uint16_t id = 0x201)
    {
        if (!can_vector_valid(frame, id))
            return false;

        ForcePayload decoded{};
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            const uint16_t raw = can_load_u16(&frame.data[2 + 2 * i]);
            const int32_t value = raw < 0x8000 ? int32_t(raw) : int32_t(raw) - 65536;
            if (value < -USB_MAX_MAGNITUDE || value > USB_MAX_MAGNITUDE)
                return false;

            decoded.force[i] = value;
        }

        out = decoded;
        return true;
    }

    inline bool encode_can_position(CANFrame& out, const uint16_t* positions, uint8_t sequence,
                                    uint16_t id = 0x181)
    {
        if (!positions || id > 0x7FF)
            return false;

        out = {};
        out.id = id;
        out.length = CAN_VECTOR_LENGTH;
        out.data[0] = CAN_VERSION_AXES;
        out.data[1] = sequence;

        for (uint8_t i = 0; i < NUM_AXIS; ++i)
            can_store_u16(&out.data[2 + 2 * i], positions[i]);
        return true;
    }

    inline bool decode_can_position(const CANFrame& frame, PositionPayload& out,
                                    uint16_t id = 0x181)
    {
        if (!can_vector_valid(frame, id))
            return false;

        PositionPayload decoded{};
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
            decoded.position[i] = can_load_u16(&frame.data[2 + 2 * i]);
        out = decoded;
        return true;
    }
} // namespace SunFFB
#endif
