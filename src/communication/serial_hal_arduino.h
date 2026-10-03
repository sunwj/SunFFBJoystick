#ifndef _SERIAL_HAL_ARDUINO_H_
#define _SERIAL_HAL_ARDUINO_H_

#include <Arduino.h>

namespace SunFFB
{
    class ArduinoSerialHal
    {
        public:
        explicit ArduinoSerialHal(HardwareSerial& serial)
            : mSerial(serial) {}

        size_t write(const uint8_t* data, uint16_t len)
        {
            // One TX owner: defer instead of blocking a real-time task on a full FIFO.
            if (mSerial.availableForWrite() < len) return 0;
            return mSerial.write(data, len);
        }

        int available()
        {
            return mSerial.available();
        }

        size_t readSome(uint8_t* out, size_t capacity)
        {
            // ESP32 HardwareSerial bulk read uses a zero UART timeout.
#if defined(ARDUINO_ARCH_ESP32)
            return mSerial.read(out, capacity);
#else
            size_t count = 0;
            while (count < capacity && mSerial.available() > 0) {
                const int byte = mSerial.read();
                if (byte < 0) break;
                out[count++] = uint8_t(byte);
            }
            return count;
#endif
        }

        uint8_t read()
        {
            return mSerial.read();
        }

        private:
        HardwareSerial& mSerial;
    };
}

#endif
