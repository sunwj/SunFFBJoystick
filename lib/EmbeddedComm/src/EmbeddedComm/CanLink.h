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
