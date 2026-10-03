/**
 * Application payloads: one int32 force or uint16 position value per configured axis.
 * UART transmits these layouts; CAN uses its own compact codec, not the raw force structure.
 * Peers must agree on axis count and little-endian layout; input tasks map the position center.
 */

#ifndef SUNFFB_MOTOR_PAYLOAD_H
#define SUNFFB_MOTOR_PAYLOAD_H

#include <stdint.h>
#include "config_ffb.h"

namespace SunFFB
{
#pragma pack(push, 1)

    struct ForcePayload
    {
        int32_t force[NUM_AXIS];
    };

    struct PositionPayload
    {
        uint16_t position[NUM_AXIS];
    };

#pragma pack(pop)
} // namespace SunFFB
#endif
