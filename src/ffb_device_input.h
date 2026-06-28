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
        void update_buttons(uint8_t buttonState) { inputData.buttons = buttonState; };
        void update_axis(const int16_t axis[NUM_AXIS]);
        void update_position_deadband(const int32_t posDeadBand[NUM_AXIS]) { memcpy((void*)metrics.positionDeadBand, posDeadBand, sizeof(metrics.positionDeadBand)); };
        void update_speed_deadband(const int32_t speedDeadBand[NUM_AXIS]) { memcpy((void*)metrics.speedDeadBand, speedDeadBand, sizeof(metrics.speedDeadBand)); };
        void update_acceleration_deadband(const int32_t accelerationDeadBand[NUM_AXIS]) { memcpy((void*)metrics.accelerationDeadBand, accelerationDeadBand, sizeof(metrics.accelerationDeadBand)); };

        const float* get_position() const { return (const float*)metrics.position; };
        const float* get_speed() const { return (const float*)metrics.speed; };
        const float* get_acceleration() const { return (const float*)metrics.acceleration; };

        const float* get_max_position() const { return (const float*)metrics.maxPosition; };
        const float* get_max_speed() const { return (const float*)metrics.maxSpeed; };
        const float* get_max_acceleration() const { return (const float*)metrics.maxAcceleration; };

        void set_tf_position(float tf)
        {
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
                lpfPosition[i].set_time_constant(tf);
        }

        void set_tf_speed(float tf)
        {
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
            {
                lpfSpeed[i].set_time_constant(tf);
                lpfAccel[i].set_time_constant(tf);
            }
        }

        void set_cutoff_frequency_position(float cutOffFreq)
        {
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
                lpfPosition[i].set_cutoff_frequency(cutOffFreq);
        }

        void set_cutoff_frequency_speed(float cutOffFreq)
        {
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
            {
                lpfSpeed[i].set_cutoff_frequency(cutOffFreq);
                lpfAccel[i].set_cutoff_frequency(cutOffFreq);
            }
        }

        void reset();

        public:
        volatile JoystickInputReportData inputData;

        private:
        Metrics metrics;
        uint32_t tPrev = 0;

        LowPassFilter lpfPosition[NUM_AXIS];
        LowPassFilter lpfSpeed[NUM_AXIS];
        LowPassFilter lpfAccel[NUM_AXIS];
    };
} // namespace SunFFB

#endif
