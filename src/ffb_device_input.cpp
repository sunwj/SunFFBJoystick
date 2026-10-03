/**
 * Input pipeline: signed axis samples -> low-pass filters -> motion derivatives -> HID snapshot.
 * Timestamps are microseconds; derivative time intervals and filter time constants use seconds.
 * Tasks select and normalize the input source and provide synchronization; this module does no I/O.
 */

#include "ffb_device_input.h"
#include "ffb_hal.h"

namespace SunFFB
{
    void FFBDeviceInput::update_axis(const int16_t axis[NUM_AXIS])
    {
        const uint32_t currentTime = _micros();
        const float dt = (currentTime - tPrev) * 1e-6f;
        // A repeated timestamp provides no derivative interval; avoid division by zero and artificial motion.
        if (dt <= 0.f)
        {
            tPrev = currentTime;
            return;
        }

        // Clamp dt to prevent LPF pulse on task suspension
        const float dtClamped = dt < 0.01f ? dt : 0.01f;

        const float alphaPos = dtClamped / (tF_position + dtClamped);
        // First-order low-pass coefficient: alpha=dt/(tau+dt). Zero tau passes the current sample through.
        const float alphaSpeed = dtClamped / (tF_speed + dtClamped);

#pragma unroll
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            const float position = lpfPosition[i] += alphaPos * (axis[i] - lpfPosition[i]);

            float newSpeed = (position - metrics.position[i]) / dt;
            // Clamp speed and acceleration to each axis maximum to keep condition normalization bounded.
            newSpeed = lpfSpeed[i] += alphaSpeed * (newSpeed - lpfSpeed[i]);
            if (newSpeed > metrics.maxSpeed[i])
                newSpeed = metrics.maxSpeed[i];
            else if (newSpeed < -metrics.maxSpeed[i])
                newSpeed = -metrics.maxSpeed[i];

            float newAccel = (newSpeed - metrics.speed[i]) / dt;
            newAccel = lpfAccel[i] += alphaSpeed * (newAccel - lpfAccel[i]);
            if (newAccel > metrics.maxAcceleration[i])
                newAccel = metrics.maxAcceleration[i];
            else if (newAccel < -metrics.maxAcceleration[i])
                newAccel = -metrics.maxAcceleration[i];

            inputData.axis[i] = position;
            // Input deadbands change motion metrics; they are distinct from a host condition's deadBand,
            // which the calculator applies around that effect's cpOffset.
            if (position > -metrics.positionDeadBand[i] && position < metrics.positionDeadBand[i])
                inputData.axis[i] = 0;

            if (newSpeed > -metrics.speedDeadBand[i] && newSpeed < metrics.speedDeadBand[i])
                newSpeed = 0;

            if (newAccel > -metrics.accelerationDeadBand[i] &&
                newAccel < metrics.accelerationDeadBand[i])
                newAccel = 0;

            metrics.acceleration[i] = newAccel;
            metrics.speed[i] = newSpeed;
            metrics.position[i] = inputData.axis[i];
        }

        tPrev = currentTime;
    }

    void FFBDeviceInput::reset()
    {
        // Reset metrics and filter history together; clearing only the report leaves stale derivative state.
        memset((void*)&inputData, 0, sizeof(JoystickInputReportData));
        memset((void*)&metrics, 0, sizeof(Metrics));

#pragma unroll
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            metrics.maxPosition[i] = (float)USB_AXIS_MAX_ABSOLUTE;
            metrics.maxSpeed[i] = (float)USB_AXIS_MAX_ABSOLUTE * DEFAULT_MAX_SPEED_SCALE;
            metrics.maxAcceleration[i] = (float)USB_AXIS_MAX_ABSOLUTE * DEFAULT_MAX_ACCEL_SCALE;
            lpfPosition[i] = 0;
            lpfSpeed[i] = 0;
            lpfAccel[i] = 0;
        }

        tPrev = _micros();
    }
} // namespace SunFFB
