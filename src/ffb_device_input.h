#ifndef _FFB_DEVICE_INPUT_H_
#define _FFB_DEVICE_INPUT_H_

#include <Arduino.h>
#include "ffb_report_types.h"

namespace SunFFB
{
    class FFBDeviceInput
    {
        public:
        void update_buttons(uint8_t buttonState) { inputData.buttons = buttonState; };
        void update_axis(const int16_t axis[NUM_AXIS]);
        void update_position_deadband(const int32_t posDeadBand[NUM_AXIS]) { memcpy((void*)metrics.positionDeadBand, posDeadBand, sizeof(metrics.positionDeadBand)); };
        void update_speed_deadband(const int32_t speedDeadBand[NUM_AXIS]) { memcpy((void*)metrics.speedDeadBand, speedDeadBand, sizeof(metrics.speedDeadBand)); };
        void update_acceleration_deadband(const int32_t accelerationDeadBand[NUM_AXIS]) { memcpy((void*)metrics.accelerationDeadBand, accelerationDeadBand, sizeof(metrics.accelerationDeadBand)); };

        const int32_t* get_position() const { return (const int32_t*)metrics.postion; };
        const int32_t* get_speed() const { return (const int32_t*)metrics.speed; };
        const int32_t* get_acceleration() const { return (const int32_t*)metrics.acceleration; };

        const int32_t* get_max_position() const { return (const int32_t*)metrics.maxPosition; };
        const int32_t* get_max_speed() const { return (const int32_t*)metrics.maxSpeed; };
        const int32_t* get_max_acceleration() const { return (const int32_t*)metrics.maxAcceleration; };

        void set_tf_position(float tf) { tFPos = (tf > 0.f) ? tf : 0.f; };
        void set_tf_speed(float tf) { tFSpeed = (tf > 0.f) ? tf : 0.f; };

        void set_cutoff_frequency_position(float cutOffFreq) { tFPos = (cutOffFreq > 0.f) ? 1.f / (2.f * float(M_PI) * cutOffFreq) : 0.f; }
        void set_cutoff_frequency_speed(float cutOffFreq) { tFSpeed = (cutOffFreq > 0.f) ? 1.f / (2.f * float(M_PI) * cutOffFreq) : 0.f; }

        void reset();

        public:
        volatile JoystickInputReportData inputData;

        private:
        volatile Metrics metrics;
        uint32_t tPrev = 0;

        float tFPos = 0.f;
        float tFSpeed = 0.f;
    };

    inline void FFBDeviceInput::update_axis(const int16_t axis[NUM_AXIS])
    {
        uint32_t currentTime = micros();
        float dt = (currentTime - tPrev) * 1e-6f;

        float alphaPos = dt / (tFPos + dt);
        float alphaSpeed = dt / (tFSpeed + dt);

        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            // inputData.axis[i] = axis[i];
            inputData.axis[i] += (axis[i] - inputData.axis[i]) * alphaPos;
            if(inputData.axis[i] > -metrics.positionDeadBand[0] && inputData.axis[i] < metrics.positionDeadBand[i]) inputData.axis[i] = 0;

            float newSpeed = (inputData.axis[i] - metrics.postion[i]) / (dt * 1e3f);
            newSpeed = metrics.speed[i] + alphaSpeed * (newSpeed - metrics.speed[i]);
            if(newSpeed > -metrics.speedDeadBand[i] && newSpeed < metrics.speedDeadBand[i]) newSpeed = 0;

            float newAccel = (newSpeed - metrics.speed[i]) / (dt * 1e3f);
            newAccel = metrics.acceleration[i] + alphaSpeed * (newAccel - metrics.acceleration[i]);
            if(newAccel > -metrics.accelerationDeadBand[i] && newAccel < metrics.accelerationDeadBand[i]) newAccel = 0;

            metrics.acceleration[i] = newAccel;
            metrics.speed[i] = newSpeed;
            metrics.postion[i] = inputData.axis[i];
        }

        tPrev = currentTime;
    }

    inline void FFBDeviceInput::reset()
    {
        memset((void*)&inputData, 0, sizeof(JoystickInputReportData));
        memset((void*)&metrics, 0, sizeof(Metrics));
        metrics.maxPosition[0] = USB_AXIS_MAX_ABSOLUTE;
        metrics.maxPosition[1] = USB_AXIS_MAX_ABSOLUTE;
        metrics.maxSpeed[0] = USB_AXIS_MAX_ABSOLUTE * 0.0025f;
        metrics.maxSpeed[1] = USB_AXIS_MAX_ABSOLUTE * 0.0025f;
        metrics.maxAcceleration[0] = USB_AXIS_MAX_ABSOLUTE;
        metrics.maxAcceleration[1] = USB_AXIS_MAX_ABSOLUTE;
        tPrev = millis();
    }
}

#endif