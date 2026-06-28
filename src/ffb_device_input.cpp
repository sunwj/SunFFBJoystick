#include "ffb_device_input.h"

namespace SunFFB
{
    void FFBDeviceInput::update_axis(const int16_t axis[NUM_AXIS])
    {
        const uint32_t currentTime = micros();
        const float dt = (currentTime - tPrev) * 1e-6f;
        if (dt <= 0.f)
        {
            tPrev = currentTime;
            return;
        }

        const float alphaPos = dt / (lpfPosition[0].get_time_constant() + dt);
        const float alphaSpeed = dt / (lpfSpeed[0].get_time_constant() + dt);

        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            const float position = lpfPosition[i].filter_alpha(axis[i], alphaPos);

            float newSpeed = (position - metrics.position[i]) / dt;
            newSpeed = lpfSpeed[i].filter_alpha(newSpeed, alphaSpeed);
            if(newSpeed > metrics.maxSpeed[i])
                newSpeed = metrics.maxSpeed[i];
            else if(newSpeed < -metrics.maxSpeed[i])
                newSpeed = -metrics.maxSpeed[i];

            float newAccel = (newSpeed - metrics.speed[i]) / dt;
            newAccel = lpfAccel[i].filter_alpha(newAccel, alphaSpeed);
            if(newAccel > metrics.maxAcceleration[i])
                newAccel = metrics.maxAcceleration[i];
            else if(newAccel < -metrics.maxAcceleration[i])
                newAccel = -metrics.maxAcceleration[i];

            if(position > -metrics.positionDeadBand[i] && position < metrics.positionDeadBand[i])
                inputData.axis[i] = 0;

            if(newSpeed > -metrics.speedDeadBand[i] && newSpeed < metrics.speedDeadBand[i])
                newSpeed = 0;

            if(newAccel > -metrics.accelerationDeadBand[i] && newAccel < metrics.accelerationDeadBand[i])
                newAccel = 0;

            metrics.acceleration[i] = newAccel;
            metrics.speed[i] = newSpeed;
            metrics.position[i] = inputData.axis[i];
        }

        tPrev = currentTime;
    }

    void FFBDeviceInput::reset()
    {
        memset((void*)&inputData, 0, sizeof(JoystickInputReportData));
        memset((void*)&metrics, 0, sizeof(Metrics));

        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            metrics.maxPosition[i] = (float)USB_AXIS_MAX_ABSOLUTE;
            metrics.maxSpeed[i] = (float)USB_AXIS_MAX_ABSOLUTE * DEFAULT_MAX_SPEED_SCALE;
            metrics.maxAcceleration[i] = (float)USB_AXIS_MAX_ABSOLUTE * DEFAULT_MAX_ACCEL_SCALE;
            lpfPosition[i].reset();
            lpfSpeed[i].reset();
            lpfAccel[i].reset();
        }
        tPrev = micros();
    }
}
