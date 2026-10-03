/**
 * Generic classic CAN link validating frame capacity and ID range before forwarding to the HAL.
 * Motor messages and sequences belong to the application; hardware retransmission belongs to the driver.
 * HAL send/receive must be nonblocking. The caller owns the HAL and its lifetime.
 */

#ifndef EMBEDDED_COMM_CAN_LINK_H
#define EMBEDDED_COMM_CAN_LINK_H

#include "CanFrame.h"

namespace EmbeddedComm
{
    // One TX owner and one RX owner. HAL methods must not block.
    template <typename Hal> class CanLink
    {
        public:
        explicit CanLink(Hal& hal) : mHal(hal)
        {
        }

        bool send(const CANFrame& frame)
        {
            if (!valid(frame))
                return false;

            return mHal.send(frame);
        }

        bool receive(CANFrame& frame)
        {
            // Validate a local received frame first; rejected input leaves the caller's output unchanged.
            CANFrame received;
            if (!mHal.receive(received) || !valid(received))
                return false;

            frame = received;
            return true;
        }

        private:
        static bool valid(const CANFrame& frame)
        {
            return frame.length <= sizeof(frame.data) &&
                   frame.id <= (frame.extended ? 0x1FFFFFFFu : 0x7FFu);
        }

        Hal& mHal;
    };
} // namespace EmbeddedComm
#endif
