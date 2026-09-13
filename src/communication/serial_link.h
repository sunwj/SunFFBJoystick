#ifndef _SERIAL_LINK_H_
#define _SERIAL_LINK_H_

#include <stdint.h>
#include <cstring>
#include "config_ffb.h"

namespace SunFFB
{
    static constexpr uint8_t CRC8_TABLE[256] = {
        0x00,0x31,0x62,0x53,0xC4,0xF5,0xA6,0x97,0xB8,0x89,0xDA,0xEB,0x7C,0x4D,0x1E,0x2F,
        0x64,0x55,0x06,0x37,0xA0,0x91,0xC2,0xF3,0xDC,0xED,0xBE,0x8F,0x18,0x29,0x7A,0x4B,
        0xC8,0xF9,0xAA,0x9B,0x0C,0x3D,0x6E,0x5F,0x70,0x41,0x12,0x23,0xB4,0x85,0xD6,0xE7,
        0xAC,0x9D,0xCE,0xFF,0x68,0x59,0x0A,0x3B,0x14,0x25,0x76,0x47,0xD0,0xE1,0xB2,0x83,
        0x80,0xB1,0xE2,0xD3,0x44,0x75,0x26,0x17,0x38,0x09,0x5A,0x6B,0xFC,0xCD,0x9E,0xAF,
        0xE4,0xD5,0x86,0xB7,0x20,0x11,0x42,0x73,0x5C,0x6D,0x3E,0x0F,0x98,0xA9,0xFA,0xCB,
        0x48,0x79,0x2A,0x1B,0x8C,0xBD,0xEE,0xDF,0xF0,0xC1,0x92,0xA3,0x34,0x05,0x56,0x67,
        0x2C,0x1D,0x4E,0x7F,0xE8,0xD9,0x8A,0xBB,0x94,0xA5,0xF6,0xC7,0x50,0x61,0x32,0x03,
        0x00,0x31,0x62,0x53,0xC4,0xF5,0xA6,0x97,0xB8,0x89,0xDA,0xEB,0x7C,0x4D,0x1E,0x2F,
        0x64,0x55,0x06,0x37,0xA0,0x91,0xC2,0xF3,0xDC,0xED,0xBE,0x8F,0x18,0x29,0x7A,0x4B,
        0xC8,0xF9,0xAA,0x9B,0x0C,0x3D,0x6E,0x5F,0x70,0x41,0x12,0x23,0xB4,0x85,0xD6,0xE7,
        0xAC,0x9D,0xCE,0xFF,0x68,0x59,0x0A,0x3B,0x14,0x25,0x76,0x47,0xD0,0xE1,0xB2,0x83,
        0x80,0xB1,0xE2,0xD3,0x44,0x75,0x26,0x17,0x38,0x09,0x5A,0x6B,0xFC,0xCD,0x9E,0xAF,
        0xE4,0xD5,0x86,0xB7,0x20,0x11,0x42,0x73,0x5C,0x6D,0x3E,0x0F,0x98,0xA9,0xFA,0xCB,
        0x48,0x79,0x2A,0x1B,0x8C,0xBD,0xEE,0xDF,0xF0,0xC1,0x92,0xA3,0x34,0x05,0x56,0x67,
        0x2C,0x1D,0x4E,0x7F,0xE8,0xD9,0x8A,0xBB,0x94,0xA5,0xF6,0xC7,0x50,0x61,0x32,0x03
    };

    __attribute__((always_inline))
    static inline uint8_t calc_crc8(const uint8_t* data, uint8_t len)
    {
        uint8_t crc = 0;
        for (uint8_t i = 0; i < len; ++i)
            crc = CRC8_TABLE[crc ^ data[i]];
        return crc;
    }

    __attribute__((always_inline))
    static inline uint8_t calc_frame_crc8(uint8_t msgId, const uint8_t* payload, uint8_t len)
    {
        uint8_t crc = CRC8_TABLE[msgId];
        crc = CRC8_TABLE[crc ^ len];
        for (uint8_t i = 0; i < len; ++i)
            crc = CRC8_TABLE[crc ^ payload[i]];
        return crc;
    }

    static constexpr uint8_t SERIAL_MSG_FORCE     = 0x01;
    static constexpr uint8_t SERIAL_MSG_POSITION  = 0x02;
    static constexpr uint8_t SERIAL_MSG_HEARTBEAT = 0x03;

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

    template <typename Hal>
    class FFBSerialLink
    {
        public:
        static constexpr uint8_t SERIAL_SYNC      = 0xAA;
        static constexpr uint8_t SERIAL_MAX_PAYLOAD = 64;

        explicit FFBSerialLink(Hal& hal)
            : mHal(hal), mState(SerialState_IDLE) {}

        void sendForce(const int32_t* forces)
        {
            ForcePayload p;
            memcpy(p.force, forces, sizeof(p.force));
            sendRaw(SERIAL_MSG_FORCE, reinterpret_cast<const uint8_t*>(&p), sizeof(p));
        }

        void sendPosition(const uint16_t* positions)
        {
            PositionPayload p;
            memcpy(p.position, positions, sizeof(p.position));
            sendRaw(SERIAL_MSG_POSITION, reinterpret_cast<const uint8_t*>(&p), sizeof(p));
        }

        uint8_t receive(uint8_t* payload)
        {
            // Report only frames completed during this call. Keeping the previous
            // message ID would make an idle/partial receive look like a new frame.
            mLastMsgId = 0;
            mLastPayloadLen = 0;
            while (mHal.available() > 0) {
                uint8_t b = mHal.read();
                processByte(b, payload);
            }
            return mLastMsgId;
        }

        bool receivePosition(PositionPayload& out)
        {
            uint8_t payload[SERIAL_MAX_PAYLOAD];
            uint8_t msgId = receive(payload);
            if (msgId == SERIAL_MSG_POSITION && mLastPayloadLen == sizeof(out)) {
                memcpy(&out, payload, sizeof(out));
                return true;
            }
            return false;
        }

        uint32_t getCrcErrors() const { return mCrcErrors; }
        uint32_t getLenErrors() const { return mLenErrors; }
        void resetStats() { mCrcErrors = 0; mLenErrors = 0; }

        private:
        static constexpr uint8_t SerialState_IDLE      = 0;
        static constexpr uint8_t SerialState_HAVE_SYNC = 1;
        static constexpr uint8_t SerialState_HAVE_ID   = 2;
        static constexpr uint8_t SerialState_RECEIVING = 3;
        static constexpr uint8_t SerialState_CHECK_CRC = 4;

        void sendRaw(uint8_t msgId, const uint8_t* payload, uint8_t len)
        {
            if (len > SERIAL_MAX_PAYLOAD) return;

            uint8_t frame[SERIAL_MAX_PAYLOAD + 4];
            frame[0] = SERIAL_SYNC;
            frame[1] = msgId;
            frame[2] = len;
            memcpy(&frame[3], payload, len);
            frame[3 + len] = calc_frame_crc8(msgId, payload, len);
            mHal.write(frame, 4 + len);
        }

        void processByte(uint8_t b, uint8_t* payload)
        {
            switch (mState) {
                case SerialState_IDLE:
                    if (b == SERIAL_SYNC)
                        mState = SerialState_HAVE_SYNC;
                    break;

                case SerialState_HAVE_SYNC:
                    mMsgId = b;
                    mState = SerialState_HAVE_ID;
                    break;

                case SerialState_HAVE_ID:
                    mPayloadLen = b;
                    if (mPayloadLen == 0 || mPayloadLen > SERIAL_MAX_PAYLOAD) {
                        ++mLenErrors;
                        mState = SerialState_IDLE;
                        mLastMsgId = 0;
                        break;
                    }
                    mIdx = 0;
                    mState = SerialState_RECEIVING;
                    break;

                case SerialState_RECEIVING:
                    mBuffer[mIdx++] = b;
                    if (mIdx == mPayloadLen)
                        mState = SerialState_CHECK_CRC;
                    break;

                case SerialState_CHECK_CRC: {
                    const uint8_t expected = calc_frame_crc8(mMsgId, mBuffer, mPayloadLen);
                    if (b == expected) {
                        memcpy(payload, mBuffer, mPayloadLen);
                        mLastMsgId = mMsgId;
                        mLastPayloadLen = mPayloadLen;
                    } else {
                        ++mCrcErrors;
                        mLastMsgId = 0;
                    }
                    mState = SerialState_IDLE;
                    break;
                }
            }
        }

        Hal& mHal;
        uint8_t mBuffer[SERIAL_MAX_PAYLOAD];
        uint8_t mMsgId = 0;
        uint8_t mPayloadLen = 0;
        uint8_t mIdx = 0;
        uint8_t mLastMsgId = 0;
        uint8_t mLastPayloadLen = 0;
        uint8_t mState;
        uint32_t mCrcErrors = 0;
        uint32_t mLenErrors = 0;
    };
}

#endif
