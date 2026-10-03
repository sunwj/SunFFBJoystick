/**
 * Reusable C++17 serial framing, independent of firmware axis count, FreeRTOS and motor messages.
 * Variable: AA ID LEN PAYLOAD CRC. Fixed: AB ID PAYLOAD CRC, with application-defined lengths.
 * CRC excludes sync bytes. Incremental parsing retains partial frames and bounds per-call work.
 * The HAL must outlive the link; received payload views expire at the next receive call.
 */

#ifndef EMBEDDED_COMM_SERIAL_LINK_H
#define EMBEDDED_COMM_SERIAL_LINK_H

#include <stdint.h>
#include <cstring>
#include <type_traits>
#include <utility>
#include <cstddef>

namespace EmbeddedComm
{
    // CRC-8/MAXIM-DOW (poly 0x31, reflected poly 0x8C, init/xorout 0x00).
    static constexpr uint8_t CRC8_TABLE[256] = {
        0x00, 0x5E, 0xBC, 0xE2, 0x61, 0x3F, 0xDD, 0x83, 0xC2, 0x9C, 0x7E, 0x20, 0xA3, 0xFD, 0x1F,
        0x41, 0x9D, 0xC3, 0x21, 0x7F, 0xFC, 0xA2, 0x40, 0x1E, 0x5F, 0x01, 0xE3, 0xBD, 0x3E, 0x60,
        0x82, 0xDC, 0x23, 0x7D, 0x9F, 0xC1, 0x42, 0x1C, 0xFE, 0xA0, 0xE1, 0xBF, 0x5D, 0x03, 0x80,
        0xDE, 0x3C, 0x62, 0xBE, 0xE0, 0x02, 0x5C, 0xDF, 0x81, 0x63, 0x3D, 0x7C, 0x22, 0xC0, 0x9E,
        0x1D, 0x43, 0xA1, 0xFF, 0x46, 0x18, 0xFA, 0xA4, 0x27, 0x79, 0x9B, 0xC5, 0x84, 0xDA, 0x38,
        0x66, 0xE5, 0xBB, 0x59, 0x07, 0xDB, 0x85, 0x67, 0x39, 0xBA, 0xE4, 0x06, 0x58, 0x19, 0x47,
        0xA5, 0xFB, 0x78, 0x26, 0xC4, 0x9A, 0x65, 0x3B, 0xD9, 0x87, 0x04, 0x5A, 0xB8, 0xE6, 0xA7,
        0xF9, 0x1B, 0x45, 0xC6, 0x98, 0x7A, 0x24, 0xF8, 0xA6, 0x44, 0x1A, 0x99, 0xC7, 0x25, 0x7B,
        0x3A, 0x64, 0x86, 0xD8, 0x5B, 0x05, 0xE7, 0xB9, 0x8C, 0xD2, 0x30, 0x6E, 0xED, 0xB3, 0x51,
        0x0F, 0x4E, 0x10, 0xF2, 0xAC, 0x2F, 0x71, 0x93, 0xCD, 0x11, 0x4F, 0xAD, 0xF3, 0x70, 0x2E,
        0xCC, 0x92, 0xD3, 0x8D, 0x6F, 0x31, 0xB2, 0xEC, 0x0E, 0x50, 0xAF, 0xF1, 0x13, 0x4D, 0xCE,
        0x90, 0x72, 0x2C, 0x6D, 0x33, 0xD1, 0x8F, 0x0C, 0x52, 0xB0, 0xEE, 0x32, 0x6C, 0x8E, 0xD0,
        0x53, 0x0D, 0xEF, 0xB1, 0xF0, 0xAE, 0x4C, 0x12, 0x91, 0xCF, 0x2D, 0x73, 0xCA, 0x94, 0x76,
        0x28, 0xAB, 0xF5, 0x17, 0x49, 0x08, 0x56, 0xB4, 0xEA, 0x69, 0x37, 0xD5, 0x8B, 0x57, 0x09,
        0xEB, 0xB5, 0x36, 0x68, 0x8A, 0xD4, 0x95, 0xCB, 0x29, 0x77, 0xF4, 0xAA, 0x48, 0x16, 0xE9,
        0xB7, 0x55, 0x0B, 0x88, 0xD6, 0x34, 0x6A, 0x2B, 0x75, 0x97, 0xC9, 0x4A, 0x14, 0xF6, 0xA8,
        0x74, 0x2A, 0xC8, 0x96, 0x15, 0x4B, 0xA9, 0xF7, 0xB6, 0xE8, 0x0A, 0x54, 0xD7, 0x89, 0x6B,
        0x35};

    static inline uint8_t calc_crc8(const uint8_t* data, uint8_t len)
    {
        // Checksum an arbitrary byte range; the caller decides which framing fields to include.
        uint8_t crc = 0;

        for (uint8_t i = 0; i < len; ++i)
            crc = CRC8_TABLE[crc ^ data[i]];
        return crc;
    }

    static inline uint8_t calc_frame_crc8(uint8_t msgId, const uint8_t* payload, uint8_t len)
    {
        // Variable-frame helper: include ID and LEN in CRC, but exclude the AA sync byte.
        uint8_t crc = CRC8_TABLE[msgId];
        crc = CRC8_TABLE[crc ^ len];

        for (uint8_t i = 0; i < len; ++i)
            crc = CRC8_TABLE[crc ^ payload[i]];
        return crc;
    }

    enum class SerialFraming : uint8_t
    {
        Variable,
        Fixed,
        Mixed
    };

    enum class SerialFrameFormat : uint8_t
    {
        Variable,
        Fixed
    };

    // Fixed frames have a length determined by their message ID, never by received bytes.
    // Custom protocols can supply a different constexpr length(uint8_t) policy.
    struct NoFixedLayout
    {
        static constexpr uint16_t length(uint8_t)
        {
            return 0xFFFF;
        }
    };

    struct SerialFrameView
    {
        // payload borrows receiver storage; copy the bytes before retaining them asynchronously.
        uint8_t messageId = 0;
        uint8_t length = 0;
        const uint8_t* payload = nullptr;
        SerialFrameFormat format = SerialFrameFormat::Variable;
    };

    template <typename T, typename = void> struct SerialHasBulkRead : std::false_type
    {
        // Compile-time capability detection retains byte-read HAL compatibility without virtual dispatch.
    };

    template <typename T>
    struct SerialHasBulkRead<
        T, std::void_t<decltype(std::declval<T&>().readSome(std::declval<uint8_t*>(), size_t{}))>>
        : std::true_type
    {
    };

    // One RX owner and one TX owner are allowed concurrently. Multiple owners of
    // either direction require external locking. RX views live until the next receive.
    template <typename Hal, SerialFraming Framing = SerialFraming::Variable, size_t MaxPayload = 64,
              typename FixedLayout = NoFixedLayout>
    class SerialLink
    {
        public:
        static_assert(MaxPayload > 0 && MaxPayload <= 255, "Payload length is an 8-bit field");
        static constexpr uint8_t SERIAL_SYNC = 0xAA;
        static constexpr uint8_t SERIAL_FIXED_SYNC = 0xAB;
        static constexpr size_t SERIAL_MAX_PAYLOAD = MaxPayload;
        static constexpr SerialFrameFormat DEFAULT_FORMAT = Framing == SerialFraming::Fixed
                                                                ? SerialFrameFormat::Fixed
                                                                : SerialFrameFormat::Variable;

        explicit SerialLink(Hal& hal) : mHal(hal)
        {
        }

        bool sendRaw(uint8_t id, const uint8_t* payload, size_t length,
                     SerialFrameFormat format = DEFAULT_FORMAT)
        {
            // Validate format, capacity and pointers before framing; zero-length messages allow nullptr payloads.
            // One write is not wire completion; a short write returns false and leaves retry policy to the caller.
            if (length > MaxPayload || (length && !payload))
                return false;

            if constexpr (Framing == SerialFraming::Variable)
                if (format != SerialFrameFormat::Variable)
                    return false;

            if constexpr (Framing == SerialFraming::Fixed)
                if (format != SerialFrameFormat::Fixed)
                    return false;

            const bool fixed = format == SerialFrameFormat::Fixed;
            if (fixed && FixedLayout::length(id) != length)
                return false;

            mTx[0] = fixed ? SERIAL_FIXED_SYNC : SERIAL_SYNC;
            mTx[1] = id;
            size_t cursor = 2;
            uint8_t crc = CRC8_TABLE[id];
            if (!fixed)
            {
                mTx[cursor++] = uint8_t(length);
                crc = CRC8_TABLE[crc ^ uint8_t(length)];
            }

            // Copy and checksum in one pass; one HAL write per complete frame.
            for (size_t i = 0; i < length; ++i)
            {
                const uint8_t byte = payload[i];
                mTx[cursor++] = byte;
                crc = CRC8_TABLE[crc ^ byte];
            }

            mTx[cursor++] = crc;
            return mHal.write(mTx, cursor) == cursor;
        }

        bool receiveFrame(SerialFrameView& out, size_t byteBudget = MaxPayload + 4)
        {
            // Each call invalidates the previous view while retaining partial-frame state for the next call.
            // byteBudget bounds parsing work; unconsumed prefetched bytes remain in mRx.
            out = {};
            mLastPayloadLen = 0;

            while (byteBudget)
            {
                if (mRxBegin == mRxEnd)
                {
                    const int available = mHal.available();
                    if (available <= 0)
                        return false;

                    size_t count = size_t(available);
                    if (count > sizeof(mRx))
                        count = sizeof(mRx);
                    if (count > byteBudget)
                        count = byteBudget;
                    if constexpr (SerialHasBulkRead<Hal>::value)
                    {
                        mRxEnd = mHal.readSome(mRx, count);
                    }
                    else
                    {
                        // Legacy HALs still work; production HAL uses a nonblocking bulk read.
                        mRxEnd = 0;

                        while (mRxEnd < count)
                            mRx[mRxEnd++] = mHal.read();
                    }

                    mRxBegin = 0;
                    if (!mRxEnd)
                        return false;
                }

                --byteBudget;
                if (processByte(mRx[mRxBegin++]))
                {
                    mLastPayloadLen = mLength;
                    out = {mId, mLength, mPayload, mFormat};
                    return true;
                }
            }

            return false;
        }

        uint8_t receive(uint8_t* out, size_t capacity = MaxPayload)
        {
            // Legacy copying API: an oversized output frame is consumed and counted, never copied out of bounds.
            // Zero also means no result here; protocols using message ID zero should use receiveFrame.
            SerialFrameView frame;
            if (!receiveFrame(frame))
                return 0;

            if (frame.length > capacity || (frame.length && !out))
            {
                ++mOutputErrors;
                return 0;
            }

            if (frame.length)
                memcpy(out, frame.payload, frame.length);
            return frame.messageId;
        }

        bool hasPendingInput()
        {
            // Check prefetched bytes as well as the hardware FIFO; one HAL read can contain several frames.
            return mRxBegin < mRxEnd || mHal.available() > 0;
        }

        uint8_t getLastPayloadLength() const
        {
            return mLastPayloadLen;
        }

        uint32_t getCrcErrors() const
        {
            return mCrcErrors;
        }

        uint32_t getLenErrors() const
        {
            return mLenErrors;
        }

        uint32_t getOutputErrors() const
        {
            return mOutputErrors;
        }

        void resetStats()
        {
            mCrcErrors = mLenErrors = mOutputErrors = 0;
        }

        // Call after a known peer reset or a timed-out partial frame. Keeps prefetched bytes.
        void resetReceiver()
        {
            mState = State::Idle;
            mLastPayloadLen = 0;
        }

        private:
        enum class State : uint8_t
        {
            Idle,
            Id,
            Length,
            Payload,
            Crc
        };

        void seekSync(uint8_t byte)
        {
            if constexpr (Framing != SerialFraming::Fixed)
            {
                if (byte == SERIAL_SYNC)
                {
                    mFormat = SerialFrameFormat::Variable;
                    mState = State::Id;
                    return;
                }
            }

            if constexpr (Framing != SerialFraming::Variable)
            {
                if (byte == SERIAL_FIXED_SYNC)
                {
                    mFormat = SerialFrameFormat::Fixed;
                    mState = State::Id;
                }
            }
        }

        void acceptLength(uint16_t length, uint8_t byte)
        {
            // A wide length accepts the policy's 0xFFFF unknown-ID sentinel before rejecting oversized values.
            // An invalid byte may also be the next frame's sync marker; try resynchronizing immediately.
            if (length > MaxPayload)
            {
                ++mLenErrors;
                mState = State::Idle;
                seekSync(byte);
                return;
            }

            mLength = uint8_t(length);
            mIndex = 0;
            mState = length ? State::Payload : State::Crc;
        }

        bool processByte(uint8_t byte)
        {
            // Idle -> Id -> [Length] -> Payload -> Crc; fixed frames skip the Length state.
            // Return true only after CRC succeeds; corrupted payloads never reach the application.
            switch (mState)
            {
                case State::Idle:
                    seekSync(byte);
                    break;
                case State::Id:
                    mId = byte;
                    mCrc = CRC8_TABLE[byte];
                    if constexpr (Framing == SerialFraming::Fixed)
                        acceptLength(FixedLayout::length(byte), byte);
                    else if constexpr (Framing == SerialFraming::Variable)
                        mState = State::Length;
                    else
                    {
                        if (mFormat == SerialFrameFormat::Fixed)
                            acceptLength(FixedLayout::length(byte), byte);
                        else
                            mState = State::Length;
                    }

                    break;
                case State::Length:
                    mCrc = CRC8_TABLE[mCrc ^ byte];
                    acceptLength(byte, byte);
                    break;
                case State::Payload:
                    mPayload[mIndex++] = byte;
                    mCrc = CRC8_TABLE[mCrc ^ byte];
                    if (mIndex == mLength)
                        mState = State::Crc;
                    break;
                case State::Crc:
                    mState = State::Idle;
                    if (byte == mCrc)
                        return true;

                    ++mCrcErrors;
                    seekSync(byte);
                    break;
            }

            return false;
        }

        Hal& mHal;

        uint8_t mPayload[MaxPayload] = {};
        // Separate TX, RX and payload arrays allow one sender and one receiver to operate concurrently.
        uint8_t mTx[MaxPayload + 4];
        uint8_t mRx[32];

        size_t mRxBegin = 0, mRxEnd = 0;

        State mState = State::Idle;
        SerialFrameFormat mFormat = SerialFrameFormat::Variable;
        uint8_t mId = 0, mLength = 0, mIndex = 0, mCrc = 0, mLastPayloadLen = 0;

        uint32_t mCrcErrors = 0, mLenErrors = 0, mOutputErrors = 0;
    };
} // namespace EmbeddedComm

#endif
