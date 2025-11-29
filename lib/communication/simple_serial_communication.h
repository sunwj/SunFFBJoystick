#ifndef _SIMPLE_SERIAL_COMMUNICATION_H_
#define _SIMPLE_SERIAL_COMMUNICATION_H_

#include <Arduino.h>

#ifndef _HEADER_CONST_
#define _HEADER_CONST_
static constexpr uint8_t HEADER = 0x55;
#endif

template <typename... T>
inline void packData(uint8_t*& buffer, const T&... args)
{
    ((memcpy(buffer, &args, sizeof(args)), buffer += sizeof(args)), ...);
}

template <typename... T>
void send_packet_var_args(HardwareSerial& serial, const T&... args)
{
    constexpr uint16_t argsSize = (sizeof(T) + ...);
    constexpr uint16_t packetSize = 1 + argsSize + 1;
    static uint8_t buffer[packetSize];
    buffer[0] = HEADER;
    uint8_t* ptr = &buffer[1];

    packData(ptr, args...);

    uint8_t checkSum = 0x55;
    for (uint8_t* p = &buffer[1]; p < &buffer[packetSize - 1]; ++p) checkSum ^= *p;
    buffer[packetSize - 1] = checkSum;

    serial.write(buffer, packetSize);
}

template <typename... T>
inline void unpackData(const uint8_t*& buffer, T&... args)
{
    ((memcpy(&args, buffer, sizeof(args)), buffer += sizeof(args)), ...);
}

template <typename... T>
bool receive_packet_var_args(HardwareSerial& serial, T&... args)
{
    constexpr uint16_t totalSize = (sizeof(T) + ...);
    constexpr uint16_t packetSize = 1 + totalSize + 1;
    static uint8_t buffer[packetSize];
    static uint16_t idx = 0;

    while (serial.available())
    {
        uint8_t data = serial.read();

        if (idx == 0)
        {
            if (data == HEADER) buffer[idx++] = data;
            continue;
        }

        buffer[idx++] = data;

        if (idx == packetSize)
        {
            idx = 0;

            uint8_t checkSum = 0x55;
            for (uint16_t i = 1; i < packetSize - 1; ++i) checkSum ^= buffer[i];

            if (checkSum == buffer[packetSize - 1])
            {
                const uint8_t* payload = &buffer[1];
                unpackData(payload, args...);
                return true;
            }
            return false;
        }
    }
    return false;
}

void send_packet_buffer(HardwareSerial& serial, const uint8_t* buffer, uint16_t length)
{
    uint8_t checkSum = 0x55;
    for (auto i = 0; i < length; ++i) checkSum ^= buffer[i];
    
    serial.write(HEADER);
    serial.write(buffer, length);
    serial.write(checkSum);
}

bool receive_packet_buffer(HardwareSerial& serial, uint8_t* buffer, uint16_t length)
{
    uint16_t packetSize = 1 + length + 1;
    static uint16_t idx = 0;

    while (serial.available())
    {
        uint8_t data = serial.read();

        if (idx == 0)
        {
            if (data == HEADER) ++idx;
            continue;
        }

        if (idx <= length) buffer[idx - 1] = data;

        ++idx;

        if (idx == packetSize)
        {
            idx = 0;

            uint8_t checkSum = 0x55;
            for (uint16_t i = 0; i < length; ++i) checkSum ^= buffer[i];

            return (checkSum == data);
        }
    }
    return false;
}

#endif
