/**
 * Axis samples and derived motion metrics, with per-axis filter history and a sample timestamp.
 * Metric pointers borrow internal storage; synchronize access rather than retaining unlocked snapshots.
 * volatile on inputData is not a substitute for inter-task synchronization.
 */

#ifndef _FFB_DEVICE_INPUT_H_
#define _FFB_DEVICE_INPUT_H_

#include <cstring>
#include <math.h>
#include "ffb_report_types.h"

namespace SunFFB
{
    // Consumes normalized signed samples from ADC or external feedback, filters and derives motion.
    // Owned by joystick_task; shared state guarded by semaphoreFFBDeviceInput.
    class FFBDeviceInput
    {
        public:
        // Set button bitmask directly (read from GPIO by caller).
        void update_buttons(uint8_t buttonState)
        {
            inputData.buttons = buttonState;
        };

        // Sample new raw axis values, apply LPF, derive speed/acceleration.
        void update_axis(const int16_t axis[NUM_AXIS]);

        // Input deadbands use position units, position units/s and position units/s^2 respectively.
        // These affect shared metrics, unlike a host condition's per-effect deadBand parameter.
        void update_position_deadband(const int32_t posDeadBand[NUM_AXIS])
        {
            memcpy((void*)metrics.positionDeadBand, posDeadBand, sizeof(metrics.positionDeadBand));
        };

        void update_speed_deadband(const int32_t speedDeadBand[NUM_AXIS])
        {
            memcpy((void*)metrics.speedDeadBand, speedDeadBand, sizeof(metrics.speedDeadBand));
        };

        void update_acceleration_deadband(const int32_t accelerationDeadBand[NUM_AXIS])
        {
            memcpy((void*)metrics.accelerationDeadBand, accelerationDeadBand,
                   sizeof(metrics.accelerationDeadBand));
        };

        const float* get_position() const
        {
            return (const float*)metrics.position;
        };

        // All metric getters borrow arrays of NUM_AXIS floats; keep the input lock while reading.
        const float* get_speed() const
        {
            return (const float*)metrics.speed;
        };

        const float* get_acceleration() const
        {
            return (const float*)metrics.acceleration;
        };

        const float* get_max_position() const
        {
            return (const float*)metrics.maxPosition;
        };

        const float* get_max_speed() const
        {
            return (const float*)metrics.maxSpeed;
        };

        const float* get_max_acceleration() const
        {
            return (const float*)metrics.maxAcceleration;
        };

        // Set position low-pass time constant in seconds (shared by all axes); zero bypasses LPF.
        void set_tf_position(float tf)
        {
            tF_position = tf;
        }

        // Speed and acceleration share this low-pass time constant in seconds.
        void set_tf_speed(float tf)
        {
            tF_speed = tf;
        }

        // Set position LPF cutoff in Hz (converts to time constant).
        void set_cutoff_frequency_position(float cutOffFreq)
        {
            if (cutOffFreq > 0.f)
                tF_position = 1.f / (2.f * float(M_PI) * cutOffFreq);
        }

        // Set speed/accel LPF cutoff in Hz (converts to time constant).
        void set_cutoff_frequency_speed(float cutOffFreq)
        {
            if (cutOffFreq > 0.f)
                tF_speed = 1.f / (2.f * float(M_PI) * cutOffFreq);
        }

        // Reset all filters and metrics to initial state.
        void reset();

        public:
        volatile JoystickInputReportData
            inputData; // HID report struct (shared with send_report_task)

        private:
        Metrics metrics;    // derived position/speed/acceleration (owned by joystick_task)
        uint32_t tPrev = 0; // last update timestamp (micros)

        float tF_position = DEFAULT_SPEED_TC; // shared position LPF time constant
        float tF_speed = DEFAULT_SPEED_TC;    // shared speed/accel LPF time constant

        float lpfPosition[NUM_AXIS] = {0}; // per-axis position LPF state
        float lpfSpeed[NUM_AXIS] = {0};    // per-axis speed LPF state
        float lpfAccel[NUM_AXIS] = {0};    // per-axis acceleration LPF state
    };
} // namespace SunFFB

#endif
