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

        void write(const uint8_t* data, uint16_t len)
        {
            mSerial.write(data, len);
        }

        int available()
        {
            return mSerial.available();
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
