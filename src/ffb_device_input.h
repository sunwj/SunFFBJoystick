#ifndef _FFB_DEVICE_INPUT_H_
#define _FFB_DEVICE_INPUT_H_

#include <Arduino.h>
#include "ffb_report_types.h"
#include "low_pass_filter.h"

namespace SunFFB
{
    class FFBDeviceInput
    {
        public:
        void update_buttons(uint8_t buttonState) {inputData.buttons = buttonState;};
        void update_axis(const int16_t axis[NUM_AXIS], float cutOffFreqPos, float cutOffFreqSpeed, float speedScale = 1.f, float accelerationScale = 1.f);
        void update_position_deadband(const int32_t posDeadBand[NUM_AXIS]) {memcpy((void*)metrics.positionDeadBand, posDeadBand, sizeof(metrics.positionDeadBand));};
        void update_speed_deadband(const int32_t speedDeadBand[NUM_AXIS]) {memcpy((void*)metrics.speedDeadBand, speedDeadBand, sizeof(metrics.speedDeadBand));};
        void update_acceleration_deadband(const int32_t accelerationDeadBand[NUM_AXIS]) {memcpy((void*)metrics.accelerationDeadBand, accelerationDeadBand, sizeof(metrics.accelerationDeadBand));};

        const int32_t* get_position() const {return (const int32_t*)metrics.postion;};
        const int32_t* get_speed() const {return (const int32_t*)metrics.speed;};
        const int32_t* get_acceleration() const {return (const int32_t*)metrics.acceleration;};

        void reset();

        public:
        volatile JoystickInputReportData inputData;

        private:
        volatile Metrics metrics;
        uint32_t tPrev = 0;
    };

    inline void FFBDeviceInput::update_axis(const int16_t axis[NUM_AXIS], float cutOffFreqPos, float cutOffFreqSpeed, float speedScale, float accelerationScale)
    {
        uint32_t currentTime = micros();
        float dt = (currentTime - tPrev) & 0xFFFFFFFF;

        float alphaPos = 1.f;
        float alphaSpeed = 1.f;
        if(cutOffFreqPos > 1e-5f)
        {
            alphaPos = 2 * M_PI * cutOffFreqPos * (dt * 1e-6f);
            alphaPos /= (alphaPos + 1.f);
        }
        if(cutOffFreqSpeed > 1e-5f)
        {
            alphaSpeed = 2 * M_PI * cutOffFreqSpeed * (dt * 1e-6f);
            alphaSpeed /= (alphaSpeed + 1.f);
        }

        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            // inputData.axis[i] = axis[i];
            inputData.axis[i] += (axis[i] - inputData.axis[i]) * alphaPos;
            if(inputData.axis[i] > -metrics.positionDeadBand[0] && inputData.axis[i] < metrics.positionDeadBand[i]) inputData.axis[i] = 0;

            float newSpeed = (inputData.axis[i] - metrics.postion[i]) * speedScale / (dt * 1e-3f);
            newSpeed = metrics.speed[i] + alphaSpeed * (newSpeed - metrics.speed[i]);
            if(newSpeed > -metrics.speedDeadBand[i] && newSpeed < metrics.speedDeadBand[i]) newSpeed = 0;

            float newAccel = (newSpeed - metrics.speed[i]) * accelerationScale / (dt * 1e-3f);
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
        tPrev = millis();
    }
}

#endif