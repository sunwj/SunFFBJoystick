#ifndef _FFB_DEVICE_INPUT_H_
#define _FFB_DEVICE_INPUT_H_

#include <cstring>
#include <math.h>
#include "ffb_report_types.h"

namespace SunFFB
{
    // Reads raw ADC, applies LPF, derives speed/acceleration.
    // Owned by joystick_task; shared state guarded by semaphoreFFBDeviceInput.
    class FFBDeviceInput
    {
        public:
        // Set button bitmask directly (read from GPIO by caller).
        void update_buttons(uint8_t buttonState) { inputData.buttons = buttonState; };

        // Sample new raw axis values, apply LPF, derive speed/acceleration.
        void update_axis(const int16_t axis[NUM_AXIS]);

        // Set deadband thresholds for condition effects.
        void update_position_deadband(const int32_t posDeadBand[NUM_AXIS]) { memcpy((void*)metrics.positionDeadBand, posDeadBand, sizeof(metrics.positionDeadBand)); };
        void update_speed_deadband(const int32_t speedDeadBand[NUM_AXIS]) { memcpy((void*)metrics.speedDeadBand, speedDeadBand, sizeof(metrics.speedDeadBand)); };
        void update_acceleration_deadband(const int32_t accelerationDeadBand[NUM_AXIS]) { memcpy((void*)metrics.accelerationDeadBand, accelerationDeadBand, sizeof(metrics.accelerationDeadBand)); };

        const float* get_position() const { return (const float*)metrics.position; };
        const float* get_speed() const { return (const float*)metrics.speed; };
        const float* get_acceleration() const { return (const float*)metrics.acceleration; };

        const float* get_max_position() const { return (const float*)metrics.maxPosition; };
        const float* get_max_speed() const { return (const float*)metrics.maxSpeed; };
        const float* get_max_acceleration() const { return (const float*)metrics.maxAcceleration; };

        // Set low-pass time constant for position LPF (shared by all axes).
        void set_tf_position(float tf) { tF_position = tf; }

        // Set low-pass time constant for speed and acceleration LPFs (shared).
        void set_tf_speed(float tf) { tF_speed = tf; }

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
        volatile JoystickInputReportData inputData;     // HID report struct (shared with send_report_task)

        private:
        Metrics metrics;                                // derived position/speed/acceleration (owned by joystick_task)
        uint32_t tPrev = 0;                             // last update timestamp (micros)
        float tF_position = DEFAULT_SPEED_TC;           // shared position LPF time constant
        float tF_speed = DEFAULT_SPEED_TC;              // shared speed/accel LPF time constant

        float lpfPosition[NUM_AXIS] = {0};              // per-axis position LPF state
        float lpfSpeed[NUM_AXIS] = {0};                 // per-axis speed LPF state
        float lpfAccel[NUM_AXIS] = {0};                 // per-axis acceleration LPF state
    };
} // namespace SunFFB

#endif
