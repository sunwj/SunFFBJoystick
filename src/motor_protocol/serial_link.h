#ifndef SUNFFB_MOTOR_SERIAL_LINK_H
#define SUNFFB_MOTOR_SERIAL_LINK_H

#include <EmbeddedComm/SerialLink.h>
#include "motor_payload.h"

namespace SunFFB
{
    using EmbeddedComm::calc_crc8;
    using EmbeddedComm::calc_frame_crc8;
    using EmbeddedComm::SerialFrameFormat;
    using EmbeddedComm::SerialFrameView;
    using EmbeddedComm::SerialFraming;

    constexpr uint8_t SERIAL_MSG_FORCE = 0x01;
    constexpr uint8_t SERIAL_MSG_POSITION = 0x02;
    constexpr uint8_t SERIAL_MSG_HEARTBEAT = 0x03;

    struct MotorFixedLayout
    {
        static constexpr uint16_t length(uint8_t id)
        {
            return id == SERIAL_MSG_FORCE       ? sizeof(ForcePayload)
                   : id == SERIAL_MSG_POSITION  ? sizeof(PositionPayload)
                   : id == SERIAL_MSG_HEARTBEAT ? 0
                                                : 0xFFFF;
        }
    };

    template <typename Hal, SerialFraming Framing = SerialFraming::Variable, size_t MaxPayload = 64,
              typename FixedLayout = MotorFixedLayout>
    class MotorSerialLink : public EmbeddedComm::SerialLink<Hal, Framing, MaxPayload, FixedLayout>
    {
        using Base = EmbeddedComm::SerialLink<Hal, Framing, MaxPayload, FixedLayout>;

        public:
        explicit MotorSerialLink(Hal& hal) : Base(hal)
        {
        }

        bool sendForce(const int32_t* forces, SerialFrameFormat format = Base::DEFAULT_FORMAT)
        {
            return this->sendRaw(SERIAL_MSG_FORCE, reinterpret_cast<const uint8_t*>(forces),
                                 sizeof(ForcePayload), format);
        }

        bool sendPosition(const uint16_t* positions,
                          SerialFrameFormat format = Base::DEFAULT_FORMAT)
        {
            return this->sendRaw(SERIAL_MSG_POSITION, reinterpret_cast<const uint8_t*>(positions),
                                 sizeof(PositionPayload), format);
        }

        bool sendHeartbeat()
        {
            return this->sendRaw(SERIAL_MSG_HEARTBEAT, nullptr, 0);
        }

        bool receivePosition(PositionPayload& out)
        {
            SerialFrameView frame;
            if (!this->receiveFrame(frame) || frame.messageId != SERIAL_MSG_POSITION ||
                frame.length != sizeof(out))
                return false;

            memcpy(&out, frame.payload, sizeof(out));
            return true;
        }
    };

    // Compatibility aliases for existing firmware and motor-controller clients.
    using FFBFixedLayout = MotorFixedLayout;

    template <typename Hal, SerialFraming Framing = SerialFraming::Variable, size_t MaxPayload = 64,
              typename FixedLayout = MotorFixedLayout>
    using FFBSerialLink = MotorSerialLink<Hal, Framing, MaxPayload, FixedLayout>;
} // namespace SunFFB
#endif
