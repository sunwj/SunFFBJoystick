/**
 * Hardware-independent classic CAN frame container without motor protocol fields.
 * length is a 0..8-byte DLC; extended and remote select extended identifiers and RTR frames.
 */

#ifndef EMBEDDED_COMM_CAN_FRAME_H
#define EMBEDDED_COMM_CAN_FRAME_H

#include <stdint.h>

namespace EmbeddedComm
{
    // Classic CAN 2.0 frame; standard/extended IDs and remote frames are supported.
    struct CANFrame
    {
        uint32_t id = 0;
        uint8_t length = 0;
        bool extended = false;
        bool remote = false;

        uint8_t data[8] = {};
    };

} // namespace EmbeddedComm
#endif
