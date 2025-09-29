#ifndef _SERIAL_COMMUNICATION_H_
#define _SERIAL_COMMUNICATION_H_

#include <Arduino.h>

#define HEADER_0 0x55
#define HEADER_1 0xAA

namespace SunFFB
{
    template <typename T>
    void send_packet(const T* x, const T* y, HardwareSerial* serial)
    {
        constexpr uint8_t dSize = sizeof(T);
        constexpr uint8_t packetSize = 2 + dSize * 2 + 1;
        uint8_t buffer[packetSize] = {0};

        buffer[0] = HEADER_0;
        buffer[1] = HEADER_1;

        memcpy(&buffer[2], x, dSize);
        memcpy(&buffer[2 + dSize], y, dSize);

        #pragma unroll
        for(uint8_t i = 2; i < packetSize - 1; ++i)
            buffer[packetSize - 1] ^= buffer[i];
        
        serial->write(buffer, packetSize);
    }

    template <typename T>
    bool receive_packet(T* x, T* y, HardwareSerial* serial)
    {
        constexpr uint8_t dSize = sizeof(T);
        constexpr uint8_t packetSize = 2 + dSize * 2 + 1;
        static uint8_t buffer[packetSize];
        static uint8_t idx = 0;

        while(serial->available())
        {
            uint8_t data = serial->read();

            if((idx == 0) && (data == HEADER_0))
                buffer[idx++] = data;
            else if((idx == 1) && (data == HEADER_1))
                buffer[idx++] = data;
            else if(idx >= 2)
            {
                buffer[idx++] = data;

                if(idx == packetSize)
                {
                    idx = 0;

                    uint8_t checkSum = 0;
                    #pragma unroll
                    for(uint8_t i = 2; i < packetSize - 1; ++i)
                        checkSum ^= buffer[i];
                    
                    if(checkSum == buffer[packetSize - 1])
                    {
                        memcpy(x, &buffer[2], dSize);
                        memcpy(y, &buffer[2 + dSize], dSize);
                        return true;
                    }
                }
            }
            else
                idx = 0;
        }
        return false;
    }
}

#endif