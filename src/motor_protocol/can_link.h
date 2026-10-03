#ifndef SUNFFB_CAN_LINK_H
#define SUNFFB_CAN_LINK_H

#include "can_protocol.h"
#include <EmbeddedComm/CanLink.h>

namespace SunFFB
{
    enum class CANReceiveResult : uint8_t
    {
        Empty,
        Ignored,
        Position
    };

    // One TX owner / one RX owner. No per-frame allocation or waiting.
    template <typename Hal> class MotorCANLink
    {
        public:
        explicit MotorCANLink(Hal& hal, CANIds ids = {}, uint32_t resyncUs = 100000)
            : mHal(hal), mIds(ids), mResyncUs(resyncUs)
        {
        }

        bool sendForce(const int32_t* forces)
        {
            CANFrame frame;
            if (!mIds.valid() || !encode_can_force(frame, forces, mTxSequence, mIds.force))
                return false;

            if (!mHal.send(frame))
                return false;

            ++mTxSequence;
            return true;
        }

        bool sendPosition(const uint16_t* positions)
        {
            CANFrame frame;
            if (!mIds.valid() || !encode_can_position(frame, positions, mTxSequence, mIds.position))
                return false;

            if (!mHal.send(frame))
                return false;

            ++mTxSequence;
            return true;
        }

        bool sendHeartbeat(uint8_t status = 0)
        {
            if (!mIds.valid())
                return false;

            CANFrame frame;
            frame.id = mIds.heartbeat;
            frame.length = 3;
            frame.data[0] = CAN_VERSION_AXES;
            frame.data[1] = mTxSequence;
            frame.data[2] = status;
            if (!mHal.send(frame))
                return false;

            ++mTxSequence;
            return true;
        }

        CANReceiveResult pollPosition(PositionPayload& out, uint32_t nowUs)
        {
            CANFrame frame;
            if (!mHal.receive(frame))
                return CANReceiveResult::Empty;

            PositionPayload decoded{};
            if (!mIds.valid() || !decode_can_position(frame, decoded, mIds.position))
            {
                ++mIgnored;
                return CANReceiveResult::Ignored;
            }

            if (mHasSequence && frame.data[1] == mRxSequence &&
                uint32_t(nowUs - mLastRxUs) < mResyncUs)
            {
                ++mDuplicates;
                return CANReceiveResult::Ignored;
            }

            out = decoded;
            mHasSequence = true;
            mRxSequence = frame.data[1];
            mLastRxUs = nowUs;
            return CANReceiveResult::Position;
        }

        void resetReceiver()
        {
            mHasSequence = false;
        }

        uint32_t getIgnoredFrames() const
        {
            return mIgnored;
        }

        uint32_t getDuplicates() const
        {
            return mDuplicates;
        }

        private:
        EmbeddedComm::CanLink<Hal> mHal;
        CANIds mIds;

        uint32_t mResyncUs, mLastRxUs = 0, mIgnored = 0, mDuplicates = 0;

        uint8_t mTxSequence = 0, mRxSequence = 0;
        bool mHasSequence = false;
    };
    template <typename Hal> using FFBCANLink = MotorCANLink<Hal>;

} // namespace SunFFB
#endif
